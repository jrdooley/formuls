//------------------------------------------------------//
//------------------------looper------------------------//
//------------------------------------------------------//
declare names "fooper";
declare author "formuls";
declare copyright "formuls";
declare version "1.0";
declare license "MIT";
declare options "[osc:on]";

import("stdfaust.lib");

//-----MAIN-----//
process = fooper(0,tempo(0));

//----------------------------------------------------------------------------------------//
//----------------------------------FOOPER------------------------------------------------//
//----------------------------------------------------------------------------------------//
//---------------fooper-------------//
fooper(v,tempo) = (rwtable(fooperTabSize,0.0,recIndex,_,readIndex) : ampenvelope : *(level))
  with {
    fooperTabSize = 960002; //20 seconds of audio at 48kHz + 2 extra samples. 1st sample of table empty for readback without sound, last sample empty for write without overwriting existing sample.
    fooperrec = button("fooperrecord%v") : int;
    // foopermasteramp = hslider("foopermasteramp",0,0,1,0.01);
    recIndex = ((+(1) : %(fooperTabSize)) ~ *(fooperrec)) : +(1); // records audio to tables below

    //--------calculate the sample duration---//
    I = int(fooperrec);				// convert button "Record" signal from float to integer
    R = (I-I') <= 0;		// Reset capture when button is pressed
    D = (+(I):*(R))~_ : +(1) : ba.sAndH(1-(fooperrec)) : si.smoo;		// Compute capture duration while button is pressed: 0..NNNN0..MMM

    //-------- paramter control interface
    play = checkbox("play%v") : int;
    loop = checkbox("loop%v") : int;
    reverse = checkbox("reverse%v");

    speed = hslider("speed%v",1,0.01,20,0.125) : automRec(_,tempo,sperecord,speloop,0,speact) : vbargraph("speedO%v",0,1);
    sperecord = checkbox("sperecord%v");
    speloop = checkbox("speloop%v");
    speact = checkbox("speact%v");

    readoffset = hslider("readoffset%v",0,0,1,0.01) : automRec(_,tempo,rearecord,realoop,0,reaact) : vbargraph("readoffsetO%v",0,1) : *(D);
    rearecord = checkbox("rearecord%v");
    realoop = checkbox("realoop%v");
    reaact = checkbox("reaact%v");

    duration = hslider("duration%v",1,0.0001,1,0.0001) : automRec(_,tempo,durrecord,durloop,0,duract) : vbargraph("durationO%v",0,1) : *(D) : /(speed) : +(1) : si.smoo : int;
    durrecord = checkbox("durrecord%v");
    durloop = checkbox("durloop%v");
    duract = checkbox("duract%v");

    envelope = hslider("envelope%v",0,0,1,0.01) : automRec(_,tempo,envrecord,envloop,0,envact) : vbargraph("envelopeO%v",0,1) : 1-(_) : pow(10) : *(9999) : +(1) : si.smoo;
    envrecord = checkbox("envrecord%v");
    envloop = checkbox("envloop%v");
    envact = checkbox("envact%v");

    dropout = hslider("dropout%v",0,0,1,0.01) : automRec(_,tempo,drorecord,droloop,0,droact) : vbargraph("dropoutO%v",0,1) : pow(2) : si.smoo;
    drorecord = checkbox("drorecord%v");
    droloop = checkbox("droloop%v");
    droact = checkbox("droact%v");

    level = hslider("level%v",0,0,1,0.01) : pow(2) : si.smoo;

    //-------- Table read and envelope control
    //"offset" adjusts the read start point. Sample and hold makes sure that offset value only changes when the read counter has completed a cycle
    offset = readoffset : ba.sAndH((countPlay <=(0)) | (countLoop <=(0)));
    // "count" creates a counter to read through the table: countPlay is Oneshot; countLoop is looped
    countPlay = ba.countup(duration,play) <: _,(duration)-_ : si.interpolate(reverse);
    countLoop = ba.sweep(duration,play) <: _,(duration)-_ : si.interpolate(reverse);
    // Applies offset and read spead to table read. Modulo of total length of recorded audio
    run = (countPlay : *(speed) : +(offset) : +(1)), (countLoop : *(speed) : +(offset) : %(D) : +(1)) : si.interpolate(loop) : int;
    dropoutX = no.lfnoise0(4800) : *(0.5) : +(0.5) : ba.sAndH(countLoop <=(1)) : >(dropout);
    // "run" triggers counters and controls random sample drop out
    readIndex = run : *(play) : *(dropoutX);
    // Grain/sample envelope: raised cosine envelope applied to amplitude
    ampenvelope = _ : *(cos(readIndex : /(duration) : /(speed) : *(ma.PI *(2))) : *(0.5) : +(0.5) : 1-(_) : *(envelope) : (_,1 : min));
  };

  //-----------AUTOMATION_RECORD---------------//
  /* Records incoming  values for automation playback */
  automRec(val,tem,rec,loo,trix,act) = _ <: _,memory : si.interpolate(act)
  with {
    tempo2 = tem : *((ma.SR)/1000) : int; // read value every 1ms
    tableSize = 48000; //

    /* calculate the sample duration */
    I = int(rec);		// convert button "Record" signal from float to integer
    R = (I-I') <= 0;		// Reset capture when button is pressed
    D = (+(I):*(R))~_ : ba.sAndH(1-(rec)) : int;		// Compute capture duration while button is pressed: 0..NNNN0..MMM
    D2 = D-(1) : *(tem) : int;

    /* table write and read */
    recIndex = (+(1) : *(rec))~_ : _/(tempo2) : *(rec) : int; // records audio to tables below
    counter = ba.countup(D2,trix), ba.sweep(D2,loo) : si.interpolate(loo) : /(tempo2) : int; // counter to read from the table

    memory = _ <: rwtable(tableSize,0.0,recIndex,_,counter), _ : si.interpolate(rec) :> si.smoo ; //write and read automation to and from table
  };

  /* automRec global params */
  tempo(v) = hslider("tempo%v",1,0.01,1,0.01); // controls automRec read speed
  trigx(v) = button("trigx%v"); //triggers automation read
