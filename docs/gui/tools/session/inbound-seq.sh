#!/bin/zsh
# inbound-seq.sh PORT -- what Pd might send to the synth-1 sequencer panel
S=${0:A:h}; P=$1
o() { python3 $S/oscsend.py $P "$@"; }
o /aseqwrite1-3 0; o /aseqwrite1-20 1; o /seqwrite1-5 1; o /seqwrite1-20 1; o /seqwriteoff1-6 0.5; o /seqwriteoff1-21 0.9
