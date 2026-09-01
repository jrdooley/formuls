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
3. on **Stop formuls**, shuts both down again while the window stays open, so a
   different device or channel count can be chosen,
4. quits fully when the window is closed — stopping the engine and killing the
   Open Stage Control process it started,
5. quits when **the Pd patch asks it to** (see below).

## Source files

| File | What it does |
| --- | --- |
| `Source/Main.cpp` | App entry point; creates the main window, installs the look and feel, handles quitting. |
| `Source/MainComponent.h/.cpp` | The window contents: device/channel combo boxes, Start/Stop button, status line. |
| `Source/FormulsEngine.h/.cpp` | libpd embedded behind a `juce::AudioIODeviceCallback`; loads `_main.pd`, renders audio, receives patch messages. |
| `Source/OpenStageControlProcess.h/.cpp` | Starts/stops the Open Stage Control server (via the shared `formuls::ChildProcess`); also works out the GUI's web addresses. |
| `Source/FormulsLookAndFeel.h` | **All styling** — window title/size, colours, fonts, layout metrics. |
| `Source/ResourceLocator.h` | Finds the `pd/` and `gui/` resources both in the packaged app and in development builds. |
| `formuls.jucer` | Projucer project. Generates `Builds/MacOSX` (Xcode) and `Builds/LinuxMakefile`. |

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

## The performance readout

Above the status line both apps show a live readout:

```
DSP 32.6%  peak 69.0%   CPU 34.1%   missed 0
```

* **DSP** -- how much of its deadline the audio callback used, smoothed. A
  512-frame buffer at 48 kHz must be filled in 10.67 ms; if rendering takes
  3.5 ms that is 33%. **This is the number that predicts dropouts**, and it
  does not care how many cores the machine has.
* **peak** -- the worst single callback since the last reset. Audio glitches
  come from individual late buffers, not from the average, so a healthy
  average with a peak near 100% is worth knowing about.
* **CPU** -- the whole process (all threads) as a share of one core. This is
  the figure comparable to Activity Monitor.
* **missed** -- callbacks that went over their deadline. Shown only when
  non-zero.

**Click the readout to reset the peak and missed counters** -- reset, play
hard for a minute, then look again.

DSP and CPU answer different questions, and the gap between them is the
interesting part: if CPU climbs while DSP stays flat, the extra work is in
message handling, the GUI or OSC traffic rather than in the audio graph.
That is exactly the distinction to look at when comparing these two builds
under heavy modulation with a tablet connected.

The measurement code is shared verbatim by both apps
(`src/shared/PerformanceMeter.h`) precisely so the two readouts mean the
same thing and can be compared directly. Measured side by side on the same
machine, patch and device, the two builds agree closely -- which matches
what independent external measurement showed.

The first few callbacks after the stream opens are excluded from peak and
missed: they pay cold caches and thread promotion, and would otherwise
report a phantom dropout on every run.

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

## The control GUI never outlives the app

The launcher starts a node.js server that holds ports 9000 and 9001. If it
were left running after the app went away, the *next* launch's GUI would
silently fail to bind those ports -- so both apps guarantee the server dies
with them, however they die.

Signals cannot provide that guarantee on their own: SIGKILL (Force Quit)
is uncatchable and a hard crash may never reach a handler. So instead of
reacting to our own death, the kernel announces it. `ChildProcess::start()`
opens a pipe and spawns a one-line `/bin/sh` watchdog whose stdin is the
read end; the app holds the write end open for as long as it lives. When
the app dies for any reason, the kernel closes its descriptors, the pipe
hits EOF, and the watchdog kills the server. On a clean stop the app first
writes a byte, so the watchdog's read *succeeds* and it exits quietly while
the app terminates the server itself.

Both pipe ends are close-on-exec, so the server never inherits the write
end -- if it did, the pipe would never reach EOF and the mechanism would be
silently dead.

Verified by force-killing (`kill -9`) both apps mid-session: the server
process dies with them and port 9001 is released. Exactly one watchdog runs
while the server is up, and none is left behind afterwards.

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

That build also logs the address panel's contents, and — if the environment
variable `FORMULS_SNAPSHOT_PATH` is set — writes a PNG of the window to that
path four seconds after launch. JUCE renders the snapshot itself, so it works
without granting screen-recording permission:

```bash
FORMULS_SNAPSHOT_PATH=/tmp/formuls.png ./build/Release/formuls.app/Contents/MacOS/formuls
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
