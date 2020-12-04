import("stdfaust.lib");
fo = library("formuls.lib");

// signal inputs: 1) signal to process 2)sidechain/envelopfollower signal 3)fo.automrec trigger
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
    panning = hslider("panning",0.5,0,1,0.01) : fo.automrec(_,tempo,precord,ploop,trigxx,pact) : fo.chaos(pchaos,15) : vbargraph("panningO",0,1) : si.smoo;
    precord = checkbox("precord"); // activates fo.automrec recording
    ploop = checkbox("ploop"); // activates fo.automrec read looping
    pact = checkbox("pact"); // switches fo.automrec on and off
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

tempo = hslider("tempo",1,0.01,1,0.01); // controls fo.automrec read speed
