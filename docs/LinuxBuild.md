git clone --recursive <your-repo-url>   # --recursive to pull the vcpkg submodule too
cd ModelViewer-Qt

# same system deps CI installs via apt (X11/GL/fontconfig dev headers etc.)
sudo apt-get install -y build-essential cmake ninja-build pkg-config \
  libx11-dev libxext-dev libxrender-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxi-dev libgl1-mesa-dev libglu1-mesa-dev libegl1-mesa-dev \
  libxkbcommon-dev libxkbcommon-x11-dev libfontconfig1-dev libfreetype6-dev \
  libtbb-dev autoconf automake libtool imagemagick

export VCPKG_OVERLAY_TRIPLETS="$PWD/vcpkg/triplets"
export VCPKG_DEFAULT_TRIPLET=x64-linux

cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DCMAKE_INSTALL_PREFIX=install \
  -G Ninja

cmake --build build --config Release --parallel
cmake --install build --prefix install --config Release