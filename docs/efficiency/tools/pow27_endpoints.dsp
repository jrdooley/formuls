import("stdfaust.lib");
pow27(x) = ba.tabulate(1, ^(2.7), 1024, 0, 1, x).lin;
// input is a ramp 0..1; emit tabulated vs exact
process = _ <: pow27, ^(2.7);
