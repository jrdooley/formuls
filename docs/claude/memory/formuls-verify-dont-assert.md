---
name: formuls-verify-dont-assert
description: "On formuls, claims must be demonstrated — compile it, measure it, load the patch. Guessing has cost this project real time."
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 3a526a53-6893-49a5-9ed3-319a823aab4e
  modified: 2026-09-06T22:01:57.500Z
---

James's own session notes set this bar before I arrived ("verified, not
assumed"), and the 6 September audit confirmed why it matters.

**Why:** a previous session recorded two OSC routing "defects" in `_main.pd`
that did not exist — the objects were miscounted by eye — and the branch was
reset on the strength of it. In the same codebase, a Faust refactor that looked
correct made `f_repeater~` allocate **28 GiB**; only compiling it and summing
the generated struct revealed that.

**How to apply:**

- **Pd patches:** count objects with a parser, never by eye. `#X text` comments
  occupy connection indices. After any hand edit, resolve every connection to
  its source/destination *objects* before and after, then load the patch in Pd
  and read the console.
- **Faust:** compile with the build's own flags (`faust -vec -lv 0 -vs 4`) and
  measure the generated `mydsp` struct. Faust sizes `ba.sec2samp` tables for
  **192 kHz** (`platform.lib`: `SR = min(192000.0, ...)`), so table maths is 4×
  larger than 48 kHz arithmetic suggests.
- **"Did my change cause this?"** Build two artefacts differing only in the
  suspect file and diff their console output. For the JUCE app, `xcodebuild ...
  GCC_PREPROCESSOR_DEFINITIONS='$(inherited) FORMULS_AUTOSTART_TEST=1'` makes it
  runnable headlessly, and `src/tools/screenshot-gui.py` drives the GUI
  deterministically.
- **Say what was *not* verified.** The Linux build path and the ALSA/JACK device
  case could not be tested on macOS; both were marked untested rather than
  quietly shipped as done. Distinguish "not attributable to this change" from
  "pre-existing" — they are different claims.

Do this economically: see [[keep-cache-read-cost-down]] for cutting the
transport overhead without cutting the checking.
