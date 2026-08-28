import("stdfaust.lib");

fx = library("ffx.lib");

ott = fx.ott;

process = _,_ : ott : _,_;