// ds3-nx - discs and title update on the Switch.
//
// ds2-nx's deadspace2/src/ds2_disc_swap.cpp (itself adapted from Dead-Space-2-Recomp's PC build), for Dead Space 3.
// Each disc is an extracted folder mounted with HostPathDevice (the Switch cannot map a whole ISO in memory); the
// bigfiles are under 4 GB, so FAT32 SD cards work.
//
// Layout the game sees:
//   game:                 the title update folder: disc 1's executables + Title Update 1's .xexp files
//   d:, \Device\Cdrom0    the disc in the drive (disc 1 or 2), switched by XamSwapDisc
//
// The game asks for disc 2 at chapter 5 (disc1_info.txt / disc2_info.txt list the chapters). What differs from
// Dead Space 2: the disc is identified by deadspace_game_f.xex (the game module), and XamSwapDisc is taken over through
// the SDK's handler (sdk-patches/0014) instead of rebinding an import thunk at a fixed TU address.

#include "ds3_pch.h"
#include "ds3_disc_swap.h"

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/util/xex2_info.h>
#include <rex/system/xevent.h>
#include <rex/thread.h>

#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>

using rex::X_STATUS;  // SDK status macros use this unqualified typedef.

namespace ds3::disc_swap {
namespace {
using namespace rex::filesystem;

constexpr std::array<const char*, 2> kDevices{"\\Device\\Ds3Disc1", "\\Device\\Ds3Disc2"};
constexpr std::array<const char*, 2> kAliases{"d:", "\\Device\\Cdrom0"};
constexpr const char* kUpdate = "\\Device\\Ds3TitleUpdate";
constexpr uint32_t kTitleId = 0x4541099D;
constexpr const char* kGameXex = "deadspace_game_f.xex";

VirtualFileSystem* fs = nullptr;
std::array<Entry*, 2> roots{};
Entry* update_root = nullptr;
unsigned current_disc = 0;

struct CloseFile {
  void operator()(File* f) const {
    if (f) f->Destroy();
  }
};

template <typename T>
T Read(File& file, size_t offset) {
  T value{};
  const size_t size = file.entry()->size();
  if (offset > size || sizeof(T) > size - offset) throw std::runtime_error("Truncated disc XEX header");
  size_t count = 0;
  if (file.ReadSync({reinterpret_cast<uint8_t*>(&value), sizeof(T)}, offset, &count) != X_STATUS_SUCCESS ||
      count != sizeof(T))
    throw std::runtime_error("Cannot read disc XEX header");
  return value;
}

// Which disc a mounted folder is: the XEX execution info (title, disc number) and the disc's marker file.
unsigned Identify(Device& device) {
  auto* entry = device.ResolvePath(kGameXex);
  File* raw = nullptr;
  if (!entry || entry->Open(FileAccess::kGenericRead, &raw) != X_STATUS_SUCCESS)
    throw std::runtime_error(std::string("Disc folder has no readable ") + kGameXex);
  std::unique_ptr<File, CloseFile> file(raw);
  auto header = Read<rex::xex2_header>(*file, 0);
  if (header.magic != 0x58455832 ||
      header.header_count > (entry->size() - offsetof(rex::xex2_header, headers)) / sizeof(rex::xex2_opt_header))
    throw std::runtime_error("Invalid disc XEX header");
  for (uint32_t i = 0; i < header.header_count; ++i) {
    auto opt = Read<rex::xex2_opt_header>(*file, offsetof(rex::xex2_header, headers) +
                                                     i * sizeof(rex::xex2_opt_header));
    if (opt.key != rex::XEX_HEADER_EXECUTION_INFO) continue;
    auto info = Read<rex::xex2_opt_execution_info>(*file, opt.offset);
    if (info.title_id != kTitleId || info.disc_count != 2 || info.disc_number < 1 || info.disc_number > 2)
      throw std::runtime_error("Folder is not a Dead Space 3 two-disc release");
    const auto marker = "disc" + std::to_string(info.disc_number) + "_info.txt";
    if (!device.ResolvePath(marker)) throw std::runtime_error("DS3 disc identity marker is missing");
    REXLOG_INFO("DS3_DISC_ID disc={} title={:08X} media={:08X}", info.disc_number, uint32_t(info.title_id),
                uint32_t(info.media_id));
    return info.disc_number;
  }
  throw std::runtime_error("Disc XEX execution info missing");
}

unsigned Mount(const std::filesystem::path& folder, unsigned expected) {
  auto disc = std::make_unique<HostPathDevice>(kDevices[expected - 1], folder, true);
  if (!disc->Initialize()) throw std::runtime_error("Cannot open disc folder: " + folder.string());
  const auto number = Identify(*disc);
  if (number != expected)
    throw std::runtime_error("Folder " + folder.string() + " holds disc " + std::to_string(number) +
                             ", expected disc " + std::to_string(expected));
  auto* root = disc->ResolvePath("");
  if (!root || !fs->RegisterDevice(std::move(disc))) throw std::runtime_error("Cannot register disc mount");
  roots[number - 1] = root;
  return number;
}

bool Select(unsigned number) {
  if (number < 1 || number > roots.size() || !roots[number - 1]) return false;
  if (fs->ResolvePath("game:") != update_root) return false;
  struct Previous {
    bool present;
    std::string target;
  };
  std::array<Previous, kAliases.size()> previous;
  for (size_t i = 0; i < kAliases.size(); ++i)
    previous[i].present = fs->FindSymbolicLink(kAliases[i], previous[i].target);
  try {
    for (auto alias : kAliases) {
      fs->UnregisterSymbolicLink(alias);
      if (!fs->RegisterSymbolicLink(alias, kDevices[number - 1]) || fs->ResolvePath(alias) != roots[number - 1])
        throw std::runtime_error("Alias verification failed");
    }
    if (fs->ResolvePath("game:") != update_root) throw std::runtime_error("Title update mapping changed");
  } catch (...) {
    for (size_t i = 0; i < kAliases.size(); ++i) {
      fs->UnregisterSymbolicLink(kAliases[i]);
      if (previous[i].present && !fs->RegisterSymbolicLink(kAliases[i], previous[i].target))
        throw std::runtime_error("Disc alias rollback failed");
    }
    return false;
  }
  current_disc = number;
  REXLOG_INFO("DS3_DISC_SELECT disc={} device={} title_update_kept=true", number, kDevices[number - 1]);
  return true;
}
}  // namespace

void Prepare(rex::Runtime& runtime, const std::filesystem::path& update_directory,
             const std::filesystem::path& disc1, const std::filesystem::path& disc2) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(update_directory / "deadspace_game_f.xexp", ec))
    throw std::runtime_error("Title Update 1 is required: deadspace_game_f.xexp is missing from " +
                             update_directory.string() + " (tools/stage_sd.py, INSTALL.md)");
  fs = runtime.file_system();
  Mount(disc1, 1);
  if (!disc2.empty() && std::filesystem::is_directory(disc2, ec)) {
    Mount(disc2, 2);
  } else {
    REXLOG_WARN("ds3: no disc 2 folder; the game will stop at the disc swap in chapter 5");
  }
  auto update = std::make_unique<HostPathDevice>(kUpdate, update_directory, true);
  if (!update->Initialize()) throw std::runtime_error("Cannot initialize title update mount");
  update_root = update->ResolvePath("");
  if (!update_root || !fs->RegisterDevice(std::move(update))) throw std::runtime_error("Cannot register title update mount");
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  fs->UnregisterSymbolicLink("game:");
  if (!fs->RegisterSymbolicLink("game:", kUpdate) || !Select(1))
    throw std::runtime_error("Cannot select disc 1 and the title update mapping");
}

}  // namespace ds3::disc_swap

