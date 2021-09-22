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

process = si.bus(2) : fx.repeater : sp.stereoize(fx.djeq);
