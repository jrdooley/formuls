//----------------------------------------------------------------------------------------//
//----------------------------------FORMULS_MASTER_BUS_FX---------------------------------//
//----------------------------------------------------------------------------------------//
// compile with: faust2puredata -vec -lv 0 -vs 4 -clang

declare names "formuls";
declare author "James Dooley: info@formuls.co.uk";
declare copyright "James Dooley";
declare version "1.0";
declare license "MIT";
declare options "[osc:on]";

import("stdfaust.lib");
fx = library("ffx.lib");

reverbinlevel = hslider("reverblimiterlevel",0,0,1,0.01) : si.smoo;

routing = fx.digitaliser,fx.digitaliser,_,_,fx.digitaliser,fx.digitaliser : _,_, *(reverbinlevel),*(reverbinlevel), fx.repeater :> _,_ : fx.limiter, fx.limiter;

process = routing;
