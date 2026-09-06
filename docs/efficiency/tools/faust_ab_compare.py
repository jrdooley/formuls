import subprocess, sys, struct, math
def run(binp, args):
    out = subprocess.run([binp]+args, capture_output=True).stdout
    n = len(out)//4
    return struct.unpack('<%df'%n, out[:n*4])
a_bin, b_bin = sys.argv[1], sys.argv[2]
args = sys.argv[3:]
A = run(a_bin, args); B = run(b_bin, args)
n = min(len(A), len(B))
if n == 0: print("EMPTY OUTPUT"); sys.exit(1)
md=0.0; sa=0.0; sd=0.0; peak=0.0
for i in range(n):
    d = abs(A[i]-B[i]); md = max(md,d)
    sa += A[i]*A[i]; sd += d*d; peak=max(peak,abs(A[i]))
rmsA = math.sqrt(sa/n); rmsD = math.sqrt(sd/n)
db = 20*math.log10(rmsD/rmsA) if rmsD>0 and rmsA>0 else float('-inf')
print(f"  samples={n}  peak(A)={peak:.6f}  rms(A)={rmsA:.6f}")
print(f"  max|diff|={md:.3e}  rms(diff)={rmsD:.3e}  -> error {db:.1f} dB relative to signal")
