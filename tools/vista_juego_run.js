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
  const sc = JSON.parse(fs.readFileSync(path.join(dir, 'escena.json')));
  console.log('mallas', await p.evaluate(async s => await window.init(s, '/'), sc));
  const n = sc.jugadores[0].n;
  const fr = which === 'todos' ? [...Array(Math.floor(n / step) + 1).keys()].map(i => i * step) : which.split(',').map(Number);
  const out = path.join(dir, process.env.SALIDA || 'cuadros'); fs.mkdirSync(out, { recursive: true });
  const views = JSON.parse(process.env.VISTAS || '[{"yaw":35,"pitch":-12,"dist":7.6,"fov":34},{"yaw":90,"pitch":-4,"dist":8.2,"fov":34}]');
  for (const f of fr) {
    const url = await p.evaluate(([f, v]) => { window.draw(f, v); return document.getElementById('c').toDataURL('image/png'); }, [f, views]);
    fs.writeFileSync(path.join(out, String(f).padStart(4, '0') + '.png'), Buffer.from(url.split(',')[1], 'base64'));
  }
  await b.close(); console.log('listo', fr.length);
})();
