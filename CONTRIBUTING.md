# Contributing to formuls

formuls is free software under the **GNU General Public License v3.0 or later**
(see [LICENSE](LICENSE) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)).
Patches are welcome. There is one piece of paperwork, and this file explains why
it exists rather than just asking for it.

## Copyright, and why sign-off is required

Every commit in formuls' history to date is by James Dooley, so he holds
copyright in the whole work. That matters for a practical reason: a sole
copyright holder can relicense their own work at any time — to a later GPL, to
AGPL, or under separate commercial terms alongside the public GPL release. The
moment a contribution lands from someone else, they hold copyright in that
contribution, and the project can no longer be relicensed without their
agreement.

Projects get permanently stuck this way, not by choosing copyleft but by
accumulating contributors and no record of who agreed to what. So formuls uses
the **Developer Certificate of Origin**: a one-line sign-off in which you state
that you wrote the contribution, or otherwise have the right to submit it under
the project's licence.

Add it with `git commit -s`, which appends:

    Signed-off-by: Your Name <your@email>

Use your real name and a real address. A pull request whose commits are not
signed off cannot be merged.

The DCO certifies the *origin* of your contribution; it does not assign your
copyright to anyone. You keep it, and your contribution is licensed
GPL-3.0-or-later like the rest of formuls.

## Licence headers on new files

Source files carry a two-line SPDX notice, after any shebang:

    // SPDX-License-Identifier: GPL-3.0-or-later
    // Copyright (C) 2014-2026 James Dooley (aka formuls)

Use `#` for shell and Python, `REM` for `.bat`, `//` for C++, JavaScript and
Faust. Add your own copyright line below the existing one for a file you create
or substantially rewrite. The short SPDX form is used rather than the GPL's full
eleven-line boilerplate: it is machine-readable, it is what the LICENSE file is
there to expand, and across sixty-odd files the long form is mostly noise.

Files under `src/libs/` (git submodules) and `src/prebuilt/` are other people's
work and are left exactly as upstream ships them. `docs/**/tools/` holds one-off
analysis and measurement scripts rather than parts of the program, and is not
stamped.

## Before you open a pull request

- `.github/workflows/version.yml` checks the six build scripts agree on
  `VERSION`; it runs on every pull request and must pass.
- A change to a build script, `src/plugin/**` or `src/tools/plugin-probe/**`
  also triggers the full macOS, Linux and Windows builds. See
  [docs/release.md](docs/release.md).
- `AGENTS.md` describes the layout and the project's conventions.

## The Developer Certificate of Origin

Reproduced verbatim from <https://developercertificate.org/>.

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```
