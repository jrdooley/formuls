#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2014-2026 James Dooley (aka formuls)

# Sign (and optionally notarise) the built formuls plugins for distribution.
#
#   SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
#   NOTARY_PROFILE=formuls-notary \
#     src/tools/sign-macos-plugin.sh formuls-0.3.3.vst3 formuls-0.3.3.component
#
# build-plugin-macOS.sh calls this when SIGN_IDENTITY is set, with both
# bundles, so the two are notarised in one submission.
#
# SIGN_IDENTITY  required. A Developer ID Application certificate in your
#                keychain (`security find-identity -v -p codesigning` lists
#                them), or "-" for an ad-hoc signature with the same hardened
#                runtime, to test the result on this Mac.
# NOTARY_PROFILE optional. A notarytool keychain profile, made once with
#                `xcrun notarytool store-credentials <name> --apple-id ...
#                --team-id ...`. With it, the plugins are notarised and their
#                tickets stapled; without it, they are only signed.
#
# Writes formuls-<version>-macOS-<arch>-plugins.zip in the repo root: the file
# to upload to a release. See docs/release.md section 4.
#
# This is not src/tools/sign-macos.sh with different arguments. That script is
# app-shaped: it adds NSLocalNetworkUsageDescription to Info.plist (the host
# owns that prompt for a plugin), and it signs Contents/Resources/gui/node,
# which plugin bundles deliberately do not contain.
#
# Nothing here is signed with entitlements, and that is not an omission.
# Entitlements are granted to a process when it launches; a plugin's
# Contents/MacOS/formuls is a Mach-O bundle (MH_BUNDLE) that is loaded into the
# host's address space and never becomes a process of its own, so the host's
# entitlements govern it. codesign bears this out: given --entitlements it
# embeds them in an .app and silently drops them from a .vst3 or .component,
# with no warning. In particular, whether library validation applies to the Pd
# externals libpd loads is the host's choice (audio hosts commonly disable it),
# not ours to entitle. src/app/entitlements/ is for the app only.
#
# Every binary is signed on its own, inside-out, rather than with --deep:
# notarisation rejects --deep signatures.

set -e

[[ $# -ge 1 ]] || { echo "usage: sign-macos-plugin.sh <bundle> [bundle...]"; exit 1; }
: "${SIGN_IDENTITY:?set SIGN_IDENTITY to a Developer ID Application identity, or - for ad hoc}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

BUNDLES=()
for b in "$@"; do
    b="${b%/}"
    [[ -f "$b/Contents/MacOS/formuls" ]] || { echo "not a formuls plugin bundle: $b"; exit 1; }
    [[ ! -e "$b/Contents/Resources/gui/node" ]] || { echo "$b has a bundled node; use sign-macos.sh"; exit 1; }
    BUNDLES+=("$b")
done

# A secure timestamp is required for notarisation, and needs a network
# connection; an ad-hoc signature cannot have one.
if [[ "$SIGN_IDENTITY" == "-" ]]; then TS=(--timestamp=none); else TS=(--timestamp); fi
sign() { codesign --force --options runtime "${TS[@]}" --sign "$SIGN_IDENTITY" "$@"; }

for BUNDLE in "${BUNDLES[@]}"; do
    # Innermost first: an outer signature seals the inner ones as they are.
    # The externals must already have been rebound to this bundle's libpd —
    # rebinding rewrites the Mach-O in place and invalidates any signature.
    for ext in "$BUNDLE"/Contents/Resources/pd/externals/*.pd_darwin; do sign "$ext"; done
    sign "$BUNDLE/Contents/Frameworks/libpd.dylib"
    sign "$BUNDLE"
    codesign --verify --strict --deep --verbose=2 "$BUNDLE"
    echo "signed: $BUNDLE"
done

VERSION="$(sed -n 's/^VERSION=//p' "$ROOT/build-plugin-macOS.sh" | tr -d '"')"
ARCH=$(lipo -archs "${BUNDLES[1]}/Contents/MacOS/formuls" | tr ' ' '-')
ZIP="$ROOT/formuls-$VERSION-macOS-$ARCH-plugins.zip"

# Both bundles go in one archive, in the folder layout CI's unsigned zip uses
# (release.yml, the "Package the plugins" step), so the install instructions in
# docs/plugin/README.md hold for either download. ditto takes a single source,
# and keeps the bundles' symlinks, metadata and signatures.
STAGE="$(mktemp -d)/formuls-$VERSION-macOS-$ARCH-plugins"
pack() {
    rm -f "$ZIP"
    rm -rf "$STAGE"; mkdir -p "$STAGE"
    for BUNDLE in "${BUNDLES[@]}"; do ditto "$BUNDLE" "$STAGE/formuls.${BUNDLE##*.}"; done
    ditto -c -k --sequesterRsrc --keepParent "$STAGE" "$ZIP"
}

if [[ -n "${NOTARY_PROFILE:-}" ]]; then
    [[ "$SIGN_IDENTITY" == "-" ]] && { echo "an ad-hoc signature cannot be notarised"; exit 1; }
    pack
    xcrun notarytool submit "$ZIP" --keychain-profile "$NOTARY_PROFILE" --wait
    # Attach each ticket to its bundle so it loads offline, then pack again:
    # the zip that was sent has no tickets in it.
    for BUNDLE in "${BUNDLES[@]}"; do
        xcrun stapler staple "$BUNDLE"
        xcrun stapler validate "$BUNDLE"
        # A plugin is loaded, not launched, so it is assessed as an install.
        # Informational: this reports "assessments disabled" on a Mac with
        # Gatekeeper turned off, which is not a signing failure.
        spctl --assess --type install --verbose=2 "$BUNDLE" || true
    done
fi

pack
echo "release zip: $ZIP"
