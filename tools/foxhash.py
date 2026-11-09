# PES21 FX (07/11) - StrCode de Fox Engine (PES 2021 / MGSV): CityHash64 1.0.3 con semilla 0, recortado a 48 bits; StrCode32 = los
# 32 bits bajos. Sirve para nombrar los hashes de huesos que la DLL ve en el motor (volcado "boca por huesos") y los de .mtar / .frig.
# Uso: foxhash.py nombre [nombre...]  |  foxhash.py --prueba  |  foxhash.py --buscar HEX [HEX...] (contra la lista de huesos conocidos)
import sys

M = 0xFFFFFFFFFFFFFFFF
k0 = 0xc3a5c85c97cb3127; k1 = 0xb492b66fbe98f273; k2 = 0x9ae16a3b2f90404f; k3 = 0xc949d7c7509e6557

def rot(v, n): return v if n == 0 else ((v >> n) | (v << (64 - n))) & M
def rot1(v, n): return ((v >> n) | (v << (64 - n))) & M   # RotateByAtLeast1
def f64(b, i): return int.from_bytes(b[i:i + 8], 'little')
def f32(b, i): return int.from_bytes(b[i:i + 4], 'little')
def smix(v): return v ^ (v >> 47)

def h16(u, v):   # Hash128to64
    km = 0x9ddfea08eb382d69
    a = ((u ^ v) * km) & M; a ^= a >> 47
    b = ((v ^ a) * km) & M; b ^= b >> 47
    return (b * km) & M

def h0to16(b):
    n = len(b)
    if n > 8:
        a = f64(b, 0); c = f64(b, n - 8)
        return h16(a, rot1((c + n) & M, n)) ^ c
    if n >= 4:
        a = f32(b, 0)
        return h16((n + (a << 3)) & M, f32(b, n - 4))
    if n > 0:
        a = b[0]; c = b[n >> 1]; d = b[n - 1]
        y = (a + (c << 8)) & M; z = (n + (d << 2)) & M
        return (smix(((y * k2) & M) ^ ((z * k3) & M)) * k2) & M
    return k2

def h17to32(b):
    n = len(b)
    a = (f64(b, 0) * k1) & M; bb = f64(b, 8); c = (f64(b, n - 8) * k2) & M; d = (f64(b, n - 16) * k0) & M
    return h16((rot((a - bb) & M, 43) + rot(c, 30) + d) & M, (a + rot((bb ^ k3) & M, 20) - c + n) & M)

def weak(w, x, y, z, a, b):
    a = (a + w) & M; b = rot((b + a + z) & M, 21)
    c = a; a = (a + x) & M; a = (a + y) & M; b = (b + rot(a, 44)) & M
    return ((a + z) & M, (b + c) & M)

def weakb(b, i, a, bb): return weak(f64(b, i), f64(b, i + 8), f64(b, i + 16), f64(b, i + 24), a, bb)

def h33to64(b):
    n = len(b)
    z = f64(b, 24)
    a = (f64(b, 0) + ((n + f64(b, n - 16)) & M) * k0) & M
    bb = rot((a + z) & M, 52); c = rot(a, 37)
    a = (a + f64(b, 8)) & M; c = (c + rot(a, 7)) & M
    a = (a + f64(b, 16)) & M
    vf = (a + z) & M; vs = (bb + rot(a, 31) + c) & M
    a = (f64(b, 16) + f64(b, n - 32)) & M; z = f64(b, n - 8)
    bb = rot((a + z) & M, 52); c = rot(a, 37)
    a = (a + f64(b, n - 24)) & M; c = (c + rot(a, 7)) & M
    a = (a + f64(b, n - 16)) & M
    wf = (a + z) & M; ws = (bb + rot(a, 31) + c) & M
    r = smix((((vf + ws) & M) * k2 + ((wf + vs) & M) * k0) & M)
    return (smix((r * k0 + vs) & M) * k2) & M

