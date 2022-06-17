//----------------------------------------------------------------------------------------//
//----------------------------------FORMULS_MASTER_BUS_FX---------------------------------//
//----------------------------------------------------------------------------------------//

declare names "formuls";
declare author "James Dooley: info@formuls.co.uk";
declare copyright "James Dooley";
declare version "1.0";
declare license "MIT";
declare options "[osc:on]";

import("stdfaust.lib");
fx = library("ffx.lib");

reverbinlevel = hslider("reverblimiterlevel",0,0,1,0.01) : si.smoo;

routing = _,_,_,_,_,_ : _,_, *(reverbinlevel),*(reverbinlevel),fx.repeater :> _,_ : fx.limiter, fx.limiter;
// routing = si.bus(2) : _, *(reverbinlevel) :> _ : fx.repeater : fx.limiter : fx.djfilter;
process = routing;
