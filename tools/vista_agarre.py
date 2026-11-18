# busca la mejor forma de calzar dos manos derechas (agarre con el pulgar arriba) midiendo con las mallas del juego
import sys, math, itertools, numpy as np
sys.path.insert(0, '/home/user/Pes_GraphicalUpgrade/tools'); sys.path.insert(0, '/home/user/Pes_GraphicalUpgrade/tools/fox')
import vista_juego as V
G = V.G
B = '/tmp/claude-0/mb2/modelo_base'; CM = '/tmp/claude-0/cm/common/anime/FoxAnim'
rig = V.Rig(CM, B, None); bx = V.bind_extra(rig, '/tmp/claude-0/prev/7511_face_high.fmdl')
ms = V.pieza(B + '/naked_hand_r.fmdl')
HR = [V.BI['handR']] + [V.BI['hR:' + n] for n in V.HAND_ASK]
for m in ms: m['bi'] = np.asarray(m['bi']); m['bw'] = np.asarray(m['bw'])
def pose(k, a='open_full_half', b='handshake_normal1'):
    return V.mezcla([(1 - k, rig.clip('hand', a).muestra(0)), (k, rig.clip('hand', b).muestra(0))])
def mano(p, q, ps):
    W = {'handR': [np.array(p, float), np.array(q, float)]}; ex = V.dedos_mundo(rig, W, 1.0, 'R', ps)
    mats = np.zeros((V.NB, 12), np.float32)
    for b in ['handR'] + list(ex):
        if b in W: pp, pq = W[b]; Rb_ = V.qmat3(V.RB[b]); bp = np.array(V.BIND[b])
        else: pp, pq = ex[b]; bp, Rb_ = bx[b]
        M = V.qmat3(tuple(pq)) @ np.asarray(Rb_).T; t = np.asarray(pp) - M @ bp
        mats[V.BI[b]] = np.concatenate([M, t[:, None]], 1).T.reshape(-1)
    P = []; Nn = []
    for m in ms:
        sel = np.isin(m['bi'][:, 0].astype(int), HR) & (m['bw'][:, 0] > 0.5)
        p_, n_ = V.piel(m, mats.reshape(-1)); P.append(p_[sel]); Nn.append(n_[sel])
    return np.concatenate(P), np.concatenate(Nn)
def q_de(dedos, palma):   # mano derecha: x local = -dedos, y local = -normal de la palma
    x = -np.array(dedos, float); x /= np.linalg.norm(x); y = -np.array(palma, float); y = y - x * np.dot(y, x); y /= np.linalg.norm(y); z = np.cross(x, y)
    return np.array(G.qmat(tuple(x), tuple(y), tuple(z)))
def par(t, a, s, u, k, tw=0.0):
    """mano A: palma hacia +X, dedos arriba y adelante (+Z) con angulo a; centro de la palma en (-t, u, -s) aprox; B = A girada 180 en Y"""
    ar = math.radians(a); d = np.array([0.0, math.cos(ar), math.sin(ar)]); n = np.array([1.0, 0, 0])
    if tw: c_, s_ = math.cos(math.radians(tw)), math.sin(math.radians(tw)); n = np.array([c_, 0, s_]); d = d - n * np.dot(d, n); d /= np.linalg.norm(d)
    th = np.cross(n, d)                       # eje del pulgar en el plano de la palma
    c = -t * n - s * d + u * th; q = q_de(d, n); w = c - d * G.HANDO
    ps = pose(k)
    Pa, Na = mano(w, q, ps)
    Ry = np.diag([-1.0, 1.0, -1.0]); Pb = Pa @ Ry.T; Nb = Na @ Ry.T
    return Pa, Na, Pb, Nb, (w, q)
def medir(Pa, Na, Pb, Nb):
    d2 = ((Pa[:, None, :] - Pb[None, :, :]) ** 2).sum(2); j = d2.argmin(1); dm = np.sqrt(d2[np.arange(len(Pa)), j])
    pen = -np.einsum('vr,vr->v', Pa - Pb[j], Nb[j]); pen[dm > 0.03] = 0
    return float(pen.max()), float((pen > 0.004).mean()), float(dm.min()), float((dm < 0.008).mean())
if __name__ == '__main__':
    Pa, Na, Pb, Nb, _ = par(0.0, 43, 0.0, 0.0, 1.0)
    print('vertices mano', len(Pa), 'actual (centros juntos): pen max %.1f mm, %.0f%% de vertices > 4 mm adentro, dist min %.1f mm, cerca %.0f%%' % ((lambda r: (r[0]*1000, r[1]*100, r[2]*1000, r[3]*100))(medir(Pa, Na, Pb, Nb))))
    res = []
    for t, a, s, u, k in itertools.product((0.006, 0.010, 0.014, 0.018, 0.022), (30, 42, 54), (-0.02, 0.0, 0.02, 0.04), (-0.02, 0.0, 0.02), (0.5, 0.8, 1.0)):
        Pa, Na, Pb, Nb, _ = par(t, a, s, u, k); pm, pf, dm, cf = medir(Pa[::2], Na[::2], Pb, Nb)
        res.append((pm, pf, dm, cf, t, a, s, u, k))
    # menos penetracion con mas contacto
    res.sort(key=lambda r: r[0] * 1000 + r[1] * 300 - r[3] * 60 - r[8] * 2)
    for r in res[:14]: print('pen %.1f mm (%.0f%%) dist min %.1f mm cerca %.0f%% | t %.3f ang %d s %.2f u %.2f dedos %.1f' % (r[0]*1000, r[1]*100, r[2]*1000, r[3]*100, *r[4:]))
