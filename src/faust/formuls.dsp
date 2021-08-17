//----------------------------------------------------------------------------------------//
//------------------------------------------formuls---------------------------------------//
//----------------------------------------------------------------------------------------//
declare names "formuls";
declare author "James Dooley: info@formuls.co.uk";
declare copyright "James Dooley";
declare version "1.0";
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
  vol = hslider("volume",0,0,1,0.01) : vbargraph("volumeO",0,1) : si.smoo;
};

mute = hslider("mute",0,0,1,1) :  si.smoo;

//---------------------------------------------------------------------------------------//
//-------------------------------------------MAIN----------------------------------------//
//---------------------------------------------------------------------------------------//

/* Signal inputs: 1)input synth voice frequency modulation; 2)input ADSR trigger; 3)input envelope follower; Signal outputs: 1)left audio channel; 2)right audio channel */
process(mod,trig,env) = (fs.synth(mod,trig) : fx.fx : *(env : fx.envelopefollower) : *(0.2) : volume : sp.panner(mute) <: fx.panner,_);
