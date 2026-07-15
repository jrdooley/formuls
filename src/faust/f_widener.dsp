import("stdfaust.lib");

fx = library("ffx.lib");

widener = fx.widener;

process = _,_ : widener : _,_;