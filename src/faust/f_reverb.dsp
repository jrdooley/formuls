// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2014-2026 James Dooley (aka formuls)

declare author "James Dooley: info@formuls.co.uk";
declare copyright "James Dooley";
declare license "GPL-3.0-or-later";

import("stdfaust.lib");

fx = library("ffx.lib");

rev = fx.rev;

process = _,_ : rev : _,_;
