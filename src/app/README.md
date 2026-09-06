# The formuls JUCE application

This folder contains the JUCE application that replaced the old Python/Tkinter
front end *and* the old standalone `formulsengine` binary (RtAudio + libpd).
It is one app that:

1. shows a window for choosing the **audio output device**, the **number of
   output channels** (2 = stereo mix, 14 = individual synth/bus outputs) and
   the **sample rate** (44.1/48/88.2/96 kHz, 48 kHz default -- see
   `kSampleRateChoices` in MainComponent.cpp to offer other rates),
2. on **Start formuls**, boots libpd *inside the app* (loading `src/pd/_main.pd`)
   and launches the bundled node.js build of **Open Stage Control** (the formuls
   GUI) as a child process, then lists the **web addresses the GUI can be
   opened at** (see below),
3. **records its own output to a WAV file** at the press of the **Record**
   button, matching the running sample rate and channel count (see below),
4. on **Stop formuls**, shuts both down again while the window stays open, so a
   different device or channel count can be chosen,
5. quits fully when the window is closed — stopping the engine and killing the
   Open Stage Control process it started,
6. quits when **the Pd patch asks it to** (see below).

## Source files

| File | What it does |
| --- | --- |
| `Source/Main.cpp` | App entry point; creates the main window, installs the look and feel, handles quitting. |
| `Source/MainComponent.h/.cpp` | The window contents: device/channel combo boxes, Start/Stop and Record buttons, status line. |
| `Source/AudioRecorder.h/.cpp` | Writes the engine's output to a WAV file on a background thread. |
| `Source/FormulsEngine.h/.cpp` | libpd embedded behind a `juce::AudioIODeviceCallback`; loads `_main.pd`, renders audio, receives patch messages. |
| `Source/OpenStageControlProcess.h/.cpp` | Starts/stops the Open Stage Control server with `juce::ChildProcess`; also works out the GUI's web addresses. |
| `Source/FormulsLookAndFeel.h` | **All styling** — window title/size, colours, fonts, layout metrics. |
| `Source/ResourceLocator.h` | Finds the `pd/` and `gui/` resources both in the packaged app and in development builds. |
| `formuls.jucer` | Projucer project. Generates `Builds/MacOSX` (Xcode) and `Builds/LinuxMakefile`. |

## Recording

The **Record** button sits next to Start/Stop and is enabled only while the
engine is running. The first press starts a take; the next press ends it and
opens a "save as" dialog.

The file always matches what the engine is actually producing:

* **Sample rate** — the rate the audio device really opened at, which is not
  always the rate that was asked for (`FormulsEngine::getActualSampleRate()`).
* **Channels** — one WAV channel per *selected* output channel, so "2 channels"
  gives a stereo file and "14 channels" gives a 14-channel file. This follows
  the combo box, not the hardware: if the device has fewer outputs than that,
  the channels it does not have are recorded as silence, so a 14-channel take
  keeps its channel numbering on any device.
* **Format** — 24-bit PCM WAV (`AudioRecorder::bitsPerSample`; set it to 32 to
  write 32-bit float instead, which `juce::WavAudioFormat` does automatically
  at that depth and which cannot clip). Files over 4 GB become RF64, which the
  JUCE writer switches to on its own, so long multichannel takes are not
  truncated.

Audio is written straight to a timestamped file in **`~/Music/formuls/`**
(falling back to `~/Documents/formuls`, then the temp folder) while recording;
the save dialog just *moves* that finished file wherever you want it. Nothing
is recorded to a scratch file that could be lost — if the dialog is cancelled,
or the app is closed mid-take, or Stop formuls is pressed while recording, the
audio is still in that folder and the status line says exactly where.

The audio thread only ever hands blocks to a
`juce::AudioFormatWriter::ThreadedWriter`, which copies them into a FIFO that a
background thread drains to disk — no disk I/O and no allocation happens in the
audio callback. `FormulsEngine` offers it every block it renders; the recorder
ignores them until a take is started, so recording costs nothing while idle.

## The control GUI addresses

Once the GUI server is running, the window shows a read-only panel listing
every address the GUI can be opened at, so nobody has to go hunting for the
machine's IP address:

```
Open the control GUI in a web browser:

On this machine:
   http://127.0.0.1:9001

On a tablet or phone on the same network:
   http://192.168.1.112:9001
   http://192.168.139.3:9001
```

The text is selectable (Cmd+C, or right-click for Copy) but not editable.

The addresses come from `OpenStageControlProcess::getBrowserAddresses()`,
which enumerates the machine's own network interfaces via
`juce::IPAddress::getAllAddresses()`. It deliberately does **not** parse the
addresses Open Stage Control prints to its stdout, so the panel does not break
if another program changes its log wording. Loopback (`127.x`) is listed once,
first and explicitly; self-assigned link-local addresses (`169.254.x`) are
skipped because no other device can reach them; IPv6 is omitted, since the GUI
is used over IPv4 and bracketed IPv6 URLs are painful to type on a tablet.

If the machine is on no network at all, the panel says so instead of listing
addresses.

The port is defined once, as `OpenStageControlProcess::guiPort` — it is used
both for the server's `--port` argument and for these URLs, so changing that
constant changes both. `patchOscPort` (9000) is the matching constant for the
UDP port the Pd patch listens on.

## Changing the styling of the main window

Open **`Source/FormulsLookAndFeel.h`** — it is the single place for all of it,
and its header comment is a how-to. In short:

* **Window title / size / resizability** — the constants in `formuls::style`
  (`windowTitle`, `windowWidth`, `windowHeight`, `windowResizable`).
