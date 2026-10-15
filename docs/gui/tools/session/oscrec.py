"""oscrec.py PORT LOGFILE -- stands in for Pd: decodes every OSC message o-s-c sends
and appends `t address args-json` lines to LOGFILE. Bundles are unpacked."""
import socket, struct, sys, json, time

def rstr(b, i):
    j = b.index(b'\0', i)
    s = b[i:j].decode('utf8', 'replace')
    return s, (j + 4) & ~3

def decode(b):
    if b.startswith(b'#bundle'):
        out, i = [], 16
        while i < len(b):
            n = struct.unpack('>i', b[i:i + 4])[0]; out += decode(b[i + 4:i + 4 + n]); i += 4 + n
        return out
    addr, i = rstr(b, 0)
    tags, i = rstr(b, i)
    args = []
    for t in tags[1:]:
        if t == 'f': args.append(round(struct.unpack('>f', b[i:i + 4])[0], 6)); i += 4
        elif t == 'i': args.append(struct.unpack('>i', b[i:i + 4])[0]); i += 4
        elif t == 'd': args.append(struct.unpack('>d', b[i:i + 8])[0]); i += 8
        elif t == 's': v, i = rstr(b, i); args.append(v)
        elif t == 'T': args.append(True)
        elif t == 'F': args.append(False)
        elif t == 'N': args.append(None)
        else: args.append('?' + t)
    return [(addr, tags, args)]

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); sock.bind(('127.0.0.1', int(sys.argv[1])))
log = open(sys.argv[2], 'a', buffering=1)
while True:
    data, _ = sock.recvfrom(65536)
    for addr, tags, args in decode(data):
        log.write(f'{time.time():.3f} {addr} {tags} {json.dumps(args)}\n')
