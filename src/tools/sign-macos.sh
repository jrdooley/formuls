#!/bin/zsh
# Sign (and optionally notarise) a built formuls.app for distribution.
#
#   SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
#   NOTARY_PROFILE=formuls-notary \
#     src/tools/sign-macos.sh formuls-0.3.1.app
#
# SIGN_IDENTITY  required. A Developer ID Application certificate in your
#                keychain (`security find-identity -v -p codesigning` lists
#                them), or "-" for an ad-hoc signature with the same hardened
#                runtime and entitlements, to test the result on this Mac.
# NOTARY_PROFILE optional. A notarytool keychain profile, made once with
#                `xcrun notarytool store-credentials <name> --apple-id ...
#                --team-id ...`. With it, the app is notarised and the ticket
#                stapled; without it, the app is only signed.
#
# Writes <app>-macOS-<arch>.zip next to the app: the file to upload to a
# release. See docs/release.md.
#
# Every binary is signed on its own, inside-out, rather than with --deep:
# notarisation rejects --deep signatures, and node and the app need different
# entitlements (src/app/entitlements/).

set -e

APP="${1:?usage: sign-macos.sh <path to formuls.app>}"
APP="${APP%/}"
: "${SIGN_IDENTITY:?set SIGN_IDENTITY to a Developer ID Application identity, or - for ad hoc}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENT="$ROOT/src/app/entitlements"

[[ -d "$APP/Contents/MacOS" ]] || { echo "not an app bundle: $APP"; exit 1; }

# Tablets connect to the GUI server over the local network. macOS asks the
# player's permission for local-network access; this is the text it shows.
/usr/libexec/PlistBuddy -c "Delete :NSLocalNetworkUsageDescription" "$APP/Contents/Info.plist" 2>/dev/null || true
/usr/libexec/PlistBuddy -c "Add :NSLocalNetworkUsageDescription string formuls serves its control interface to tablets and phones on your network." "$APP/Contents/Info.plist"

# A secure timestamp is required for notarisation, and needs a network
# connection; an ad-hoc signature cannot have one.
if [[ "$SIGN_IDENTITY" == "-" ]]; then TS=(--timestamp=none); else TS=(--timestamp); fi
sign() { codesign --force --options runtime "${TS[@]}" --sign "$SIGN_IDENTITY" "$@"; }

# Innermost first: an outer signature seals the inner ones as they are.
for ext in "$APP"/Contents/Resources/pd/externals/*.pd_darwin; do sign "$ext"; done
sign "$APP/Contents/Frameworks/libpd.dylib"
# node compiles JavaScript to machine code at run time (V8's JIT), which the
# hardened runtime forbids unless entitled.
sign --entitlements "$ENT/node.entitlements" "$APP/Contents/Resources/gui/node"
sign --entitlements "$ENT/formuls.entitlements" "$APP"

codesign --verify --strict --deep --verbose=2 "$APP"
echo "signed: $APP"

ARCH=$(lipo -archs "$APP/Contents/MacOS/formuls" | tr ' ' '-')
ZIP="${APP%.app}-macOS-$ARCH.zip"

if [[ -n "$NOTARY_PROFILE" ]]; then
    [[ "$SIGN_IDENTITY" == "-" ]] && { echo "an ad-hoc signature cannot be notarised"; exit 1; }
    # notarytool takes a zip; ditto keeps the bundle's symlinks and metadata.
    rm -f "$ZIP"
    ditto -c -k --keepParent "$APP" "$ZIP"
    xcrun notarytool submit "$ZIP" --keychain-profile "$NOTARY_PROFILE" --wait
    # Attach the ticket to the app so it opens offline, then zip it again:
    # the zip that was sent has no ticket in it.
    xcrun stapler staple "$APP"
    xcrun stapler validate "$APP"
    spctl --assess --type execute --verbose=2 "$APP"
fi

rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
echo "release zip: $ZIP"
