# PES21 FX (26/10 e) - la forma anatomica de la pelvis "sin short": en lugar de una superficie parametrica (cadera cuadrada,
# gluteos tipo panal, escalon en la cintura), la pelvis es la de un cuerpo real: la malla base de MakeHuman (CC0, hombre joven;
# mh_fetch.py -> mh_pelvis_cc0.npz) deformada (thin plate spline) para coincidir con TorsoBody: con la banda baja del torso
# (y 1.10 .. 1.175) y con la parte de arriba de los muslos (y 0.66 .. 0.78) y su borde exacto (29 + 29 vertices). Sobre esa
# superficie S:
#   - reshape_torso: la parte de abajo del torso de TorsoBody (malla 3, por debajo de y 1.105; del todo por debajo de 1.06) pasa a la
#     forma de S (sin el escalon de la cintura del short: el torso de TorsoBody abajo esta hecho para la pretina), con el detalle
#     fino de TorsoBody (ombligo, surcos) encima. El borde de abajo del torso (92 vertices) queda sobre S.
#   - build_on_surface: la pelvis con la topologia de pelvis_mesh.build_pelvis (anillos del cuerpo hasta la entrepierna, puente,
#     anillos de las piernas) proyectada sobre S (rayos horizontales desde el eje del cuerpo / el centro de cada pierna), soldada
#     EXACTO al borde nuevo del torso y al de los muslos. Frente liso (tipo Ken): el bulto de la entrepierna de MakeHuman se alisa.
# Cadera, trocanteres, gluteos con su pliegue y la raya, sacro y bajo vientre salen de la anatomia real.
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', 'torso'))
import pelvis_mesh as pm
from torso_mesh import gauss_matrix, vertex_normals, rotate_min

def ss(a, b, x):
    t = np.clip((np.asarray(x, float) - a) / (b - a), 0.0, 1.0); return t * t * (3 - 2 * t)
def g(u): return np.exp(-0.5 * np.asarray(u, float) ** 2)

# ---------------------------------------------------------------- geometria
def plane_section(P, T, axis, val):
    """segmentos (n, 2, 3) de la interseccion de la malla con el plano coord[axis] = val"""
    d = P[:, axis] - val
    dd = d[T]
    cross = ~((dd > 0).all(1) | (dd < 0).all(1))
    segs = []
    for tri, dv in zip(T[cross], dd[cross]):
        pts = []
        for a, b in ((0, 1), (1, 2), (2, 0)):
            if (dv[a] > 0) != (dv[b] > 0):
                t = dv[a] / (dv[a] - dv[b]); pts.append(P[tri[a]] + t * (P[tri[b]] - P[tri[a]]))
        if len(pts) == 2: segs.append(pts)
    return np.array(segs)

def _ray_hits(O, D, P, T, far):
    """por rayo (origen O, direccion unitaria D, casi horizontales) la distancia del corte mas lejano (far) o mas cercano con la malla"""
    V0 = P[T[:, 0]]; E1 = P[T[:, 1]] - V0; E2 = P[T[:, 2]] - V0
    lo = np.minimum(np.minimum(P[T[:, 0]], P[T[:, 1]]), P[T[:, 2]])[:, 1]; hi = np.maximum(np.maximum(P[T[:, 0]], P[T[:, 1]]), P[T[:, 2]])[:, 1]
    out = np.full(len(O), np.nan)
    for i in range(len(O)):
        o, d = O[i], D[i]
        sel = (lo <= o[1] + 0.03) & (hi >= o[1] - 0.03)
        if not sel.any(): continue
        v0, e1, e2 = V0[sel], E1[sel], E2[sel]
        pv = np.cross(d, e2); det = np.sum(e1 * pv, 1)
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        tv = o - v0; u = np.sum(tv * pv, 1) * inv
        qv = np.cross(tv, e1); v = (qv @ d) * inv; t = np.sum(e2 * qv, 1) * inv
        hit = ok & (u >= -1e-6) & (v >= -1e-6) & (u + v <= 1 + 1e-6) & (t > 1e-6)
        if hit.any(): out[i] = t[hit].max() if far else t[hit].min()
    return out
