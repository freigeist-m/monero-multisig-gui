#!/usr/bin/env bash
set -euo pipefail

# Ensure we're in the MSYS2 MinGW x64 shell
if [[ "${MSYSTEM:-}" != "MINGW64" ]]; then
  echo "ERROR: Run this from the MSYS2 MinGW x64 shell (MSYSTEM=MINGW64)." >&2
  exit 1
fi

# Always prefer mingw64 bin first
export PATH="/mingw64/bin:$PATH"

# ---- Tools ----
WINDEPLOYQT="${WINDEPLOYQT:-/mingw64/bin/windeployqt6.exe}"
QTPATHS="${QTPATHS:-/mingw64/bin/qtpaths6.exe}"

# Find qmlimportscanner (MSYS2 puts it under share/qt6/bin)
QMLIMPORTSCANNER_CANDIDATES=(
  /mingw64/share/qt6/bin/qmlimportscanner.exe
  /mingw64/bin/qmlimportscanner.exe
  /ucrt64/share/qt6/bin/qmlimportscanner.exe
  /ucrt64/bin/qmlimportscanner.exe
  /clang64/share/qt6/bin/qmlimportscanner.exe
  /clang64/bin/qmlimportscanner.exe
)
QMLIMPORTSCANNER=""
for cand in "${QMLIMPORTSCANNER_CANDIDATES[@]}"; do
  if [[ -x "$cand" ]]; then QMLIMPORTSCANNER="$cand"; break; fi
done

[[ -x "$WINDEPLOYQT" ]] || { echo "windeployqt6 not found at $WINDEPLOYQT" >&2; exit 1; }
[[ -x "$QTPATHS"    ]] || { echo "qtpaths6 not found at $QTPATHS" >&2; exit 1; }
[[ -x "$QMLIMPORTSCANNER" ]] || {
  echo "qmlimportscanner.exe not found. Try: pacman -S --needed mingw-w64-x86_64-qt6-declarative" >&2
  exit 1
}

# Put qmlimportscanner's dir at the front of PATH (windeployqt calls it by name)
export PATH="$(dirname "$QMLIMPORTSCANNER"):$PATH"

echo "Using tools:"
echo "  windeployqt6:     $WINDEPLOYQT"
echo "  qmlimportscanner: $QMLIMPORTSCANNER"
echo "  qtpaths6:         $QTPATHS"

# ---- Args ----
VERSION="${1:?Usage: $0 <version> <path-to-exe>}"
EXE_POSIX="${2:?Usage: $0 <version> <path-to-exe>}"

# ---- Paths ----
EXE_WIN="$(cygpath -w "$EXE_POSIX")"
EXE_DIR_POSIX="$(cd "$(dirname "$EXE_POSIX")"; pwd)"
EXE_BASENAME="$(basename "$EXE_POSIX")"

ROOT_POSIX="$(cd "$(dirname "$0")/.."; pwd)"
QML_POSIX="$ROOT_POSIX/qml"
[[ -d "$QML_POSIX" ]] || { echo "ERROR: QML dir not found: $QML_POSIX" >&2; exit 1; }
QML_WIN="$(cygpath -w "$QML_POSIX")"

# Final output bundle
OUT_POSIX="$ROOT_POSIX/dist/Monero_Multisig_GUI-$VERSION"
mkdir -p "$OUT_POSIX"

echo "Deploying:"
echo "  EXE : $EXE_WIN"
echo "  QML : $QML_WIN"
echo "  OUT : $(cygpath -w "$OUT_POSIX")"

# Help windeployqt find QML modules: system + app qml
# (windeployqt uses qmlimportscanner which consults this)
SYS_QML="/mingw64/share/qt6/qml"
export QML2_IMPORT_PATH="$SYS_QML:$QML_POSIX"

# 1) Deploy NEXT TO THE EXE (this is the most reliable mode on MSYS2)
#    NOTE: No --dir here on purpose.
echo "==> Running windeployqt next to the built EXE..."
"$WINDEPLOYQT" --verbose=2 --release --qmldir "$QML_WIN" "$EXE_WIN"

