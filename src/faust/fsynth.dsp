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

//-------------------------------------------MAIN----------------------------------------//
//Signal inputs: 1)input synth voice frequency modulation; 2)input ADSR trigger;
//Signal outputs: 1)output of synth; 2)automrec trigger;

process = _,_ : ro.cross(2) : formuls(tempo);

//------------------------------------------FUNCTIONS------------------------------------//

//----------------------------------------------------------------------------------------//
//-------------------------------------SYNTH----------------------------------------------//
//----------------------------------------------------------------------------------------//
//------FORMULS------//
/* main synth function */
// Two audio signals in: 1st is sent to ADSR to trigger it; 2nd receives from another synth and controls FM modulation
formuls(tempo,sigtrig) = (_ : *(extmod) <: par(i,16,(_ : (Oscillator((ox(i,c,t)),orx(i,c,t),otx(i,c,t),o2fx(i,c,t),o2dx(i,c,t),nx,wx(i,c,t),tx(i,c,c2))) : (ADSR(ax(i,c,t),dx(i,c,t),sx(i,c,t),rx(i,c,t),tx2(i,c,c2),adsrsel(i,c,t))) : *(vx(i,c,t)))) :> _),t
with{
  //-----------SEQUENCER------------//
  seqwrite = hslider("seqwrite",0,0,64,1) : *(2) : int;
  seqval = hslider("seqvalue",0,0,1,1) : int;
  seqread = hslider("seqread",130,0,130,1) : int;
  seqreadout = hslider("seqreadout",0,0,64,1) : *(2) : int;
  seqrandomadd = hslider("seqrandomadd",0,0,1,0.01) : *(2) : -(1); // value lies between -1 and 1.
  seqrandadd = (seqread : %(2)) == (0);

  //write and read to and from table 64 is always at 0, 65 is the dead zone that "seqwrite" always defaults to.
  seq1 = rwtable(130,0.0,seqwrite,seqval,seqread);
  seq2 = rwtable(130,0.0,seqwrite,seqval,seqreadout);
  seqtrig = seq1, (seq2 : vbargraph("seqvalO",0,1)) : * :> _ : +(random(seqrandomadd,seqrandadd,0)) : min(1);

  //--------EUCLID---------//
  euclidupdate = _ : ba.sAndH(seqread : %(2) : ==(0) : ba.impulsify);
  euclidon = checkbox("euclidon") : ba.sAndH(seqread : %(2));
  eucbeats = hslider("euclidbeats",0,0,64,1) : *(2) : euclidupdate : int;
  eucoffset = hslider("euclidoffset",0,0,64,1) : *(2) : euclidupdate : int;
  eucbar = hslider("euclidbar",64,0,64,1) : *(2) : euclidupdate : int;
  euccounter = +(1)~(ba.sAndH(seqread : %(2) : ba.impulsify)) : int;

  // euclid = euccounter : +(eucoffset) : *(eucbeats) : %(eucbar) : <(eucbeats) : ba.impulsify;
  euclid = euccounter : +(eucoffset) : *(eucbeats) : %(eucbar) : <(eucbeats) : ba.impulsify;

  //-----------COUNTER/VOICE_SELECT------------//
  /* i = index/instance, c = current counter index, c2 = next counter index, t = trigger */
  /* Oscillator controls */
  adsron = checkbox("adsron") : int; // toggle between polyphonic adsr and monophone sustained oscillator tone.
  adsrsel(i,x,y) = adsron : ba.sAndH((i==x) & (y == 1)); // this allows adsr selection for each voice. It is embedded in the ADSR function: ADSR(a,d,s,r,t,adsrSel(i,x,y))

  thresh = hslider("threshold",1,0,1,0.01); // sets ADSR trigger threshold for incomgin signal
  st = sigtrig : >(thresh) : ba.impulsify : int;
  t = button("triggerx") : +(st) : +((seqtrig,euclid : ba.selectn(2,euclidon))) : min(1) : int; // triggers synth voice
  //Counter to select voice
  monophonic(tri) = checkbox("monophonic") : vbargraph("monophonicO",0,1) : ba.sAndH(tri) : int;
  counterMod = 16,2 :> ba.selectn(2,monophonic(t));
  tx(i,x,y) = int((i==x) | (i!=y)); // oscillator trigger
  tx2(i,x,y) = int((i==x) | (i!=y)) : @(4800); //ADSR trigger, delay by 4800 samps to ensure oscillator values are set before sounding.
  c = +(adsron)~(ba.sAndH(t : ba.impulsify)) : %(counterMod) : vbargraph("voicenumberO",0,16) : int; // Incremental counter for voice select.
  c2 = c +(1) : %(counterMod) : int; // Used to turn off next voice in preparation for triggering.

  //--------SCALE_QUANTISATION------//
  // writes scale values to a table, then quantises incoming op1freq values to that scale.
  scalewrite = hslider("scalewrite",0,0,11,1) : int;
  scalevalue = hslider("scalevalue",0,0,12,0.01);
  scalelength = hslider("scalelength",12,1,12,1) : int;
  scale = _ : rwtable(12,0.0,scalewrite,scalevalue,_);
  scalequantise = _ <: (/(scalelength) : int : *(12)),(_ : int : %(scalelength) : scale) : +;

  //------GENERATIVE--------//
  // generates random pitches with existing "scale quantisation"
  genrange = hslider("genrange",0,0,1,0.01) : chaos(grchaos,0) : vbargraph("genrangeO",0,1) : *(4) : *(scalelength) : ba.sAndH(gentrig) : +(1) : int;
  grchaos = hslider("grchaos",0,0,1,0.01) : pow(3);
  genstepsize = hslider("genstepsize",1,1,12,0.1) : min(genrange) : *(gendirection) : int;
  gendirection = hslider("gendirection",1,-1,1,2) : vbargraph("gendirectionO",-1,1) : int;
  genrepeatin = hslider("genrepeat",1,1,8,1) : int;
  genrepeat = ((((ba.sAndH(gentrig))~+(1) : %(genrepeatin)) == 0) : ba.impulsify), gentrig : ba.selectn(2,genrepeatin <(2)) : int;
  gentrig = t : ba.impulsify : int;
  genon = checkbox("genon") : int;
  generative = (_ *(genon) : ba.sAndH((genrepeat) | (genon == 0 : ba.impulsify)))~+(genstepsize) : %(genrange);

  //----SYNTH_PARAMETERS----//
  //------OSCILLATOR-------//
  /* FM Oscillator: sine -> triangle -> sawtooth -> square. */
  // o1f = op1 freq, o1sr = op1 slide range, o1st = op1 slide time,o2f = op2 freq, o2d = op2 depth, na = noise, wa = waveshape, ta = trigger
  Oscillator(o1f,o1sr,o1st,o2f,o2d,na,wa,ta) = freqset : fmosc
    with{
    cf = o1f : +(o1f : *((en.ar(0.001,o1st,ta) : pow(2.7)) : *(o1sr))) : min(19000); //calculate op1 freq during freq slide
    freqcontrol =  cf : +(na) : +(os.oscsin(cf*o2f) : *(cf) : *(o2d)); //where the freq modulation happens
    freqset = _ : *(freqcontrol) : +(freqcontrol);
    clip(x) = x : min(1,_) : max(0,_);
    oscphase = ta == (0);
    wa1 = wa : si.smoo;
    fmosc = _ <: (((os.hs_oscsin(_,oscphase)), (os.lf_triangle) : si.interpolate(clip(wa1))), (os.lf_saw) : si.interpolate(clip(wa1-(1)))), os.lf_squarewave : si.interpolate(clip(wa1-(2)));
  };

  // extmod: external input modulates frequency of Oscillator.
  extmod = hslider("extmodulation",0,0,1,0.01) : automrec(_,tempo,extrecord,extloop,t,extact)  : chaos(extchaos,1) : vbargraph("extmodulationO",0,1) : si.smoo; // amount of noise to add to carrier frequency
  extrecord = checkbox("extrecord");
  extloop = checkbox("extloop");
  extact = checkbox("extact");
  extchaos = hslider("extchaos",0,0,1,0.01);

  // waveshape: sine -> triangle -> rising sawtooth -> squarewave
  waveshape = hslider("waveshape",0,0,1,0.01) : automrec(_,tempo,wrecord,wloop,t,wact) : chaos(wchaos,2) : vbargraph("waveshapeO",0,1) : *(3); // choose between sine and sawtooth
  wrecord = checkbox("wrecord"); // activates automrec recording
  wloop = checkbox("wloop"); // activates automrec read looping
  wact = checkbox("wact"); // switches automrec on and off
  wchaos = hslider("wchaos",0,0,1,0.01) : pow(3);
  wx(i,x,y) = waveshape : ba.sAndH((i==x) & (i!=y)); // function to decide whether message should be sent to a specific voice (see i,c,c2 & t definitions above)

  op1freq = hslider("op1freq",0.5,0,1,0.001) : automrec(_,tempo,op1frecord,op1floop,t,op1fact) : chaos(op1fchaos,3) : vbargraph("op1freqO",0,1); // carrier frequency
  op1frecord = checkbox("op1frecord");
  op1floop = checkbox("op1floop");
  op1fact = checkbox("op1fact");
  op1fchaos = hslider("op1fchaos",0,0,1,0.01) : pow(2.7);
  ox(i,x,y) = op1freq : *(scalelength) : *(10) : +(generative) : scalequantise : ba.midikey2hz : ba.sAndH((i==x) & (y==1) : ba.impulsify) : vbargraph("op1pitchO%i",0,22050);

  op1sliderange = hslider("op1sliderange",0,0,1,0.001) : automrec(_,tempo,op1srrecord,op1srloop,t,op1sract) : chaos(op1srchaos,4) : vbargraph("op1sliderangeO",0,1) : *(5); //upper frequency for carrier to "slide" down from
  op1srrecord = checkbox("op1srrecord");
  op1srloop = checkbox("op1srloop");
  op1sract = checkbox("op1sract");
  op1srchaos = hslider("op1srchaos",0,0,1,0.01);
  orx(i,x,y) = op1sliderange : ba.sAndH((i==x) & (y == 1) : ba.impulsify);

  op1slidetime = hslider("op1slidetime",0,0,1,0.001) : automrec(_,tempo,op1strecord,op1stloop,t,op1stact) : chaos(op1stchaos,5) : vbargraph("op1slidetimeO",0,1) : pow(1.7) : *(2); // time taken for carrier frequency slide
  op1strecord = checkbox("op1strecord");
  op1stloop = checkbox("op1stloop");
  op1stact = checkbox("op1stact");
  op1stchaos = hslider("op1stchaos",0,0,1,0.01);
  otx(i,x,y) = op1slidetime : ba.sAndH((i==x) & (y == 1) : ba.impulsify);

  op2freq = hslider("op2freq",0,0,1,0.001) : automrec(_,tempo,op2frecord,op2floop,t,op2fact)  : chaos(op2fchaos,6) : vbargraph("op2freqO",0,1) : *(1.999) : +(0.001); // mod freq is relative to op1 freq
  op2frecord = checkbox("op2frecord");
  op2floop = checkbox("op2floop");
  op2fact = checkbox("op2fact");
  op2fchaos = hslider("op2fchaos",0,0,1,0.01) : pow(2.7);
  o2fx(i,x,y) = op2freq : ba.sAndH((i==x) & (i!=y));

  op2depth = hslider("op2depth",0,0,1,0.001) : automrec(_,tempo,op2drecord,op2dloop,t,op2dact) : chaos(op2dchaos,7) : vbargraph("op2depthO",0,1) : *(2);// mod depth is relative to op1 freq
  o2out = op2depth;
  op2drecord = checkbox("op2drecord");
  op2dloop = checkbox("op2dloop");
  op2dact = checkbox("op2dact");
  op2dchaos = hslider("op2dchaos",0,0,1,0.01) : pow(2.7);
  o2dx(i,x,y) = op2depth : ba.sAndH((i==x) & (i!=y));

  noise = hslider("noise",0,0,1,1) : automrec(_,tempo,nrecord,nloop,t,nact)  : chaos(nchaos,8) : vbargraph("noiseO",0,1) : pow(4.2) : *(22050) : *(no.noise) : fi.lowpass(1,noisefilterfreq); // amount of noise to add to carrier frequency
  nrecord = checkbox("nrecord");
  nloop = checkbox("nloop");
  nact = checkbox("nact");
  nchaos = hslider("nchaos",0,0,1,0.01) : pow(2.7);
  nx = noise;

  noisefilterfreq = hslider("noisefilterfreq",0,0,1,0.001) : automrec(_,tempo,nfrecord,nfloop,t,nfact) : chaos(nfchaos,9) : vbargraph("noisefilterfreqO",0,1) : pow(4.2) : *(0.999) : +(0.001) : *(22050); // noise filter amount
  nfrecord = checkbox("nfrecord");
  nfloop = checkbox("nfloop");
  nfact = checkbox("nfact");
  nfchaos = hslider("nfchaos",0,0,1,0.01) : pow(2.7);

  //---------ADSR----------//
  /* ADSR envelope for FMSynth module */
  ADSR(a,d,s,r,t,ON) = _ <: _, *(en.dx7envelope(a,d,r,1,1,s,0,0,t) : pow(4)) : ba.selectn(2,ON);
  /* ADSR controls */
  attack = hslider("attack",0,0,1,0.001) : automrec(_,tempo,arecord,aloop,t,aact)  : chaos(achaos,10) : vbargraph("attackO",0,1) : pow(3.1) : *(0.999) : +(0.001) : *(10);
  arecord = checkbox("arecord");
  aloop = checkbox("aloop");
  aact = checkbox("aact");
  achaos = hslider("achaos",0,0,1,0.01);
  ax(i,x,y) = attack : ba.sAndH((i==x) & (y == 1) : ba.impulsify);

  decay = hslider("decay",0,0,1,0.001) : automrec(_,tempo,drecord,dloop,t,dact)  : chaos(dchaos,11) : vbargraph("decayO",0,1) : pow(3.1) : *(0.999) : +(0.001) : *(10);
  drecord = checkbox("drecord");
  dloop = checkbox("dloop");
  dact = checkbox("dact");
  dchaos = hslider("dchaos",0,0,1,0.01);
  dx(i,x,y) = decay : ba.sAndH((i==x) & (y == 1) : ba.impulsify);

  sustain = hslider("sustain",1,0,1,0.01) : automrec(_,tempo,srecord,sloop,t,sact)  : chaos(schaos,12) : vbargraph("sustainO",0,1) : pow(1.7);
  srecord = checkbox("srecord");
  sloop = checkbox("sloop");
  sact = checkbox("sact");
  schaos = hslider("schaos",0,0,1,0.01);
  sx(i,x,y) = sustain : ba.sAndH((i==x) & (y == 1) : ba.impulsify);

  release = hslider("release",0,0,1,0.001) : automrec(_,tempo,rrecord,rloop,t,ract)  : chaos(rchaos,13) : vbargraph("releaseO",0,1) : pow(3.1) : *(0.999) : +(0.001) : *(10);
  rrecord = checkbox("rrecord");
  rloop = checkbox("rloop");
  ract = checkbox("ract");
  rchaos = hslider("rchaos",0,0,1,0.01);
  rx(i,x,y) = release : ba.sAndH((i==x) & (y == 1) : ba.impulsify);

  /* voice velocity/amplitude control. If 'c2', then voice is quietened to prepare for next trigger */
  velocity = hslider("velocity",1,0,1,0.01) : chaos(vchaos,14) : pow(2.7);
  vchaos = hslider("vchaos",0,0,1,0.01);
  vx(i,x,y) = velocity : si.smoothAndH((i==x) & (y==1), 0.999);
};

//----------------------------------------------------------------------------------------//
//----------------------------------AUTOMATION-------------------------------------------//
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

// random : random truth test: r = chance that random will produce "1", b = trigger the random calculation
random(chance,trigger,seed) = randomnoise(32,seed) : <(chance) : ba.sAndH((trigger : ba.impulsify)) : *(trigger);
