import("stdfaust.lib");

fx = library("ffx.lib");

rev = fx.reverb;

process = _,_ : rev : _,_;
