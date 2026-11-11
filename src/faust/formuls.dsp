//----------------------------------------------------------------------------------------//
//------------------------------------------formuls---------------------------------------//
//----------------------------------------------------------------------------------------//
// compile with: faust2puredata -vec -lv 0 -vs 4 -clang
declare names "formuls";
declare author "James Dooley: info@formuls.co.uk";
declare copyright "James Dooley";
declare license "GPL-3.0-or-later";
declare options "[osc:on]";

import("stdfaust.lib");
fx = library("ffx.lib");
fs = library("fsynth.lib");
// fo = library("fosc.lib");

//--------------------------------//
//-------GLOBAL_PARAMETERS--------//
//--------------------------------//

//------VOLUME----//
// The x(0.1) is a fixed -20 dB pad, not a taste decision. formuls runs six
// instances of this synth, each summing POLYPHONY = 20 voices with no 1/N
// normalisation (fsynth.lib), so an instance playing chords measured a peak of
// 1.508 -- +3.57 dBFS on its own, before anything else was playing. With the
// pad: one instance peaks at -16.4 dBFS, six summing incoherently at -8.6, and
// six in unison -- the worst case there is -- at -0.87, which is 0.13 dB off
// the safety limiter's ceiling. A true 1/(20*6) would be -41.6 dB and would
// leave sparse playing inaudible; this keeps the distance between a single note
// and a dense chord, which is the instrument.
//
// It goes HERE, after fx.fx in process(), deliberately: ffx.lib's chain holds
// saturation and bitcrush2, both level-dependent, so padding after them leaves
// every effect seeing the drive it sees today. Padding earlier would have
// retuned two effects as a side effect of a level fix.
//
// The slider itself stays 0..1 so the Open Stage Control widgets and the OSC
// namespace are unchanged; only what 1.0 means has moved. See
// docs/headroom/README.md.
volume =  _ : *(vol)
with{
  vol = hslider("volume",0,0,1,0.01) : *(0.1) : si.smoo;
};

mute = hslider("mute",0,0,1,0.01) : 1-(_) : si.smoo;

//---------------------------------------------------------------------------------------//
//-------------------------------------------MAIN----------------------------------------//
//---------------------------------------------------------------------------------------//

/* Signal inputs: 1)input synth voice frequency modulation;  2)input envelope follower; Signal outputs: 1)left audio channel; 2)right audio channel; 3) mono signal when synth muted */
process(mod,env) = fs.synth(mod) : fx.fx : *(env : fx.envelopefollower) : volume <: fx.panner <: *(mute),*(mute),_,_;