def city64(b):
    n = len(b)
    if n <= 16: return h0to16(b)
    if n <= 32: return h17to32(b)
    if n <= 64: return h33to64(b)
    x = f64(b, 0); y = f64(b, n - 16) ^ k1; z = f64(b, n - 56) ^ k0
    v = weakb(b, n - 64, n, y); w = weakb(b, n - 32, (n * k1) & M, k0)
    z = (z + smix(v[1]) * k1) & M
    x = (rot((z + x) & M, 39) * k1) & M; y = (rot(y, 33) * k1) & M
    rest = (n - 1) & ~63; i = 0
    while True:
        x = (rot((x + y + v[0] + f64(b, i + 16)) & M, 37) * k1) & M
        y = (rot((y + v[1] + f64(b, i + 48)) & M, 42) * k1) & M
        x ^= w[1]; y ^= v[0]
        z = rot(z ^ w[0], 33)
        v = weakb(b, i, (v[1] * k1) & M, (x + w[0]) & M)
        w = weakb(b, i + 32, (z + w[1]) & M, y)
        z, x = x, z
        i += 64; rest -= 64
        if rest == 0: break
    return h16((h16(v[0], w[0]) + smix(y) * k1 + z) & M, (h16(v[1], w[1]) + x) & M)

def city64seeds(b, s0, s1): return h16((city64(b) - s0) & M, s1)
def strcode(s):   # Fox StrCode (estilo GzsTool): el texto con NUL final, semilla 1 = (primer byte << 16) + largo (con el NUL), 48 bits
    b = s.encode('utf-8')
    seed1 = ((b[0] << 16) + len(b)) if b else 0   # largo SIN el NUL
    return city64seeds(b + b'\0', k2, seed1) & 0xFFFFFFFFFFFF
def strcode32(s): return strcode(s) & 0xFFFFFFFF

CONOCIDOS = {'run_2_0': 1914981404, 'kick_long_4_0_inside_y0_000_punch': 1649381241}   # (pes-gani) ; en el motor sk_head = FF3D8D77 (ver --buscar)
HUESOS = ['sk_chest', 'sk_neck', 'sk_head', 'dsk_neckback', 'dsk_scm', 'dsk_clavicle_l', 'dsk_clavicle_r', 'skf_head', 'skf_jaw',
          'skf_brow_i_l', 'skf_brow_o_l', 'skf_brow_i_r', 'skf_brow_o_r', 'skf_glabella', 'skf_cheek_l', 'skf_cheek_r', 'skf_cheek_s_l',
          'skf_cheek_s_r', 'skf_orbicularisoculi_b_l', 'skf_orbicularisoculi_b_r', 'skf_eyelid_t_l', 'skf_eyelid_b_l', 'skf_eyelid_t_r',
          'skf_eyelid_b_r', 'skf_nose', 'skf_nosewing_l', 'skf_nosewing_r', 'skf_nasolabialfold_t_l', 'skf_nasolabialfold_t_r',
          'skf_lip_t_l', 'skf_lip_t_c', 'skf_lip_t_r', 'skf_lip_volume', 'skf_lip_s_l', 'skf_lip_s_r', 'skf_lip_b_l', 'skf_lip_b_r',
          'skf_lip_b_c', 'skf_doublechin', 'skf_eye_l', 'skf_eye_r', 'sk_eye_l', 'sk_eye_r', 'skf_eyeball_l', 'skf_eyeball_r',
          'skf_eyelash_t_l', 'skf_eyelash_t_r', 'skf_tongue', 'skf_teeth_b', 'skf_teeth_t', 'skf_ear_l', 'skf_ear_r', 'skf_hair', 'sk_hair',
          'dsk_hip', 'sk_root_hip', 'root_hip', 'sk_belly', 'sk_thigh_l', 'sk_thigh_r', 'sk_foot_l', 'sk_foot_r', 'sk_hand_l', 'sk_hand_r']

if __name__ == '__main__':
    args = sys.argv[1:]
    if args and args[0] == '--buscar':
        quiero = {int(h, 16) for h in args[1:]}
        for n in HUESOS:
            if strcode32(n) in quiero: print('%08X = %s' % (strcode32(n), n))
    elif args and args[0] == '--prueba':
        for n, h in CONOCIDOS.items():
            c = strcode32(n); print('%-10s esperado %08X  calculado %08X  %s' % (n, h, c, 'OK' if c == h else 'NO'))
    else:
        for n in args: print('%s  StrCode %012X  StrCode32 %08X' % (n, strcode(n), strcode32(n)))
