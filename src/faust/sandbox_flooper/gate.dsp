import("stdfaust.lib");

gate = _ : ef.gate_mono(thresh,att,hold,rel) : _
with {
  thresh = hslider("thresh",0,0,1,0.01) : *(60) : -(60);
  att = hslider("att",0,0,1,0.01) : *(0.25) : +(0.001);
  hold = hslider("hold",0,0,1,0.01) : *(0.25) : +(0.001);
  rel = hslider("rel",0,0,1,0.01) : +(0.001);
};

process = gate;
