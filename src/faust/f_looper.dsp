//------------------------------------------------------//
//------------------------flooper------------------------//
//------------------------------------------------------//
declare names "flooper";
declare author "formuls";
declare copyright "formuls";
declare version "1.0";
declare license "MIT";
declare options "[osc:on]";

import("stdfaust.lib");
fx = library("ffx.lib");

//-----MAIN-----//
//Signal inputs: 1)input synth voice ; 2)input from fx (pre-flooper processing);
//Signal outputs: 1)output of flooper to fx for processing; 2)output to

process(l,r) = l,r : fx.flooper : fx.pitchshift, fx.pitchshift;