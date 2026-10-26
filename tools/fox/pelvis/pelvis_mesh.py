# PES21 FX (26/10 c) - pelvis de piel para "sin short" (SinCamiseta, tecla 4): reemplaza la malla 0 de pants_*_sub.fmdl (el short)
# por una pelvis de piel con gluteos y el frente liso (tipo Ken), soldada EXACTO al borde de abajo del torso de TorsoBody (92
# vertices) y al borde de arriba de los muslos (thigh_*.fmdl, 29 + 29). Topologia propia (no la del short):
#   filas del cuerpo (un solo anillo cada una): fila 0 = el borde del torso, ... fila K = a la altura de la entrepierna;
#   la fila K se parte en dos mitades (adelante F y atras B en la linea media) y con un "puente" de vertices que pasa por debajo
#   de la entrepierna (de B a F) arma el primer anillo de cada pierna; despues 2 anillos por pierna hasta el borde del muslo.
# Las cantidades entran en el mismo lugar que el short (790 vertices / 1244 triangulos en pants_out_sub; 804 / 1258 en pants_in):
# lo que sobra son vertices sin usar y triangulos vacios.
import sys, os
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', 'torso'))
from fmdl import Fmdl, meshes

ZC = 0.058                                  # centro (z) de la seccion del torso
def ss(a, b, x):
    t = np.clip((np.asarray(x, float) - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)
def g(u): return np.exp(-0.5 * np.asarray(u, float) ** 2)
def angdiff(a, b): return (np.asarray(a, float) - b + 180.0) % 360.0 - 180.0

# ---------------------------------------------------------------- bordes (de las mallas de TorsoBody)
def _weld(m):
    P = m["pos"].astype(np.float64)
    q = np.round(P / 1e-4).astype(np.int64)
    _, uid, inv = np.unique(q, axis=0, return_index=True, return_inverse=True)
    return P[uid], inv.ravel()[m["faces"]], uid, inv.ravel()

def _boundary_loops(Pw, Fw, sel):
    Es = np.sort(np.concatenate([Fw[:, [0, 1]], Fw[:, [1, 2]], Fw[:, [2, 0]]]), 1)
    k, c = np.unique(Es, axis=0, return_counts=True); be = k[c == 1]
    be = be[sel[be[:, 0]] & sel[be[:, 1]]]
    nb = {}
    for a, b in be: nb.setdefault(a, []).append(b); nb.setdefault(b, []).append(a)
    seen, out = set(), []
    for s in list(nb):
        if s in seen: continue
        loop = [s]; seen.add(s); cur = s
        while True:
            nx = [v for v in nb[cur] if v not in seen]
            if not nx: break
            cur = nx[0]; loop.append(cur); seen.add(cur)
        out.append(np.array(loop))
    return out

def ring_data(m, bone_names, sel_fn, nloops):
    """anillos del borde (abiertos) de la malla m: por vertice soldado del anillo, datos del vertice original (posicion, normal,
    tangente, uv, pesos con nombres de hueso). Devuelve lista de dicts, uno por anillo (los nloops mas largos)"""
    Pw, Fw, uid, inv = _weld(m)
    loops = sorted(_boundary_loops(Pw, Fw, sel_fn(Pw)), key=len, reverse=True)[:nloops]
    out = []
    for L in loops:
        orig = uid[L]                                   # un vertice original por vertice soldado
        W = []
        for v in orig:
            d = {}
            for k in range(4):
                if m["w"][v, k] > 0: d[bone_names[m["idx"][v, k]]] = d.get(bone_names[m["idx"][v, k]], 0.0) + float(m["w"][v, k])
            W.append(d)
        out.append(dict(P=Pw[L], N=m["nrm"][orig, :3].astype(np.float64), T=m["tan"][orig].astype(np.float64), W=W))
    return out

def load_rings(common, ver_file):
    fp = Fmdl(os.path.join(common, ver_file + ".fmdl")); mp = meshes(fp)
    pn = [b["name"] for b in fp.bones]
    top = ring_data(mp[3], pn, lambda P: P[:, 1] < 1.03, 1)[0]
    ft = Fmdl(os.path.join(common, "thigh_short.fmdl")); mt = meshes(ft)[0]
    tn = [b["name"] for b in ft.bones]
    legs = ring_data(mt, tn, lambda P: P[:, 1] > 0.80, 2)
    legs.sort(key=lambda r: r["P"][:, 0].mean())       # derecha (x < 0), izquierda (x > 0)
    return top, legs[1], legs[0], fp, mp

# ---------------------------------------------------------------- forma
# seccion del cuerpo alrededor del eje (x = 0, z = ZC): angulo th (grados) desde adelante (+z) hacia la izquierda (+x)
def body_radius(th, y):
    """radio (m) de la seccion del cuerpo a la altura y (pelvis y gluteos) en el angulo th"""
    th = np.asarray(th, float); y = np.asarray(y, float)
    r = np.radians(th)
    s, c = np.sin(r), np.cos(r)
    a = 0.150 + 0.034 * ss(1.00, 0.89, y)                       # medio ancho: la cadera se abre hacia abajo
    f = 0.099 - 0.004 * ss(0.99, 0.93, y) - 0.011 * ss(0.92, 0.86, y)   # adelante: panza baja y el pubis (liso) apenas hacia atras
    b = 0.097 + 0.004 * ss(1.00, 0.90, y)                       # atras (el sacro); los gluteos van aparte
    zr = np.where(c >= 0, f, b)
    n = 2.6                                                      # superelipse (cadera algo cuadrada)
    rr = 1.0 / ((np.abs(s) / a) ** n + (np.abs(c) / zr) ** n) ** (1.0 / n)
    # gluteos: dos masas redondas atras (x ~ +-7 cm), las mas salidas a y ~ 0.90; la raya entre los dos
    gl = 0.030 * g((y - 0.902) / 0.042) * (g(angdiff(th, 150) / 23) + g(angdiff(th, -150) / 23))
    cleft = 0.012 * g(angdiff(th, 180) / 5.5) * ss(0.975, 0.925, y)
    # costado (trocanter) y frente (bajo vientre liso, el pliegue de la ingle apenas)
    side = 0.007 * g((y - 0.905) / 0.035) * (g(angdiff(th, 90) / 20) + g(angdiff(th, -90) / 20))
    belly = 0.005 * g((y - 0.955) / 0.03) * g(th / 40)
    ingle = 0.004 * ss(0.96, 0.90, y) * (g(angdiff(th, 38 - 400 * (0.95 - y)) / 6) + g(angdiff(th, -(38 - 400 * (0.95 - y))) / 6))
    return rr + gl - cleft + side + belly - ingle

def ring_polar(R, center=(0.0, ZC)):
    th = np.degrees(np.arctan2(R[:, 0] - center[0], R[:, 2] - center[1]))
    rad = np.hypot(R[:, 0] - center[0], R[:, 2] - center[1])
    return th, rad

def periodic_interp(th_q, th, val):
    o = np.argsort(th); th = th[o]; val = val[o]
    thx = np.concatenate([th - 360, th, th + 360]); vx = np.concatenate([val, val, val], 0)
    if vx.ndim == 1: return np.interp(th_q, thx, vx)
    return np.stack([np.interp(th_q, thx, vx[:, k]) for k in range(vx.shape[1])], -1)

# ---------------------------------------------------------------- topologia y posiciones
def _loop_param_interp(Lp, ang, q):
    """posicion sobre el anillo Lp (n x 3, ordenado por angulo creciente 'ang', grados) en los angulos q (lineal, periodico)"""
    return periodic_interp(q, ang, Lp)

def _zipper(A, B, ua, ub):
    """triangulos entre dos anillos cerrados A (ids) y B (ids) con parametros crecientes ua, ub en [0, 1): cierre por avance"""
    na, nb = len(A), len(B)
    # arranque: el de B mas cerca de A[0]
    j0 = int(np.argmin(np.abs(((ub - ua[0] + 0.5) % 1.0) - 0.5)))
    i, j = 0, 0
    ua2 = np.concatenate([ua, ua[:1] + 1.0]); ubr = np.roll(ub, -j0); ubr = np.where(ubr < ubr[0], ubr + 1.0, ubr)
    if ubr[0] > ua2[0] + 0.5: ubr = ubr - 1.0
    ub2 = np.concatenate([ubr, ubr[:1] + 1.0])
    Br = np.roll(np.asarray(B), -j0)
    tris = []
    while i < na or j < nb:
        a0, a1 = A[i % na], A[(i + 1) % na]
        b0, b1 = Br[j % nb], Br[(j + 1) % nb]
        if j >= nb or (i < na and ua2[i + 1] <= ub2[j + 1]):
            tris.append((a0, a1, b0)); i += 1
        else:
            tris.append((a0, b1, b0)); j += 1
    return tris

def build_pelvis(top, legL, legR, torso_mesh=None, counts=(92, 82, 74, 68, 64, 62, 60), leg_counts=(33, 31), nbridge=4,
                 yK=0.876, y_low=0.797):
    """vertices (P), anillos y triangulos de la pelvis. Devuelve dict con P (V x 3), F (tris), y metadatos por vertice:
    kind (0 cuerpo, 1 puente, 2 pierna), s (0 arriba .. 1 abajo en el cuerpo; 1..2 en las piernas), side (+1 izq, -1 der, 0
    medio), ref (indice del vertice del anillo que copia: arriba / muslo, o -1)"""
    P, kind, sv, side, ref_top, ref_leg = [], [], [], [], [], []
    def add(p, k, s, sd, rt=-1, rl=-1):
        P.append(np.asarray(p, float)); kind.append(k); sv.append(s); side.append(sd); ref_top.append(rt); ref_leg.append(rl)
        return len(P) - 1
    K = len(counts) - 1
    th0, r0 = ring_polar(top["P"]); y0 = top["P"][:, 1]
    o = np.argsort(th0)
    # pendiente del torso en el borde (dr/dy, hacia abajo): vertices del torso 1..2.5 cm arriba del borde, por angulo
    slope = np.zeros(len(th0))
    if torso_mesh is not None:
        Pt = torso_mesh["pos"].astype(np.float64)
        tht, rt = ring_polar(Pt)
        for i in range(len(th0)):
            m = (np.abs(angdiff(tht, th0[i])) < 6) & (Pt[:, 1] > y0[i] + 0.008) & (Pt[:, 1] < y0[i] + 0.025)
            if m.sum() >= 3:
                A = np.polyfit(Pt[m, 1] - y0[i], rt[m] - r0[i], 1); slope[i] = -A[0]     # dr por metro hacia abajo
        slope = np.clip(slope, -0.6, 0.6)
    def r_at(th, y, s):
        rt0 = periodic_interp(th, th0, r0); yt0 = periodic_interp(th, th0, y0); sl = periodic_interp(th, th0, slope)
        ext = rt0 + sl * (yt0 - y)                       # el torso seguido hacia abajo
        w = ss(0.0, 0.75, s)
        return ext * (1 - w) + body_radius(th, y) * w
    rows, rows_u = [], []
    # fila 0: el borde del torso (exacto)
    u0 = ((th0 + 360.0) % 360.0) / 360.0
    o = np.argsort(u0)                                           # por parametro creciente desde adelante (th = 0)
    ids = [add(top["P"][i], 0, 0.0, np.sign(top["P"][i, 0]) if abs(top["P"][i, 0]) > 1e-4 else 0, rt=i) for i in o]
    rows.append(ids); rows_u.append(u0[o])
    for k in range(1, K + 1):
        s = k / K
        n = counts[k]
        # angulos: con 0 (adelante) y 180 (atras) en la fila K (n par), espaciado parejo por largo de arco
        thd = np.linspace(-180, 180, 721)[:-1]
        yd = periodic_interp(thd, th0, y0) * (1 - s) + yK * s
        rd = r_at(thd, yd, s)
        pts = np.stack([rd * np.sin(np.radians(thd)), yd, ZC + rd * np.cos(np.radians(thd))], 1)
        seg = np.linalg.norm(np.roll(pts, -1, 0) - pts, axis=1); cum = np.concatenate([[0], np.cumsum(seg)])
        Ltot = cum[-1]; cum = cum[:-1]
        # parametro de arco desde adelante (th = 0)
        i0 = int(np.argmin(np.abs(thd))); cum0 = (cum - cum[i0]) % Ltot
        if k == K:
            half = n // 2
            # izquierda: de 0 a 180 (th creciente), arco hasta atras
            iB = int(np.argmin(np.abs(np.abs(thd) - 180)))
            arcB = cum0[iB] if cum0[iB] > 0 else Ltot / 2
            targ = np.concatenate([np.linspace(0, arcB, half + 1)[:-1], arcB + np.linspace(0, Ltot - arcB, half + 1)[:-1]])
        else:
            targ = np.linspace(0, Ltot, n + 1)[:-1]
        ordr = np.argsort(cum0)
        th_s = np.interp(targ, np.concatenate([cum0[ordr], [Ltot]]), np.concatenate([((thd[ordr] + 360) % 360), [360.0]]))
        th_s = np.where(th_s > 180, th_s - 360, th_s)
        if k == K: th_s[0] = 0.0; th_s[n // 2] = 180.0
        ys = periodic_interp(th_s, th0, y0) * (1 - s) + yK * s
        rs = r_at(th_s, ys, s)
        ps = np.stack([rs * np.sin(np.radians(th_s)), ys, ZC + rs * np.cos(np.radians(th_s))], 1)
        ids = [add(ps[j], 0, s, 0 if abs(th_s[j]) < 1e-6 or abs(abs(th_s[j]) - 180) < 1e-6 else np.sign(ps[j, 0])) for j in range(n)]
        uk = ((th_s + 360.0) % 360.0) / 360.0
        assert np.all(np.diff(uk) > 0), "fila %d sin orden" % k
        rows.append(ids); rows_u.append(uk)
    # puente por debajo de la entrepierna: de B (atras) a F (adelante), en x = 0
    rowK = rows[K]; nK = len(rowK)
    iF, iB = rowK[0], rowK[nK // 2]
    PF, PB = P[iF], P[iB]
    zc_, hz = 0.5 * (PF[2] + PB[2]), 0.5 * (PF[2] - PB[2])
    bridge = []
    for j in range(1, nbridge + 1):
        t = j / (nbridge + 1)
        a = np.pi * t
        y = yK - (yK - y_low) * np.abs(np.sin(a)) ** 0.55
        z = zc_ - hz * np.cos(a) + 0.006 * np.sin(a)             # un poco mas adelante en el medio
        bridge.append(add((0.0, y, z), 1, 1.0, 0))
    # anillos de las piernas
    left_half = rowK[0: nK // 2 + 1]                               # F .. B por la izquierda
    right_half = [rowK[0]] + rowK[nK // 2:][::-1]                  # F, ..., B por la derecha (th de 0 a -180)
    legs_out = {}
    for sd, leg, half in ((+1, legL, left_half), (-1, legR, right_half)):
        c = leg["P"].mean(0)
        def lang(p):                                               # angulo alrededor de la pierna: 0 adelante, + hacia afuera
            p = np.atleast_2d(p)
            return np.degrees(np.arctan2(sd * (p[:, 0] - c[0]), p[:, 2] - c[2]))
        L0 = list(half) + bridge[::-1] if sd > 0 else list(half) + bridge[::-1]
        # orden por angulo creciente alrededor de la pierna; despues todo por largo de arco normalizado desde F (asi los anillos
        # intermedios reparten los vertices como el primero y el ultimo y no quedan abanicos)
        L0 = np.array(L0)
        a0 = lang(np.array([P[i] for i in L0]))
        o0 = np.argsort(a0); L0 = L0[o0]; a0 = a0[o0]
        kF = int(np.where(L0 == iF)[0][0]); L0 = np.roll(L0, -kF); a0 = np.roll(a0, -kF)
        PL0 = np.array([P[i] for i in L0])
        T = leg["P"]; aT = lang(T); oT = np.argsort(aT)
        kT = int(np.argmin(np.abs(angdiff(aT[oT], a0[0])))); oT = np.roll(oT, -kT)
        PT = T[oT]
        def arcpar(Q):
            seg = np.linalg.norm(np.roll(Q, -1, 0) - Q, axis=1); c = np.concatenate([[0], np.cumsum(seg)])
            return c[:-1] / c[-1]
        l0 = arcpar(PL0); lT = arcpar(PT)
        def at(Q, lam, q):
            lx = np.concatenate([lam, [1.0]]); Qx = np.concatenate([Q, Q[:1]], 0)
            return np.stack([np.interp(q, lx, Qx[:, k]) for k in range(3)], -1)
        loops, loops_l = [list(L0)], [l0]
        for li, n in enumerate(leg_counts):
            t = (li + 1) / (len(leg_counts) + 1)
            q = (np.arange(n) + 0.5) / n
            p0 = at(PL0, l0, q); p1 = at(PT, lT, q)
            p = p0 * (1 - t) + p1 * t
            qa = lang(p)
            out = p - np.array([c[0], 0, c[2]]); out[:, 1] = 0; out /= np.maximum(np.linalg.norm(out, axis=1, keepdims=True), 1e-9)
            back = g(angdiff(qa, 180) / 50); outer = g(angdiff(qa, 90) / 50); inner = g(angdiff(qa, -90) / 35)
            bulge = (0.0035 + 0.0075 * back + 0.002 * outer) * (1 - inner) * np.sin(np.pi * t)
            p = p + out * bulge[:, None]
            ids = [add(p[j], 2, 1.0 + t, sd) for j in range(n)]
            loops.append(ids); loops_l.append(q)
        ids = [add(PT[j], 2, 2.0, sd, rl=int(oT[j])) for j in range(len(PT))]
        loops.append(ids); loops_l.append(lT)
        loops_a = loops_l
        legs_out[sd] = (loops, loops_a)
    # triangulos (y de que parte es cada uno: 0 cuerpo, 1 pierna izquierda, 2 pierna derecha)
    F, chart = [], []
    for k in range(K):
        tt = _zipper(rows[k], rows[k + 1], rows_u[k], rows_u[k + 1]); F += tt; chart += [0] * len(tt)
    lam = {+1: {}, -1: {}}
    for sd in (+1, -1):
        loops, la = legs_out[sd]
        for k in range(len(loops)):
            for vid, l in zip(loops[k], la[k]): lam[sd][int(vid)] = float(l)
        for k in range(len(loops) - 1):
            tt = _zipper(list(loops[k]), list(loops[k + 1]), np.asarray(la[k]), np.asarray(la[k + 1]))
            F += tt; chart += [1 if sd > 0 else 2] * len(tt)
    P = np.array(P); F = np.array(F, np.int64); chart = np.array(chart)
    ok = (F[:, 0] != F[:, 1]) & (F[:, 1] != F[:, 2]) & (F[:, 0] != F[:, 2])
    F = F[ok]; chart = chart[ok]
    return dict(P=P, F=F, chart=chart, kind=np.array(kind), s=np.array(sv), side=np.array(side), ref_top=np.array(ref_top),
                ref_leg=np.array(ref_leg), rows=rows, legs=legs_out, bridge=bridge, lam=lam, K=K)

def orient_faces(P, F, ref_out_fn):
    """giro coherente de los triangulos (por aristas compartidas) y signo global: cross(P1 - P0, P2 - P0) hacia ADENTRO, como
    las mallas de TorsoBody (ref_out_fn(p) = direccion hacia afuera aproximada en un punto)"""
    from collections import defaultdict, deque
    F = F.copy()
    edge_faces = defaultdict(list)
    for fi, (a, b, c) in enumerate(F):
        for u, v in ((a, b), (b, c), (c, a)): edge_faces[(min(u, v), max(u, v))].append(fi)
    seen = np.zeros(len(F), bool)
    for start in range(len(F)):
        if seen[start]: continue
        seen[start] = True; dq = deque([start])
        while dq:
            f = dq.popleft(); a, b, c = F[f]
            for u, v in ((a, b), (b, c), (c, a)):
                for g2 in edge_faces[(min(u, v), max(u, v))]:
                    if g2 == f or seen[g2]: continue
                    x, y, z = F[g2]
                    # coherente si la arista compartida va al reves en el otro triangulo
                    same = any((p, q) == (u, v) for p, q in ((x, y), (y, z), (z, x)))
                    if same: F[g2] = F[g2][[0, 2, 1]]
                    seen[g2] = True; dq.append(g2)
    fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    cen = P[F].mean(1)
    out = ref_out_fn(cen)
    if np.sum(np.sum(fn * out, 1) > 0) > len(F) / 2: F = F[:, [0, 2, 1]]   # que apunte hacia adentro
    return F

def outward_dir(c):
    """direccion hacia afuera aproximada (para el signo de los triangulos): radial desde el eje del cuerpo; abajo, hacia abajo"""
    o = np.stack([c[:, 0], np.zeros(len(c)), c[:, 2] - ZC], 1)
    low = c[:, 1] < 0.86
    o[low, 1] -= 0.06
    return o

def vertex_normals_out(P, F):
    n = np.zeros_like(P)
    fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    for k in range(3): np.add.at(n, F[:, k], fn)
    return -n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)   # cross va hacia adentro

def finalize_normals(M, top, legL, legR, F, blend_rows=1):
    """normales: las de la malla; en los bordes, las del anillo del torso / del muslo; la fila siguiente a cada borde mezcla"""
    P = M["P"]
    N = vertex_normals_out(P, F)
    fixed = np.zeros(len(P), bool)
    for i in np.where(M["ref_top"] >= 0)[0]: N[i] = top["N"][M["ref_top"][i]]; fixed[i] = True
    for sd, leg in ((1, legL), (-1, legR)):
        for i in np.where((M["ref_leg"] >= 0) & (M["side"] == sd))[0]: N[i] = leg["N"][M["ref_leg"][i]]; fixed[i] = True
    N /= np.linalg.norm(N, axis=1, keepdims=True)
    # vecinos de los bordes: mitad y mitad con el promedio de sus vecinos fijos
    E = np.concatenate([F[:, [0, 1]], F[:, [1, 2]], F[:, [2, 0]]])
    acc = np.zeros_like(N); cnt = np.zeros(len(P))
    for a, b in E:
        if fixed[b] and not fixed[a]: acc[a] += N[b]; cnt[a] += 1
        if fixed[a] and not fixed[b]: acc[b] += N[a]; cnt[b] += 1
    m = cnt > 0
    N[m] = 0.5 * N[m] + 0.5 * acc[m] / cnt[m][:, None]
    N /= np.linalg.norm(N, axis=1, keepdims=True)
    return N
