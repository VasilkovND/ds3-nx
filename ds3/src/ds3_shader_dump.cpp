// ds3-nx - collects the game's shaders as it creates them, for the shader library.
//
// Why: the deko3d renderer draws with shaders translated ahead of time (XenosRecomp -> HLSL -> SPIR-V), found
// by a fingerprint of each shader's container. Dead Space 2 kept its shaders RefPack-compressed inside its bigfiles,
// and Dead Space 3 is expected to do the same; every shader, however, goes through the XDK's
// ds3_D3DDevice_CreateVertexShader / ds3_D3DDevice_CreatePixelShader with its container already in guest memory
// (r3 = pFunction). Wrapping those two functions catches every shader the game really uses, whatever its archive
// format. The two names come from codegen/names.toml; CMake compiles this file only when both are there.
//
// Each new container is written once to <NRO folder>/shaders_dump/{vs,ps}_<xxh3>.bin (XXH3_64bits of the
// whole container, the key XenosRecomp itself uses). Copy that folder back to the PC to build the library
// (tools/shaders.sh). Containers the library does not have are always written, so the first start without a library
// dumps the whole set the game creates; --ds3_shader_dump=true writes the others too. Dead Space 2 created its
// ~1,300 at startup and writing them froze that first load for ~40 s.
//
// The same two hooks feed the shader library (ds3_shaders.h): before the call, which library entry the container
// is; after it, where the new object keeps its microcode, so the ring can tell which shader an IM_LOAD is.
//
// Container layout (XenosRecomp's ShaderContainer, big-endian): flags (0x102A11xx), virtualSize,
// physicalSize, ...; the container is virtualSize + physicalSize bytes.

#include "ds3_pch.h"

#include "ds3_shaders.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include <xxhash.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>

REXCVAR_DEFINE_BOOL(ds3_shader_dump, false, "DS3",
                    "Write every shader container the game creates to shaders_dump/ next to the executable.");

namespace {

std::mutex g_mutex;
std::unordered_set<uint64_t> g_seen;
std::filesystem::path g_dir;
std::atomic<uint32_t> g_written{0};
std::atomic<uint32_t> g_rejected{0};

// `missing`: the library has no entry for it, or there is no library at all. Those are always written: without
// ds3_shaders.dslib next to the NRO the first start dumps the game's whole set (it creates every shader at startup),
// which is how a player makes the library from their own copy (INSTALL.md). The rest only with ds3_shader_dump.
void Dump(uint8_t* base, uint32_t address, const char* stage, bool missing) {
  if (!address || !(missing || REXCVAR_GET(ds3_shader_dump))) return;
  const uint32_t flags = REX_LOAD_U32(address);
  const uint32_t virtual_size = REX_LOAD_U32(address + 4);
  const uint32_t physical_size = REX_LOAD_U32(address + 8);
  const uint64_t size = uint64_t(virtual_size) + physical_size;
  if ((flags & 0xFFFF0000u) != 0x102A0000u || size < 36 || size > (4u << 20)) {
    if (g_rejected.fetch_add(1) < 8) {
      REXLOG_WARN("[shaders] {} at {:08X}: not a shader container (flags {:08X}, size {})", stage, address,
                  flags, size);
    }
    return;
  }
  const uint8_t* data = REX_RAW_ADDR(address);
  const uint64_t hash = XXH3_64bits(data, size);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_seen.insert(hash).second) return;
  if (g_dir.empty()) {
    g_dir = rex::filesystem::GetExecutableFolder() / "shaders_dump";
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
  }
  char name[64];
  std::snprintf(name, sizeof(name), "%s_%016llx.bin", stage, static_cast<unsigned long long>(hash));
  const auto path = g_dir / name;
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) return;  // from an earlier run
  if (FILE* f = std::fopen(path.string().c_str(), "wb")) {
    std::fwrite(data, 1, size_t(size), f);
    std::fclose(f);
    const uint32_t n = g_written.fetch_add(1) + 1;
    if (missing && ds3::shaders::Global().cargada()) {
      REXLOG_WARN("[shaders] {} not in the library: written to shaders_dump/{} ({} bytes)", stage, name, size);
    } else if (n <= 4 || n % 100 == 0) {
      REXLOG_INFO("[shaders] {} new containers written (last: {} {} bytes, flags {:08X})", n, name, size, flags);
    }
  }
}

// The library entry of the container at `address` (r3 before the call), or nullptr.
const rex::x360::EntradaShader* Entrada(uint8_t* base, uint32_t address) {
  auto& biblioteca = ds3::shaders::Global();
  if (!address || !biblioteca.cargada()) return nullptr;
  const uint32_t flags = REX_LOAD_U32(address);
  const uint64_t size = uint64_t(REX_LOAD_U32(address + 4)) + REX_LOAD_U32(address + 8);
  if ((flags & 0xFFFF0000u) != 0x102A0000u || size < 36 || size > (4u << 20)) return nullptr;
  return biblioteca.PorContenedor({REX_RAW_ADDR(address), size_t(size)});
}

// After the call r3 is the new object.
void Registrar(PPCContext& ctx, uint32_t container, const rex::x360::EntradaShader* entrada) {
  auto& biblioteca = ds3::shaders::Global();
  if (container && biblioteca.cargada()) {
    biblioteca.RegistrarObjeto(rex::system::kernel_memory(), ctx.r3.u32, entrada);
  }
}

}  // namespace

REX_EXTERN(__imp__ds3_D3DDevice_CreateVertexShader);
REX_EXTERN(ds3_D3DDevice_CreateVertexShader) {
  const uint32_t container = ctx.r3.u32;
  const rex::x360::EntradaShader* entrada = Entrada(base, container);
  Dump(base, container, "vs", !entrada);
  __imp__ds3_D3DDevice_CreateVertexShader(ctx, base);
  Registrar(ctx, container, entrada);
}

REX_EXTERN(__imp__ds3_D3DDevice_CreatePixelShader);
REX_EXTERN(ds3_D3DDevice_CreatePixelShader) {
  const uint32_t container = ctx.r3.u32;
  const rex::x360::EntradaShader* entrada = Entrada(base, container);
  Dump(base, container, "ps", !entrada);
  __imp__ds3_D3DDevice_CreatePixelShader(ctx, base);
  Registrar(ctx, container, entrada);
}
