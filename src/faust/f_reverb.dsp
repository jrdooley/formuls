import("stdfaust.lib");

fx = library("ffx.lib");

rev = fx.rev;

process = _,_ : rev : _,_;