def ray_far(O, D, P, T): return _ray_hits(O, D, P, T, True)
def ray_near(O, D, P, T): return _ray_hits(O, D, P, T, False)

# ---------------------------------------------------------------- MakeHuman -> TorsoBody
def load_mh():
    d = np.load(os.path.join(HERE, "mh_pelvis_cc0.npz"))
    return d["P"].astype(np.float64), d["T"].astype(np.int64)

def global_tf(P):
    """escala / corrimiento global (medido entre las secciones de los dos cuerpos a la altura de la cintura y los muslos)"""
    Q = P.copy()
    Q[:, 0] *= 1.05; Q[:, 1] -= 0.014; Q[:, 2] = (Q[:, 2] - 0.036) * 1.03 + 0.066
    return Q

def _ring_targets(Pm, Tm, Pv, Fv, ys, dth, xsign, far):
    src, dst = [], []
    for y in ys:
        S = plane_section(Pv, Fv, 1, y).reshape(-1, 3)
        if xsign: S = S[S[:, 0] * xsign > 0]
        c = np.array([0.5 * (S[:, 0].min() + S[:, 0].max()) if xsign else 0.0, y, 0.5 * (S[:, 2].min() + S[:, 2].max())])
        ang = np.degrees(np.arctan2(S[:, 0] - c[0], S[:, 2] - c[2]))
        for a in np.arange(-180, 180, dth):
            k = np.argmin(np.abs((ang - a + 180) % 360 - 180))
            p = S[k]; d = p - c; d[1] = 0; d /= np.linalg.norm(d)
            t = (ray_far if far else ray_near)(c[None], d[None], Pm, Tm)[0]
            if np.isnan(t): continue
            src.append(c + t * d); dst.append(p)
    return src, dst

def warp_to_torsobody(ms, th, rings, torso_y=(1.10, 1.125, 1.15, 1.175), thigh_y=(0.66, 0.70, 0.74, 0.78), dth=6.0):
    """S (vertices) y T (triangulos) de MakeHuman deformado sobre TorsoBody. ms = mallas de pants_*_sub, th = malla de thigh_short,
    rings = {+1: anillo del muslo izquierdo (n x 3), -1: derecho}"""
    from scipy.interpolate import RBFInterpolator
    P, T = load_mh(); Pm = global_tf(P)
    P3 = ms[3]["pos"].astype(float); F3 = ms[3]["faces"]
    src, dst = _ring_targets(Pm, T, P3, F3, torso_y, dth, 0, True)
    Pt = th["pos"].astype(float); Ft = th["faces"]
    for sx in (1, -1):
        s2, d2 = _ring_targets(Pm, T, Pt, Ft, thigh_y, dth, sx, False); src += s2; dst += d2
        R = rings[sx]; cc = R.mean(0)
        for p in R:   # el borde del muslo, exacto
            c = np.array([cc[0], p[1], cc[2]]); d = p - c; d[1] = 0; d /= np.linalg.norm(d)
            t = ray_near(c[None], d[None], Pm, T)[0]
            if np.isnan(t): continue
            src.append(c + t * d); dst.append(p)
            # (los muslos de TorsoBody son mas gruesos que la cadera de MakeHuman): de la parte de afuera del borde (adelante,
            # costado, atras-afuera) la cadera sigue vertical unos 4 cm antes de pasar a la forma de MakeHuman; asi no queda
            # un escalon hacia adentro justo arriba de la costura
            ang = np.degrees(np.arctan2(sx * (p[0] - cc[0]), p[2] - cc[2]))
            if 15.0 <= ang <= 150.0:
                for dy in (0.02, 0.045):
                    c2 = c + np.array([0.0, dy, 0.0]); t2 = ray_near(c2[None], d[None], Pm, T)[0]
                    if np.isnan(t2): continue
                    src.append(c2 + t2 * d); dst.append(p + np.array([0.0, dy, 0.0]))
    src = np.array(src); dst = np.array(dst)
    rb = RBFInterpolator(src, dst - src, kernel="thin_plate_spline", smoothing=1e-7)
    return Pm + rb(Pm), T

