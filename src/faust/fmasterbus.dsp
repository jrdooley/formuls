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

revlimsend = hslider("reverblimitersend",0,0,1,0.01) : si.smoo;
revclean = 1-(revlimsend);

process = si.bus(4) : si.bus(2),(fx.reverb :> si.bus(2) <: si.bus(6) : _*(revclean),_*(revclean),_*(revlimsend),_*(revlimsend),_*(revclean),_*(revclean)) :> (si.bus(2) : fx.repeater : sp.stereoize(fx.djeq : fx.limiter)), sp.stereoize(*(0.5));
