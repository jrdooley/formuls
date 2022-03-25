#!/bin/bash

# $1 = audio output

var=$(pwd) # finds wokring directory

# starts open-stage-control and loads formuls-ui. This is available at 127.0.0.1:8080. GUI sends OSC messages on 8888, receives messages on 8889
./open-stage-control/open-stage-control.app/Contents/MacOS/open-stage-control -s 127.0.0.1:8888 -o 8889 -l ./open-stage-control/_main.json &

# starts "audio" instance of pd, loads _main-audio.pd patch
./formulsengine 1 $1 4 &

# starts "control" instance of pd, loads _main-control.pd patch
./formulsengine 0 $1 1 &

# start Audio Plugin Host
./AudioPluginHost/AudioPluginHost.app/Contents/MacOS/AudioPluginHost

# when pd is quit, the script kills all child processes and exits
trap 'kill $(jobs -pr)' SIGINT SIGTERM EXIT # kills all processes

exit 0
