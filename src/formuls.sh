#!/bin/bash

var=$(pwd) # finds wokring directory

if [ $1 = -g ]; then
  # WITH GUI (for editing)
  # starts open-stage-control and loads formuls-ui. This is available at 127.0.0.1:8080. GUI sends OSC messages on 8888, receives messages on 8889
  ./Resources/open-stage-control/open-stage-control.app/Contents/MacOS/open-stage-control -s 127.0.0.1:8888 -o 8889 -l ./Resources/open-stage-control/open-stage-control/_main.json &

  # start Audio Plugin Host
  ./Resources/AudioPluginHost/AudioPluginHost.app/Contents/MacOS/AudioPluginHost &

  # starts "control" instance of pd, loads _main.pd patch
  ./Resources/Pd/Pd-0.51-4.app/Contents/Resources/bin/pd -r 48000 -audiobuf 10 -noadc -nodac -midiaddindev Xkey25 -midiaddoutdev IAC Bus 1 ./Resources/Pd/pd/control/_main-control.pd &

  # starts "audio" instance of pd, loads _main.pd patch
  ./Resources/Pd/Pd-0.51-4.app/Contents/Resources/bin/pd -r 48000 -audiobuf 10 -noadc -audioaddoutdev "BlackHole 16ch" -outchannels 14 -midiaddindev Xkey25 ./Resources/Pd/pd/audio/_main-audio.pd

  # when pd is quit, the script kills all child processes and exits
  trap 'kill $(jobs -pr)' SIGINT SIGTERM EXIT # kills all processes

  exit 0

fi

# NO GUI OPTION (Default)
# starts open-stage-control and loads formuls-ui. This is available at 127.0.0.1:8080. GUI sends OSC messages on 8888, receives messages on 8889
./Resources/open-stage-control/open-stage-control.app/Contents/MacOS/open-stage-control -n -s 127.0.0.1:8888 -o 8889 -l ./Resources/open-stage-control/open-stage-control/_main.json --read-only --fullscreen true &

# start Audio Plugin Host
./Resources/AudioPluginHost/AudioPluginHost.app/Contents/MacOS/AudioPluginHost &

# starts "control" instance of pd, loads _main.pd patch
./Resources/Pd/Pd-0.51-4.app/Contents/Resources/bin/pd -nogui -r 48000 -audiobuf 10 -noadc -nodac -midiaddindev Xkey25 -midiaddoutdev IAC Bus 1 ./Resources/Pd/pd/control/_main-control.pd &

# starts "audio" instance of pd, loads _main.pd patch
./Resources/Pd/Pd-0.51-4.app/Contents/Resources/bin/pd -nogui -r 48000 -audiobuf 10 -noadc -audioaddoutdev "BlackHole 16ch" -outchannels 14 -midiaddindev Xkey25 ./Resources/Pd/pd/audio/_main-audio.pd

# when pd is quit, the script kills all child processes and exits
trap 'kill $(jobs -pr)' SIGINT SIGTERM EXIT # kills all processes

exit 0