# 2) Seed qt.conf into the EXE directory (helps double-click)
cat > "$EXE_DIR_POSIX/qt.conf" <<'CONF'
[Paths]
Plugins=.
Qml2Imports=./qml
Libraries=.
CONF

# 3) Copy the deployed tree (next to the EXE) into our final OUT dir
echo "==> Copying deployed files to $OUT_POSIX ..."
# Ensure we bring typical Qt folders if present
rsync -a --ignore-existing \
  "$EXE_DIR_POSIX/$EXE_BASENAME" \
  "$EXE_DIR_POSIX/qt.conf" \
  "$EXE_DIR_POSIX/"Qt6*.dll \
  "$EXE_DIR_POSIX/"lib*.dll \
  "$EXE_DIR_POSIX"/{platforms,imageformats,iconengines,generic,networkinformation,scenegraph,sqldrivers,tls,printsupport,platformthemes,qml,qmltooling,translations} \
  "$OUT_POSIX/" 2>/dev/null || true

# Fallback: if rsync missed anything because some dirs don't exist, do a broad copy for known dirs
for d in platforms imageformats iconengines generic networkinformation scenegraph sqldrivers tls printsupport platformthemes qml qmltooling translations; do
  if [[ -d "$EXE_DIR_POSIX/$d" ]]; then
    mkdir -p "$OUT_POSIX/$d"
    cp -ru "$EXE_DIR_POSIX/$d/"* "$OUT_POSIX/$d/" || true
  fi
done

echo "Done. Output at: $OUT_POSIX"

RUNTIME_DLLS=(
  # GCC / pthreads
  libgcc_s_seh-1.dll
  libstdc++-6.dll
  libwinpthread-1.dll

  # Crypto / compression / text
  libsodium-*.dll
  libb2-1.dll
  libbz2-1.dll
  libzstd.dll
  zlib1.dll
  libdouble-conversion.dll
  libmd4c.dll
  libpcre2-8-0.dll
  libpcre2-16-0.dll

  # Fonts / shaping / images
  libharfbuzz-*.dll
  libgraphite2*.dll
  libfreetype-*.dll
  libpng16-16.dll

  # ICU (version may change, wildcard)
  libicudt*.dll
  libicuin*.dll
  libicuuc*.dll

  # GLib / gettext / iconv
  libglib-2.0-0.dll
  libintl-8.dll
  libiconv-2.dll

  # Brotli (Qt’s freetype pipeline may pull these)
  libbrotlidec*.dll
  libbrotlicommon*.dll
)

# Copy from MinGW bin if present
for pat in "${RUNTIME_DLLS[@]}"; do
  for src in /mingw64/bin/$pat; do
    [[ -e "$src" ]] && cp -u "$src" "$OUT_POSIX/" 2>/dev/null || true
  done
done

# D3D shader compiler (Qt Quick on some GPUs)
if [[ ! -e "$OUT_POSIX/D3Dcompiler_47.dll" ]]; then
  if [[ -e /mingw64/bin/D3Dcompiler_47.dll ]]; then
    cp -u /mingw64/bin/D3Dcompiler_47.dll "$OUT_POSIX/" || true
  elif [[ -e /c/Windows/System32/D3Dcompiler_47.dll ]]; then
    cp -u /c/Windows/System32/D3Dcompiler_47.dll "$OUT_POSIX/" || true
  fi
fi



# Quick sanity check: make sure we have at least some key bits
need=( "Qt6Core.dll" "platforms/qwindows.dll" "qml/QtQuick/qtquick2plugin.dll" )
missing=0
for f in "${need[@]}"; do
  if [[ ! -e "$OUT_POSIX/$f" ]]; then
    echo "WARN: Missing $f in output bundle." >&2
    missing=1
  fi
done
if [[ $missing -ne 0 ]]; then
  echo "NOTE: If files are missing, check windeployqt output above for errors." >&2
fi