REX_EXTERN(ds3_XamSwapDisc);

namespace rex::kernel::xam {
void SetSwapDiscHandler(void (*handler)(PPCContext& ctx, uint8_t* base));  // sdk-patches/0014
}

namespace ds3::disc_swap {
// The game module calls the SDK's XamSwapDisc export directly (an import, not a thunk the dispatcher could rebind, as
// Dead Space 2's port did): the SDK hands the call to this handler instead of its stub.
void RegisterImport(rex::Runtime&) {
  rex::kernel::xam::SetSwapDiscHandler(ds3_XamSwapDisc);
  REXLOG_INFO("ds3: XamSwapDisc goes to the disc swap");
}
}  // namespace ds3::disc_swap

// XamSwapDisc(disc number, completion event, ...): the game's request for the other disc. Point d: at it
// and signal the event, as the console does once the new disc is in.
REX_EXTERN(ds3_XamSwapDisc) {
  using namespace ds3::disc_swap;
  const uint32_t requested = ctx.r3.u32;
  const uint32_t event_address = ctx.r4.u32;
  ctx.r3.u64 = X_STATUS_UNSUCCESSFUL;
  REXLOG_INFO("DS3_DISC_REQUEST requested={} current={} event={:08X}", requested, current_disc, event_address);
  try {
    if (!fs || !event_address) throw std::runtime_error("Swap has no mount state or completion event");
    auto* kernel = rex::system::kernel_state();
    auto event = rex::system::XObject::GetNativeObject(kernel, base + event_address);
    if (!event || event->type() != rex::system::XObject::Type::Event)
      throw std::runtime_error("Invalid swap completion event");
    auto lock = rex::thread::global_critical_region::AcquireDirect();
    if (!Select(requested)) throw std::runtime_error("Requested disc unavailable or alias transaction failed");
    static_cast<rex::system::XEvent*>(event.get())->Set(1, false);
    ctx.r3.u64 = X_STATUS_SUCCESS;
    REXLOG_INFO("DS3_DISC_COMPLETE disc={} event={:08X}", requested, event_address);
  } catch (const std::exception& error) {
    REXLOG_ERROR("DS3_DISC_FAILED requested={} reason={}", requested, error.what());
  }
}
