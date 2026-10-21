# Making a release

How formuls is built, signed and published for macOS, Linux and Windows, and
everything learned getting there. The first two sections are the steps; the rest
explain them and what to do when they go wrong.

Placeholders used throughout: `YOUR NAME`, `TEAMID` (your 10-character Apple
Team ID), `you@example.com` (your Apple ID). Nothing here is a secret, but
nothing personal is written down either.

| download | built | signed | file |
|---|---|---|---|
| macOS (Apple Silicon) | on your Mac, `build-macOS.sh` | Developer ID + notarised, on your Mac | `formuls-<version>-macOS-arm64.zip` |
| macOS (Apple Silicon), CI build | GitHub Actions | ad hoc only | `formuls-<version>-macOS-arm64-unsigned.zip` |
| macOS plugins (VST3, AU) | GitHub Actions | ad hoc only | `formuls-<version>-macOS-arm64-plugins-unsigned.zip` |
| Linux x64 | GitHub Actions | not needed | `formuls-<version>-linux-x64.tar.gz` |
| Windows x64 | GitHub Actions | no (see [Windows](#7-windows)) | `formuls-<version>-windows-x64.zip` |
| Linux x64 plugins (VST3, LV2) | GitHub Actions | not needed | `formuls-<version>-linux-x64-plugins.tar.gz` |
| Windows x64 plugin (VST3) | GitHub Actions | no | `formuls-<version>-windows-x64-plugins.zip` |

GitHub builds all three platforms, but the Mac builds it makes are signed ad hoc
(`codesign -s -`), never with your Developer ID: the certificate's private key
never leaves your own Mac. Gatekeeper blocks an ad-hoc app downloaded from the
web (right-click → Open, or `xattr -dr com.apple.quarantine`, gets past it), so
their zips are named `-unsigned`. They prove the Mac build works and are handy
for testing. The Mac app players download is the one you sign and notarise
yourself (section 2).

---

## 1. One-off setup

### 1a. The Developer ID certificate

You need a **Developer ID Application** certificate: Apple's certificate for apps
distributed outside the Mac App Store. The others with similar names are not
needed:

| certificate | for | needed |
|---|---|---|
| **Developer ID Application** | apps you distribute yourself | **yes** |
| Developer ID Installer | `.pkg` installers | no, releases are zips |
| Apple Development | running builds on your own test devices | no |
| Apple Distribution / Mac App Distribution | the Mac App Store | no |

**Through Xcode (simplest):**

1. Xcode → **Settings…** (⌘,) → **Accounts**. Add your Apple ID with **+** if it
   is not listed.
2. Select your team → **Manage Certificates…** → **+** → **Developer ID
   Application** → **Done**.

Xcode makes the private key on your Mac, gets the certificate from Apple, and puts
both in your login keychain.

**Or through the website:** Keychain Access → Certificate Assistant → *Request a
Certificate From a Certificate Authority…* → *Saved to disk*. Upload the
`.certSigningRequest` at developer.apple.com → Certificates → **+** → Developer ID
Application, download the `.cer`, double-click it.

**Check:**

```bash
security find-identity -v -p codesigning
```

```
1) 0A1B2C…  "Developer ID Application: YOUR NAME (TEAMID)"
   1 valid identities found
```

The quoted text is your **signing identity**; the part in brackets is your **Team
ID**.

Notes:

- On an organisation team only the **Account Holder** can usually create Developer
  ID certificates; a greyed-out option means that.
- **Back it up.** The certificate is useless without its private key, which exists
  only in your keychain. Keychain Access → right-click the certificate → Export… →
  a password-protected `.p12`, kept somewhere safe. Lose the key and you need a new
  certificate.
- A certificate lasts about five years, and a team can only have a few. Apps
  signed and notarised while it was valid keep opening after it expires.

### 1b. The notarisation profile

Notarisation is Apple scanning the app and vouching for it. `notarytool` needs to
sign in to Apple as you, with an **app-specific password**, never your normal one.

1. [account.apple.com](https://account.apple.com) → **Sign-In and Security** →
   **App-Specific Passwords** → **+**. Name it, e.g. `formuls notary`, and copy the
   password (`abcd-efgh-ijkl-mnop`).
2. Store it in the keychain under a profile name. Type the command, with **your
   own** Apple ID and Team ID in place of the placeholders:

   ```bash
   xcrun notarytool store-credentials formuls-notary --apple-id you@example.com --team-id TEAMID
   ```

   Paste the app-specific password when asked. It checks it with Apple and
   finishes with *Credentials saved to Keychain*.

From then on scripts refer to the profile name `formuls-notary`; the password is
never in a file, a command line or the repository.

### 1c. GitHub

- **`gh` needs the `workflow` permission** to push changes to
  `.github/workflows/`. GitHub keeps it separate because a workflow runs code on
  GitHub's machines with the repository's secrets. Grant it once:

  ```bash
  gh auth refresh -h github.com -s workflow
  ```

  Press Enter, paste the one-time code in the browser, approve. Without it a push
  fails with *refusing to allow an OAuth App to create or update workflow … without
  `workflow` scope*.
- Actions are free for public repositories. Runs and logs are in the repository's
  **Actions** tab.

---

## 2. Each release

1. **Set the version** in all six build scripts (`VERSION=…` in
   `build-macOS.sh`, `build-linux.sh`, `build-plugin-macOS.sh` and
   `build-plugin-linux.sh`; `set VERSION=…` in `build-windows.bat` and
   `build-plugin-windows.bat`) and merge that to `main`. CI refuses to build if
   they differ.

2. **Tag and push.** CI checks the tag matches `VERSION`, builds the macOS,
   Linux and Windows apps and plugins, checks the plugins in a test host, and
   creates a **draft** release with all six archives attached (the two Mac ones
   named `-unsigned`):

   ```bash
   git tag v0.3.2
   git push origin v0.3.2
   ```

3. **Build, sign and notarise the Mac app** from the same commit:

   ```bash
   SIGN_IDENTITY="Developer ID Application: YOUR NAME (TEAMID)" NOTARY_PROFILE=formuls-notary ./build-macOS.sh
   ```

   Building takes several minutes, notarisation about three more. The first time,
   macOS asks whether `codesign` may use your key: choose **Always Allow**, or it
   asks once per file and the build waits for you. It writes
   `formuls-<version>.app` and `formuls-<version>-macOS-arm64.zip`.

4. **Test the zip on a Mac with Gatekeeper on** (see
   [Checking a signed build](#checking-a-signed-build)): download it, unzip,
   double-click. It should open with no warning.

5. **Swap the Mac app zip in the draft**: add the signed one and delete CI's
   unsigned one, then write the notes and publish on GitHub:

   ```bash
   gh release upload v0.3.2 formuls-0.3.2-macOS-arm64.zip
   ```

   ```bash
   gh release delete-asset v0.3.2 formuls-0.3.2-macOS-arm64-unsigned.zip
   ```

   The Mac plugins have no Developer ID build yet, so
   `formuls-0.3.2-macOS-arm64-plugins-unsigned.zip` is the only one; keep it, or
   remove it the same way if only signed Mac downloads should be published.

Release zips, `.tar.gz` files and the Linux/Windows output folders are in
`.gitignore`, so they cannot be committed by accident.

---

## 3. Mac signing in detail (`src/tools/sign-macos.sh`)

`build-macOS.sh` calls this when `SIGN_IDENTITY` is set; without it the build
signs ad hoc (`codesign -s -`), which runs only on the Mac that built it. To sign
an app that is already built:

```bash
SIGN_IDENTITY="Developer ID Application: YOUR NAME (TEAMID)" NOTARY_PROFILE=formuls-notary src/tools/sign-macos.sh formuls-0.3.2.app
```

Without `NOTARY_PROFILE` it signs and zips but does not notarise.
`SIGN_IDENTITY=-` signs ad hoc with the same hardened runtime and entitlements, for
testing the result without a certificate.

### What it does, in order

1. **Adds `NSLocalNetworkUsageDescription`** to `Info.plist`. Tablets reach the GUI
   over the local network, and macOS shows this text when asking the player's
   permission.
2. **Signs each binary on its own, innermost first**, with the hardened runtime
   (`--options runtime`) and a secure timestamp (`--timestamp`). Notarisation
   requires both. The bundle holds six binaries:

   | file | entitlements |
   |---|---|
   | `Resources/pd/externals/*.pd_darwin` (4) | none |
   | `Frameworks/libpd.dylib` | none |
   | `Resources/gui/node` | `src/app/entitlements/node.entitlements` |
   | the app itself | `src/app/entitlements/formuls.entitlements` |

   Innermost first because an outer signature seals the inner ones as they are.
   `codesign --deep` is not used: notarisation rejects it, and it cannot give
   `node` and the app different entitlements.
3. **Verifies** with `codesign --verify --strict --deep`.
4. **Notarises:** zips the app with `ditto`, sends it with
   `xcrun notarytool submit --keychain-profile … --wait`, and waits for Apple's
   verdict.
5. **Staples** the ticket to the app (`xcrun stapler staple`), so it opens without
   asking Apple's servers: important at a venue with no internet.
6. **Checks** with `spctl --assess`, which should say
   `accepted, source=Notarized Developer ID`.
7. **Zips again** for the release: the zip that was sent has no ticket in it.

### The entitlements, and why

- **`node`: `allow-jit` and `allow-unsigned-executable-memory`.** V8 compiles
  JavaScript to machine code while running. The hardened runtime forbids
  writable-then-executable memory unless entitled, so without these `node` (and
  with it the whole GUI) is killed at start. This is the most common way a
  notarised app containing `node` fails.
- **The app: `disable-library-validation`.** libpd loads the Pd externals as
  plug-ins. With library validation on, macOS only lets a hardened app load code
  signed by the same team. That would work for a Developer ID build, but not for an
  ad-hoc test build or any external added later; audio hosts commonly turn it off.

### Why `ditto` and not `zip`

`ditto -c -k --keepParent` keeps the bundle's symlinks and extended attributes
exactly. Anything that changes a file inside the bundle breaks its signature.

### Checking a signed build

```bash
codesign -dv --verbose=2 formuls-0.3.2.app      # Authority=Developer ID…, TeamIdentifier, flags=…(runtime)
codesign -d --entitlements - formuls-0.3.2.app  # the app's entitlements
xcrun stapler validate formuls-0.3.2.app        # ticket attached
spctl --assess --type execute -vv formuls-0.3.2.app
```

If `spctl` also prints `override=security disabled`, **Gatekeeper is turned off on
that Mac**. The app still passed, but double-clicking it there does not show what
players will see. Test on a Mac with Gatekeeper on, or turn it back on with
`sudo spctl --master-enable`.

---

## 4. Troubleshooting the Mac build

Every problem below happened while setting this up.

**`notarytool store-credentials` says *Your Apple ID has been locked*.**
Usually it is not. Apple's notary service gives this reply for most failed
sign-ins, including an Apple ID that does not exist. It happened here when the
command was run with the placeholder `YOUR_APPLE_ID` still in it. Check the Apple
ID is your real one, and that the password is the app-specific one, made for that
same Apple ID. If the account really is locked, unlock it at
[account.apple.com](https://account.apple.com), then make a **new** app-specific
password: unlocking cancels the old ones.

**`timestamps differ by 1522 seconds - check your system clock`.**
`codesign` asks Apple's timestamp server for the time, and refuses if your clock
disagrees. Here the clock was right again moments later (probably resyncing after
the Mac woke from sleep). Check with `sntp time.apple.com`, then rerun only the
signing step, `src/tools/sign-macos.sh <app>`; the app does not need rebuilding.
When signing fails, `build-macOS.sh` stops before its clean-up, so also remove
`build/`, `src/libs/libpd/libs/libpd.dylib` and the libpd and `abl_link~` object
files (`make clean` in each), or just let the next full build do it.

**`unsealed contents present in the bundle root`.**
Something sits at the top of the `.app` folder, next to `Contents/`. Here it was an
empty `.claude` folder, created when a tool's working directory was the app. Look
with `ls -la formuls-0.3.2.app` and remove anything but `Contents`. Notarisation
would reject it too.

**The app opens but the GUI never loads.**
`node` was signed without its entitlements. Check with
`codesign -d --entitlements - formuls-0.3.2.app/Contents/Resources/gui/node`; it
should list `allow-jit`.

**Notarisation is *Invalid*.**
Read Apple's log, which names each problem file:

```bash
xcrun notarytool log <submission id> --keychain-profile formuls-notary
```

Common causes: a binary not signed with your identity, missing hardened runtime, no
secure timestamp, or a `--deep` signature.

**Signing asks for the keychain password for every file.**
Choose **Always Allow** in the dialog, not Allow.

---

## 5. CI in detail (`.github/workflows/release.yml`)

### When it runs

| trigger | builds | release |
|---|---|---|
| a tag `v*` is pushed | macOS, Linux, Windows | a draft, with all attached |
| a pull request changes a build script or the workflow | macOS, Linux, Windows | no; builds kept as downloads on the run |
| **Run workflow** in the Actions tab (on `main`, or any branch once the file is on `main`) | macOS, Linux, Windows | no |

A run that only changes the workflow on a branch cannot be started by hand until
the file is on `main`; that is why pull requests that touch the build trigger it.

### Jobs

- **`version`** reads `VERSION` from all six scripts (app and plugin), fails if
  they differ, and on a tag fails unless the tag is `v<VERSION>`.
- **`linux`** (Ubuntu 24.04): installs JUCE's Linux dependencies (including
  `libxi-dev`, which JUCE 9 needs and older lists omit), `puredata-dev` for Pd's
  headers and `clang`; builds Faust from source (cached after the first run, which
  takes about 5 minutes); downloads JUCE; runs `build-linux.sh` under `xvfb-run`
  (Projucer is a GUI app even when resaving from the command line); packs a
  `.tar.gz`.
- **`windows`** (Windows Server 2022): downloads JUCE, installs Faust silently, sets
  up a Visual Studio developer environment, runs `build-windows.bat`, zips the
  result.
- **`macos`** (macOS 15, Apple Silicon): builds Faust from source (cached, as on
  Linux: Homebrew's Faust is newer than the pinned one), downloads JUCE, runs
  `SIGN_IDENTITY=- ./build-macOS.sh` (an ad-hoc signature with the release
  build's hardened runtime and entitlements, so it behaves like the signed app
  apart from Gatekeeper), and renames the zip `-unsigned`. Its plugins are the
  VST3 and AU from `build-plugin-macOS.sh`, zipped with `ditto`.
- All three jobs then build the plugins and check them with
  `src/tools/plugin-probe` (audio and MIDI everywhere; the editor window on
  Linux and Windows. On macOS only the VST3 is probed: an AU has to be
  installed to be found). A failure in any job stops the release, so a draft
  always has all six archives.
- **`release`** (tags only): downloads the six archives (not the editor
  snapshots) and creates the draft with `gh release create --draft`.

Versions are pinned to the ones the Mac is built with: **JUCE 9.0.1**, **Faust
2.85.9**, **Node 22.17.0**, **Open Stage Control 1.31.0**.

### Downloading a CI build to test

From the run's page in the Actions tab, or:

```bash
gh run download <run id> -n linux-x64
gh run download <run id> -n windows-x64
gh run download <run id> -n macos-arm64
```

---

## 6. Linux

- **The Faust externals are built without `faust2puredata`.** On Linux it always
  compiles with `-march=native`, overriding any `CXXFLAGS`. On GitHub's machines,
  which have AVX-512, that put 256 AVX-512 and 2,137 AVX instructions into
  `formuls~` alone, and the app crashed with **`Illegal instruction`** (exit code
  132) on any CPU without them, as soon as Pd loaded the first Faust external.
  `build-linux.sh` now runs `faust -a puredata.cpp` and the compiler itself with
  `-march=x86-64-v2 -mtune=generic` (SSE4.2: every x86-64 CPU since about 2009).
- **Checking a build for this:** count AVX-512 and AVX instructions in each
  binary; a portable build has none:

  ```bash
  for b in formuls libs/libpd.so pd/externals/*.pd_linux; do echo "$b $(objdump -d $b | grep -c %zmm) $(objdump -d $b | grep -c %ymm)"; done
  ```

- **Tested headless** in an x86-64 Ubuntu 24.04 VM (OrbStack, which runs x86-64
  under Rosetta, without AVX): `xvfb-run -a ./formuls --start` starts the engine,
  loads all four externals, and serves the GUI on port 9001. No system libraries
  were missing (`ldd`), though that VM already had JUCE's dependencies installed.

## 7. Windows

Windows differs from the other two in ways worth knowing before changing
`build-windows.bat`:

- **The Pd externals must be built against libpd, not Pd.** On macOS and Linux an
  external can leave Pd's functions unresolved and pick them up from whatever
  loads it. A Windows DLL must name the DLL it imports from when it is built.
  Externals made for ordinary Pd import from `pd.dll`; in formuls, Pd is
  `libpd.dll`. So the externals are compiled here, against `libpd.lib`.
- **`faust2puredata` is a Unix shell script**, so the batch file does what it does:
  `faust -i -a puredata.cpp …` writes C++, then `cl` builds each DLL, exporting its
  `<name>_tilde_setup` function explicitly (Pd finds externals by that name).
  `abl_link~`'s makefile expects MinGW, so it is built with `cl` the same way.
- **libpd needs pthreads under MSVC**, from vcpkg. It must be the standalone vcpkg
  (`C:\vcpkg`, or set `FORMULS_VCPKG`): the copy inside Visual Studio, which a
  Developer Command Prompt puts in `VCPKG_ROOT`, only installs for projects with a
  `vcpkg.json` and fails with *does not have a classic mode instance*.
  `pthreadVC3.dll` ships next to `libpd.dll`.
- **Python runs in UTF-8 mode** (`PYTHONUTF8=1`). Otherwise Python on Windows
  reads and writes files in the Windows code page (cp1252), which crashes on
  characters in Open Stage Control's sources, or worse, silently misreads UTF-8.
- **The `.jucer` module paths are rewritten by `src/tools/jucer-module-path.py`**,
  not an inline `python -c`: cmd's quoting makes one-liners fragile. The script
  stops if Projucer does not write the Visual Studio project.
- **Brackets inside `if … ( … )` blocks must be escaped** (`^(` `^)`), even in
  `echo` text: an unescaped `)` ends the block early, and the next line runs
  unconditionally.
- **Batch files keep Windows line endings** (`.gitattributes`): `cmd` can misread
  `goto` labels in files with Unix ones.
- **Unsigned.** Windows SmartScreen warns about unsigned downloads until they are
  widely used. Signing needs a separate Windows code-signing certificate, which is
  not set up.

---

## 8. Limits and what is untested

- **The Mac build is Apple Silicon only.** `node` and libpd are built for the
  Mac's own architecture, and the prebuilt `abl_link~` is arm64 only (see
  `src/prebuilt/README.md`). An Intel or universal build needs all three for
  x86_64 as well.
- **Linux has been run headless, Windows only inspected.** The Linux build starts,
  loads every external and serves the GUI (section 6). The Windows package has been
  checked to contain every file, with the externals importing `libpd.dll`. Neither
  has been played with sound; someone needs to play each new build once.
- **Mac signing was checked end to end:** an ad-hoc signature with the same runtime
  and entitlements started the engine, loaded every external and served the GUI,
  and the Developer ID build was notarised (*Accepted*), stapled and passed
  `spctl`.
