import("stdfaust.lib");
fo = library("formuls.lib");

// signal inputs: 1) signal to process 2)fo.automrec trigger
// Signal outputs: 1) output of fx sent to fmix;
process = _,_ : ro.cross(2) : fxprocess(tempo);

// fxprocess(tempo,t) = saturation(tempo,t) : bitcrush(tempo,t) : pitchshift(tempo,t) : delay(tempo,t) : am(tempo,t) : resfilter(tempo,t) : gate(tempo,t);
fxprocess(tempo,t) = _ <: _,(saturation(tempo,t) : bitcrush(tempo,t) : pitchshift(tempo,t) : delay(tempo,t) : am(tempo,t) : resfilter(tempo,t) : gate(tempo,t)) : si.interpolate(fxsend(tempo,t));

tempo = hslider("tempo",1,0.01,1,0.01); // controls fo.automrec read speed

fxsend(tempo,trigx) = fxsend2
with {
  fxsend2 = hslider("fxsend",1,0,1,0.01) : fo.automrec(_,tempo,fxsrecord,fxsloop,trigx,fxsact) : fo.chaos(fxschaos,33) : vbargraph("fxsendO",0,1);
  fxsrecord = checkbox("fxsrecord");
  fxsloop = checkbox("fxsloop");
  fxsact = checkbox("fxsact");
  fxschaos = hslider("fxschaos",0,0,1,0.01) : pow(3);
};

