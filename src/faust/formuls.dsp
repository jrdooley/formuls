//----------------------------------------------------------------------------------------//
//------------------------------------------formuls---------------------------------------//
//----------------------------------------------------------------------------------------//
declare names "formuls";
declare author "formuls";
declare copyright "formuls";
declare version "1.0";
declare license "MIT";
declare options "[osc:on]";

import("stdfaust.lib");

//-------------------------------------------MAIN----------------------------------------//
//Signal inputs: 1st input synth voice frequency modulation;  2nd input ADSR trigger; 3rd input envelope follower;
process = _,_,_ : ro.cross(3) : envelopefollower *(formuls(tempo,trigx)) : saturation(tempo,trigx) : bitcrush(tempo,trigx) : pitchshift(tempo,trigx) : delay(tempo,trigx) : resfilter(tempo,trigx) : am(tempo,trigx) : masteramplitude <: stereopanner(tempo,trigx);

//------------------------------------------FUNCTIONS------------------------------------//

//----------------------------------------------------------------------------------------//
//-------------------------------MASTER_AMPLITUDE,PANNING_&_SIDECHAIN---------------------//
//----------------------------------------------------------------------------------------//
//------MASTER_AMPLITUDE----//
masteramplitude =  *(masteramp)
with{
  masteramp = hslider("masteramp",0,0,1,0.01) : vbargraph("masterampO",0,1): si.smoo;
};
//------PANNING----//
stereopanner(tempo,trigx) = *(1-panning), *(panning)
with{
    panning = hslider("panning",0.5,0,1,0.01) : automRec(_,tempo,precord,ploop,trigx,pact) : chaos(pchaos) : vbargraph("panningO",0,3) : si.smoo;
    precord = checkbox("precord"); // activates automRec recording
    ploop = checkbox("ploop"); // activates automRec read looping
    pact = checkbox("pact"); // switches automRec on and off
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
//-------------------------------------SYNTH_FUNCTIONS------------------------------------//
//----------------------------------------------------------------------------------------//
//------FORMULS------//
/* main synth function */
// Two audio signals in: 1st is sent to ADSR to trigger it; 2nd receives from another synth and controls FM modulation
formuls(tempo,trigx,sigtrig) = _ *(extfeed) : si.smooth(0.99) <: par(i,16,(_ : (Oscillator((ox(i,c,t)),orx(i,c,t),otx(i,c,t),o2fx(i,c,t),o2dx(i,c,t),nx,wx(i,c,t),tx(i,c,c2))) <: *(ADSR(ax(i,c,t),dx(i,c,t),sx(i,c,t),rx(i,c,t),tx2(i,c,c2),adsrsel(i,c,t))) : *(vx(i,c,t,c2)))) :> _
with{
  //-----------SEQUENCER------------//
  seqwrite = hslider("seqwrite",0,0,64,1) : *(2) : int;
  seqval = hslider("seqvalue",0,0,1,1) : int;
  seqread = hslider("seqread",130,0,130,1) : int;
  seqreadout = hslider("seqreadout",0,0,64,1) : *(2) : int;
  randomadd = hslider("randomadd",0,0,1,0.01) : *(2) : -(1);

  // random truth test: r = chance that random will produce "1", b = trigger the random calculation
  random(r,b) = no.lfnoise0(32) : <(r) : ba.sAndH((b : ba.impulsify)) : *(b);
  seqrandadd = (seqread : %(2)) == (0);

  //write and read to and from table 64 is always at 0, 65 is the dead zone that "seqwrite" always defaults to.
  seq1 = rwtable(130,0.0,seqwrite,seqval,seqread);
  seq2 = rwtable(130,0.0,seqwrite,seqval,seqreadout);
  seqtrig = seq1, (seq2 : vbargraph("seqvalO",0,1)) :> _ : +(random(randomadd,seqrandadd)) : min(1);

  //--------EUCLID---------//
  euclidupdate = _ : ba.sAndH(seqread : %(2) : ==(0) : ba.impulsify);
  euclidon = checkbox("euclidon");
  eucbeats = hslider("euclidbeats",0,0,64,1) : euclidupdate : int;
  eucoffset = hslider("euclidoffset",0,0,64,1) : *(2) : euclidupdate : int;
  eucbar = hslider("euclidbar",64,0,64,1) : *(2) : euclidupdate : int;

  euclid = seqread : +(eucoffset) : *(eucbeats) : %(eucbar) : <(eucbeats) : ba.impulsify;
  //-----------COUNTER/VOICE_SELECT------------//
  /* i = index/instance, c = current counter index, c2 = next counter index, t = trigger */
  /* Oscillator controls */
  adsron = checkbox("adsron") : int; // toggle between polyphonic adsr and monophone sustained oscillator tone.
  adsrsel(i,x,y) = adsron : ba.sAndH((i==x) & (y == 1)); // this allows adsr selection for each voice. It is embedded in the ADSR function: ADSR(a,d,s,r,t,adsrSel(i,x,y))

  thresh = hslider("threshold",1,0,1,0.01); // sets ADSR trigger threshold for incomgin signal
  st = sigtrig : an.amp_follower_ud(0.005,0.01) : >(thresh) : ba.impulsify : int;
  t = button("triggerx") : +(st) : +((seqtrig,euclid : si.interpolate(euclidon))) : min(1) : int; // triggers synth voice

  //Counter to select voice
  monophonic(tri) = checkbox("monophonic") : vbargraph("monophonicO",0,3) : ba.sAndH(tri) : int;
  counterMod = 16,2 :> si.interpolate(monophonic(t));
  tx(i,x,y) = int((i==x) | (i!=y)); // oscillator trigger
  tx2(i,x,y) = int((i==x) | (i!=y)) : @(4800); //ADSR trigger, delay by 4800 samps to ensure oscillator values are set before sounding.
  c = +(adsron)~(ba.sAndH(t : ba.impulsify)) : %(counterMod) : int; // Incremental counter for voice select.
  c2 = c +(1) : %(counterMod) : int; // Used to turn off next voice in preparation for triggering.

  //----SYNTH_PARAMETERS----//
  //------OSCILLATOR-------//
  /* FM Oscillator: sine -> triangle -> sawtooth -> square. */
  Oscillator(o1f,o1sr,o1st,o2f,o2d,na,wa,ta) = freqset : fmosc
    with{
    cf = o1f : +(o1f : *((en.ar(0.001,o1st,ta) : pow(4)) : *(o1sr))); //calculate op1 freq during freq slide
    freqcontrol =  cf : +(na) : +(os.oscsin(cf*o2f) : *(cf) : *(o2d)); //where the freq modulation happens
    freqset = _ : *(freqcontrol) : +(freqcontrol);
    clip(x) = x : min(1,_) : max(0,_);
    fmosc = _ <: (((os.oscsin, (os.lf_triangle *(2) : -(1)) : si.interpolate(clip(wa))), (os.lf_sawpos *(2) : -(1)) : si.interpolate(clip(wa-(1)))), os.square : si.interpolate(clip(wa-(2))));
  };

  extfeed = hslider("extfeed",0,0,1,0.01) : automRec(_,tempo,extrecord,extloop,trigx,extact)  : chaos(extchaos) : vbargraph("extfeedO",0,1) : si.smoo; // amount of noise to add to carrier frequency
  extrecord = checkbox("extrecord");
  extloop = checkbox("extloop");
  extact = checkbox("extact");
  extchaos = hslider("extchaos",0,0,1,0.01);

  waveshape = hslider("waveshape",0,0,1,0.01) : automRec(_,tempo,wrecord,wloop,trigx,wact) : chaos(wchaos) : vbargraph("waveshapeO",0,1) : *(3); // choose between sine and sawtooth
  wrecord = checkbox("wrecord"); // activates automRec recording
  wloop = checkbox("wloop"); // activates automRec read looping
  wact = checkbox("wact"); // switches automRec on and off
  wx(i,x,y) = waveshape : ba.sAndH((i==x) & (i!=y)); // function to decide whether message should be sent to a specific voice (see i,c,c2 & t definitions above)
  wchaos = hslider("wchaos",0,0,1,0.01) : pow(3);

  //--------SCALE_QUANTISATION------//
  scalewrite = hslider("scalewrite",0,0,11,1) : int;
  scalevalue = hslider("scalevalue",0,0,12,0.01);
  scalelength = hslider("scalelength",12,1,12,1) : int;
  scale = _ : rwtable(12,0.0,scalewrite,scalevalue,_);
  scalequantise = _ <: (/(scalelength) : int : *(12)),(_ : int : %(scalelength) : scale) : +;

  //------GENERATIVE--------//
  // +(x) steps, -(x) steps
  // step size
  // repeat before note change
  gensteps = hslider("gensteps",0,0,1,0.01) : *(4) : *(scalelength) : ba.sAndH(gentrig) : +(1) : vbargraph("genstepsO",1,100) int;
  genstepsize = hslider("genstepsize",1,1,12,0.1) : min(gensteps) : *(gendirection) : int;
  gendirection = hslider("gendirection",1,-1,1,2) : vbargraph("gendirectionO",-1,1) int;
  genrepeatin = hslider("genrepeat",1,1,8,1) : int;
  genrepeat = ((((ba.sAndH(gentrig))~+(1) : %(genrepeatin)) == 0) : ba.impulsify), gentrig : si.interpolate(genrepeatin < 2) : int;
  gentrig = t : ba.impulsify : int;
  genreset = checkbox("genreset") : int;
  generative = (_ *(genreset) : ba.sAndH((genrepeat) | (genreset == 0 : ba.impulsify)))~+(genstepsize) : %(gensteps);

  op1freq = hslider("op1freq",0.5,0,1,0.001) : automRec(_,tempo,op1frecord,op1floop,trigx,op1fact) : chaos(op1fchaos) : vbargraph("op1freqO",0,1); // carrier frequency
  op1frecord = checkbox("op1frecord");
  op1floop = checkbox("op1floop");
  op1fact = checkbox("op1fact");
  op1fchaos = hslider("op1fchaos",0,0,1,0.01) : pow(2.7);
  ox(i,x,y) = op1freq : *(scalelength) : *(10) : +(generative) : scalequantise : ba.midikey2hz : ba.sAndH((i==x) & (y==1)) : vbargraph("op1pitchO",0,22050);

  op1sliderange = hslider("op1sliderange",0,0,1,0.001) : automRec(_,tempo,op1srrecord,op1srloop,trigx,op1sract) : vbargraph("op1sliderangeO",0,1); //upper frequency for carrier to "slide" down from
  op1srrecord = checkbox("op1srrecord");
  op1srloop = checkbox("op1srloop");
  op1sract = checkbox("op1sract");
  orx(i,x,y) = op1sliderange : ba.sAndH((i==x) & (y == 1));

  op1slidetime = hslider("op1slidetime",0,0,1,0.001) : automRec(_,tempo,op1strecord,op1stloop,trigx,op1stact) : vbargraph("op1slidetimeO",0,1) : *(2); // time taken for carrier frequency slide
  op1strecord = checkbox("op1strecord");
  op1stloop = checkbox("op1stloop");
  op1stact = checkbox("op1stact");
  otx(i,x,y) = op1slidetime : ba.sAndH((i==x) & (y == 1));

  op2freq = hslider("op2freq",0,0,1,0.001) : automRec(_,tempo,op2frecord,op2floop,trigx,op2fact)  : chaos(op2fchaos) : vbargraph("op2freqO",0,1) : *(1.999) : +(0.001); // mod freq is relative to op1 freq
  op2frecord = checkbox("op2frecord");
  op2floop = checkbox("op2floop");
  op2fact = checkbox("op2fact");
  op2fchaos = hslider("op2fchaos",0,0,1,0.01) : pow(2.7);
  o2fx(i,x,y) = op2freq : ba.sAndH((i==x) & (i!=y));

  op2depth = hslider("op2depth",0,0,1,0.001) : automRec(_,tempo,op2drecord,op2dloop,trigx,op2dact) : chaos(op2dchaos) : vbargraph("op2depthO",0,1) : *(2); // mod depth is relative to op1 freq
  o2out = op2depth;
  op2drecord = checkbox("op2drecord");
  op2dloop = checkbox("op2dloop");
  op2dact = checkbox("op2dact");
  op2dchaos = hslider("op2dchaos",0,0,1,0.01) : pow(2.7);
  o2dx(i,x,y) = op2depth : ba.sAndH((i==x) & (i!=y));

  noise = hslider("noise",0,0,1,1) : automRec(_,tempo,nrecord,nloop,trigx,nact)  : chaos(nchaos) : vbargraph("noiseO",0,1) : pow(4.2) : *(22050) : *(no.noise) : fi.lowpass(1,noisefilterfreq); // amount of noise to add to carrier frequency
  nrecord = checkbox("nrecord");
  nloop = checkbox("nloop");
  nact = checkbox("nact");
  nchaos = hslider("nchaos",0,0,1,0.01) : pow(2.7);
  nx = noise;

  noisefilterfreq = hslider("noisefilterfreq",0,0,1,0.001) : automRec(_,tempo,nfrecord,nfloop,trigx,nfact) : chaos(nfchaos) : vbargraph("noisefilterfreqO",0,1) : pow(4.2) : *(0.999) : +(0.001) : *(22050); // noise filter amount
  nfrecord = checkbox("nfrecord");
  nfloop = checkbox("nfloop");
  nfact = checkbox("nfact");
  nfchaos = hslider("nfchaos",0,0,1,0.01) : pow(2.7);

  //---------ADSR----------//
  /* ADSR envelope for FMSynth module */
  ADSR(a,d,s,r,t,ON) = _,(en.dx7envelope(a,d,r,1,1,s,0,0,t) : pow(4)) : si.interpolate(ON);
  /* ADSR controls */
  attack = hslider("attack",0,0,1,0.001) : automRec(_,tempo,arecord,aloop,trigx,aact)  : chaos(achaos) : vbargraph("attackO",0,1) : pow(3.1) : *(0.999) : +(0.001) : *(10);
  arecord = checkbox("arecord");
  aloop = checkbox("aloop");
  aact = checkbox("aact");
  achaos = hslider("achaos",0,0,1,0.01);
  ax(i,x,y) = attack : ba.sAndH((i==x) & (y == 1));

  decay = hslider("decay",0,0,1,0.001) : automRec(_,tempo,drecord,dloop,trigx,dact)  : chaos(dchaos) : vbargraph("decayO",0,1) : pow(3.1) : *(0.999) : +(0.001) : *(10);
  drecord = checkbox("drecord");
  dloop = checkbox("dloop");
  dact = checkbox("dact");
  dchaos = hslider("dchaos",0,0,1,0.01);
  dx(i,x,y) = decay : ba.sAndH((i==x) & (y == 1));

  sustain = hslider("sustain",1,0,1,0.01) : automRec(_,tempo,srecord,sloop,trigx,sact)  : chaos(schaos) : vbargraph("sustainO",0,1) : pow(2);
  srecord = checkbox("srecord");
  sloop = checkbox("sloop");
  sact = checkbox("sact");
  schaos = hslider("schaos",0,0,1,0.01);
  sx(i,x,y) = sustain : ba.sAndH((i==x) & (y == 1));

  release = hslider("release",0,0,1,0.001) : automRec(_,tempo,rrecord,rloop,trigx,ract)  : chaos(rchaos) : vbargraph("releaseO",0,1) : pow(3.1) : *(0.999) : +(0.001) : *(10);
  rrecord = checkbox("rrecord");
  rloop = checkbox("rloop");
  ract = checkbox("ract");
  rchaos = hslider("rchaos",0,0,1,0.01);
  rx(i,x,y) = release : ba.sAndH((i==x) & (y == 1));

  /* voice velocity/amplitude control. If 'c2', then voice is quietened to prepare for next trigger */
  voiceamp = hslider("voiceamp",0,0,1,0.01) : chaos(vchaos) : pow(2.7);
  vchaos = hslider("vchaos",0,0,1,0.01);
  vx(i,x,y,z) = voiceamp : ba.sAndH((i==x) & (y==1)) : *(i!=z) : si.smoo;
};

//----------------------------------------------------------------------------------------//
//---------------------------------------FX_FUNCTIONS-------------------------------------//
//----------------------------------------------------------------------------------------//
//------AMPLITUDE_MODULATION-----//
am(tempo,trigx) = _ : *(1-(amdepth *(amfreq : amosc)))
with{
  amwave = hslider("amwave",0,0,1,0.001) : automRec(_,tempo,amwrecord,amwloop,0,amwact) : chaos(amwchaos) : vbargraph("amwreqO",0,1) : *(3);
  amwrecord = checkbox("amwrecord");
  amwloop = checkbox("amwloop");
  amwact = checkbox("amwact");
  amwchaos = hslider("amwchaos",0,0,1,0.01) : pow(3);

  amfreq = hslider("amfreq",0,0,1,0.001) : automRec(_,tempo,amfrecord,amfloop,0,amfact) : chaos(amfchaos) : vbargraph("amfreqO",0,1) : pow(3.1) : *(99.9) : +(0.1) : si.smoo;
  amfrecord = checkbox("amfrecord");
  amfloop = checkbox("amfloop");
  amfact = checkbox("amfact");
  amfchaos = hslider("amfchaos",0,0,1,0.01) : pow(3);

  amdepth = hslider("amdepth",0,0,1,0.01) : automRec(_,tempo,amdrecord,amdloop,0,amdact) : chaos(amdchaos) : vbargraph("amdepthO",0,1) : pow(3.1) : si.smoo;
  amdrecord = checkbox("amdrecord");
  amdloop = checkbox("amdloop");
  amdact = checkbox("amdact");
  amdchaos = hslider("amdchaos",0,0,1,0.01);

  clip(x) = x : min(1,_) : max(0,_);
  amosc = _ <: (((((os.oscsin : *(0.5) : +(0.5)), os.lf_triangle : si.interpolate(clip(amwave))), os.lf_sawpos : si.interpolate(clip(amwave-(1)))), os.lf_squarewave : si.interpolate(clip(amwave-2))));
};
//-----------SATURATION---------------//
/* tan waveshape distortion */
saturation(tempo,trigx) = _ <: _, (*(1.57) : ma.tanh) : si.interpolate(saturationamount)
with{
  saturationamount = hslider("saturation",0,0,1,0.01) : automRec(_,tempo,satrecord,satloop,0,satact) : chaos(satchaos) : vbargraph("saturationO",0,1) : si.smoo;
  satrecord = checkbox("satrecord");
  satloop = checkbox("satloop");
  satact = checkbox("satact");
  satchaos = hslider("satchaos",0,0,1,0.01);
};
//-----------RESONANT_FILTER---------------//
resfilter(tempo,trigx) = _ : fi.resonlp(fc,res,1)
with {
  fc = hslider("filtercutoff",1,0,1,0.001) : automRec(_,tempo,fcrecord,fcloop,0,fcact) : chaos(fcchaos) : vbargraph("filtercutoffO",0,1) : pow(3.1) : *(22030) : +(20) : si.smoo;
  fcrecord = checkbox("fcrecord");
  fcloop = checkbox("fcloop");
  fcact = checkbox("fcact");
  fcchaos = hslider("fcchaos",0,0,1,0.01) : pow(2.7);

  res = hslider("filterresonance",0,0,1,0.001) : automRec(_,tempo,frrecord,frloop,0,fract) : chaos(frchaos) : vbargraph("filterresonanceO",0,1) : *(99) : +(1) : si.smoo;
  frrecord = checkbox("frrecord");
  frloop = checkbox("frloop");
  fract = checkbox("fract");
  frchaos = hslider("frchaos",0,0,1,0.01) : pow(2.7);
};
//-----------BITCRUSHER---------------//
bitcrush(tempo,trigx) = _ : ba.downSample(bit)
with {
  bit = hslider("bitcrush",0,0,1,0.01) : automRec(_,tempo,bitrecord,bitloop,0,bitact) : chaos(bitchaos) : vbargraph("bitcrushO",0,1) : 1-(_) : pow(3) : *(47950) : +(50) : si.smoo;
  bitrecord = checkbox("bitrecord");
  bitloop = checkbox("bitloop");
  bitact = checkbox("bitact");
  bitchaos = hslider("bitchaos",0,0,1,0.01);
};
//------------PITCHSHIFT------------//
pitchshift(tempo,trigx) = _ : ef.transpose(100,50,pshift)
with {
  pshift = hslider("pitchshift",0.5,0,1,0.1) : automRec(_,tempo,pitrecord,pitloop,0,pitact) : chaos(pitchaos) : vbargraph("pitchshiftO",0,1) : *(48) : -(24) : si.smoo;
  pitrecord = checkbox("pitrecord");
  pitloop = checkbox("pitloop");
  pitact = checkbox("pitact");
  pitchaos = hslider("pitchaos",0,0,1,0.01) : pow(2.7);
};

delay(tempo,trigx) = _ <:_,(*(delsend) : ef.echo(1,deltime,feedback)) :> _
with {
  delsend = hslider("delsend",0,0,1,0.01) : automRec(_,tempo,dsrecord,dsloop,0,dsact) : chaos(dschaos) : vbargraph("delsendO",0,1) : si.smoo;
  dsrecord = checkbox("dsrecord");
  dsloop = checkbox("dsloop");
  dsact = checkbox("dsact");
  dschaos = hslider("dschaos",0,0,1,0.01);


  deltime = hslider("deltime",0,0,1,0.01) : automRec(_,tempo,dtrecord,dtloop,0,dtact) : chaos(dtchaos) : vbargraph("deltimeO",0,1) : si.smoo;
  dtrecord = checkbox("dtrecord");
  dtloop = checkbox("dtloop");
  dtact = checkbox("dtact");
  dtchaos = hslider("dtchaos",0,0,1,0.01);

  feedback = hslider("delfeedback",0,0,1,0.01) : automRec(_,tempo,dfrecord,dfloop,0,dfact) : chaos(dfchaos) : vbargraph("delfeedbackO",0,1) : si.smoo;
  dfrecord = checkbox("dfrecord");
  dfloop = checkbox("dfloop");
  dfact = checkbox("dfact");
  dfchaos = hslider("dfchaos",0,0,1,0.01);
};

//----------------------------------------------------------------------------------------//
//----------------------------------AUTOMATION-------------------------------------------//
//----------------------------------------------------------------------------------------//
//-----------AUTOMATION_RECORD---------------//
/* Records incoming  values for automation playback */
// val = value to record into automation; tem = tempo/automation read speed (0-1); rec = start/stop recording (0/1); loo = loop automation; trix = trigger one cycle of recorded automation; act = automation active
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
  counter = ba.countup(D2,trix), ba.sweep(D2,loo) : si.interpolate(loo) : /(tempo2) : *(act) : int; // counter to read from the table

  memory = _ <: rwtable(tableSize,0.0,recIndex,_,counter), _ : si.interpolate(rec) :> si.smoo ; //write and read automation to and from table
};

/* automRec global params */
tempo = hslider("tempo",1,0.01,1,0.01); // controls automRec read speed
trigx = button("trigx"); //triggers automation read

//----------------------------------------------------------------------------------------//
//----------------------------------CHAOS-------------------------------------------------//
//----------------------------------------------------------------------------------------//
chaos(value) = _ : +(no.lfnoise0(8) : *(value) : si.smoo) : (_,1 : min) : (_,0 : max);
