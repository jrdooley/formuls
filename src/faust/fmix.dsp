import("stdfaust.lib");

// signal inputs: 1) signal to process 2)sidechain/envelopfollower signal 3)automrec trigger
// signal outputs: 1) left channel signal; 2) right channel signal.
process = _,_,_ : ro.cross(3) : mixprocess(tempo);

mixprocess(tempo,var1) = _,_ : ro.cross(2) : (*(envelopefollower) : channelvolume) : stereopanner(tempo,var1);

//----------------------------------------------------------------------------------------//
//---------------------------------CHANNEL_VOLUME,PANNING_&_SIDECHAIN---------------------//
//----------------------------------------------------------------------------------------//
//------CHANNEL_VOLUME----//
channelvolume =  _ : *(volume)
with{
  volume = hslider("volume",0,0,1,0.01) : vbargraph("volumeO",0,1) : si.smoo;
};
//------PANNING----//
stereopanner(tempo,trigxx) = _ : sp.panner(panning)
with{
    panning = hslider("panning",0.5,0,1,0.01) : automrec(_,tempo,precord,ploop,trigxx,pact) : chaos(pchaos,15) : vbargraph("panningO",0,1) : si.smoo;
    precord = checkbox("precord"); // activates automrec recording
    ploop = checkbox("ploop"); // activates automrec read looping
    pact = checkbox("pact"); // switches automrec on and off
    pchaos = hslider("pchaos",0,0,1,0.01) : pow(4);
};
//------ENVELOPE_FOLLOWER/SIDECHAIN----//
envelopefollower = _ : *(1): an.amp_follower_ar(0.005, enveloperelease) <: env, side : ba.selectn(2,envelopeside) : _
with{
  enveloperelease = hslider("enveloperelease",0,0,1,0.01) : vbargraph("envelopereleaseO",0,1) : *(0.99) : +(0.01) : si.smoo;
  envelopedepth = hslider("envelopedepth",0,0,1,0.01) : vbargraph("envelopedepthO",0,1) : si.smoo;
  envelopeside = checkbox("envelopeside") : vbargraph("envelopesideO",0,1);
  env = *(envelopedepth) : +(1-envelopedepth); // envelope follow algorithm
  side = 1-(_) *(envelopedepth); // sidechain algorithm
};

//----------------------------------------------------------------------------------------//
//----------------------------------AUTOMATION--------------------------------------------//
//----------------------------------------------------------------------------------------//
//-----------AUTOMATION_RECORD---------------//
/* Records incoming  values for automation playback */
// val = value to record into automation; tem = tempo/automation read speed (0-1); rec = start/stop recording (0/1); loo = loop automation; trix = trigger one cycle of recorded automation; act = automation active
automrec(val,tem,rec,loo,trix,act) = _ <: _,memory : ba.selectn(2,act)
with {
  tempo2 = tem : *((ma.SR)/1000) : int; // read value every 1ms
  tableSize = 48000; // with a read periodicity of 1ms, @48kHz SR this creates 48 seconds of memory to record automation to.

  /* calculate the sample duration */
  I = int(rec);		// convert button "Record" signal from float to integer
  R = (I-I') <= 0;		// Reset capture when button is pressed
  D = (+(I):*(R))~_ : ba.sAndH(1-(rec)) : int;		// Compute capture duration while button is pressed: 0..NNNN0..MMM
  D2 = D-(1) : *(tem) : int;

  /* table write and read */
  recindex = (+(1) : *(rec))~_ : _/(tempo2) : *(rec) : int; // records audio to tables below
  counter = ba.countup(D2,trix), ba.sweep(D2,loo) : ba.selectn(2,loo) : /(tempo2) : *(act) : int; // counter to read from the table

  memory = _ <: rwtable(tableSize,0.0,recindex,_,counter), _ : ba.selectn(2,rec) :> si.smoo ; //write and read automation to and from table
  // ADD REVERSE READ FUNCTION
};

/* automrec global params */
tempo = hslider("tempo",1,0.01,1,0.01); // controls automrec read speed

//----------------------------------------------------------------------------------------//
//----------------------------------CHAOS_RANDOM------------------------------------------//
//----------------------------------------------------------------------------------------//
// geneates a random value. can be seeded so you have independent randoms.
randomnoise(freq,seed) = randomx / (RANDMAX) : ba.latch(os.oscrs(freq))
with{
	mask = 4294967295; // 2^32-1
	randomx = +(10000 +(seed)) ~ *(1103515245) & mask; // "linear congruential"
	RANDMAX	= 2147483647.0; // = 2^31-1 = MAX_SIGNED_INT in 32 bits
};
//chaos: generates a continual stream of random values between 0 and 1 and adds it to the incoming signal
chaos(value,seed) = _ : +(randomnoise(8,seed) : *(value) : si.smoo) : min(1,_) : max(0,_) : _;
