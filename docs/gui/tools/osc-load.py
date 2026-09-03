#!/usr/bin/env python3
"""Drive real widget addresses into an Open Stage Control server.

Two modes, because the two questions need different shapes of load:

  throttled   N addresses updated every 1/HZ seconds, the way a modulated
              parameter does. This is the realistic load: bursty, one round
              per GUI-throttle tick. Use it to reproduce the instrument.

  steady      one message every GAP seconds, round-robin over the address
              list, no bursts. Use it to price a single widget update -
              burstiness would otherwise confound the per-message figure.

    python3 osc-load.py throttled 19001 addrs-params.txt --n 84 --secs 20 --hz 25
    python3 osc-load.py steady    19001 addrs-xy.txt     --count 6000 --gap 0.001 --arity 2

--arity is how many floats each message carries: 1 for a fader, 2 for an xy
pad. Sending one float to an xy widget is silently ignored by the client and
prices at ~2 us, which looks like a spectacular result and is not one.

TRAP: OSC pads strings to a multiple of 4 *including* the terminating null,
and adds nothing when the length is already aligned. Writing the padding as
`4 - len(b) % 4` instead of `(4 - len(b) % 4) % 4` appends four spurious
nulls to every address whose length is a multiple of 4, which misaligns the
type tag and makes osc.js drop the message. That looked exactly like the
server dropping 28% of its input under load. See the caveat in ../README.md.
"""
import argparse, math, socket, struct, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("mode", choices=["throttled", "steady"])
ap.add_argument("port", type=int)
ap.add_argument("addrfile")
ap.add_argument("--n", type=int, default=84, help="throttled: how many addresses to drive")
ap.add_argument("--secs", type=float, default=20.0, help="throttled: duration")
ap.add_argument("--hz", type=float, default=25.0, help="throttled: update rate per address")
ap.add_argument("--count", type=int, default=6000, help="steady: messages to send")
ap.add_argument("--gap", type=float, default=0.001, help="steady: seconds between messages")
ap.add_argument("--arity", type=int, default=1, help="floats per message (1 fader, 2 xy)")
ap.add_argument("--no-filter", action="store_true",
                help="keep /GET and /seqpos*, which are skipped by default as "
                     "housekeeping. Needed to price them on their own -- they "
                     "are not widget values in the ordinary sense, but they are "
                     "real traffic and /seqpos is not gated by inter-act")
a = ap.parse_args()

addrs = []
for line in open(a.addrfile):
    parts = line.rstrip("\n").split("\t")
    addr = parts[-1]
    if not a.no_filter and (addr == "/GET" or addr.startswith("/seqpos")):
        continue          # server/patch housekeeping, not widget values
    addrs.append(addr)
if not addrs:
    sys.exit(f"no usable addresses in {a.addrfile}")


def pad(b):
    return b + b"\x00" * ((4 - len(b) % 4) % 4)


def msg(addr, vals):
    return (pad(addr.encode() + b"\x00")
            + pad(("," + "f" * len(vals)).encode() + b"\x00")
            + b"".join(struct.pack(">f", v) for v in vals))


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 8 << 20)
dest = ("127.0.0.1", a.port)
sent = 0

if a.mode == "throttled":
    live = addrs[:a.n]
    period = 1.0 / a.hz
    end = time.time() + a.secs
    tick = 0
    while time.time() < end:
        t0 = time.time()
        for i, addr in enumerate(live):
            v = 0.5 + 0.45 * math.sin(tick * 0.05 + i * 0.11)
            s.sendto(msg(addr, [v] * a.arity), dest)
            sent += 1
        tick += 1
        dt = period - (time.time() - t0)
        if dt > 0:
            time.sleep(dt)
    print(f"{len(live)} addresses x {a.hz:g} Hz -> sent {sent} messages "
          f"in {a.secs:.0f}s = {sent / a.secs:.0f} msg/s")
else:
    for i in range(a.count):
        s.sendto(msg(addrs[i % len(addrs)], [(i % 100) / 100.0] * a.arity), dest)
        t = time.time() + a.gap          # busy-wait: sleep() cannot resolve 1 ms reliably
        while time.time() < t:
            pass
    print(f"sent {a.count} messages ({a.arity} float(s) each), "
          f"one every {a.gap * 1000:.1f} ms")
