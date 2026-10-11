#!/usr/bin/env bash
# One-time setup for building ModelViewer's .deb inside WSL (or any bare
# Ubuntu 26.04 box). Run this yourself inside the target distro - it needs
# sudo (apt-get) and compiles vcpkg + the two vendored ports (basisu,
# openimagedenoise), so it isn't something to run unattended.
#
# CGAL is the one dependency taken from upstream instead of apt: Ubuntu 26.04's libcgal-dev is 6.1.x and the code needs 6.2 or newer. CGAL is
# header-only, so this installs the release's library tarball (checksum-verified) under /usr/local; nothing CGAL-related is needed to RUN the .deb.
#
# Ubuntu 22.04/24.04's apt-packaged Qt6/Assimp/zstd are too old for this
# project (missing Qt6Svg's QImage::flipped(), Assimp's
# KHR_materials_anisotropy, a zstd config exposing the zstd::libzstd target) -
# this is the same gap that caused repeated CI failures on ubuntu-24.04,
# which is why CI is pinned to ubuntu-26.04 (see .github/workflows/build.yml).
# Use Ubuntu 26.04 here too: `wsl --install -d Ubuntu-26.04` from PowerShell,
# then run this script inside that distro.
#
# Usage: ./setup-wsl-build-env.sh [path-to-clone-repo-into]
#   (defaults to ~/dev/ModelViewer-Qt)

set -euo pipefail

if [[ "$(lsb_release -rs 2>/dev/null)" != "26.04" ]]; then
  echo "warning: this project's Linux packaging deps need Ubuntu 26.04 - continuing anyway" >&2
fi

# Matches .github/workflows/build.yml's "Install system dependencies (Ubuntu)"
# step exactly, so this environment tracks whatever CI already validated.
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  curl \
  zip \
  unzip \
  tar \
  git \
  gcc \
  g++ \
  qt6-base-dev \
  qt6-tools-dev \
  qt6-l10n-tools \
  qt6-svg-dev \
  qt6-webengine-dev \
  qt6-image-formats-plugins \
  libqt-advanced-docking-system-dev \
  libocct-foundation-dev \
  libocct-modeling-data-dev \
  libocct-modeling-algorithms-dev \
  libocct-data-exchange-dev \
  libocct-ocaf-dev \
  libembree-dev \
  libassimp-dev \
  libmeshoptimizer-dev \
  libopenexr-dev \
  libfreetype-dev \
  libglm-dev \
  libwebp-dev \
  libjpeg-turbo8-dev \
  libtbb-dev \
  libzstd-dev \
  libboost-dev \
  libeigen3-dev \
  libgmp-dev \
  libmpfr-dev \
  libhdf5-dev \
  libnetcdf-dev \
  libcgns-dev \
  imagemagick \
  dpkg-dev

# --- CGAL 6.2 or newer, from upstream -------------------------------------------------------------------------------------------------------
CGAL_VERSION="6.2.1"
CGAL_SHA256="8e7ce1e30e8ffcb3e69d153c3a561bb1f38bbb4becd8e6544cbaa0fb2889af62"   # CGAL-6.2.1-library.tar.xz (GitHub release asset digest)
CGAL_PREFIX="/usr/local"
CGAL_NEEDED_NR=1060200000                                                          # CGAL_VERSION_NR of 6.2.0 (the code needs >= 6.2)

installed_nr=0
if [[ -f "$CGAL_PREFIX/include/CGAL/version.h" ]]; then
  installed_nr="$(sed -n 's/^#define CGAL_VERSION_NR \([0-9]*\).*/\1/p' "$CGAL_PREFIX/include/CGAL/version.h" | head -1)"
  installed_nr="${installed_nr:-0}"
fi
if (( installed_nr >= CGAL_NEEDED_NR )); then
  echo "CGAL already installed in $CGAL_PREFIX (CGAL_VERSION_NR $installed_nr), skipping"
else
  echo "Installing CGAL $CGAL_VERSION into $CGAL_PREFIX"
  CGAL_TMP="$(mktemp -d)"
  curl -fL "https://github.com/CGAL/cgal/releases/download/v$CGAL_VERSION/CGAL-$CGAL_VERSION-library.tar.xz" -o "$CGAL_TMP/cgal.tar.xz"
  echo "$CGAL_SHA256  $CGAL_TMP/cgal.tar.xz" | sha256sum -c -
  tar -xf "$CGAL_TMP/cgal.tar.xz" -C "$CGAL_TMP"
  cmake -S "$CGAL_TMP/CGAL-$CGAL_VERSION" -B "$CGAL_TMP/build" -DCMAKE_BUILD_TYPE=Release
  sudo cmake --install "$CGAL_TMP/build" --prefix "$CGAL_PREFIX"
  rm -rf "$CGAL_TMP"
fi
if dpkg -s libcgal-dev >/dev/null 2>&1; then
  echo "note: apt's libcgal-dev is installed too (older than 6.2); $CGAL_PREFIX is searched first, but remove it if CMake ever picks the old one." >&2
fi

REPO_DIR="${1:-$HOME/dev/ModelViewer-Qt}"
if [[ -d "$REPO_DIR/.git" ]]; then
  echo "Repo already present at $REPO_DIR, pulling dev"
  git -C "$REPO_DIR" fetch origin
  git -C "$REPO_DIR" checkout dev
  git -C "$REPO_DIR" pull
else
  echo "Cloning into $REPO_DIR"
  git clone --branch dev https://github.com/sharjith/ModelViewer-Qt.git "$REPO_DIR"
fi
git -C "$REPO_DIR" submodule update --init --recursive

cd "$REPO_DIR"

# Bootstraps vcpkg itself, then builds/fetches just the two deps with no apt
# package (openimagedenoise's overlay port repackages prebuilt binaries;
# basisu is a plain CMake project) - see packaging/linux/vendor/vcpkg.json.
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install --triplet x64-linux \
  --x-manifest-root=packaging/linux/vendor \
  --x-install-root=packaging/linux/vendor/vcpkg_installed \
  --overlay-ports=overlay-ports \
  --overlay-triplets=vcpkg/triplets

cat <<EOF

Setup done. Repo is at: $REPO_DIR

To build the .deb from here on (this is the part you run yourself):

  cmake --preset linux_system_release
  cmake --build out/build/linux_system_release --parallel
  cd out/build/linux_system_release && cpack -G DEB

(No --target: the Linux .desktop icon is generated by a sibling ALL target,
modelviewer_icon_png, not a dependency of ModelViewer - scoping the build to
--target ModelViewer skips it and cpack fails looking for modelviewer.png.
No ctest step either - this project has no registered tests yet, so it just
prints "No tests were found!!!" and adds nothing.)

The resulting .deb lands in out/build/linux_system_release/*.deb - upload it
with packaging/publish-release.sh from there (or copy it out to wherever you
run that script from).
EOF
