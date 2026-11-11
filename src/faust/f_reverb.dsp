declare author "James Dooley: info@formuls.co.uk";
declare copyright "James Dooley";
declare license "GPL-3.0-or-later";

import("stdfaust.lib");

fx = library("ffx.lib");

rev = fx.rev;

process = _,_ : rev : _,_;
