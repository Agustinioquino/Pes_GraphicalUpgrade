# PES21 FX (16/11) - compara PesTorsoShrinkMM (pes_skintone.hlsli) con el achicado real del modelo: python3 torso_regrow_ajuste.py <sin achicar> <achicado>
import sys, os, numpy as np
sys.path[:0] = ["/home/user/Pes_GraphicalUpgrade/tools/fox/torso", "/home/user/Pes_GraphicalUpgrade/tools/fox"]
from torso_mesh import load_parts
S = os.getcwd()   # carpetas con los fmdl: <sin achicar> <achicado>
def ss(x): x = np.clip(x, 0, 1); return x*x*(3-2*x)
def load(d, fn="pants_out_sub"):
    T = load_parts(os.path.join(S, d), [(fn, i) for i in (1,2,3)])
    return T["P"], np.concatenate([p["nrm"] for p in T["parts"]])
def shader(P, N, mode, par):
    p = P.copy(); p[:,0] = np.abs(p[:,0])
    n = N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-8)
    front = ss((n[:,2] + 0.3)/0.6); base = 5 + 4*front
    da = p - np.array([0.2329, 1.3372, 0.0181]); wx = np.exp(-0.5*np.sum(da*da,1)/0.025**2)
    belly = ss((p[:,1]-1.18)/0.10)
    w0 = p[:,0]; w1 = p[:,2]-0.035; rho = np.maximum(np.hypot(w0,w1),1e-5); c1 = w1/rho
    c2 = 2*c1*c1-1; c3 = (4*c1*c1-3)*c1; c4 = 2*c2*c2-1
    rr = 0.07043 - 0.00015*c1 - 0.01002*c2 + 0.00154*c3 + 0.00102*c4
    yy = 1.53524 - 0.01994*c1 - 0.01021*c2 + 0.00642*c3 - 0.00158*c4
    dn = np.hypot(np.maximum(rho-rr,0), np.minimum(p[:,1]-yy,0))
    sA = (p - np.array([0.2631,1.3704,0.0195])) @ np.array([0.7611,-0.6432,-0.0839])
    if mode == 0:
        n0, n1 = par; wn = ss((dn-n0)/(n1-n0))
    else:
        a0, a1, b0, b1, t0, t1 = par
        wo = ss((dn-a0)/(a1-a0)); wnew = ss((dn-b0)/(b1-b0))
        t = ss((-sA - t0)/(t1-t0)); wn = wo*(1-t) + wnew*t
    t = (base + (14-base)*wx)*belly*wn
    own = 2*(1-ss((sA-0.15)/0.07))
    k = ss(sA/0.025)
    t = np.where(sA>0, t*(1-k)+own*k, t)
    t[(p[:,1]<1.18)|(p[:,1]>1.75)] = 0
    return t, dn, sA
if __name__ == "__main__":
    P0, _ = load(sys.argv[1]); P1, N1 = load(sys.argv[2])
    true = np.linalg.norm(P0-P1, axis=1)*1000
    print("achicado real: max %.2f media %.2f" % (true.max(), true.mean()))
    for name, mode, par in [("viejo z349 1.8", 0, (0.018, 0.048)), ("viejo z349 4", 0, (0.04, 0.07)), ("3..11", 0, (0.03, 0.11))]:
        t, dn, sA = shader(P1, N1, mode, par); e = t - true
        print("%-16s err medio %.2f rms %.2f max +%.2f %.2f | sobra>2mm %d falta>2mm %d" % (name, np.abs(e).mean(), np.sqrt((e*e).mean()), e.max(), e.min(), (e>2).sum(), (e<-2).sum()))
    
