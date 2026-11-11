// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2014-2026 James Dooley (aka formuls)

//----------------------------------------------------------------------------------------//
//----------------------------------FORMULS_REPEATER--------------------------------------//
//----------------------------------------------------------------------------------------//
// compile with: faust2puredata -vec -lv 0 -vs 4 -clang

declare names "formuls";
declare author "James Dooley: info@formuls.co.uk";
declare copyright "James Dooley";
declare version "1.0";
declare license "GPL-3.0-or-later";
declare options "[osc:on]";

import("stdfaust.lib");
fx = library("ffx.lib");

process = fx.repeater;
