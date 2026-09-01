# formuls, à la carte — a prototype without JUCE

This folder is a working prototype of the formuls launcher built from small
individual libraries instead of the JUCE framework, so the two approaches can
be compared side by side. It does everything the JUCE app (`src/app`) does:

- device / channel-count / sample-rate selection (same menus, same defaults,
  same nearest-rate fallback with a note in the status line),
- Start/Stop boots and shuts down libpd + Open Stage Control together,
- the read-only panel listing the control GUI's web addresses,
- closing the window quits everything; the Pd patch can quit the app by
  sending to `formuls-quit`.

## What replaced what

| Job | JUCE app | This prototype | Licence |
| --- | --- | --- | --- |
| Audio device I/O | `juce::AudioDeviceManager` | **RtAudio 5.1** (`vendor/`, restored from this repo's git history — the original formulsengine used it) | MIT-style |
| DSP | libpd (unchanged) | libpd (unchanged) | BSD |
| Launcher window | `juce_gui_basics` + LookAndFeel | **plain Cocoa** (`src/main.mm`) | system |
| node child process | `juce::ChildProcess` | `src/ChildProcess.*` — ~70 lines of `posix_spawn` | ours |
| GUI web addresses | `juce::IPAddress` | ~40 lines of `getifaddrs` in `src/OpenStageControl.cpp` | ours |
| Resource lookup | `juce::File` | `std::filesystem` | system |
| Build system | Projucer → Xcode project | one 60-line **Makefile** | — |

Nothing here is AGPL or commercially licensed, so — unlike the JUCE build —
there is no licence tension with formuls' CC BY-NC-SA distribution.

## Numbers (measured on this machine, Apple Silicon, Release)

|  | JUCE app | à la carte |
| --- | --- | --- |
| Own source (excl. vendored/generated) | ~1300 lines | ~1200 lines |
| Framework/vendored code compiled in | ~½M lines of JUCE modules | ~12k lines (RtAudio) |
| Binary size | 4.4 MB | 0.2 MB |
| Clean build time | minutes | seconds |
| Build prerequisites | Xcode + JUCE + Projucer | Xcode command line tools |

## Building and running

libpd must be built first (both apps share it):

```bash
cd ../libs/libpd && make UTIL=true EXTRA=true
install_name_tool -id @rpath/libpd.dylib libs/libpd.dylib
codesign --force -s - libs/libpd.dylib
```

then:

```bash
cd ../../app-alacarte
make run
```

A development run finds `src/pd` by walking up from the executable, exactly
like the JUCE app. To run against a fully assembled bundle's resources
(including node + Open Stage Control):

```bash
FORMULS_RESOURCE_ROOT=/path/to/formuls.app/Contents/Resources ./build/formuls-alacarte
```

Test hooks (runtime env vars, no special build): `FORMULS_AUTOSTART=1`
presses Start after 1 s, `FORMULS_TEST_SAMPLERATE=44100` picks that rate,
`FORMULS_SNAPSHOT_PATH=/tmp/w.png` writes a window screenshot after 4 s.

## Styling

The constants at the top of `src/main.mm` hold the window size and the
formuls green palette (window background, address panel, status line).
The pop-ups and button are native macOS controls and keep the system look —
that is the main visual difference from the JUCE version, which draws its
own custom-styled widgets.

## Honest limitations

- **The GUI layer is macOS-only.** Engine, OpenStageControl, ChildProcess
  and ResourceLocator are portable C++ (ChildProcess is POSIX, so Linux is
  ~free; Windows would need a CreateProcess variant). Cross-platform would
  mean one `main` per platform, or swapping `main.mm` for FLTK or Dear
  ImGui.
- **No plugin story.** JUCE (or iPlug2/DPF) earns its keep the day formuls
  should become a VST3/AU plugin; none of these pieces provide that.
- **Device-change robustness.** JUCE's device layer handles hot-plugging
  and default-device switches more gracefully than RtAudio 5.1. If the
  selected interface disappears mid-performance, this prototype's stream
  just stops.
- RtAudio 5.1 is vendored as-is from the repo's history and compiled with
  warnings suppressed; upgrading to RtAudio 6 (or replacing it with
  miniaudio) would be the first step if this prototype were promoted.

## One bug worth remembering

`pd::PdBase::receiveMessages()` **crashes** (it warns but does not return)
if called before `init(..., queued = true)` has set the ring buffer up.
The message pump here therefore no-ops while the engine is stopped — see
`Engine::receiveMessages()`. The JUCE app avoids the same trap by only
starting its timer inside `FormulsEngine::start()`.
