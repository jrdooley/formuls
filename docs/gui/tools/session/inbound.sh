#!/bin/zsh
# inbound.sh PORT -- what Pd might send to one slider compound, plus LFO freq mode on
S=${0:A:h}; P=$1
o() { python3 $S/oscsend.py $P "$@"; }
o /attack1 0.6; o /attackchaos1 0.3; o /attacklfofreq1 0.7; o /attacklfodepth1 0.2; o /attackmoddepth1 -0.4
o /attackmod1 0 1 0 1 0 0; o /attackmod1/5 1
o /attackymodlabel1 3
o /attackquantise1 1; o /attacktrigstep1 1; o /attackchaostrigseq1 1; o /attackact1 0
o /lfofreq1 1
