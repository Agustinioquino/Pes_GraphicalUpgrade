# PES21 FX (31/10) - expresiones faciales, paso 1: que huesos tiene la cara y como los ve el vertex shader.
# El VS de la cabeza (faab87ee) y el de la cara animada (455eb97b) reciben 32 matrices (cbuffer b6, VSBones): la paleta
# de cada malla es su "grupo de huesos" del fmdl. Para exagerar / suavizar la expresion en el VS hace falta saber, por malla,
# en que lugar de la paleta esta el hueso de la cabeza (referencia rigida) y cuales son los huesos de la cara (cejas,
# parpados, labios, mandibula, mejillas, ojos). Este script lo lista para una o varias caras y compara si el orden es el
# mismo en todas (si lo es, el indice va fijo en el shader; si no, la DLL tiene que pasarlo por dibujo).
# Solo lee. Uso:
#   python face_rig.py <archivo.fmdl | carpeta> [mas ...] [--max 6] [--json salida.json]
# Con una carpeta recorre las subcarpetas y toma los face*.fmdl / *.fmdl con "face", "eye", "mouth", "oral" en el nombre.
import sys, os, re, json, argparse, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [os.path.join(HERE, "..")]
import numpy as np
import fmdl

CLASES = [   # (clase, regex sobre el nombre del hueso en minusculas); la primera que coincide
    ("cabeza",    r"(^|_)head|skl_00[0-9]_?head"),
    ("cuello",    r"neck|nape"),
    ("mandibula", r"jaw|chin|mandib"),
    ("lengua",    r"tongue|tng"),
    ("dientes",   r"teeth|tooth"),
    ("ojo",       r"(^|_)eye(ball)?(_|$)|eye_?[lr]|pupil|iris"),
    ("parpado",   r"lid|blink|lash"),
    ("ceja",      r"brow|forehead|glabel"),
    ("labio",     r"lip|mouth|oral|corner|smile"),
    ("mejilla",   r"cheek|zyg|malar|nasolab"),
    ("nariz",     r"nose|nostril|nasal"),
    ("oreja",     r"ear"),
    ("cara",      r"face|fc_|^f_|^skf_|^dskf_"),
]
def clase(nombre):
    n = nombre.lower()
    for c, rx in CLASES:
        if re.search(rx, n): return c
    return ""

def buscar(rutas):
    out = []
    for r in rutas:
        if os.path.isfile(r): out.append(r); continue
        for dp, dn, fn in os.walk(r):
            for f in sorted(fn):
                fl = f.lower()
                if fl.endswith(".fmdl") and any(k in fl for k in ("face", "eye", "mouth", "oral", "head")):
                    out.append(os.path.join(dp, f))
    return out

def analizar(path):
    f = fmdl.Fmdl(path)
    huesos = []
    for i, b in enumerate(f.bones):
        p = b["parent"]
        huesos.append({"i": i, "nombre": b["name"], "padre": f.bones[p]["name"] if 0 <= p < len(f.bones) else "",
                       "clase": clase(b["name"]), "world": [round(x, 4) for x in b["world"][:3]]})
    mallas = []
    try: ms = fmdl.meshes(f)
    except Exception as e: ms = []; print("  (no se pudieron leer las mallas: %s)" % e)
    for k, m in enumerate(ms):
        grp = f.groups[m["group"]] if m["group"] < len(f.groups) else []
        pal = []
        cnt = collections.Counter(); wmax = collections.defaultdict(float)
        if m["idx"] is not None and m["w"] is not None:
            for j in range(4):
                for bid, w in zip(m["idx"][:, j], m["w"][:, j]):
                    if w > 0.02: cnt[int(bid)] += 1; wmax[int(bid)] = max(wmax[int(bid)], float(w))
        for li, bid in enumerate(grp):
            nm = f.bones[bid]["name"] if bid < len(f.bones) else "?%d" % bid
            pal.append({"local": li, "hueso": bid, "nombre": nm, "clase": clase(nm), "vertices": cnt.get(bid, 0), "peso_max": round(wmax.get(bid, 0.0), 3)})
        cab = [p["local"] for p in pal if p["clase"] == "cabeza"]
        uvb = None
        if m["uv"] is not None and len(m["uv"]): uvb = [round(float(x), 3) for x in (*m["uv"].min(0), *m["uv"].max(0))]
        posb = None
        if m["pos"] is not None and len(m["pos"]): posb = [round(float(x), 3) for x in (*m["pos"].min(0), *m["pos"].max(0))]
        mallas.append({"malla": k, "grupo": m["group"], "vertices": m["nvert"], "material": m["mat"], "paleta": pal,
                       "cabeza_local": cab, "uv_bbox": uvb, "pos_bbox": posb})
    return {"archivo": path, "huesos": huesos, "mallas": mallas}

def imprimir(r, maxm):
    print("=" * 100); print(r["archivo"])
    hs = r["huesos"]
    print("%d huesos" % len(hs))
    for h in hs:
        print("  %3d %-28s padre %-24s %-10s %s" % (h["i"], h["nombre"], h["padre"], h["clase"], h["world"]))
    for m in r["mallas"][:maxm] if maxm else r["mallas"]:
        print("-- malla %d: %d vertices, grupo %d, material %d, uv %s, pos %s" % (m["malla"], m["vertices"], m["grupo"], m["material"], m["uv_bbox"], m["pos_bbox"]))
        print("   cabeza en la paleta: %s" % (m["cabeza_local"] or "NO"))
        for p in m["paleta"]:
            print("   [%2d] %-28s %-10s vertices %5d  peso max %.2f" % (p["local"], p["nombre"], p["clase"], p["vertices"], p["peso_max"]))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rutas", nargs="+"); ap.add_argument("--max", type=int, default=0, help="mallas por archivo (0 = todas)")
    ap.add_argument("--json", default=""); ap.add_argument("--limite", type=int, default=40, help="archivos como maximo")
    a = ap.parse_args()
    arch = buscar(a.rutas)[:a.limite]
    if not arch: print("no encontre fmdl de cara en", a.rutas); return
    res = []
    for p in arch:
        try: r = analizar(p)
        except Exception as e: print("ERROR", p, e); continue
        imprimir(r, a.max); res.append(r)
    # resumen: el orden de la paleta (nombres) por malla, es el mismo en todas las caras?
    print("=" * 100); print("RESUMEN (%d archivos)" % len(res))
    firmas = collections.Counter()
    for r in res:
        base = os.path.basename(r["archivo"]).lower()
        for m in r["mallas"]:
            firmas[(base, m["malla"], tuple(p["nombre"] for p in m["paleta"]))] += 1
    por = collections.defaultdict(list)
    for (base, k, sig), n in firmas.items(): por[(base, k)].append((n, sig))
    for (base, k), lst in sorted(por.items()):
        lst.sort(reverse=True)
        n0, sig0 = lst[0]
        cab = [i for i, nm in enumerate(sig0) if clase(nm) == "cabeza"]
        print("%s malla %d: %d variante(s) de paleta; la mas comun (%d caras) tiene %d huesos, cabeza en %s" % (base, k, len(lst), n0, len(sig0), cab or "NO"))
        clases = collections.Counter(clase(nm) or "otro" for nm in sig0)
        print("   clases:", dict(clases))
    if a.json:
        with open(a.json, "w", encoding="utf-8") as fo: json.dump(res, fo, ensure_ascii=False, indent=1)
        print("json:", a.json)

if __name__ == "__main__":
    main()
