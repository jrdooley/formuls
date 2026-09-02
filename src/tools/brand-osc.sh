#!/bin/sh
# brand-osc.sh <open-stage-control-dir> <version>
#
# Rebrands the Open Stage Control client's greeting header, which ships as:
#
#     Open Stage Control <span class="version">v1.31.0</span>
#
# and becomes:
#
#     formuls <span class="version">0.3.0-beta</span>
#
# The build scripts call this straight after unpacking the downloaded
# Open Stage Control package, so the shipped GUI carries formuls' own name
# and version rather than the toolkit's.
#
# Deliberately matched on the text rather than on a line number: the header
# is at line 40 in 1.31.0, but that will move the moment Open Stage Control
# is updated, and a rebrand that silently does nothing is worse than one
# that stops the build. If the markup ever changes, this fails loudly with
# the line it actually found.
#
# POSIX sh, and it avoids `sed -i` because BSD and GNU sed disagree about
# that flag -- so it works unchanged on macOS and Linux.

set -e

OSC_DIR="$1"
VERSION="$2"

if [ -z "$OSC_DIR" ] || [ -z "$VERSION" ]; then
    echo "usage: brand-osc.sh <open-stage-control-dir> <version>" >&2
    exit 1
fi

INDEX="$OSC_DIR/client/index.html"

if [ ! -f "$INDEX" ]; then
    echo "ERROR: no such file: $INDEX" >&2
    echo "       (expected the client folder of an Open Stage Control package)" >&2
    exit 1
fi

# Already branded? Then this is a rebuild over an existing tree; nothing to do.
if grep -q "formuls <span class=\"version\">" "$INDEX"; then
    echo "Open Stage Control client already branded; leaving it alone"
    exit 0
fi

if ! grep -q 'Open Stage Control <span class="version">' "$INDEX"; then
    echo "ERROR: could not find the Open Stage Control greeting header in:" >&2
    echo "       $INDEX" >&2
    echo "       Its markup must have changed. Look for the line holding" >&2
    echo "       'class=\"version\"' and update brand-osc.sh to match." >&2
    exit 1
fi

TMP="$INDEX.branding.tmp"
sed -E 's|Open Stage Control <span class="version">[^<]*</span>|formuls <span class="version">'"$VERSION"'</span>|' \
    "$INDEX" > "$TMP"
mv "$TMP" "$INDEX"

# Confirm it actually took, rather than trusting sed's exit status.
if ! grep -q "formuls <span class=\"version\">$VERSION</span>" "$INDEX"; then
    echo "ERROR: rebranding did not take effect in $INDEX" >&2
    exit 1
fi

echo "branded Open Stage Control client as \"formuls $VERSION\""
