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
fo = library("formuls.lib");
fx = library("ffx.lib");
fs = library("fsynth.lib");

//--------------------------------//
//-------GLOBAL_PARAMETERS--------//
//--------------------------------//

/* fo.automrec global params */
tempo = hslider("tempo",1,0.01,1,0.01); // controls fo.automrec read speed
//------VOLUME----//
volume =  _ : *(vol)
with{
  vol = hslider("volume",0,0,1,0.01) : vbargraph("volumeO",0,1) : si.smoo;
};
mute = hslider("mute",0,0,1,1) :  si.smoo;
//--------------------------------//


//---------------------------------------------------------------------------------------//
//-------------------------------------------MAIN----------------------------------------//
//---------------------------------------------------------------------------------------//

/* Signal inputs: 1)input synth voice frequency modulation; 2)input ADSR trigger; 3)input envelope follower; Signal outputs: 1)left audio channel; 2)right audio channel */
process(mod,trig,env) = fs.synth(mod,tempo,trig) : fx.fx(_,tempo,trig) : *(env : fx.envelopefollower) : *(0.0625) : volume : sp.panner(mute) <: fx.panner(tempo,trig),_;
//---------------------------------------------------------------------------------------//
