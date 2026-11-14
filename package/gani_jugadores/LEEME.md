# Gestos propios por jugador (08/11)

Todo va en `PES21FX\anim\face\` (copiar esta carpeta tal cual, el juego cerrado):

- `gestos.txt`: gesto propio -> clip del juego que lo dispara.
- `7511\sonrisa.gani`, `7511\risa.gani`: la sonrisa de Messi pasada a huesos desde la escultura de Blender (messi7.blend, keys `sonrisa` y `sonrisa_grande`) sumada a la sonrisa general del mod. Cuando el juego pone `smil_soft` / `smil_mho` en Messi, la DLL lo convierte en esta. En el editor, con el slider Sonrisa también.
- `todos\entre_respirar.gani` (5 s, respira por la nariz), `todos\quieto_cachetes.gani` (1.8 s, infla los cachetes), `todos\arranque_soplar.gani` (1.3 s, sopla y entrecierra los ojos). Para cualquier jugador. `entre_` sale cada 4..12 s con la cara quieta; `quieto_` solo si lleva 1.5 s parado; `arranque_` al arrancar a correr después de estar quieto; `corriendo_` (no hay ejemplo) corriendo a más de 3.5 m/s.

Agregar jugadores: carpeta con su id (el de `face\real\<id>`). Agregar gestos: un .gani con el nombre y, si es un gesto nuevo, su línea en gestos.txt. La DLL relee cada 30 s.

Necesita la DLL de `claude/project-thread-vamwp7` (gani.cpp + exprbones.cpp, 08/11). Log: `gani: jugador <id>: N gestos propios`, `gani: jugador <id> hace 'sonrisa'`.

Herramientas: `tools/gani_jugador.py` (shape keys -> clip del jugador), `tools/gani_gestos.py --entre` (los tres de ejemplo).
Boca: solo huesos skf_, mandíbula con tope 19°. No se toca nada de la boca.

## eye_anime_file0.mtar (08/11, leído de la PC)

256 clips de 5 canales: raíz, una unidad con giro + **posición en cm (7.3, -266, 0) = el punto al que miran los ojos**, a 2.7 m adelante, con hasta 193 claves, y un giro más. Es la mirada de las escenas (los `*_eyes_target`), no el parpadeo. El parpadeo está en `Face\Animations\Add\add_eyeblink_single.gani`, un clip que no se reemplaza (regla). Así que por livecpk no hay dónde meter el parpadeo del mod: sigue por la DLL o por shader. Un escritor de .mtar es trivial (tabla de 16 B por clip + los .gani pegados) pero no tiene uso hoy.
