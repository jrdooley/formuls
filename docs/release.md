# Making a release

A release has three downloads:

| | built | signed |
|---|---|---|
| macOS (Apple Silicon) | on your Mac, `build-macOS.sh` | Developer ID, notarised, on your Mac |
| Linux x64 | GitHub Actions (`.github/workflows/release.yml`) | not needed |
| Windows x64 | GitHub Actions | not signed (see below) |

The Mac build is signed locally so the certificate never leaves your machine.

## Once: set up signing on your Mac

1. **Certificate.** In Xcode, open Settings → Accounts, select your team, then
   Manage Certificates → **+** → *Developer ID Application*. Check it is there:

   ```bash
   security find-identity -v -p codesigning
   ```

   The line to use looks like `Developer ID Application: Your Name (TEAMID)`.

2. **Notarisation password.** Make an app-specific password at
   [appleid.apple.com](https://appleid.apple.com) (Sign-In and Security →
   App-Specific Passwords). Store it in the keychain under a profile name; it
   asks for the password itself:

   ```bash
   xcrun notarytool store-credentials formuls-notary --apple-id YOUR_APPLE_ID --team-id YOUR_TEAM_ID
   ```

   Scripts only ever use the profile name, never the password.

## Each release

1. **Set the version** in all three scripts (`VERSION=` in `build-macOS.sh`,
   `build-linux.sh` and `build-windows.bat`), commit it to `main`.

2. **Tag and push.** The workflow checks the tag matches `VERSION`, builds Linux
   and Windows, and creates a **draft** release with both attached:

   ```bash
   git tag v0.3.2
   git push origin v0.3.2
   ```

   Progress and logs are in the repository's **Actions** tab.

3. **Build, sign and notarise the Mac app:**

   ```bash
   SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" NOTARY_PROFILE=formuls-notary ./build-macOS.sh
   ```

   This writes `formuls-<version>-macOS-arm64.zip`. Notarisation usually takes a
   few minutes. To sign an app that is already built, run
   `src/tools/sign-macos.sh formuls-<version>.app` with the same two variables.

4. **Add the Mac zip to the draft release**, write the notes, publish. From the
   terminal:

   ```bash
   gh release upload v0.3.2 formuls-0.3.2-macOS-arm64.zip
   ```

## What the Mac signing does (`src/tools/sign-macos.sh`)

- Adds `NSLocalNetworkUsageDescription` to `Info.plist`: tablets reach the GUI
  over the local network, and macOS asks the player's permission with this text.
- Signs each binary on its own, innermost first, with the hardened runtime and a
  secure timestamp: the Pd externals, `libpd.dylib`, `node`, then the app.
  `--deep` is not used: notarisation rejects it, and the parts need different
  entitlements (`src/app/entitlements/`):
  - **`node`**: `allow-jit` and `allow-unsigned-executable-memory`. V8 compiles
    JavaScript to machine code at run time, which the hardened runtime otherwise
    kills.
  - **the app**: `disable-library-validation`. libpd loads the externals as
    plug-ins; with validation on, only plug-ins signed by the same team load.
- Notarises the zip with `notarytool`, staples the ticket to the app so it opens
  offline, checks it with `spctl`, and zips it again for the release.

`SIGN_IDENTITY=-` signs ad hoc with the same runtime and entitlements, to test on
this Mac without a certificate. That was checked: the engine starts, every Pd
external loads and `node` serves the GUI.

## Limits

- **The Mac build is Apple Silicon only.** `node` and libpd are built for the
  machine's own architecture, and the prebuilt `abl_link~` is arm64 only (see
  `src/prebuilt/README.md`). An Intel or universal build would need those three
  for x86_64 too.
- **Windows is unsigned.** Windows SmartScreen warns about unsigned apps until
  they are widely downloaded. Signing needs a separate code-signing certificate.
- **Windows and Linux are untested in use.** CI shows they build; someone needs to
  play each new build once.
