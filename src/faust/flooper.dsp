//------------------------------------------------------//
//------------------------flooper------------------------//
//------------------------------------------------------//
declare names "flooper";
declare author "formuls";
declare copyright "formuls";
declare version "1.0";
declare license "MIT";
declare options "[osc:on]";

import("stdfaust.lib");
fo = library("formuls.lib");

//-----MAIN-----//
//Signal inputs: 1)input synth voice ; 2)input from fx (pre-flooper processing);
//Signal outputs: 1)output of flooper to fx for processing; 2)output to

process = _,_ : ro.cross(2) : flooper(tempo);

//----------------------------------------------------------------------------------------//
//----------------------------------FLOOPER-----------------------------------------------//
//----------------------------------------------------------------------------------------//
//---------------flooper-------------//
flooper(tempo,trigx) = (rwtable(floopertablesize,0.0,recindex,_,readindex) : ampenvelope : *(level)) : *(play :si.smoo)
  with {
    //-------- paramter control interface
    record = button("record") : int;
    play = checkbox("play") : int;
    loop = checkbox("loop") : *(play) : int;
    reverse = checkbox("reverse") : int;
    level = hslider("level",0,0,1,0.01) : pow(2) : si.smoo;

    speed = hslider("speed",1,0.125,20,0.125) : fo.automrec(_,tempo,sperecord,speloop,trigx,speact) : fo.chaos(spechaos,1) : max(0.125) : vbargraph("speedO",0.125,1);
    sperecord = checkbox("sperecord");
    speloop = checkbox("speloop");
    speact = checkbox("speact");
    spechaos = hslider("spechaos",0,0,1,0.01);

    duration = hslider("duration",1,0.0001,1,0.0001) : fo.automrec(_,tempo,durrecord,durloop,trigx,duract) : fo.chaos(durchaos,2) : vbargraph("durationO",0,1) : pow(3);
    durrecord = checkbox("durrecord");
    durloop = checkbox("durloop");
    duract = checkbox("duract");
    durchaos = hslider("durchaos",0,0,1,0.01);

    readoffset = hslider("readoffset",0,0,1,0.01) : fo.automrec(_,tempo,rearecord,realoop,trigx,reaact) : fo.chaos(reachaos,3) : vbargraph("readoffsetO",0,1);
    rearecord = checkbox("rearecord");
    realoop = checkbox("realoop");
    reaact = checkbox("reaact");
    reachaos = hslider("reachaos",0,0,1,0.01);

    envelope = hslider("envelope",0,0,1,0.01) : fo.automrec(_,tempo,envrecord,envloop,trigx,envact) : vbargraph("envelopeO",0,1) : 1-(_) : pow(10) : *(9999) : +(1) : si.smoo;
    envrecord = checkbox("envrecord");
    envloop = checkbox("envloop");
    envact = checkbox("envact");

    dropout = hslider("dropout",0,0,1,0.01) : fo.automrec(_,tempo,drorecord,droloop,trigx,droact) : vbargraph("dropoutO",0,1) : pow(2) : si.smoo;
    drorecord = checkbox("drorecord");
    droloop = checkbox("droloop");
    droact = checkbox("droact");

    //----table record functions-----//
    floopertablesize = 1920002; //40 seconds of audio at 48kHz + 2 extra samples. 1st sample of table empty for readback without sound, last sample empty for write without overwriting existing sample.
    recindex = ((+(1) : %(floopertablesize)) ~ *(record)) : +(1); // records audio to tables below
    //--------calculate the recorded sample duration---//
    I = int(record);				// convert button "Record" signal from float to integer
    R = (I-I') <= 0;		// Reset capture when button is pressed
    D = (+(I):*(R))~_ : +(1) : ba.sAndH(1-(record)) : si.smoo;		// Compute capture duration while button is pressed: 0..NNNN0..MMM

    //-------- Table read and envelope control-------//
    // "readindex" triggers counters and controls random sample drop out
    samplelength = D : *(duration) : /(speed) : int; // calculates the duration of the sample/grain in samples
    speed2 = speed : si.smoo;

    countforward = ba.countup(samplelength,play==(0)), ba.sweep(samplelength,play) : ba.selectn(2,loop); // forward playback of sample
    countbackward = ba.countdown(samplelength,play==(0)), ((<=(0) : ba.countdown(samplelength,play*(_)))~(_)) : ba.selectn(2,loop); // reverse/backward playback of sample
    countfandb = countforward,countbackward : ba.selectn(2,reverse); //aggregate of countforward and countbackward functions
    countmaster = countfandb <: *(speed2 : ba.sAndH(countfandb ==(0)));
    sampledropout = no.lfnoise0(32) : *(0.5) : +(0.5) : ba.sAndH(countmaster <=(1) : ba.impulsify) : >(dropout); // calculate whether a sample/grain should play or not
    readindex =  countmaster : +(readoffset *(D) : ba.sAndH(countfandb ==(0) : ba.impulsify)) : %(D) : *(sampledropout) : int; //the main counter that reads from the rwtable
    // Grain/sample envelope: raised cosine envelope applied to amplitude
    ampenvelope = _ : *(cos(countmaster/(samplelength*(speed2)) : *(ma.PI *(2))) : *(0.5) : +(0.5) : 1-(_) : *(envelope) : (_,1 : min));
  };

// fo.automrec global params //
tempo = hslider("tempo",1,0.01,1,0.01); // controls fo.automrec read speed
