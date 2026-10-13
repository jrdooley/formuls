#!/usr/bin/env python3
"""Point every JUCE module path in a .jucer file at one folder.

    jucer-module-path.py src/app/formuls.jucer C:/Users/me/JUCE/modules

The committed .jucer uses ../../../../JUCE/modules, which only resolves when the
repository sits exactly two levels below the home folder. The build scripts
rewrite it to an absolute path, resave with Projucer, then restore the file.
build-macOS.sh and build-linux.sh do this with sed; Windows has no sed, and an
inline `python -c` inside a batch file is too easily broken by cmd's quoting,
so build-windows.bat uses this instead.

Fails if the folder does not hold JUCE's modules, or if nothing was replaced.
"""

import os
import re
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    jucer, modules = sys.argv[1], sys.argv[2]
    modules = os.path.abspath(modules).replace("\\", "/")

    if not os.path.isdir(os.path.join(modules, "juce_core")):
        sys.exit(f"jucer-module-path: no juce_core in {modules}")

    with open(jucer, encoding="utf8") as f:
        text = f.read()

    new, count = re.subn(r'path="[^"]*JUCE/modules"',
                         lambda m: f'path="{modules}"', text)
    if count == 0:
        sys.exit(f"jucer-module-path: no JUCE/modules paths found in {jucer}")

    with open(jucer, "w", encoding="utf8", newline="") as f:
        f.write(new)
    print(f"jucer-module-path: {count} module paths -> {modules}")


if __name__ == "__main__":
    main()
