# Monero Multisig GUI

Minimal Qt 6 / QML desktop application for coordinating Monero multisig wallets.

## Table of Contents

- [Introduction](#introduction)
- [Features](#features)
- [Supporting the Project](#supporting-the-project)
- [Building from Source](#building-from-source)
  - [Linux (Ubuntu/Debian)](#building-from-source-linux)
  - [Windows](#building-from-source-windows)
- [Planned Features](#planned-features)
- [License](#license)

## Introduction

Monero Multisig GUI is a lightweight desktop application designed to simplify the process of creating and coordinating Monero multisig (multi-signature) wallets.

**What is Multisig?**  
Multisig (multi-signature) wallets require multiple signatures from different participants to authorize a transaction. This provides enhanced security for shared funds, requiring consensus from multiple parties (e.g., 2-of-3, 3-of-5) before any transaction can be executed.

## Features

- **Multisig Wallet Generation**: Easy setup and coordination of Monero multisig wallets
- **Multisig Transfers**: Easy creation of transfers among parties
- **Privacy-Focused**: Built on Monero's privacy-preserving cryptocurrency technology & Tor Project Network

## Supporting the Project

If you find this project useful and would like to support its continued development, donations are greatly appreciated:

**Monero Donation Address:**
```
85pvPfsGkFNXPNXX8ZjizHgojtL2BXuYJSXSMNzN4SWsfMf5ZZTGyTD4UxHZviZmPTMYebYHMWUUxP3WdE4bo8tdBgbsBGM
```

Your support helps fund development time, testing, and ongoing maintenance of this open-source project.

---

## Building from Source (Linux)

**Build target:** Linux (Ubuntu/Debian-based distributions)  
**Packaging:** AppImage (single-file portable application)

### 0) Clone the Repository

First, clone the project from GitHub:
```bash
git clone --branch v0.1.0 https://github.com/freigeist-m/monero-multisig-gui.git
cd monero-multisig-gui
```

Alternatively, to clone the latest development version:
```bash
git clone https://github.com/freigeist-m/monero-multisig-gui.git
cd monero-multisig-gui
```

### 1) Prerequisites (Linux)

Install Monero dependencies and build tools:

```bash
sudo apt update && sudo apt install \
  build-essential \
  cmake \
  pkg-config \
  libssl-dev \
  libzmq3-dev \
  libunbound-dev \
  libsodium-dev \
  libunwind8-dev \
  liblzma-dev \
  libreadline6-dev \
  libexpat1-dev \
  libhidapi-dev \
  libusb-1.0-0-dev \
  libprotobuf-dev \
  protobuf-compiler \
  libudev-dev \
  libboost-chrono-dev \
  libboost-date-time-dev \
  libboost-filesystem-dev \
  libboost-locale-dev \
  libboost-program-options-dev \
  libboost-regex-dev \
  libboost-serialization-dev \
  libboost-system-dev \
  libboost-thread-dev \
  python3 \
  ccache \
  git \
  curl \
  autoconf \
  libtool \
  gperf
```

Install Qt 6.8.2+ (via Qt online installer recommended):

1. Download the Qt online installer from https://www.qt.io/download-qt-installer
2. Install Qt 6.8.2 or later with the following components:
   - Desktop gcc 64-bit
   - Qt Quick
   - Qt Network
   - Qt HTTP Server

**Minimum Qt version:** 6.8.2+

### 2) Build with Qt Creator

Launch Qt Creator (from the Qt installation directory):

```bash
~/Qt/Tools/QtCreator/bin/qtcreator &
```

In Qt Creator:

1. **Open Project:** Navigate to and open the `monero-multisig-gui` folder
2. **Kit:** Select **Desktop Qt 6.8.2+ (GCC 64-bit)**
3. **Build Configuration:** Set to **Release**
4. **Build:** Build the project (Release mode)

The output executable will be in your Qt Creator build directory (e.g., `build-Desktop_Qt_6_8_2-Release/appmonero-multisig-gui`).

### 3) Create AppImage Package

Download linuxdeploy tools (one-time setup):

```bash
# From your home directory or a tools folder
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x linuxdeploy-*.AppImage
```

Set environment variables to point to your Qt installation:

```bash
# Point to your Qt 6.8.2 installation (adjust path if different)
export QMAKE="$HOME/Qt/6.8.2/gcc_64/bin/qmake"
export QML_SOURCES_PATHS="/path/to/monero-multisig-gui/qml"
```

Create the AppImage (from the repository root):

```bash
# Clean build (recommended)
rm -rf build AppDir

# Build and stage
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cmake --install build --prefix AppDir/usr

# Create AppImage

# Option A: using the binary directly from your *Release* build folder (e.g. Qt Creator)
# Replace <PATH_TO_RELEASE_BUILD> with your actual Release build dir
#   Examples:
#     ~/monero-multisig-gui/build-Desktop_Qt_6_8_2-Release
#     ~/monero-multisig-gui/build/Desktop_Qt_6_8_2-Release
EXECUTABLE="<PATH_TO_RELEASE_BUILD>/appmonero-multisig-gui"

# Option B: if you ran `cmake --install build --prefix AppDir/usr`, use the staged binary:
# EXECUTABLE="AppDir/usr/bin/appmonero-multisig-gui"

./linuxdeploy-x86_64.AppImage \
  --appdir AppDir \
  -e "$EXECUTABLE" \
  -d monero-multisig.desktop \
  -i resources/icons/monero_rotated_blue.svg \
  --plugin qt \
  --output appimage

This will create a file named `Monero_Multisig_GUI-x86_64.AppImage` in your current directory.

### 4) Running the AppImage

Make the AppImage executable and run it:

```bash
chmod +x Monero_Multisig_GUI-*.AppImage
./Monero_Multisig_GUI-*.AppImage
```

The AppImage is a self-contained single file that can be distributed and run on most modern Linux distributions without installation.

### Optional: Wayland Support

If you want to include Wayland platform plugins in addition to X11:

```bash
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
```

Then run the linuxdeploy command as shown above.

### Troubleshooting

**Qt version mismatch:**
Ensure `QMAKE` points to the same Qt version you used to build the app. Check with:
```bash
"$QMAKE" -query QT_VERSION
```

**Missing platform plugins:**
If the AppImage fails to start with platform plugin errors, ensure you're using a clean `AppDir`:
```bash
rm -rf AppDir
cmake --install build --prefix AppDir/usr
```

---

## Building from Source (Windows)

**Build target:** Windows (MSYS2 + Qt Creator, MinGW 64-bit)  
**Packaging:** Portable ZIP (self-contained `dist/Monero_Multisig_GUI-vX.Y.Z/`)


### 0) Clone the Repository

From the MSYS2 MINGW64 shell, clone the project:
```bash
git clone --branch v0.1.0 https://github.com/freigeist-m/monero-multisig-gui.git
cd monero-multisig-gui
```

Alternatively, to clone the latest development version:
```bash
git clone https://github.com/freigeist-m/monero-multisig-gui.git
cd monero-multisig-gui
```


### 1) Prerequisites (Windows)

Install MSYS2 → https://www.msys2.org/ (default path `C:\msys64`).

Open **MSYS2 MINGW64** shell and update the system:

```bash
pacman -Syu
```

You may need to reopen the shell, then run the update again:

```bash
pacman -Syu
```

Install all dependencies (Monero libraries, toolchain, Qt components):

```bash
pacman -S --needed \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-boost \
  mingw-w64-x86_64-openssl \
  mingw-w64-x86_64-zeromq \
  mingw-w64-x86_64-libsodium \
  mingw-w64-x86_64-hidapi \
  mingw-w64-x86_64-unbound \
  mingw-w64-x86_64-libzstd \
  mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-qt6-base \
  mingw-w64-x86_64-qt6-declarative \
  mingw-w64-x86_64-qt6-5compat \
  mingw-w64-x86_64-qt6-svg \
  mingw-w64-x86_64-qt6-shadertools \
  mingw-w64-x86_64-qt6-httpserver \
  mingw-w64-x86_64-qt6-tools \
  mingw-w64-x86_64-qt-creator \
  rsync
```

**Minimum Qt version:** 6.8.2+

### 2) Build with Qt Creator (MinGW 64-bit)

From the MSYS2 MINGW64 shell, launch Qt Creator so it inherits the MinGW environment:

```bash
qtcreator &
```

In Qt Creator:

1. **Open Project:** Navigate to and open the `monero-multisig-gui` folder
2. **Kit:** Select **Desktop Qt 6 (MinGW 64-bit)**
3. **Build Configuration:** Set to **Release**
4. **CMake Configuration:** Go to **Projects → Build → CMake** and add:
   ```
   CMAKE_CXX_FLAGS = -include cmath
   ```
5. **Build:** Build the project (Release mode)

The output executable will be in your Qt Creator build directory (e.g., `build-.../release/appmonero-multisig-gui.exe`).

### 3) Bundle for Distribution

From the repository root, run the bundling script:

```bash
cd scripts
./bundle-windows.sh v0.1.0 \
  /c/path/to/your/build/Desktop_MINGW64_MSYS2-Release/appmonero-multisig-gui.exe
```

The script will:
- Run `windeployqt6` to collect Qt DLLs, QML modules, and plugins
- Create a `qt.conf` file for standalone execution
- Copy everything to `dist/Monero_Multisig_GUI-<version>/`
- Include necessary runtime DLLs (GCC, crypto, compression, fonts, ICU, etc.)

### 4) Running the Bundle

Navigate to the `dist/Monero_Multisig_GUI-<version>/` folder and double-click `appmonero-multisig-gui.exe`.

**Important:** Do not move individual files out of the distribution folder—the relative directory structure must be maintained.

---

## Planned Features

The following features are planned for future releases:

- **macOS Support**: Native macOS application release (if donations and interest are high)
- **Headless Version**: Command-line/headless application client for backend integration and automation
- **GitHub Pages**: Documentation site with tutorials
- **Video Tutorials**: Step-by-step video guides for wallet setup and coordination


## License

Copyright (c) 2025, Monero Multisig GUI Project

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Parts of the project are originally copyright (c) 2012-2013 The Cryptonote
developers

---

## Resources

- [Monero Official Website](https://getmonero.org)
- [Monero Repository](https://github.com/monero-project)
- [Qt Framework](https://www.qt.io)
- [Tor Project](https://www.torproject.org/)

## Contact

For questions, suggestions, or issues, please open an issue on the GitHub repository.

---

**Disclaimer:** This is an independent project and is not officially affiliated with the Monero Project.
