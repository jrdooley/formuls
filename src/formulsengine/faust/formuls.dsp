//----------------------------------------------------------------------------------------//
//------------------------------------------formuls---------------------------------------//
//----------------------------------------------------------------------------------------//
// compile with: faust2puredata -vec -lv 0 -vs 4 -clang
declare names "formuls";
declare author "James Dooley: info@formuls.co.uk";
declare copyright "James Dooley";
declare license "MIT";
declare options "[osc:on]";

import("stdfaust.lib");
fx = library("ffx.lib");
fs = library("fsynth.lib");

//--------------------------------//
//-------GLOBAL_PARAMETERS--------//
//--------------------------------//

//------VOLUME----//
volume =  _ : *(vol)
with{
  vol = hslider("volume",0,0,1,0.01) : si.smoo;
};

mute = hslider("mute",0,0,1,1) : 1-(_) : si.smoo;

//---------------------------------------------------------------------------------------//
//-------------------------------------------MAIN----------------------------------------//
//---------------------------------------------------------------------------------------//

/* Signal inputs: 1)input synth voice frequency modulation;  2)input envelope follower; Signal outputs: 1)left audio channel; 2)right audio channel; 3) mono signal when synth muted */
process(mod,env) = fs.synth(mod) : fx.fx : *(env : fx.envelopefollower) : volume <: fx.panner <: *(mute),*(mute),_,_;
