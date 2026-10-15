#!/bin/zsh
# mode.sh SERVERPORT MODE ON  -- set one mode (or modeselector) as Pd would, mark the log
S=${0:A:h}
python3 $S/oscsend.py $1 /${2}1 $3
