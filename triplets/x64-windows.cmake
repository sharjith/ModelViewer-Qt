set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)

# Pinned to the exact MSVC toolset currently installed (VC/Tools/MSVC/14.44.35207) - without this,
# vcpkg folds whatever toolset version happens to be "current" into every port's ABI hash, so a
# routine Visual Studio update (which bumps the toolset minor version) silently invalidates the
# ENTIRE binary cache at once and forces a full rebuild of every dependency, CGAL included (by far
# the slowest one here). Pinning keeps the cache valid across IDE updates.
#
# This only works as long as MSVC v143 build tools (v14.44...) stays installed side-by-side with
# whatever VS installs next - open Visual Studio Installer > Modify > Individual Components and
# confirm/check that specific versioned toolset entry (not just the default "latest" one) so a
# future update doesn't remove it out from under this pin. If it ever does get removed, or you
# deliberately move to a newer toolset, update the version here to match and expect one full
# rebuild for that transition.
set(VCPKG_PLATFORM_TOOLSET_VERSION "14.44")
