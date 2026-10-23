#!/usr/bin/env python3
"""patch-osc-uxlog.py <open-stage-control-dir> <gui-dir>

Turns a downloaded Open Stage Control package into the UX-logging variant,
which also has Replay mode. Run by the build scripts only when
FORMULS_UXLOG=1. Three changes:

1. client/index.js: expose the client's IPC object as window.__formulsIpc,
   so the logger can ship its batches over the existing WebSocket rather
   than opening a second connection the server would treat as a new tablet.
2. client/index.html: load client/formuls-uxlog.js (the logger) and
   client/formuls-replay.js (the replay viewer, idle in a live session)
   after the client bundle.
3. <gui-dir>/formuls-uxlog-module.js: the custom module that writes the log,
   and formuls-replay-module.js, used instead of it in Replay mode.
   OpenStageControlProcess enables logging when, and only when, this file
   exists, so a normal build cannot log by accident.

Each edit is matched on exact text and stops the build if it is missing,
the same policy as brand-osc.sh: a newer Open Stage Control fails loudly
instead of shipping a "logging" build that records nothing.
"""
import pathlib
import shutil
import sys

HERE = pathlib.Path(__file__).resolve().parent / 'uxlog'


def replace_once(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        sys.exit(f'patch-osc-uxlog: expected exactly one {old!r} in {path}, '
                 f'found {text.count(old)}')
    path.write_text(text.replace(old, new))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    osc = pathlib.Path(sys.argv[1])
    gui = pathlib.Path(sys.argv[2])

    replace_once(osc / 'client' / 'index.js',
                 'nl=new class Ipc extends EventEmitter',
                 'nl=window.__formulsIpc=new class Ipc extends EventEmitter')

    shutil.copy(HERE / 'formuls-uxlog-client.js', osc / 'client' / 'formuls-uxlog.js')
    shutil.copy(HERE / 'formuls-replay-client.js', osc / 'client' / 'formuls-replay.js')
    tag = '<script type="module" defer src="/__APP_DIR__/client/index.js"></script>'
    replace_once(osc / 'client' / 'index.html', tag,
                 tag + '\n    <script type="module" defer src="/__APP_DIR__/client/formuls-uxlog.js"></script>'
                     + '\n    <script type="module" defer src="/__APP_DIR__/client/formuls-replay.js"></script>')

    shutil.copy(HERE / 'formuls-uxlog-module.js', gui / 'formuls-uxlog-module.js')
    shutil.copy(HERE / 'formuls-replay-module.js', gui / 'formuls-replay-module.js')
    print('patch-osc-uxlog: UX logging enabled')


if __name__ == '__main__':
    main()
