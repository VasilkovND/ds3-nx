// ds3-nx - the two discs and the title update, mounted from extracted folders (ds3_disc_swap.cpp).
#pragma once

#include <filesystem>

namespace rex { class Runtime; }

namespace ds3::disc_swap {
// Mounts disc 1 and (when present) disc 2 as folders, and the folder holding the title update (disc 1's executables
// with Title Update 1's .xexp files) as game:. d: and \Device\Cdrom0 point at the disc in the drive.
void Prepare(rex::Runtime& runtime, const std::filesystem::path& update_directory,
             const std::filesystem::path& disc1, const std::filesystem::path& disc2);
// Binds the executable's XamSwapDisc import to the swap handler. Call after the module loads, before it launches.
void RegisterImport(rex::Runtime& runtime);
}  // namespace ds3::disc_swap