* **The address panel** — `style::addressFill`, `style::addressText` and
  `style::addressFontHeight` (it uses the default monospaced font so the
  addresses line up).
* **Colours** — `style::background`, `style::widgetFill`, etc., plus the
  `setColour()` calls in the `FormulsLookAndFeel` constructor. Every JUCE
  widget exposes `ColourId` enums (e.g. `juce::TextButton::buttonColourId`);
  add a `setColour()` line for any of them to restyle that widget type
  app-wide, or call `setColour()` on an individual widget in
  `MainComponent.cpp` to restyle just that one.
* **Layout** — margins, control sizes and spacing are the `style::` layout
  constants, consumed by `MainComponent::resized()`.
* **Fonts** — `style::labelFontHeight` / `buttonFontHeight`; for full control
  override `getTextButtonFont()` / `getComboBoxFont()` etc. in
  `FormulsLookAndFeel`.
* **Custom drawing** — override `juce::LookAndFeel_V4` methods such as
  `drawButtonBackground()` or `drawComboBox()` in `FormulsLookAndFeel`.

## Quitting the app from the Pd patch

`FormulsEngine` subscribes to the Pd receive symbol **`formuls-quit`**.
Any message sent to it makes the app quit cleanly (stopping audio and the
Open Stage Control process). From anywhere in the patch:

```
[; formuls-quit bang(
```

`_main.pd` already does this where it used to say `; pd quit` (which was a
no-op under libpd): the Open Stage Control GUI sends OSC address `/pd` to udp
port 9000, and the patch turns that into `; formuls-quit bang` after a 500 ms
delay.

The plumbing: libpd runs with its *queued* message interface, so messages from
the patch land in a lock-free ring buffer on the audio thread and are delivered
on the GUI thread by a timer in `FormulsEngine` — the quit request arrives on
the message thread, where it is safe to tear everything down.

## Building

### macOS

From the repository root, `./build-macOS.sh` does everything (faust externals,
abl_link~, Open Stage Control + node download, libpd, the JUCE app) and
produces a self-contained `formuls-<version>.app`. Requirements: Xcode, JUCE
at `~/JUCE` (or `PROJUCER=/path/to/Projucer` in the environment), faust, wget.

To work on just the app:

```bash
# once: build libpd and give it an @rpath install name
cd src/libs/libpd && make UTIL=true EXTRA=true
install_name_tool -id @rpath/libpd.dylib libs/libpd.dylib
codesign --force -s - libs/libpd.dylib

# regenerate the Xcode project after editing formuls.jucer
~/JUCE/Projucer.app/Contents/MacOS/Projucer --resave src/app/formuls.jucer

# build + run
cd src/app/Builds/MacOSX
xcodebuild -project formuls.xcodeproj -configuration Release ARCHS=$(uname -m) ONLY_ACTIVE_ARCH=YES
./build/Release/formuls.app/Contents/MacOS/formuls
```

A development build run straight from `Builds/MacOSX` finds `src/pd` (and
`src/gui`, if you have downloaded Open Stage Control into it) by walking up
the directory tree — see `ResourceLocator.h` — and finds `libpd.dylib` through
a second rpath pointing back into `src/libs/libpd/libs`.

If JUCE is not at `~/JUCE`, open `formuls.jucer` in Projucer and update the
module paths (or edit the `MODULEPATH` entries in the file).

### Linux

`./build-linux.sh` is the equivalent (untested) script; it uses the
`Builds/LinuxMakefile` exporter and ships `libpd.so` in a `libs/` folder next
to the binary (matching the `$ORIGIN/libs` rpath set in `formuls.jucer`).

### Test autostart

Building with the preprocessor definition `FORMULS_AUTOSTART_TEST=1` makes the
app pick the first output device and press Start by itself one second after
launch — handy for testing the engine without clicking:

```bash
xcodebuild -project formuls.xcodeproj -configuration Release \
  GCC_PREPROCESSOR_DEFINITIONS='$(inherited) FORMULS_AUTOSTART_TEST=1'
```

That build also logs the address panel's contents and the status line, and
reads a few environment variables so a whole run can be scripted:

| Variable | Effect |
| --- | --- |
| `FORMULS_TEST_SAMPLERATE` | Selects that sample rate instead of the default 48 kHz. |
| `FORMULS_TEST_CHANNELS` | Selects that channel count instead of the default 2. |
| `FORMULS_TEST_RECORD_SECONDS` | Records for that many seconds, then finishes the file with no save dialog and logs where it went. |
| `FORMULS_SNAPSHOT_PATH` | Writes a PNG of the window to that path four seconds after launch. JUCE renders it itself, so it needs no screen-recording permission. |

```bash
FORMULS_TEST_CHANNELS=14 FORMULS_TEST_SAMPLERATE=96000 \
FORMULS_TEST_RECORD_SECONDS=3 FORMULS_SNAPSHOT_PATH=/tmp/formuls.png \
  ./build/Release/formuls.app/Contents/MacOS/formuls
```

## Runtime resource layout

The app expects (in `formuls.app/Contents/Resources`, or next to/above a
development binary — see `ResourceLocator.h`):

```
pd/_main.pd              the patch loaded by libpd
pd/externals/            compiled faust + abl_link~ externals
gui/node                 node.js binary
gui/open-stage-control/  the o-s-c node package
gui/_main.json           the formuls control layout
```

The audio device is opened at the sample rate chosen in the window (48 kHz
default) with 512-frame buffers when possible; whatever the device actually
provides is fine — libpd is initialised at the device's real sample rate
(the status line says so if it differs from the request), and `FormulsEngine`
adapts libpd's fixed 64-frame ticks to any device buffer size.
