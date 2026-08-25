#!/usr/bin/env bash
set -euo pipefail

artifact_kind="${1:?usage: $0 appimage|deb|all version}"
xds_version="${2:?usage: $0 appimage|deb|all version}"
qt_version="${QT_VERSION:-6.8.3}"
build_parallel_level="${BUILD_PARALLEL_LEVEL:-2}"

export DEBIAN_FRONTEND=noninteractive
export APPIMAGE_EXTRACT_AND_RUN=1

apt-get update -y
apt-get install -y --no-install-recommends \
  ca-certificates \
  build-essential \
  cmake \
  dpkg-dev \
  file \
  git \
  libboost-all-dev \
  libcups2 \
  libdbus-1-3 \
  libfontconfig1 \
  libfuse2 \
  libglib2.0-0 \
  libgl1-mesa-dev \
  libssl-dev \
  libxext6 \
  libxrender1 \
  libx11-xcb1 \
  libxcb-cursor0 \
  libxcb-glx0 \
  libxcb-icccm4 \
  libxcb-image0 \
  libxcb-keysyms1 \
  libxcb-randr0 \
  libxcb-render-util0 \
  libxcb-shape0 \
  libxcb-shm0 \
  libxcb-sync1 \
  libxcb-util1 \
  libxcb-xfixes0 \
  libxcb-xinerama0 \
  libxcb-xkb1 \
  libxkbcommon-x11-0 \
  patchelf \
  python3 \
  python3-pip \
  wget \
  xvfb \
  xz-utils

python3 -m pip install --no-cache-dir "aqtinstall==3.1.20"
python3 -m aqt install-qt linux desktop "$qt_version" linux_gcc_64 \
  -O /opt/Qt

git config --global --add safe.directory "$(pwd)"

qt_root="/opt/Qt/$qt_version/gcc_64"
export CMAKE_PREFIX_PATH="$qt_root"
export PATH="$qt_root/bin:$PATH"
export LD_LIBRARY_PATH="$qt_root/lib:${LD_LIBRARY_PATH:-}"
export LANG=C.UTF-8
export LC_ALL=C.UTF-8

for required_file in \
  "$qt_root/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake" \
  "$qt_root/lib/cmake/Qt6LinguistTools/Qt6LinguistToolsConfig.cmake"; do
  if [[ ! -f "$required_file" ]]; then
    echo "Required Qt component is missing: $required_file" >&2
    exit 1
  fi
done

for required_tool in qmake lrelease; do
  if ! command -v "$required_tool" >/dev/null 2>&1; then
    echo "Required Qt tool is missing: $required_tool" >&2
    exit 1
  fi
done

build_folder="build/release"
mkdir -p "$build_folder"
cmake -S . -B "$build_folder" \
  -DARCH=default \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_CXX_STANDARD=17 \
  -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" \
  -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" \
  -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=true \
  -DBOOST_IGNORE_SYSTEM_PATHS_DEFAULT=ON \
  -DBOOST_ROOT=/usr
cmake --build "$build_folder" --parallel "$build_parallel_level"
bash ci/verify-portable-linux-binary.sh \
  "$build_folder" "$build_folder/DiscreteWallet"
xvfb-run -a "$build_folder/DiscreteWallet" --version

cd appimage
chmod +x ./create-appimage.sh
./create-appimage.sh
appimage_name="Discrete-wallet-$xds_version.AppImage"
rm -f "$appimage_name"
mv -f Discrete*.AppImage "$appimage_name"
ls -l "$appimage_name"
xvfb-run -a "./$appimage_name" --version
cd ..

if [[ "$artifact_kind" == "appimage" ]]; then
  exit 0
fi

if [[ "$artifact_kind" != "deb" && "$artifact_kind" != "all" ]]; then
  echo "Unknown artifact kind: $artifact_kind" >&2
  exit 1
fi

deb_version="${xds_version#[vV]}"
deb_version="${deb_version#.}"
if [[ ! "$deb_version" =~ ^[0-9] ]]; then
  echo "Invalid Debian version derived from '$xds_version': '$deb_version'" >&2
  exit 1
fi
release_name="Discrete-wallet-linux-amd64-$xds_version"
pkgroot="build/debroot"
rm -rf "$pkgroot"
mkdir -p "$pkgroot/opt/discrete-wallet" \
  "$pkgroot/usr/share/applications" \
  "$pkgroot/usr/share/icons/hicolor/256x256/apps" \
  "$pkgroot/DEBIAN"

cp -a appimage/AppDir/. "$pkgroot/opt/discrete-wallet/"
cp appimage/AppDir/usr/share/applications/discretewallet.desktop \
  "$pkgroot/usr/share/applications/discretewallet.desktop"
sed -i 's|^Exec=.*|Exec=/opt/discrete-wallet/AppRun %U|' \
  "$pkgroot/usr/share/applications/discretewallet.desktop"
sed -i 's|^Icon=.*|Icon=discrete|' \
  "$pkgroot/usr/share/applications/discretewallet.desktop"
cp appimage/AppDir/usr/share/icons/hicolor/256x256/apps/discrete.png \
  "$pkgroot/usr/share/icons/hicolor/256x256/apps/discrete.png"

installed_size="$(du -ks "$pkgroot" | cut -f1)"
cat > "$pkgroot/DEBIAN/control" <<EOF
Package: discretewallet
Version: $deb_version
Section: utils
Priority: optional
Architecture: amd64
Maintainer: Discrete Developers <contact@discrete.cash>
Installed-Size: $installed_size
Depends: libc6 (>= 2.35), libgcc-s1, libstdc++6, libgl1, libx11-6, libxcb1, libxkbcommon-x11-0
Description: Discrete XDS wallet
 Discrete is a post-quantum-only cryptocurrency: a CryptoNote-family chain
 in which every block and transaction is post-quantum from genesis. It is
 open-source and decentralized, with no legacy elliptic-curve chain.
EOF

dpkg-deb --build "$pkgroot" "./appimage/$release_name.deb"
cmp appimage/AppDir/usr/bin/DiscreteWallet \
  "$pkgroot/opt/discrete-wallet/usr/bin/DiscreteWallet"
xvfb-run -a "$pkgroot/opt/discrete-wallet/AppRun" --version
