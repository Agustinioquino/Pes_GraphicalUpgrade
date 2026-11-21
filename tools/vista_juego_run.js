// node run_vj.js <carpeta> <cuadros separados por coma | todos> <paso>
// Dibuja la escena de vista_juego.py en Chromium sin ventana (WebGL por software) y guarda PNG por cuadro.
const { chromium } = require('playwright'); const fs = require('fs'); const path = require('path');
(async () => {
  const dir = process.argv[2], which = process.argv[3] || '0', step = +(process.argv[4] || 2);
  const b = await chromium.launch({ args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader'] });
  const p = await b.newPage({ viewport: { width: 960, height: 460 }, deviceScaleFactor: 1 });
  p.on('console', m => console.log('page:', m.text())); p.on('pageerror', e => console.log('ERR', e.message));
  await p.route('http://vista.local/**', r => {
    const f = path.join(dir, decodeURIComponent(new URL(r.request().url()).pathname));
    const ct = f.endsWith('.png') ? 'image/png' : 'text/html';
    r.fulfill({ status: 200, contentType: ct, body: fs.readFileSync(f) });
  });
  await p.goto('http://vista.local/vista_juego.html');
  const tam = (process.env.TAM || '960x460').split('x').map(Number);   // tamano del cuadro (4 vistas: 1280x720)
  await p.setViewportSize({ width: tam[0], height: tam[1] }); await p.evaluate(([w, h]) => { const c = document.getElementById('c'); c.width = w; c.height = h; }, tam);
  const sc = JSON.parse(fs.readFileSync(path.join(dir, 'escena.json')));
  console.log('mallas', await p.evaluate(async s => await window.init(s, '/'), sc));
  const n = sc.jugadores[0].n;
  const fr = which === 'todos' ? [...Array(Math.floor(n / step) + 1).keys()].map(i => i * step) : which.split(',').map(Number);
  const out = path.join(dir, process.env.SALIDA || 'cuadros'); fs.mkdirSync(out, { recursive: true });
  const views = JSON.parse(process.env.VISTAS || '[{"yaw":35,"pitch":-12,"dist":7.6,"fov":34},{"yaw":90,"pitch":-4,"dist":8.2,"fov":34}]');
  if (process.env.SNAP) await p.evaluate(() => { window._snap = true; });   // camara directo al medio (cuadros sueltos)
  for (const f of fr) {
    const url = await p.evaluate(([f, v]) => { window.draw(f, v); return document.getElementById('c').toDataURL('image/png'); }, [f, views]);
    fs.writeFileSync(path.join(out, String(f).padStart(4, '0') + '.png'), Buffer.from(url.split(',')[1], 'base64'));
  }
  await b.close(); console.log('listo', fr.length);
})();
