import sys, glob, os, json
sys.path.insert(0,'/home/user/Pes_GraphicalUpgrade/tools')
import vista_juego as V
from PIL import Image, ImageDraw, ImageFont
P=V.leer_pista(os.environ['INC']); VS=json.loads(os.environ['VISTAS'])
def act(L,f):
    k=[x for x in P[L] if x[0]<=f]; return k[-1] if k else P[L][0]
os.makedirs('vj/rot', exist_ok=True)
F='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
f1 = ImageFont.truetype(F, 17); f2 = ImageFont.truetype(F, 15); f3 = ImageFont.truetype(F, 14)
t = os.environ.get('TITULO','')
for p in sorted(glob.glob('vj/cuadros/*.png')):
    f=int(os.path.basename(p)[:4]); im = Image.open(p).convert('RGB'); d = ImageDraw.Draw(im); W,H=im.size
    # marcos de las vistas chicas y sus nombres
    for v in VS:
        x,y,w,h=[v['r'][0]*W, v['r'][1]*H, v['r'][2]*W, v['r'][3]*H]
        if v['r'][2] < 0.9 and v['r'][0] > 0: d.rectangle([x-2,y-2,x+w,y+h], outline=(40,40,40), width=2)
        d.text((x+8, y+ (30 if v['r'][1] == 0 else 6)), v['n'], font=f3, fill=(70,70,70))
    d.rectangle([0, 0, W, 26], fill=(30, 30, 30)); d.text((8, 3), t + '   cuadro %d  (%.1f s)' % (f, f/30), font=f1, fill=(240, 240, 240))
    wm = int(VS[0]['r'][2]*W)
    for L,y,col in (('A',H-46,(235,90,80)),('B',H-24,(110,140,255))):
        k=act(L,f); d.rectangle([0,y-2,wm-2,y+21],fill=(25,25,25))
        d.text((8,y), '%s  cara: %s   manos: %s / %s' % ('Rojo' if L=='A' else 'Azul', k[1], k[3], k[4]), font=f2, fill=col)
    im.save('vj/rot/%04d.png'%f)