//----------------------------------------------------------------------------------------//
//---------------------------------------FX_FUNCTIONS-------------------------------------//
//----------------------------------------------------------------------------------------//
//------AMPLITUDE_MODULATION-----//
am(tempo,trigx) = _ : *(1-(amdepth *(amfreq : amosc)))
with{
  amwave = hslider("amwave",0,0,1,0.001) : fo.automrec(_,tempo,amwrecord,amwloop,trigx,amwact) : fo.chaos(amwchaos,16) : vbargraph("amwaveO",0,1) : *(3);
  amwrecord = checkbox("amwrecord");
  amwloop = checkbox("amwloop");
  amwact = checkbox("amwact");
  amwchaos = hslider("amwchaos",0,0,1,0.01) : pow(3);

  amfreq = hslider("amfreq",0,0,1,0.001) : fo.automrec(_,tempo,amfrecord,amfloop,trigx,amfact) : fo.chaos(amfchaos,17) : vbargraph("amfreqO",0,1) : pow(3.1) : *(99.9) : max(0.1) : vbargraph("amfreqhzO",0.1,100) : si.smoo;
  amfrecord = checkbox("amfrecord");
  amfloop = checkbox("amfloop");
  amfact = checkbox("amfact");
  amfchaos = hslider("amfchaos",0,0,1,0.01) : pow(3);

  amdepth = hslider("amdepth",0,0,1,0.01) : fo.automrec(_,tempo,amdrecord,amdloop,trigx,amdact) : fo.chaos(amdchaos,18) : vbargraph("amdepthO",0,1) : pow(3.1) : si.smoo;
  amdrecord = checkbox("amdrecord");
  amdloop = checkbox("amdloop");
  amdact = checkbox("amdact");
  amdchaos = hslider("amdchaos",0,0,1,0.01);

  clip(x) = x : min(1,_) : max(0,_);
  amosc = _ <: (((((os.oscsin : *(0.5) : +(0.5)), os.lf_trianglepos : si.interpolate(clip(amwave))), os.lf_sawpos : si.interpolate(clip(amwave-(1)))), os.lf_squarewavepos : si.interpolate(clip(amwave-2))));
};
//-----------SATURATION---------------//
/* tanh waveshape distortion */
saturation(tempo,trigx) = _ <: _,(_ : *(1.57) : ma.tanh) : si.interpolate(saturationamount)
with{
  saturationamount = hslider("saturation",0,0,1,0.01) : fo.automrec(_,tempo,satrecord,satloop,trigx,satact) : fo.chaos(satchaos,19) : vbargraph("saturationO",0,1) : si.smoo;
  satrecord = checkbox("satrecord");
  satloop = checkbox("satloop");
  satact = checkbox("satact");
  satchaos = hslider("satchaos",0,0,1,0.01);
};
//-----------RESONANT_FILTER---------------//
resfilter(tempo,trigx) = _ : fi.highpass(2,highpass) : fi.resonlp(fc,res,1)
with {
  highpass = hslider("highpass",0,0,1,0.001) : fo.automrec(_,tempo,hiprecord,hiploop,trigx,hipact) : fo.chaos(hipchaos,31) : vbargraph("highpassO",0,1) : *(136) : ba.midikey2hz : si.smoo;
  hiprecord = checkbox("hiprecord");
  hiploop = checkbox("hiploop");
  hipact = checkbox("hipact");
  hipchaos = hslider("hipchaos",0,0,1,0.01) : pow(2.7);

  fc = hslider("filtercutoff",1,0,1,0.001) : fo.automrec(_,tempo,fcrecord,fcloop,trigx,fcact) : fo.chaos(fcchaos,20) : vbargraph("filtercutoffO",0,1) : *(136) : ba.midikey2hz : si.smoo;
  fcrecord = checkbox("fcrecord");
  fcloop = checkbox("fcloop");
  fcact = checkbox("fcact");
  fcchaos = hslider("fcchaos",0,0,1,0.01) : pow(2.7);

  res = hslider("filterresonance",0,0,1,0.001) : fo.automrec(_,tempo,frrecord,frloop,trigx,fract) : fo.chaos(frchaos,21) : vbargraph("filterresonanceO",0,1) : *(99) : +(1) : si.smoo;
  frrecord = checkbox("frrecord");
  frloop = checkbox("frloop");
  fract = checkbox("fract");
  frchaos = hslider("frchaos",0,0,1,0.01) : pow(2.7);
};
//-----------BITCRUSHER---------------//
bitcrush(tempo,trigx) = _ : ba.downSample(bit)
with {
  bit = hslider("bitcrush",0,0,1,0.01) : fo.automrec(_,tempo,bitrecord,bitloop,trigx,bitact) : fo.chaos(bitchaos,22) : vbargraph("bitcrushO",0,1) : 1-(_) : pow(3) : *(47950) : +(50) : si.smoo;
  bitrecord = checkbox("bitrecord");
  bitloop = checkbox("bitloop");
  bitact = checkbox("bitact");
  bitchaos = hslider("bitchaos",0,0,1,0.01);
};
//------------PITCHSHIFT------------//
pitchshift(tempo,trigx) = _ : ef.transpose(100,50,pshift)
with {
  pshift = hslider("pitchshift",0.5,0,1,0.1) : fo.automrec(_,tempo,pitrecord,pitloop,trigx,pitact) : fo.chaos(pitchaos,23) : vbargraph("pitchshiftO",0,1) : *(48) : -(24) : si.smoo;
  pitrecord = checkbox("pitrecord");
  pitloop = checkbox("pitloop");
  pitact = checkbox("pitact");
  pitchaos = hslider("pitchaos",0,0,1,0.01) : pow(2.7);
};
//-------------DELAY---------------//
delay(tempo,trigx) = _ <:_,(*(delsend) : ef.echo(1,deltime,feedback)) :> _
with {
  delsend = hslider("delaysend",0,0,1,0.01) : fo.automrec(_,tempo,dsrecord,dsloop,trigx,dsact) : fo.chaos(dschaos,24) : vbargraph("delaysendO",0,1) : si.smoo;
  dsrecord = checkbox("dsrecord");
  dsloop = checkbox("dsloop");
  dsact = checkbox("dsact");
  dschaos = hslider("dschaos",0,0,1,0.01);

  deltime = hslider("delaytime",0,0,1,0.01) : fo.automrec(_,tempo,dtrecord,dtloop,trigx,dtact) : fo.chaos(dtchaos,25) : vbargraph("delaytimeO",0,1) : si.smoo;
  dtrecord = checkbox("dtrecord");
  dtloop = checkbox("dtloop");
  dtact = checkbox("dtact");
  dtchaos = hslider("dtchaos",0,0,1,0.01);

  feedback = hslider("delayfeedback",0,0,1,0.01) : fo.automrec(_,tempo,dfrecord,dfloop,trigx,dfact) : fo.chaos(dfchaos,26) : vbargraph("delayfeedbackO",0,1) : si.smoo;
  dfrecord = checkbox("dfrecord");
  dfloop = checkbox("dfloop");
  dfact = checkbox("dfact");
  dfchaos = hslider("dfchaos",0,0,1,0.01);
};
//-------------GATE---------------//
gate(tempo,trigx) = _ : ef.gate_mono(thresh,att,hold,rel) : _
with {
  thresh = hslider("gatethresh",0,0,1,0.01) : fo.automrec(_,tempo,gtrecord,gtloop,trigx,gtact) : fo.chaos(gtchaos,27) : vbargraph("gatethreshO",0,1) : pow(0.3) : *(60) : -(60); //short = gt
  gtrecord = checkbox("gtrecord");
  gtloop = checkbox("gtloop");
  gtact = checkbox("gtact");
  gtchaos = hslider("gtchaos",0,0,1,0.01);

  att = hslider("gateattack",0,0,1,0.01) : fo.automrec(_,tempo,garecord,galoop,trigx,gaact) : fo.chaos(gachaos,28) : vbargraph("gateattackO",0,1) : pow(3) : *(0.25) : +(0.001); // short = ga
  garecord = checkbox("garecord");
  galoop = checkbox("galoop");
  gaact = checkbox("gaact");
  gachaos = hslider("gachaos",0,0,1,0.01);

  hold = hslider("gatehold",0,0,1,0.01) : fo.automrec(_,tempo,ghrecord,ghloop,trigx,ghact) : fo.chaos(ghchaos,29) : vbargraph("gateholdO",0,1) : pow(3) : *(0.25) : +(0.001); // short = gh
  ghrecord = checkbox("ghrecord");
  ghloop = checkbox("ghloop");
  ghact = checkbox("ghact");
  ghchaos = hslider("ghchaos",0,0,1,0.01);

  rel = hslider("gaterelease",0,0,1,0.01) : fo.automrec(_,tempo,grrecord,grloop,trigx,gract) : fo.chaos(grchaos,30) : vbargraph("gatereleaseO",0,1) : pow(3) : +(0.001); // short = gr
  grrecord = checkbox("grrecord");
  grloop = checkbox("grloop");
  gract = checkbox("gract");
  grchaos = hslider("grchaos",0,0,1,0.01);
};
