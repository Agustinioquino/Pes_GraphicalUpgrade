# PES21 FX (31/10 b) - el torso achicado (torso_shrink.py) para las caras con cuerpo propio (Dream Patch 21 Faces: face\real\<id>\#Win\
# con pants_*_sub.fmdl / arm*.fmdl; tambien nocloth y otras subcarpetas). Recorre la raiz, achica cada carpeta que tenga torso y,
# con --aplicar, guarda los originales en <backup>\<ruta relativa> y los reemplaza. Sin --aplicar solo prueba (sale en <tmp>).
# Con Agustin de acuerdo (31/10): primero 2-3 caras con --ids.
# Uso: python torso\shrink_faces.py <raiz (...\face\real)> <backup> <tmp> [--ids 60701,130761] [--mm 9,14,5,5] [--aplicar]
import sys, os, shutil, argparse, traceback, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE]
import torso_shrink

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("raiz"); ap.add_argument("backup"); ap.add_argument("tmp")
    ap.add_argument("--ids", default=""); ap.add_argument("--mm", default="9,14,5,5"); ap.add_argument("--aplicar", action="store_true")
    a = ap.parse_args()
    mm = [float(x) for x in a.mm.split(",")]
    ids = set(x.strip() for x in a.ids.split(",") if x.strip())
    names = set(n + ".fmdl" for n in torso_shrink.TORSO + torso_shrink.ARMS)
    ok = fail = skip = 0
    for dp, dn, fn in os.walk(a.raiz):
        mine = [f for f in fn if f.lower() in names]
        if not any(f.lower()[:-5] in torso_shrink.TORSO for f in mine): continue
        rel = os.path.relpath(dp, a.raiz)
        if ids and not (set(rel.replace("\\", "/").split("/")) & ids): skip += 1; continue
        out = os.path.join(a.tmp, rel)
        print("== %s (%s)" % (rel, ", ".join(sorted(mine))))
        try:
            if os.path.isdir(out): shutil.rmtree(out)
            torso_shrink.main(dp, out, *mm)
            done = [f for f in os.listdir(out) if f.endswith(".fmdl")]
            for f in done:
                if os.path.getsize(os.path.join(out, f)) != os.path.getsize(os.path.join(dp, f)): raise RuntimeError("%s cambio de tamano" % f)
            if a.aplicar:
                bk = os.path.join(a.backup, rel); os.makedirs(bk, exist_ok=True)
                for f in done:
                    if not os.path.exists(os.path.join(bk, f)): shutil.copy2(os.path.join(dp, f), os.path.join(bk, f))
                    shutil.copyfile(os.path.join(out, f), os.path.join(dp, f))
                print("   aplicado (%d archivos, backup en %s)" % (len(done), bk))
            ok += 1
        except Exception as e:
            fail += 1; print("   FALLO: %s" % e); traceback.print_exc(limit=2)
    print("listo: %d carpetas %s, %d fallaron, %d fuera de --ids" % (ok, "aplicadas" if a.aplicar else "probadas", fail, skip))

if __name__ == "__main__":
    main()
