# PES21 FX (08/11, Agustin: "como aplicamos expresiones custom para cada jugador") - clip .gani PROPIO de un jugador a partir de las shape keys
# esculpidas en Blender (sonrisa de Messi: /mnt/project-files/face/mallas_messi/messi7.blend). La escultura es un correctivo por vertice que
# el juego suma sobre los huesos; aca se proyecta a los huesos de la cara (media ponderada de los corrimientos de los vertices cercanos a
# cada hueso, gaussiana de 12 mm) y se suma al gesto general del mod -> un .gani que la DLL reproduce solo para ese jugador
# (PES21FX\anim\face\<id>\sonrisa.gani). Lo que no entra en huesos (arrugas, forma fina) lo sigue poniendo la escultura por shader.
#
# Uso: gani_jugador.py shapekeys.npz carpeta_konami salida/   (npz: basis + una entrada por shape key, ejes de Blender: x, -adelante, arriba)
import sys, os, math, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gani_gestos as GG

REST = {   # kDef (exprbones.cpp): reposo en el espacio del modelo (x izquierda del jugador, y arriba, z adelante), metros
    0: (0, 1.6369, 0.0795), 1: (0.0185, 1.6244, 0.1641), 2: (-0.0185, 1.6244, 0.1641), 3: (0, 1.6220, 0.1728), 5: (0.0236, 1.6297, 0.1615),
    6: (-0.0236, 1.6297, 0.1615), 7: (0.0479, 1.6215, 0.1396), 8: (-0.0479, 1.6215, 0.1396), 9: (0.0155, 1.6342, 0.1720), 10: (0, 1.6354, 0.1772),
    11: (-0.0155, 1.6342, 0.1720), 13: (0, 1.6863, 0.1763), 14: (0.0153, 1.6603, 0.1765), 15: (-0.0153, 1.6603, 0.1765), 16: (0.0220, 1.6493, 0.1656),
    17: (-0.0220, 1.6493, 0.1656), 18: (0.0425, 1.6623, 0.1526), 19: (-0.0425, 1.6623, 0.1526), 20: (0.0280, 1.6866, 0.1518), 21: (-0.0280, 1.6866, 0.1518),
    26: (0.0157, 1.7129, 0.1718), 27: (-0.0157, 1.7129, 0.1718), 28: (0.0413, 1.7149, 0.1573), 29: (-0.0413, 1.7149, 0.1573), 30: (0, 1.7038, 0.1697),
}
SIGMA = 0.012

def blender_a_juego(v): return np.stack([v[:, 0], v[:, 2], -v[:, 1]], 1)

def ajustar(basis, key, verbose=True):
    """{d: corrimiento (m, ejes de la cabeza)} que mejor sigue la shape key"""
    P = blender_a_juego(basis); D = blender_a_juego(key) - P; out = {}
    for d, r in REST.items():
        w = np.exp(-((P - np.array(r)) ** 2).sum(1) / (2 * SIGMA * SIGMA))
        if w.sum() < 1e-3: continue
        o = (D * w[:, None]).sum(0) / w.sum()
        if np.abs(o).max() < 1e-4: continue
        out[d] = tuple(float(x) for x in o)
        if verbose: print('  %-26s %+5.2f %+5.2f %+5.2f mm (peso %.1f)' % (GG.KDEF[d], o[0] * 1000, o[1] * 1000, o[2] * 1000, w.sum()))
    return out

def clip(rig, mv_extra, **sliders):
    mv, A, up, lo = GG.pose(**sliders)
    for d, o in mv_extra.items():
        a = mv.setdefault(d, [0.0, 0.0, 0.0])
        for i in range(3): a[i] += o[i]
    return GG.sostenido(rig, rig.pistas_de(mv, A, up, lo))

def main():
    npz, carpeta, salida = sys.argv[1:4]; os.makedirs(salida, exist_ok=True)
    z = np.load(npz); rig = GG.Rig(carpeta)
    for nombre, key, sl in [('sonrisa', 'sonrisa', dict(smi=1.0)), ('risa', 'sonrisa_grande', dict(smi=1.4))]:
        if key not in z: continue
        print(nombre, '<-', key)
        ex = ajustar(z['basis'], z[key])
        b = clip(rig, ex, **sl); open(os.path.join(salida, nombre + '.gani'), 'wb').write(b); print('  ->', nombre + '.gani', len(b), 'B,', len(ex), 'huesos con correctivo')

if __name__ == '__main__': main()