def axis_z(S, Ts):
    """centro (z) de la seccion del cuerpo por altura (eje de los rayos)"""
    ys = np.arange(0.86, 1.20, 0.01); zc = []
    for y in ys:
        Sx = plane_section(S, Ts, 1, y).reshape(-1, 3); Sx = Sx[np.abs(Sx[:, 0]) < 0.02]
        zc.append(0.5 * (Sx[:, 2].min() + Sx[:, 2].max()))
    return lambda y: np.interp(y, ys, np.array(zc))

# ---------------------------------------------------------------- torso: abajo, la forma de S
def reshape_torso(S, Ts, m3, y0=1.06, y1=1.105, sig_s=0.015, sig_d=0.008):
    """malla 3 del torso: por debajo de y1 (del todo por debajo de y0) la superficie es la de S (suavizada: sin el ombligo ni los
    pliegues de MakeHuman) mas el detalle fino de TorsoBody (lo de escala menor que sig_d). Devuelve pos, nrm (x, y, z, w) y tan
    nuevos por vertice de la malla (sin soldar) y la malla soldada"""
    P3 = m3["pos"].astype(float); F3 = m3["faces"]
    q = np.round(P3 / 1e-4).astype(np.int64)
    _, uid, inv = np.unique(q, axis=0, return_index=True, return_inverse=True); inv = inv.ravel()
    Pw = P3[uid]; Fw = inv[F3]
    Fw = Fw[(Fw[:, 0] != Fw[:, 1]) & (Fw[:, 1] != Fw[:, 2]) & (Fw[:, 2] != Fw[:, 0])]
    zc = axis_z(S, Ts)
    n = vertex_normals(Pw, Fw)
    if np.sum(n * (Pw - np.stack([np.zeros(len(Pw)), Pw[:, 1], zc(Pw[:, 1])], 1))) < 0: n = -n
    Gd = gauss_matrix(Pw, Fw, sig_d); Pd = Gd @ Pw
    h = np.sum((Pw - Pd) * n, 1)
    # las arrugas de la pretina del short que TorsoBody tiene marcadas en la malla (y 1.03 .. 1.09) se atenuan (sin short no
    # tienen por que estar); el ombligo queda
    navel = g(np.linalg.norm(Pw - np.array([0.0, 1.0608, 0.1750]), axis=1) / 0.02)
    h *= 1.0 - 0.7 * ss(1.095, 1.04, Pw[:, 1]) * (1.0 - navel)
    Gs = gauss_matrix(S, Ts, sig_s); Ss = Gs @ S
    w = ss(y1, y0, Pw[:, 1])
    sel = np.where(w > 0)[0]
    O = np.stack([np.zeros(len(sel)), Pd[sel, 1], zc(Pd[sel, 1])], 1)
    D = Pd[sel] - O; D[:, 1] = 0; D /= np.linalg.norm(D, axis=1, keepdims=True)
    rS = ray_far(O, D, Ss, Ts); ok = ~np.isnan(rS)
    tgt = Pw.copy()
    tgt[sel[ok]] = O[ok] + D[ok] * rS[ok][:, None] + h[sel[ok]][:, None] * n[sel[ok]]
    Pn = Pw + w[:, None] * (tgt - Pw)
    # normales y tangentes del fmdl giradas lo mismo que gira la normal de la malla soldada
    n1 = vertex_normals(Pn, Fw); n1 *= np.sign(np.sum(n1 * n, 1, keepdims=True))
    nrm = m3["nrm"].astype(float); tan = m3["tan"].astype(float)
    nn = rotate_min(n[inv], n1[inv], nrm[:, :3]); nn /= np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-9)
    tt = rotate_min(n[inv], n1[inv], tan[:, :3]); tt -= nn * np.sum(tt * nn, 1, keepdims=True)
    tt /= np.maximum(np.linalg.norm(tt, axis=1, keepdims=True), 1e-9)
    nrm2 = nrm.copy(); nrm2[:, :3] = nn; tan2 = tan.copy(); tan2[:, :3] = tt
    return Pn[inv], nrm2, tan2, dict(Pw=Pw, Fw=Fw, inv=inv, Pn=Pn, w=w, nmiss=int((~ok).sum()), moved=float(np.abs(Pn - Pw).max()))

