#!/usr/bin/env python3
"""Count the OSC traffic Pd sends to the Open Stage Control port.

Binds a UDP port in place of the o-s-c server and reports datagrams/s and
OSC messages/s (a bundle counts as one datagram and N messages), plus a
per-address histogram. Prints the summary on SIGINT/SIGTERM.

    python3 osc-sink.py 19001 addrs.txt

Argument 2 is optional; when given, every address seen is written there as
"count<TAB>address", most frequent first. That file is the address list
osc-load.py drives.
"""
import socket, struct, sys, time, collections, signal

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9001


def read_str(b, i):
    j = b.index(b"\x00", i)
    return b[i:j].decode("latin1"), (j + 4) & ~3


def parse(pkt, out):
    """Append (address, nargs) for every message in pkt; recurse into bundles."""
    if pkt[:8] == b"#bundle\x00":
        i = 16
        while i + 4 <= len(pkt):
            (n,) = struct.unpack_from(">i", pkt, i)
            i += 4
            parse(pkt[i:i + n], out)
            i += n
        return
    if pkt[:1] != b"/":
        return
    addr, i = read_str(pkt, 0)
    nargs = 0
    if i < len(pkt) and pkt[i:i + 1] == b",":
        tags, i = read_str(pkt, i)
        nargs = len(tags) - 1
    out.append((addr, nargs))


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
s.bind(("0.0.0.0", PORT))
s.settimeout(0.25)

sec = collections.defaultdict(lambda: [0, 0, 0])   # unix second -> [datagrams, messages, bytes]
addrs = collections.Counter()
t0 = None


def report(*_):
    if t0 is None:
        print("no traffic")
        sys.exit(0)
    ts = sorted(sec)
    print("\nsecond  datagrams  messages   bytes")
    for t in ts:
        d, m, b = sec[t]
        print(f"{t - ts[0]:>6}  {d:>9}  {m:>8}  {b:>7}")
    tot = [sum(sec[t][i] for t in ts) for i in range(3)]
    span = max(1, len(ts))
    print(f"\ntotal {tot[0]} datagrams, {tot[1]} messages, {tot[2]} bytes over {span}s")
    print(f"mean  {tot[0]/span:.0f} datagrams/s, {tot[1]/span:.0f} messages/s, "
          f"{tot[2]/span/1024:.1f} KiB/s")
    if len(sys.argv) > 2:
        with open(sys.argv[2], "w") as f:
            for a, c in addrs.most_common():
                f.write(f"{c}\t{a}\n")
    print(f"\ndistinct addresses: {len(addrs)}")
    for a, c in addrs.most_common(15):
        print(f"  {c:>6}  {a}")
    sys.exit(0)


signal.signal(signal.SIGINT, report)
signal.signal(signal.SIGTERM, report)

while True:
    try:
        pkt, _ = s.recvfrom(65535)
    except socket.timeout:
        continue
    now = int(time.time())
    if t0 is None:
        t0 = now
    msgs = []
    parse(pkt, msgs)
    e = sec[now]
    e[0] += 1
    e[1] += len(msgs)
    e[2] += len(pkt)
    for a, _n in msgs:
        addrs[a] += 1
