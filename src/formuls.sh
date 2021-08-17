#!/bin/bash

var=$(pwd) # finds wokring directory

# starts open-stage-control and loads formuls-ui. This is available at 127.0.0.1:8080. GUI sends OSC messages on 8888, receives messages on 8889
./Resources/open-stage-control/open-stage-control.app/Contents/MacOS/open-stage-control -n -s 127.0.0.1:8888 -o 8889 -l ./Resources/open-stage-control/formuls-ui.json --read-only &

# start Audio Plugin Host
./Resources/AudioPluginHost/AudioPluginHost.app/Contents/MacOS/AudioPluginHost &

# starts "control" instance of pd, loads _main.pd patch
./Resources/Pd/Pd-0.51-4.app/Contents/Resources/bin/pd -nogui -r 48000 -audiobuf 10 -noadc -nodac -midiaddindev Xkey25 ./Resources/Pd/control/_main-control.pd &

# starts "audio" instance of pd, loads _main.pd patch
./Resources/Pd/Pd-0.51-4.app/Contents/Resources/bin/pd -nogui -r 48000 -audiobuf 10 -noadc -audioaddoutdev "BlackHole 16ch" -outchannels 4 -midiaddindev Xkey25 ./Resources/Pd/audio/_main-audio.pd

# when pd is quit, the script kills all child processes and exits
trap 'kill $(jobs -pr)' SIGINT SIGTERM EXIT # kills all processes

exit 0
