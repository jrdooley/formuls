# formuls on Android (experimental)

Branch: **`android-experimental`**. Everything here is new and has not yet been
played on real hardware — treat it as a starting point for testing, not a release.

The goal: run formuls on an Android tablet, send audio to the **system default
output** (normally a USB-C audio interface), and play it from the same web GUI as
on the desktop — either inside the app or in a browser, with sound carrying on in
the background.

## Using it

1. Plug the audio interface into the tablet, then launch formuls.
   The first launch after installing or updating spends a few seconds unpacking
   its resources; **Start** is enabled when that is done.
2. Leave the output on **System Default (Output)**. Android sends it to the USB
   interface when one is connected, and to the speakers otherwise. Specific
   devices are listed too, if you want to pin one.
3. Pick channels and sample rate as on the desktop, and press **Start formuls**.
4. The control GUI opens full screen inside the app.
   - **< formuls** (or Back) returns to the settings screen. Nothing stops.
   - **Open in browser** opens the same GUI in the tablet's browser app instead.
     formuls keeps running in the background while you use it.
   - Other devices on the same Wi-Fi can open the addresses listed on the
     settings screen, exactly as on the desktop.
5. While formuls runs, a **"formuls is running"** notification is shown (Android
   13+ asks once for permission to show it). Tap it to return to formuls.
6. **Stop formuls** stops the engine and the GUI server. Swiping formuls away in
   the recent-apps list quits it, like closing the window on the desktop — a
   recording in progress is cut short if you do that.

Recordings are copied to the shared **Music/formuls** folder when they finish.
Presets are named and picked in-app (there are no file dialogs), and live in the
app's own folder on shared storage (`Android/data/net.formuls.formuls/files/presets`).

## Building

```bash
export ANDROID_HOME=~/Library/Android/sdk   # or wherever your SDK lives
./build-android.sh            # -> formuls-0.3.0-android-debug.apk
adb install -r formuls-0.3.0-android-debug.apk
```

