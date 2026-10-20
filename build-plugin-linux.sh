#!/usr/bin/env bash
# Builds the formuls instrument plugin (VST3 + LV2) for Linux.
#
# The plugin runs the same Pd patch and serves the same control GUI as the
# app, so it takes those resources (pd/ with its compiled .pd_linux externals,
# and the flattened gui/) from a Linux app build made by ./build-linux.sh,
# rather than compiling Faust and fetching Open Stage Control a second time.
# node is not copied: the plugin serves the GUI itself.
#
#   ./build-plugin-linux.sh                       # uses formuls-$VERSION-linux-<arch>
#   FORMULS_APP=/path/to/formuls-linux-folder ./build-plugin-linux.sh
#   INSTALL=1 ./build-plugin-linux.sh             # also copies both into ~/.vst3 and ~/.lv2
#
# Produces formuls-$VERSION-linux-<arch>-plugins/{formuls.vst3, formuls.lv2} in
# the repo root. The bundles keep the plain name: on Linux a VST3 host looks for
# Contents/<arch>-linux/<bundle name>.so, so a renamed bundle does not load.
# See docs/plugin/README.md.
#
# Bundle layouts (libpd.so sits next to the plugin binary, found through the
# $ORIGIN rpath set in formuls-plugin.jucer; ResourceLocator finds pd/ and gui/):
#   formuls.vst3/Contents/<arch>-linux/{formuls.so, libpd.so}
#   formuls.vst3/Contents/Resources/{pd, gui}
#   formuls.lv2/{formuls.so, libpd.so, manifest.ttl, dsp.ttl, pd, gui}

set -euo pipefail

VERSION=0.3.1
ROOT="$(cd "$(dirname "$0")" && pwd)"

case "$(uname -m)" in
    x86_64|amd64)  ARCH="x64";   VST3ARCH="x86_64-linux" ;;
    aarch64|arm64) ARCH="arm64"; VST3ARCH="aarch64-linux" ;;
    *) echo "Unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

APP="${FORMULS_APP:-$ROOT/formuls-$VERSION-linux-$ARCH}"
if [[ ! -f "$APP/pd/_main.pd" || ! -f "$APP/gui/_main.json" || ! -d "$APP/gui/open-stage-control/client" ]]; then
    echo "No built app resources at $APP." >&2
    echo "Run ./build-linux.sh first, or point FORMULS_APP at a built formuls Linux folder." >&2
    exit 1
fi
if ! ls "$APP"/pd/externals/*.pd_linux >/dev/null 2>&1; then
    echo "$APP/pd/externals has no .pd_linux externals: is it a Linux build?" >&2
    exit 1
fi

# Projucer: as in build-linux.sh, the ready-made one or one built from source.
if [[ -z "${PROJUCER:-}" ]]; then
    for candidate in \
        "$HOME/JUCE/Projucer" \
        "$HOME/JUCE/extras/Projucer/Builds/LinuxMakefile/build/Projucer"; do
        if [[ -x "$candidate" ]]; then PROJUCER="$candidate"; break; fi
    done
fi
if [[ ! -x "${PROJUCER:-}" ]]; then
    echo "Projucer not found; install JUCE at ~/JUCE or set PROJUCER." >&2
    exit 1
fi
JUCE_DIR="$(dirname "$PROJUCER")"
while [[ "$JUCE_DIR" != "/" && ! -d "$JUCE_DIR/modules/juce_core" ]]; do
    JUCE_DIR="$(dirname "$JUCE_DIR")"
done
if [[ ! -d "$JUCE_DIR/modules/juce_core" ]]; then
    echo "Could not find the JUCE modules folder above: $PROJUCER" >&2
    exit 1
fi

# libpd: the plugin links the submodule's build. build-linux.sh cleans that
# away when it finishes, so rebuild it if needed.
LIBPD_DIR="$ROOT/src/libs/libpd/libs"
if [[ ! -f "$LIBPD_DIR/libpd.so" ]]; then
    (cd "$ROOT/src/libs/libpd" && make UTIL=true EXTRA=true)
fi

# Generate the Makefile with the JUCE module path pointed at wherever JUCE
# really is; the tracked .jucer is restored afterwards (same approach, and the
# same temp-file sed, as build-linux.sh).
JUCER="$ROOT/src/plugin/formuls-plugin.jucer"
cp "$JUCER" "$JUCER.orig"
trap 'mv -f "$JUCER.orig" "$JUCER" 2>/dev/null || true' EXIT
sed "s|path=\"[^\"]*JUCE/modules\"|path=\"$JUCE_DIR/modules\"|g" "$JUCER.orig" > "$JUCER"
"$PROJUCER" --resave "$JUCER"
mv -f "$JUCER.orig" "$JUCER"
trap - EXIT

# The LV2 and VST3 targets end by running JUCE's manifest helpers, which load
# formuls.so to write the .ttl files and moduleinfo.json. libpd.so is not next to it yet, so let
# the loader find the submodule's copy for that step.
cd "$ROOT/src/plugin/Builds/LinuxMakefile"
LD_LIBRARY_PATH="$LIBPD_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    make CONFIG=Release -j"$(nproc)"
OUT="$ROOT/src/plugin/Builds/LinuxMakefile/build"

cd "$ROOT"
DIST="$ROOT/formuls-$VERSION-linux-$ARCH-plugins"
VST3="$DIST/formuls.vst3"
LV2="$DIST/formuls.lv2"
rm -rf "$DIST"
mkdir -p "$DIST"

cp -R "$OUT/formuls.vst3" "$VST3"
mkdir -p "$VST3/Contents/Resources"
cp -R "$APP/pd" "$VST3/Contents/Resources/pd"
rsync -a --exclude node "$APP/gui/" "$VST3/Contents/Resources/gui/"
cp "$LIBPD_DIR/libpd.so" "$VST3/Contents/$VST3ARCH/"
echo "Built $VST3"

cp -R "$OUT/formuls.lv2" "$LV2"
cp -R "$APP/pd" "$LV2/pd"
rsync -a --exclude node "$APP/gui/" "$LV2/gui/"
cp "$LIBPD_DIR/libpd.so" "$LV2/"
echo "Built $LV2"

if [[ "${INSTALL:-0}" == "1" ]]; then
    mkdir -p "$HOME/.vst3" "$HOME/.lv2"
    rm -rf "$HOME/.vst3/formuls.vst3" "$HOME/.lv2/formuls.lv2"
    cp -R "$VST3" "$HOME/.vst3/formuls.vst3"
    cp -R "$LV2" "$HOME/.lv2/formuls.lv2"
    echo "Installed into ~/.vst3 and ~/.lv2"
fi
