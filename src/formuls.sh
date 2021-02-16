#!/bin/bash

var=$(pwd) # finds wokring directory

# starts open-stage-control and loads formuls-ui. This is available at 127.0.0.1:8080. GUI sends OSC messages on 8888, receives messages on 8889
./Resources/open-stage-control/open-stage-control.app/Contents/MacOS/open-stage-control -n -s 127.0.0.1:8888 -o 8889 -l ./Resources/open-stage-control/formuls-ui.json --read-only &

# starts pd, loads _main.pd patch and then starts all sub processes (with pd~)
./Resources/Pd/Pd-0.51-3.app/Contents/Resources/bin/pd -nogui ./Resources/Pd/_main.pd

# when pd is quit, the script kills all child processes and exits
trap 'kill $(jobs -pr)' SIGINT SIGTERM EXIT # kills all processes

exit 0