Requirements (in addition to the desktop build's JUCE/Projucer and faust):

| what | version | notes |
|---|---|---|
| Android SDK | platform 35, build-tools | Android Studio installs these; so does `sdkmanager` |
| Android NDK | **28.1.13356709** | the version JUCE 9's Android exporter pins in `app/build.gradle` |
| JDK | 17 | Android Studio's bundled JBR works: `export JAVA_HOME=".../Android Studio.app/Contents/jbr/Contents/Home"` |
| CMake | ≥ 3.25 on the `PATH` | libpd's CMake project needs it; the SDK's CMake 3.22 is only used by gradle |
| curl, unzip, python3 | any | |

`FORMULS_ANDROID_ABIS` picks the ABIs (default `arm64-v8a x86_64`; `arm64-v8a` is
all a real tablet needs, `x86_64` is for the emulator). Downloads (Open Stage
Control, nodejs-mobile) are cached in `build/downloads/`; everything else under
`build/android/` is rebuilt each run.

Minimum Android version is **9 (API 28)**: libpd's `[file glob]` needs `glob()`,
which Android's C library only has from API 28, and USB audio and AAudio are only
dependable from around there anyway.

## How it works

The desktop app is one JUCE process that embeds libpd and spawns node to run
Open Stage Control. On Android the same C++ app is built with JUCE's Android
Studio exporter; the differences are all behind `#if JUCE_ANDROID`, so the macOS
and Linux builds are unchanged.

```
┌──────────── app process ─────────────┐     ┌────────── ":gui" process ──────────┐
│ JUCE app (libjuce_jni.so)             │     │ GuiServerService                    │
│   FormulsEngine: libpd + externals ───┼─ Oboe/AAudio ─→ system default output   │
│     (static: libformulspd.a, libpd.a) │     │   libformuls_node.so → libnode.so  │
│   OscBridge ←──────── udp 9000 ───────┼─────┤   Open Stage Control (node 18)      │
│   ControlGuiPanel (WebView) ─ http ───┼────→│   http/ws :9001                     │
│ FormulsAudioService (foreground)      │bind │                                     │
└───────────────────────────────────────┘────→└─────────────────────────────────────┘
```

| desktop | Android | where |
|---|---|---|
| resources in the `.app` bundle | packed into the APK's `assets/formuls`, unpacked to app storage on first launch (and after each update) | `AndroidPlatform.cpp` `extractResources()` |
| Pd externals loaded from `pd/externals/*.pd_darwin` | the same Faust objects and `abl_link~` compiled into `libformulspd.a` and registered before the patch opens | `src/android/native/` |
| `node` binary as a child process | **nodejs-mobile** (`libnode.so`, Node 18.20.4) in a separate `:gui` process | `GuiServerService.java`, `formuls_node_jni.cpp` |
| stray-server sweep over all processes | kill the app's own `:gui` process by name — exact, not heuristic | `FormulsAndroid.stopGuiServer()` |
| app keeps running with its window in the background | a `mediaPlayback` foreground service with wake, Wi-Fi and multicast locks | `FormulsAudioService.java` |
| GUI in a browser | GUI in an embedded WebView, or a browser app | `ControlGuiPanel`, `FormulsWebView.java` |
| save dialog for recordings | copied into shared `Music/formuls` via MediaStore | `FormulsAndroid.publishRecording()` |
| preset file dialogs | name prompt / popup list over the app's preset folder | `MainComponent::askForPresetNameThenSave()` / `choosePresetToLoad()` |
| Take Screenshot (python + Playwright) | replaced by **Open in browser** | `MainComponent.cpp` |

### Why the GUI server gets its own process

- `node::Start()` can only run once per process. Stop → Start needs a new node,
  so it needs a new process; stopping is simply killing it, which frees port 9001
  at once.
- node exits its whole process when the server dies. In a process of its own that
  takes down the GUI; in the app's process it would silence the engine.
- The app process **binds** to the service, so the `:gui` process inherits the
  app's foreground-service priority and keeps running in the background. If node
  crashes, Android restarts the bound service by itself.

### Why the externals are static

Android apps can only load native code from their own `lib/` folder, and libpd's
CMake build has no `dlopen` support there. The Faust sources are generated with
`faust -i -a puredata.cpp` (same `-vec -lv 0 -vs 4` options as the desktop) and
compiled with `-Dmydsp=<name>` exactly as `faust2puredata` does, which keeps the
three objects apart in one library. `faust_android_prefix.h` makes their
multichannel check resolve to libpd's `signal_setmultiout` at compile time, since
the runtime `dlsym` lookup cannot see into a library Java loaded.

### Why a custom WebView instead of `juce::WebBrowserComponent`

JUCE's WebView enables pinch zoom (two fingers on two faders would zoom the page)
and leaves DOM storage off (Open Stage Control's client uses `localStorage`).
`FormulsWebView` fixes both and hands the Back key to the app, which would
otherwise go straight to Android and close formuls. `JUCE_WEB_BROWSER` is set to
0 in the `.jucer`, so adding `juce_gui_extra` (for `AndroidViewComponent`) does
not pull WebKit into the Linux build.

## Debugging

```bash
adb logcat -s formuls formuls-gui JUCE     # app, GUI server (node stdout/stderr), JUCE
```

- The embedded GUI can be inspected from desktop Chrome at `chrome://inspect`.
- On the emulator, the GUI server is reachable from the host after
  `adb forward tcp:9001 tcp:9001` → `http://127.0.0.1:9001`.

## What has been tested

On the Android 15 emulator (arm64-v8a, Pixel Tablet profile), debug and release APKs:

- First launch unpacks the resources; installing an update unpacks them again.
- "System Default (Output)" is preselected; Start opens an AAudio stream at 48 kHz.
- libpd loads `_main.pd` with all statically linked externals
  (`formuls~`, `f_reverb~`, `f_repeater~`, `abl_link~`, which creates its Link session).
- Open Stage Control starts on nodejs-mobile in the `:gui` process and the full
  formuls GUI loads in the embedded WebView; touches reach the patch.
- Back from inside the GUI returns to settings; Back on settings while running
  sends formuls to the background.
- After 90 s in the background, both processes and the GUI server are still up.
- Stop ends the `:gui` process, frees port 9001 and removes the notification;
  Start again brings up a fresh GUI server.
- Record → Stop recording writes a 2-channel 48 kHz WAV, which is copied into
  `Music/formuls`.
- The macOS build still compiles with these changes.

## Known gaps / to test on hardware

- **Not yet run on a real tablet or with a USB interface.** The emulator has no
  USB audio, and the test did not get as far as playing loud notes (the recorded
  output responded to the GUI, but at a very low level: the instrument was not
  set up to sound).
- The build script has only been run on a macOS host, not Linux.
- 14-channel output depends on the device: Android's USB audio support for more
  than 8 output channels varies by version and vendor.
- Buffer size is requested as 512 frames but Oboe/AAudio chooses its own.
- Hot-plugging the interface while running: Oboe reports a disconnect; the app
  does not yet reopen the stream on the new default device — Stop and Start.
- Finished recordings are copied to `Music/formuls` on the message thread, so a
  very long take pauses the settings screen for a moment.
- The node runtime is nodejs-mobile 18 (the latest release); Open Stage Control
  1.31 declares `node >= 16` and has been checked to serve the formuls session
  on Node 18.
