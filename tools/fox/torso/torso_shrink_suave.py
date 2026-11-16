# PES21 FX (16/11) - achicado del torso de TorsoBody como torso_shrink.py (9 14 2 5) pero con la transicion del cuello LARGA:
# el fundido de 3 cm (4 a 7 cm del anillo) dejaba un escalon de 9 mm alrededor del cuello, a la vista sin camiseta (Agustin:
# "parche con forma de cuello", "sin escalonados"). Aca va de NECK2[0] a NECK2[1] (pendiente maxima 3 veces menor).
# El anillo del brazo y sus 2 cm quedan EXACTAMENTE como en torso_shrink.py (el arm.fmdl instalado copia ese corrimiento).
# Solo torsos (los brazos no se tocan). Uso: python3 torso_shrink_suave.py <carpeta fuente> <salida> [MM MMX MMA MMB]
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE]
import torso_shrink as ts
from torso_mesh import load_parts, geodesic_from

NECK2 = (0.03, 0.11)

def main(src_dir, out_dir, mm=9.0, mmx=14.0, mma=2.0, mmb=5.0):
    os.makedirs(out_dir, exist_ok=True)
    for fn in ts.TORSO:
        f = os.path.join(src_dir, fn + ".fmdl")
        if not os.path.exists(f): continue
        T = load_parts(src_dir, [(fn, i) for i in (1, 2, 3)])
        Pw, b = T["Pw"], T["boundary"]
        sa = ts.arm_seam(Pw, b)
        fixed = np.setdiff1d(b, sa)
        d = geodesic_from(Pw, T["edges"], fixed)
        neck = fixed[Pw[fixed, 1] > 1.40]
        dn = geodesic_from(Pw, T["edges"], neck)
        ds = geodesic_from(Pw, T["edges"], sa)
        wn0 = ts.ss((dn - ts.NECK[0]) / (ts.NECK[1] - ts.NECK[0]))
        wn1 = ts.ss((dn - NECK2[0]) / (NECK2[1] - NECK2[0]))
        t = ts.ss((ds - 0.02) / 0.04)
        wn = wn0 * (1 - t) + wn1 * t
        ax = []
        for sgn in (-1, 1):
            k = sa[np.sign(Pw[sa, 0]) == sgn]
            if len(k): ax.append(Pw[k[np.argmin(Pw[k, 1])]])
        wx = np.zeros(len(Pw))
        for a in ax: wx = np.maximum(wx, np.exp(-0.5 * np.sum((Pw - a) ** 2, 1) / 0.025 ** 2))
        belly = ts.ss((Pw[:, 1] - ts.PANZA[0]) / (ts.PANZA[1] - ts.PANZA[0]))
        N, Ns = ts.smooth_normals(T)
        front = ts.ss((Ns[:, 2] + 0.3) / 0.6)
        base = mmb + (mm - mmb) * front
        common = (base + (mmx - base) * wx) * 0.001 * ts.ss(d / ts.FADE) * belly
        amt = common * wn
        viejo = common * wn0
        print("  anillo del brazo: diferencia con torso_shrink %.4f mm; cambio maximo del achicado %.1f mm" % (np.abs(amt - viejo)[sa].max() * 1000, np.abs(amt - viejo).max() * 1000))
        ts.write_file(f, os.path.join(out_dir, fn + ".fmdl"), T, Pw - Ns * amt[:, None], N, fn, amt)

if __name__ == "__main__":
    a = sys.argv[1:]
    main(a[0], a[1], *(float(x) for x in a[2:6]))
