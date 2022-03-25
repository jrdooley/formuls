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

routing = si.bus(4) : (fx.repeater : sp.stereoize(fx.djfilter)),(sp.stereoize(*(reverbinlevel))) :>  sp.stereoize(fx.limiter);


process = routing;
