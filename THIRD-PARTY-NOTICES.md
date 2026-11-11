# Third-party notices

formuls is licensed under the GNU General Public License v3.0 or later (see
[LICENSE](LICENSE)). It is built on the work below, each under its own licence.
This file lists what a formuls download contains or links, and what each
component's licence requires. Versions are the ones pinned by the build scripts
and `.github/workflows/release.yml`.

Nothing here is legal advice; it is a record of what ships, kept so that the
obligations are visible rather than implied.

## What ships inside a formuls app or plugin

| component | version | licence | how it is used |
|---|---|---|---|
| [JUCE](https://juce.com) | 9.0.1 | commercial JUCE licence (JUCE 9 is dual-licensed AGPLv3 / commercial) | linked into the app and both plugins |
| [Pure Data](https://puredata.info) (via libpd) | libpd submodule | Standard Improved BSD | the audio engine, linked as `libpd.dylib` |
| [libpd](https://github.com/libpd/libpd) | submodule | Standard Improved BSD | embeds Pd in the app and plugins |
| [Ableton Link](https://github.com/Ableton/link) | bundled in `abl_link` | **GPLv2-or-later** | compiled into the `abl_link~` Pd external, loaded in-process by libpd |
| [abl_link](https://github.com/libpd/abl_link) | submodule | Standard Improved BSD | the Pd external wrapping Ableton Link |
| [Faust](https://faust.grame.fr) generated DSP | compiler 2.85.9 | Pd architecture file: LGPLv2.1-or-later; Faust standard libraries: per-function (MIT, STK-4.3, LGPL) | `formuls~`, `f_repeater~`, `f_reverb~` externals |
| [Open Stage Control](https://openstagecontrol.ammd.net) | 1.31.0 | **GPLv3** | the browser client served to tablets and phones |
| [Node.js](https://nodejs.org) | 22.17.0 | MIT (plus its own bundled dependencies' licences) | runs Open Stage Control in the app; **not** shipped in the plugins |

formuls' own code — `src/app`, `src/plugin`, `src/pd`, `src/faust`, `src/gui`,
`src/tools` and the build scripts — is GPLv3-or-later, Copyright (C) 2014-2026
James Dooley.

## The copyleft components, and why they are compatible

**Ableton Link, GPLv2-or-later.** This is the one copyleft component linked into
formuls' own process: it is compiled into `abl_link~.pd_darwin`, which libpd
loads at run time and which shares the engine's address space. "Or later" means
it can be used under GPLv3, so it is compatible with formuls' licence. Ableton
also license Link commercially for proprietary applications
(<link-devs@ableton.com>); formuls does not use that option.

**Open Stage Control, GPLv3.** Only the browser client is shipped. It runs in the
player's own browser and talks to formuls over a WebSocket, so it is a separate
program aggregated in the same download rather than part of formuls' binary
(GPLv3 section 5). Its own licence text ships beside it, in
`Resources/gui/open-stage-control/LICENSE`, and its source is available from the
Open Stage Control project. formuls' GPLv3 licence and its GPLv3 are in any case
the same licence.

In the app, the stock Open Stage Control *server* also ran, as a Node.js child
process. Since `EmbeddedGuiServer` it is reimplemented in C++ from the wire
protocol (see `docs/embedded-gui/README.md`); the client is still the stock one,
served unmodified.

**The Faust Pd architecture file, LGPLv2.1-or-later.** Compiled into each
generated external. LGPL permits use under the GPL, so these are distributable
under formuls' GPLv3.

Open Stage Control's own bundled npm dependencies are overwhelmingly MIT, ISC,
BSD-3 and Apache-2.0, with a handful of GPL-2 and GPL-3 packages. Those are
upstream Open Stage Control's dependency tree, inside its own distribution;
formuls adds nothing to it.

## JUCE, and building formuls yourself

The JUCE framework modules are dual-licensed under AGPLv3 and a commercial JUCE
licence. formuls' releases are built under a **commercial JUCE licence**, so no
AGPL obligation attaches to JUCE in a formuls download, and nothing in formuls'
GPLv3 licence grants you rights to JUCE.

To build formuls yourself you need your own JUCE licence. The free AGPLv3 option
is enough: formuls' own code is GPLv3-or-later, which is compatible with linking
AGPLv3 JUCE, and a build you make that way carries AGPLv3's obligations for the
JUCE part — including its network-use clause if you distribute or serve it.

## The permissive components

Pure Data, libpd and the `abl_link` wrapper are under the "Standard Improved BSD
License" (Pd's own wording), which requires the copyright notice, the conditions
and the disclaimer to be reproduced in distributions. Their licence texts ship in
`src/libs/libpd/LICENSE.txt`, `src/libs/libpd/pure-data/LICENSE.txt` and
`src/libs/abl_link/LICENSE`, and Node.js's MIT notice travels with the `node`
binary in the app bundle.

Pd's optional extras built with libpd (`bonk~`, `fiddle~`, `pd~`, `pique` and
the rest) are covered by the same Pure Data licence.
