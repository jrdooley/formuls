import subprocess, sys, numpy as np
def run(b,a):
    o=subprocess.run([b]+a,capture_output=True).stdout
    x=np.frombuffer(o,dtype='<f4'); n=len(x)//4
    return x[:n*4].reshape(-1,4)[:,0].astype(np.float64)   # left channel
A=run(sys.argv[1],sys.argv[3:]); B=run(sys.argv[2],sys.argv[3:])
n=min(len(A),len(B)); A=A[:n]; B=B[:n]
# short-term RMS envelope (10 ms)
h=480; na=n//h
ea=np.sqrt(np.array([ (A[i*h:(i+1)*h]**2).mean() for i in range(na)]))
eb=np.sqrt(np.array([ (B[i*h:(i+1)*h]**2).mean() for i in range(na)]))
m=ea>1e-6
env_db=20*np.log10(np.abs(eb[m]-ea[m])/ea[m]+1e-20)
# average magnitude spectrum over Hann-windowed frames
N=4096; hop=2048; w=np.hanning(N)
def spec(x):
    fr=[np.abs(np.fft.rfft(x[i:i+N]*w)) for i in range(0,len(x)-N,hop)]
    return np.mean(fr,axis=0)
SA=spec(A); SB=spec(B)
k=SA>SA.max()*1e-5
sp_db=20*np.log10(np.abs(SB[k]-SA[k])/SA[k]+1e-20)
print(f"  RMS-envelope agreement : max deviation {env_db.max():+6.1f} dB  (mean {env_db.mean():+6.1f} dB)")
print(f"  Avg-spectrum agreement : max deviation {sp_db.max():+6.1f} dB  (median {np.median(sp_db):+6.1f} dB)")
print(f"  Total RMS  A={np.sqrt((A**2).mean()):.6f}  B={np.sqrt((B**2).mean()):.6f}  ratio {np.sqrt((B**2).mean())/np.sqrt((A**2).mean()):.6f}")
