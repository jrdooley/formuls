"""oscsend.py PORT ADDRESS VALUE [VALUE...] -- one OSC message over UDP (floats)."""
import socket, struct, sys

def pad(b):
    return b + b'\0' * ((4 - len(b) % 4) % 4 or 0) if len(b) % 4 else b + b'\0' * 4

def osc_string(s):
    b = s.encode() + b'\0'
    return b + b'\0' * ((4 - len(b) % 4) % 4)

port, addr, vals = int(sys.argv[1]), sys.argv[2], [float(v) for v in sys.argv[3:]]
msg = osc_string(addr) + osc_string(',' + 'f' * len(vals)) + b''.join(struct.pack('>f', v) for v in vals)
socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(msg, ('127.0.0.1', port))
