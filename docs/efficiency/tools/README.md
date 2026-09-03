# Measurement tools

Helpers used to produce the numbers in `../README.md`. They assume Pd 0.56 at
`/Applications/Pd-0.56-2.app` and the compiled externals from a built app bundle.

| file | what it does |
|---|---|
| `pdparse.py` | Parses a `.pd` file into canvases, objects and connections. `dead` mode reports isolated / unwired objects, `sr` mode reports send/receive symbols with no counterpart, `count` mode gives a class histogram. |
| `pdedit.py` | Structural edits to a `.pd` file: delete objects and renumber the connections, or replace an object's text. Untouched records are preserved byte for byte (verified round-trip on all 50 patches). |
| `census.py` | Expands the abstraction tree from `_main.pd` and counts what actually exists at runtime, including `clone` multiplicity. This is what showed 427 automaters and 2225 signal objects. |
| `pdbench.sh` | Loads the patch tree headless for N seconds with DSP on or off and reports process CPU. **Only trust the `dsp=0` numbers** - see the caveat in the main README. |
| `faust_ab_arch.cpp` | Faust architecture file: sets parameters from argv (`name=value`, or `@sample:name=value` to schedule a change), renders N samples, writes raw floats to stdout. |
| `faust_ab_compare.py` | Sample-by-sample difference between two such renders. |
| `faust_ab_spectrum.py` | RMS-envelope and average-spectrum agreement between two renders - use this rather than the sample-by-sample figure whenever an oscillator's phase can drift. |

Build an A/B binary with:

    faust -a faust_ab_arch.cpp -vec -lv 0 -vs 4 -I src/faust src/faust/formuls.dsp -o /tmp/ab.cpp
    clang++ -O2 -std=c++17 -I/opt/homebrew/include /tmp/ab.cpp -o /tmp/ab

Throughput comparisons use Faust's own harness, with the flags `build-macOS.sh` ships:

    faust2bench -vec -lv 0 -vs 4 formuls.dsp && ./formuls