# ---------------------------------------------------------------- pelvis sobre S
def crotch_y(S, Ts):
    """altura de la entrepierna de S (lo mas alto del arco entre las piernas en x = 0)"""
    sec = plane_section(S, Ts, 0, 0.0).reshape(-1, 3)
    arc = sec[(sec[:, 1] < 0.95) & (sec[:, 2] > -0.02) & (sec[:, 2] < 0.12)]
    # el arco: para cada z, el punto mas bajo de la seccion que no es ni la panza ni la espalda... la entrepierna es el minimo
    # local de y en el medio: tomamos el minimo de y entre z 0.00 y 0.10 de los puntos con y > 0.75
    arc = arc[arc[:, 1] > 0.75]
    return float(arc[:, 1].min()), arc

def flatten_front(th, y, r, yc):
    """frente liso (tipo Ken): el bulto de la entrepierna de MakeHuman (adelante, entre yc y yc + 7 cm) se baja"""
    bump = g(pm.angdiff(th, 0) / 25.0) * g((y - (yc + 0.035)) / 0.030)
    return r - 0.018 * bump

def build_on_surface(top, legL, legR, S, Ts, counts=(92, 82, 74, 68, 64, 62, 60), leg_counts=(33, 31), nbridge=4, ken=True):
    """como pelvis_mesh.build_pelvis, con las posiciones sobre S. top = anillo (ya reshapeado) del torso, legL / legR los de los muslos"""
    zc = axis_z(S, Ts)
    yc, arc = crotch_y(S, Ts)
    yK = yc + 0.004                                                # ultima fila completa: justo arriba de la entrepierna
    # el borde de los muslos esta inclinado (alto afuera y adelante, 0.848; bajo adentro, 0.804): la ultima fila del cuerpo va
    # por arriba del borde en cada direccion (ybot(th)), y sobre la entrepierna a yK
    thL = []; yL = []
    for leg in (legL, legR):
        tl, _ = pm.ring_polar(leg["P"], center=(0.0, float(zc(0.85)))); thL.append(tl); yL.append(leg["P"][:, 1])
    thL = np.concatenate(thL); yL = np.concatenate(yL)
    thd0 = np.linspace(-180, 180, 721)[:-1]; ybd = np.full(len(thd0), yK)
    for i, a in enumerate(thd0):
        m = np.abs(pm.angdiff(thL, a)) < 12
        if m.any(): ybd[i] = max(yK, yL[m].max() + 0.006)
    from scipy.ndimage import gaussian_filter1d
    ybd = gaussian_filter1d(ybd, 8, mode="wrap")
    ybot = lambda th: np.interp(((np.asarray(th) + 180) % 360) - 180, thd0, ybd)
    P, kind, sv, side, ref_top, ref_leg = [], [], [], [], [], []
    def add(p, k, s, sd, rt=-1, rl=-1):
        P.append(np.asarray(p, float)); kind.append(k); sv.append(s); side.append(sd); ref_top.append(rt); ref_leg.append(rl)
        return len(P) - 1
    K = len(counts) - 1
    th0, _ = pm.ring_polar(top["P"], center=(0.0, float(zc(1.0)))); y0 = top["P"][:, 1]
    def surf(th, y):
        th = np.atleast_1d(th); y = np.atleast_1d(y)
        O = np.stack([np.zeros(len(th)), y, zc(y)], 1); D = np.stack([np.sin(np.radians(th)), np.zeros(len(th)), np.cos(np.radians(th))], 1)
        r = ray_far(O, D, S, Ts)
        if np.isnan(r).any(): raise RuntimeError("rayo sin corte con S en th %s y %s" % (th[np.isnan(r)], y[np.isnan(r)]))
        if ken: r = flatten_front(th, y, r, yc)
        return O + D * r[:, None]
    rows, rows_u = [], []
    u0 = ((th0 + 360.0) % 360.0) / 360.0
    o = np.argsort(u0)
    ids = [add(top["P"][i], 0, 0.0, np.sign(top["P"][i, 0]) if abs(top["P"][i, 0]) > 1e-4 else 0, rt=i) for i in o]
    rows.append(ids); rows_u.append(u0[o])
    for k in range(1, K + 1):
        s = k / K; n = counts[k]
        thd = np.linspace(-180, 180, 721)[:-1]
        yd = pm.periodic_interp(thd, th0, y0) * (1 - s) + ybot(thd) * s
        pts = surf(thd, yd)
        seg = np.linalg.norm(np.roll(pts, -1, 0) - pts, axis=1); cum = np.concatenate([[0], np.cumsum(seg)])
        Ltot = cum[-1]; cum = cum[:-1]
        i0 = int(np.argmin(np.abs(thd))); cum0 = (cum - cum[i0]) % Ltot
        if k == K:
            half = n // 2
            iB = int(np.argmin(np.abs(np.abs(thd) - 180))); arcB = cum0[iB]
            targ = np.concatenate([np.linspace(0, arcB, half + 1)[:-1], arcB + np.linspace(0, Ltot - arcB, half + 1)[:-1]])
        else:
            targ = np.linspace(0, Ltot, n + 1)[:-1]
        ordr = np.argsort(cum0)
        th_s = np.interp(targ, np.concatenate([cum0[ordr], [Ltot]]), np.concatenate([((thd[ordr] + 360) % 360), [360.0]]))
        th_s = np.where(th_s > 180, th_s - 360, th_s)
        if k == K: th_s[0] = 0.0; th_s[n // 2] = 180.0
        ys = pm.periodic_interp(th_s, th0, y0) * (1 - s) + ybot(th_s) * s
        ps = surf(th_s, ys)
        ids = [add(ps[j], 0, s, 0 if abs(th_s[j]) < 1e-6 or abs(abs(th_s[j]) - 180) < 1e-6 else np.sign(ps[j, 0])) for j in range(n)]
        uk = ((th_s + 360.0) % 360.0) / 360.0
        assert np.all(np.diff(uk) > 0), "fila %d sin orden" % k
        rows.append(ids); rows_u.append(uk)
    # puente por debajo de la entrepierna: sobre el arco de S en x = 0, de B (atras) a F (adelante)
    rowK = rows[K]; nK = len(rowK)
    iF, iB = rowK[0], rowK[nK // 2]
    PF, PB = P[iF], P[iB]
    oz = np.argsort(arc[:, 2]); az = arc[oz, 2]; ay = arc[oz, 1]
    bridge = []
    for j in range(1, nbridge + 1):
        t = j / (nbridge + 1)
        z = PB[2] + t * (PF[2] - PB[2])
        m = np.abs(az - z) < 0.012
        y = ay[m].min() if m.any() else yc
        y = min(y, yK - 0.002)
        bridge.append(add((0.0, y, z), 1, 1.0, 0))
    left_half = rowK[0: nK // 2 + 1]
    right_half = [rowK[0]] + rowK[nK // 2:][::-1]
    legs_out = {}
    for sd, leg, half in ((+1, legL, left_half), (-1, legR, right_half)):
        c = leg["P"].mean(0)
        def lang(p):
            p = np.atleast_2d(p); return np.degrees(np.arctan2(sd * (p[:, 0] - c[0]), p[:, 2] - c[2]))
        L0 = np.array(list(half) + bridge[::-1])
        a0 = lang(np.array([P[i] for i in L0])); o0 = np.argsort(a0); L0 = L0[o0]; a0 = a0[o0]
        kF = int(np.where(L0 == iF)[0][0]); L0 = np.roll(L0, -kF); a0 = np.roll(a0, -kF)
        PL0 = np.array([P[i] for i in L0])
        T_ = leg["P"]; aT = lang(T_); oT = np.argsort(aT)
        kT = int(np.argmin(np.abs(pm.angdiff(aT[oT], a0[0])))); oT = np.roll(oT, -kT)
        PT = T_[oT]
        def arcpar(Q):
            seg = np.linalg.norm(np.roll(Q, -1, 0) - Q, axis=1); cc = np.concatenate([[0], np.cumsum(seg)]); return cc[:-1] / cc[-1]
        l0 = arcpar(PL0); lT = arcpar(PT)
        def at(Q, lam, qq):
            lx = np.concatenate([lam, [1.0]]); Qx = np.concatenate([Q, Q[:1]], 0)
            return np.stack([np.interp(qq, lx, Qx[:, k]) for k in range(3)], -1)
        loops, loops_l = [list(L0)], [l0]
        for li, n in enumerate(leg_counts):
            t = (li + 1) / (len(leg_counts) + 1)
            qq = (np.arange(n) + 0.5) / n
            p = at(PL0, l0, qq) * (1 - t) + at(PT, lT, qq) * t
            # sobre S: rayo horizontal desde el centro de la pierna a esa altura
            O = np.stack([np.full(n, c[0]), p[:, 1], np.full(n, c[2])], 1); D = p - O; D[:, 1] = 0; D /= np.linalg.norm(D, axis=1, keepdims=True)
            r = ray_near(O, D, S, Ts); okr = ~np.isnan(r)
            # del todo sobre S en el medio; junto a los dos bordes (que ya estan sobre S) manda la interpolacion (sin pliegues)
            kb = np.sin(np.pi * t)
            p[okr] = p[okr] + kb * (O[okr] + D[okr] * r[okr][:, None] - p[okr])
            ids = [add(p[j], 2, 1.0 + t, sd) for j in range(n)]
            loops.append(ids); loops_l.append(qq)
        ids = [add(PT[j], 2, 2.0, sd, rl=int(oT[j])) for j in range(len(PT))]
        loops.append(ids); loops_l.append(lT)
        legs_out[sd] = (loops, loops_l)
    F, chart = [], []
    for k in range(K):
        tt = pm._zipper(rows[k], rows[k + 1], rows_u[k], rows_u[k + 1]); F += tt; chart += [0] * len(tt)
    lam = {+1: {}, -1: {}}
    for sd in (+1, -1):
        loops, la = legs_out[sd]
        for k in range(len(loops)):
            for vid, l in zip(loops[k], la[k]): lam[sd][int(vid)] = float(l)
        for k in range(len(loops) - 1):
            tt = pm._zipper(list(loops[k]), list(loops[k + 1]), np.asarray(la[k]), np.asarray(la[k + 1]))
            F += tt; chart += [1 if sd > 0 else 2] * len(tt)
    P = np.array(P); F = np.array(F, np.int64); chart = np.array(chart)
    ok = (F[:, 0] != F[:, 1]) & (F[:, 1] != F[:, 2]) & (F[:, 0] != F[:, 2])
    F = F[ok]; chart = chart[ok]
    return dict(P=P, F=F, chart=chart, kind=np.array(kind), s=np.array(sv), side=np.array(side), ref_top=np.array(ref_top),
                ref_leg=np.array(ref_leg), rows=rows, legs=legs_out, bridge=bridge, lam=lam, K=K, yc=yc, zc=zc)
