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

//-----MAIN-----//
//Signal inputs: 1)input synth voice ; 2)input from fx (pre-flooper processing);
//Signal outputs: 1)output of flooper to fx for processing; 2)output to

process = flooper(tempo,trigx);

//----------------------------------------------------------------------------------------//
//----------------------------------FLOOPER-----------------------------------------------//
//----------------------------------------------------------------------------------------//
//---------------flooper-------------//
flooper(tempo,trigx) = (rwtable(floopertablesize,0.0,recindex,_,readindex) : ampenvelope : pitchshift(tempo,trigx) : *(level)) : *(play :si.smoo)
  with {
    //-------- paramter control interface
    record = button("record") : int;
    play = checkbox("play") : int;
    loop = checkbox("loop") : *(play) : int;
    reverse = checkbox("reverse") : int;
    level = hslider("level",0,0,1,0.01) : pow(2) : si.smoo;

    speed = hslider("speed",1,0.01,20,0.125) : automrec(_,tempo,sperecord,speloop,trigx,speact) : chaos(spechaos,1) : vbargraph("speedO",0,1);
    sperecord = checkbox("sperecord");
    speloop = checkbox("speloop");
    speact = checkbox("speact");
    spechaos = hslider("spechaos",0,0,1,0.01);

    duration = hslider("duration",1,0.0001,1,0.0001) : automrec(_,tempo,durrecord,durloop,trigx,duract) : chaos(durchaos,2) : vbargraph("durationO",0,1);
    durrecord = checkbox("durrecord");
    durloop = checkbox("durloop");
    duract = checkbox("duract");
    durchaos = hslider("durchaos",0,0,1,0.01);

    readoffset = hslider("readoffset",0,0,1,0.01) : automrec(_,tempo,rearecord,realoop,trigx,reaact) : chaos(reachaos,3) : vbargraph("readoffsetO",0,1) : si.smoo;
    rearecord = checkbox("rearecord");
    realoop = checkbox("realoop");
    reaact = checkbox("reaact");
    reachaos = hslider("reachaos",0,0,1,0.01);

    envelope = hslider("envelope",0,0,1,0.01) : automrec(_,tempo,envrecord,envloop,trigx,envact) : vbargraph("envelopeO",0,1) : 1-(_) : pow(10) : *(9999) : +(1) : si.smoo;
    envrecord = checkbox("envrecord");
    envloop = checkbox("envloop");
    envact = checkbox("envact");

    dropout = hslider("dropout",0,0,1,0.01) : automrec(_,tempo,drorecord,droloop,trigx,droact) : vbargraph("dropoutO",0,1) : pow(2) : si.smoo;
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
    countmaster = countforward,countbackward : ba.selectn(2,reverse); //aggregate of countforward and countbackward functions
    sampledropout = no.lfnoise0(32) : *(0.5) : +(0.5) : ba.sAndH(countmaster <=(1) : ba.impulsify) : >(dropout); // calculate whether a sample/grain should play or not
    readindex = countmaster <: *(speed2 : ba.sAndH(countmaster ==(0))) +(readoffset *(D) : ba.sAndH(countmaster ==(0))) : %(D) : *(sampledropout) : int; //the main counter that reads from the rwtable
    // Grain/sample envelope: raised cosine envelope applied to amplitude
    ampenvelope = _ : *(cos(readindex/(samplelength*(speed)) : *(ma.PI *(2))) : *(0.5) : +(0.5) : 1-(_) : *(envelope) : (_,1 : min));

    //---------pitchshift FX---------//
    pitchshift(tempo,trigx) = _ : ef.transpose(100,50,pshift)
    with {
      pshift = hslider("pitchshift",0.5,0,1,0.01) : automrec(_,tempo,pitrecord,pitloop,trigx,pitact) : chaos(pitchaos,23) : vbargraph("pitchshiftO",0,1) : *(48) : -(24) : si.smoo;
      pitrecord = checkbox("pitrecord");
      pitloop = checkbox("pitloop");
      pitact = checkbox("pitact");
      pitchaos = hslider("pitchaos",0,0,1,0.01) : pow(2.7);
    };
  };
//----------------------------------------------------------------------------------------//
//----------------------------------AUTOMATION_RECORD-------------------------------------//
//----------------------------------------------------------------------------------------//
//-----------automrec---------------//
/* Records incoming  values for automation playback */
automrec(val,tem,rec,loo,trix,act) = _ <: _,memory : ba.selectn(2,act)
with {
  tempo2 = tem : *((ma.SR)/1000) : int; // read value every 1ms
  tableSize = 48000; //

  /* calculate the sample duration */
  I = int(rec);		// convert button "Record" signal from float to integer
  R = (I-I') <= 0;		// Reset capture when button is pressed
  D = (+(I):*(R))~_ : ba.sAndH(1-(rec)) : int;		// Compute capture duration while button is pressed: 0..NNNN0..MMM
  D2 = D-(1) : *(tem) : int;

  /* table write and read */
  recindex = (+(1) : *(rec))~_ : _/(tempo2) : *(rec) : int; // records audio to tables below
  counter = ba.countup(D2,trix), ba.sweep(D2,loo) : ba.selectn(2,loo) : /(tempo2) : int; // counter to read from the table

  memory = _ <: rwtable(tableSize,0.0,recindex,_,counter), _ : ba.selectn(2,rec) :> si.smoo ; //write and read automation to and from table
};
// automrec global params //
tempo = hslider("tempo",1,0.01,1,0.01); // controls automrec read speed
trigx = button("trigx"); //plays automation read

//----------------------------------------------------------------------------------------//
//----------------------------------CHAOS_RANDOM------------------------------------------//
//----------------------------------------------------------------------------------------//
//----------randomnoise----//
// geneates a random value. can be seeded so you have independent randoms.
randomnoise(freq,seed) = randomx / (RANDMAX) : ba.latch(os.oscrs(freq))
with{
	mask = 4294967295; // 2^32-1
	randomx = +(10000 +(seed)) ~ *(1103515245) & mask; // "linear congruential"
	RANDMAX	= 2147483647.0; // = 2^31-1 = MAX_SIGNED_INT in 32 bits
};

//chaos: generates a continual stream of random values between 0 and 1 and adds it to the incoming signal
chaos(value,seed) = _ : +(randomnoise(8,seed) : *(value) : si.smoo) : min(1,_) : max(0,_) : _;
