#!/bin/zsh
# serve.sh PORT SESSION.json [STATE]  -- one o-s-c server per variant, the app's own flags.
APP=${APP:-$(ls -d /Applications/formuls-*.app | tail -1)/Contents/Resources/gui}
S=${0:A:h}
PORT=$1; SESSION=$2; STATE=$3
args=(--port $PORT --send 127.0.0.1:19999 --load $S/$SESSION --read-only --no-qrcode --client-options framerate=25 hdpi=0)
[[ -n $STATE ]] && args+=(--state $S/$STATE)
exec "$APP/node" "${OSC:-$APP/open-stage-control}" $args > $S/server-$PORT.log 2>&1
