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
// cleanverb = hslider("title",default,min,max,step)

// process = si.bus(4) : si.bus(2),(fx.reverb :> si.bus(4)) :> (si.bus(2) : sp.stereoize(fx.djeq) : sp.stereoize(fx.limiter)),si.bus(2);
process = si.bus(4) : si.bus(2),(fx.reverb :> si.bus(2) <: si.bus(6)) :> (si.bus(2) : sp.stereoize(fx.djeq : fx.limiter)), sp.stereoize(*(0.5));
