import("stdfaust.lib");

// signal inputs: 1) signal to process 2)automrec trigger
//Signal outputs: 1) output of fx sent to fmix;
process = _,_ : ro.cross(2) : fxprocess(tempo);

fxprocess(tempo,t) = saturation(tempo,t) : bitcrush(tempo,t) : pitchshift(tempo,t) : delay(tempo,t) : am(tempo,t) : resfilter(tempo,t) : gate;

//----------------------------------------------------------------------------------------//
//---------------------------------------FX_FUNCTIONS-------------------------------------//
//----------------------------------------------------------------------------------------//
//------AMPLITUDE_MODULATION-----//
am(tempo,trigx) = _ : *(1-(amdepth *(amfreq : amosc)))
with{
  amwave = hslider("amwave",0,0,1,0.001) : automrec(_,tempo,amwrecord,amwloop,trigx,amwact) : chaos(amwchaos,16) : vbargraph("amwaveO",0,1) : *(3);
  amwrecord = checkbox("amwrecord");
  amwloop = checkbox("amwloop");
  amwact = checkbox("amwact");
  amwchaos = hslider("amwchaos",0,0,1,0.01) : pow(3);

  amfreq = hslider("amfreq",0,0,1,0.001) : automrec(_,tempo,amfrecord,amfloop,trigx,amfact) : chaos(amfchaos,17) : vbargraph("amfreqO",0,1) : pow(3.1) : *(99.9) : +(0.1) : vbargraph("amfreqhzO",0.1,100) : si.smoo;
  amfrecord = checkbox("amfrecord");
  amfloop = checkbox("amfloop");
  amfact = checkbox("amfact");
  amfchaos = hslider("amfchaos",0,0,1,0.01) : pow(3);

  amdepth = hslider("amdepth",0,0,1,0.01) : automrec(_,tempo,amdrecord,amdloop,trigx,amdact) : chaos(amdchaos,18) : vbargraph("amdepthO",0,1) : pow(3.1) : si.smoo;
  amdrecord = checkbox("amdrecord");
  amdloop = checkbox("amdloop");
  amdact = checkbox("amdact");
  amdchaos = hslider("amdchaos",0,0,1,0.01);

  clip(x) = x : min(1,_) : max(0,_);
  amosc = _ <: (((((os.oscsin : *(0.5) : +(0.5)), os.lf_trianglepos : si.interpolate(clip(amwave))), os.lf_sawpos : si.interpolate(clip(amwave-(1)))), os.lf_squarewavepos : si.interpolate(clip(amwave-2))));
};
//-----------SATURATION---------------//
/* tanh waveshape distortion */
saturation(tempo,trigx) = _ <: _, (*(1.57) : ma.tanh) : si.interpolate(saturationamount)
with{
  saturationamount = hslider("saturation",0,0,1,0.01) : automrec(_,tempo,satrecord,satloop,trigx,satact) : chaos(satchaos,19) : vbargraph("saturationO",0,1) : si.smoo;
  satrecord = checkbox("satrecord");
  satloop = checkbox("satloop");
  satact = checkbox("satact");
  satchaos = hslider("satchaos",0,0,1,0.01);
};
//-----------RESONANT_FILTER---------------//
resfilter(tempo,trigx) = _ : fi.resonlp(fc,res,1)
with {
  fc = hslider("filtercutoff",1,0,1,0.001) : automrec(_,tempo,fcrecord,fcloop,trigx,fcact) : chaos(fcchaos,20) : vbargraph("filtercutoffO",0,1) : pow(3.1) : *(22030) : +(20) : si.smoo;
  fcrecord = checkbox("fcrecord");
  fcloop = checkbox("fcloop");
  fcact = checkbox("fcact");
  fcchaos = hslider("fcchaos",0,0,1,0.01) : pow(2.7);

  res = hslider("filterresonance",0,0,1,0.001) : automrec(_,tempo,frrecord,frloop,trigx,fract) : chaos(frchaos,21) : vbargraph("filterresonanceO",0,1) : *(99) : +(1) : si.smoo;
  frrecord = checkbox("frrecord");
  frloop = checkbox("frloop");
  fract = checkbox("fract");
  frchaos = hslider("frchaos",0,0,1,0.01) : pow(2.7);
};
//-----------BITCRUSHER---------------//
bitcrush(tempo,trigx) = _ : ba.downSample(bit)
with {
  bit = hslider("bitcrush",0,0,1,0.01) : automrec(_,tempo,bitrecord,bitloop,trigx,bitact) : chaos(bitchaos,22) : vbargraph("bitcrushO",0,1) : 1-(_) : pow(3) : *(47950) : +(50) : si.smoo;
  bitrecord = checkbox("bitrecord");
  bitloop = checkbox("bitloop");
  bitact = checkbox("bitact");
  bitchaos = hslider("bitchaos",0,0,1,0.01);
};
//------------PITCHSHIFT------------//
pitchshift(tempo,trigx) = _ : ef.transpose(100,50,pshift)
with {
  pshift = hslider("pitchshift",0.5,0,1,0.1) : automrec(_,tempo,pitrecord,pitloop,trigx,pitact) : chaos(pitchaos,23) : vbargraph("pitchshiftO",0,1) : *(48) : -(24) : si.smoo;
  pitrecord = checkbox("pitrecord");
  pitloop = checkbox("pitloop");
  pitact = checkbox("pitact");
  pitchaos = hslider("pitchaos",0,0,1,0.01) : pow(2.7);
};
//-------------DELAY---------------//
delay(tempo,trigx) = _ <:_,(*(delsend) : ef.echo(1,deltime,feedback)) :> _
with {
  delsend = hslider("delaysend",0,0,1,0.01) : automrec(_,tempo,dsrecord,dsloop,trigx,dsact) : chaos(dschaos,24) : vbargraph("delaysendO",0,1) : si.smoo;
  dsrecord = checkbox("dsrecord");
  dsloop = checkbox("dsloop");
  dsact = checkbox("dsact");
  dschaos = hslider("dschaos",0,0,1,0.01);

  deltime = hslider("delaytime",0,0,1,0.01) : automrec(_,tempo,dtrecord,dtloop,trigx,dtact) : chaos(dtchaos,25) : vbargraph("delaytimeO",0,1) : si.smoo;
  dtrecord = checkbox("dtrecord");
  dtloop = checkbox("dtloop");
  dtact = checkbox("dtact");
  dtchaos = hslider("dtchaos",0,0,1,0.01);

  feedback = hslider("delayfeedback",0,0,1,0.01) : automrec(_,tempo,dfrecord,dfloop,trigx,dfact) : chaos(dfchaos,26) : vbargraph("delayfeedbackO",0,1) : si.smoo;
  dfrecord = checkbox("dfrecord");
  dfloop = checkbox("dfloop");
  dfact = checkbox("dfact");
  dfchaos = hslider("dfchaos",0,0,1,0.01);
};
//-------------GATE---------------//
gate = _ : ef.gate_mono(thresh,att,hold,rel) : _
with {
  thresh = hslider("gatethresh",0,0,1,0.01) : *(60) : -(60);
  att = hslider("gateattack",0,0,1,0.01) : *(0.25) : +(0.001);
  hold = hslider("gatehold",0,0,1,0.01) : *(0.25) : +(0.001);
  rel = hslider("gaterelease",0,0,1,0.01) : +(0.001);
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
