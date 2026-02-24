#!/bin/bash

# Start the formulsengine. $1 = audio device; $2 = number of audio channels 2 or 14
./pd/formulsengine $1 $2 &

# Start the O-S-C control interface
# ./gui/node gui/open-stage-control/ --send 127.0.0.1:9000 --port 9001 --read-only true --load gui/_main.json --state gui/_formuls-default.state --client-options framerate=25 hdpi=0 &
./gui/node gui/open-stage-control/ --send 127.0.0.1:9000 --port 9001 --load gui/_main.json --client-options framerate=25 hdpi=0 &

trap 'kill $(jobs -p)' EXIT