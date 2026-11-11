#!/usr/bin/env python3
"""Simulador de MotorTiempoReal.lua (modulo de Sider) fuera del juego. Necesita python3 + lupa + numpy.

OJO: esto prueba la LOGICA del modulo y que aguante memoria rota; NO valida los offsets del motor, porque la memoria
falsa se arma con lo que el autor leyo en el decompilado. La 0.2 pasaba todo aca y no habria listado nada en el juego:
las escenas se armaban colgando los objetos de escena+0x38, que en el motor es solo la cola de altas. Desde la 0.2.1 las
escenas se arman como las deja el motor (leido en FUN_1416916b0 ctor, FUN_1416927c0 alta, FUN_1416928b0 vaciado por
cuadro, FUN_141692420 baja): cola de altas + una lista por tipo de objeto, cada una con su cantidad.

Arma una copia FALSA de la memoria del motor (escenas graficas, modelos, cuerpos, entidades, luces, camara del visor,
RTTI, cabecera del exe) con los mismos offsets que usa el modulo, carga el modulo en LuaJIT con el mismo entorno
recortado que da el Sider de Agustin (sin pcall, sin select, sin error...) y corre escenarios: teclas, cuadros, mouse.
Las funciones de Windows (ReadProcessMemory y demas) y las dos funciones del juego que el modulo llama estan imitadas.

uso:  python3 mtr_sim.py <MotorTiempoReal.lua> [escenario ...]      (sin escenario: todos)
"""
import json, math, os, random, struct, sys

import numpy as np
import lupa.luajit21 as L

BASE = 0x140000000
VT = dict(modelo=0x142a0a880, luz=0x142a0ad48, sol=0x142a0add8, local=0x142a0ae68, punto=0x142a0aef8, foco=0x142a0af88,
          ent_modelo=0x142a721e8, cuerpo=0x142a72268, functor=0x142a72358, cam=0x142aa5368, visor=0x142ba51e0,
          nodo=0x142c7a730, ent_foco=0x142a48360, te=0x14255ea00, dataset=0x142a01100, otro=0x142a01200, locator=0x142a01300,
          dg_luz=0x142a226a0, dg_sol=0x142a226b0, dg_local=0x142a226c0, dg_punto=0x142a226d0, dg_foco=0x142a226e0)
RTTI = {VT['modelo']: 'Model@gr@fox', VT['foco']: 'SpotLightSource@gr@fox', VT['ent_modelo']: 'StadiumModel@rd@cobra',
        VT['cuerpo']: 'StadiumModelBody@rd@cobra', VT['cam']: 'CameraImpl@camera@rc@cobra', VT['nodo']: 'Node@?A0xc19a98f1@impl@scene@draw',
        VT['ent_foco']: 'SpotLight@grx@fox', VT['te']: 'TransformEntity@fox', VT['dataset']: 'DataSet@fox',
        VT['otro']: 'Decal@gr@fox', VT['locator']: 'Locator@fox', VT['sol']: 'DirectionalLightSource@gr@fox',
        VT['dg_foco']: 'SpotLightSource@dg@gr@fox', VT['dg_sol']: 'DirectionalLightSource@dg@gr@fox'}
RANURAS = [(0x142a0a880, 0x141697bd0), (0x142a0af88, 0x14169ed80), (0x142a0ad48, 0x14169ec20), (0x142a0add8, 0x14169eba0),
           (0x142a0ae68, 0x14169ec80), (0x142a0aef8, 0x14169ece0), (0x142a721e8, 0x141905750), (0x142a72268 + 48, 0x1419057e0),
           (0x142a72268 + 120, 0x1419064f0), (0x142a72358, 0x141905310), (0x142aa5368, 0x1419fcd70), (0x142ba51e0, 0x141ee73f0),
           (0x14255ea00, 0x1402bc920), (0x142a226a0, 0x14171d6e0), (0x142a226b0, 0x14171d620), (0x142a226c0, 0x14171d740),
           (0x142a226d0, 0x14171d7a0), (0x142a226e0, 0x14171d8c0)]
FN_COLOR, FN_MATRIZ = 0x14169f2e0, 0x14169de80
G_ESCENAS, G_VISOR = 0x14374d958, 0x1438040d0
EXE_FIN = 0x15b470000               # fin de la imagen del exe (SizeOfImage de la cabecera falsa)
N_LISTAS = 0x40                     # listas por tipo que reserva el ctor de la escena (FUN_1416916b0)
TIPO = dict(modelo=11, luz=23, otro=5)      # ids de tipo: el motor los reparte al arrancar (no son fijos): cualquiera sirve
TAM_LUZ = dict(luz=0x58, local=0x90, sol=0xa0, punto=0xb0, foco=0xf0)       # lo que libera el destructor de cada clase


class Mem:
    """memoria falsa: regiones sueltas; leer o escribir fuera de una region falla (como ReadProcessMemory)"""
    def __init__(self):
        self.reg = {}            # base -> bytearray
        self.prox = 0x1A0000000
        self.escrituras = []     # (dir, n)
        self.malas = []          # escrituras fuera de region o dentro del exe
        self.permitidas = []     # (desde, hasta): los unicos campos que el modulo tiene permiso de pisar
        self.lecturas = 0

    def nuevo(self, n, dir=None):
        if dir is None:
            dir = self.prox
            self.prox += (n + 0x4f) & ~0x3f      # un hueco entre regiones: pasarse de largo falla
        self.reg[dir] = bytearray(n)
        return dir

    def _donde(self, dir, n):
        for b, d in self.reg.items():
            if b <= dir and dir + n <= b + len(d): return d, dir - b
        return None, 0

    def leer(self, dir, n):
        self.lecturas += 1
        d, o = self._donde(dir, n)
        return bytes(d[o:o + n]) if d is not None else None

    def escribir(self, dir, datos, del_modulo=True):
        d, o = self._donde(dir, len(datos))
        if del_modulo:
            self.escrituras.append((dir, len(datos)))
            if d is None or BASE <= dir < EXE_FIN: self.malas.append((dir, len(datos)))
            elif not any(a <= dir and dir + len(datos) <= b for a, b in self.permitidas): self.malas.append((dir, len(datos)))
        if d is None: return False
        d[o:o + len(datos)] = datos
        return True

    # atajos para armar
    def u64(self, dir, v): assert self.escribir(dir, struct.pack('<Q', v), False), hex(dir)
    def u32(self, dir, v): assert self.escribir(dir, struct.pack('<I', v), False), hex(dir)
    def u8(self, dir, v): assert self.escribir(dir, bytes([v]), False), hex(dir)
    def f(self, dir, *v): assert self.escribir(dir, struct.pack('<%df' % len(v), *v), False), hex(dir)
    def rf(self, dir, n): return struct.unpack('<%df' % n, self.leer(dir, 4 * n))
    def r32(self, dir): return struct.unpack('<I', self.leer(dir, 4))[0]
    def r64(self, dir): return struct.unpack('<Q', self.leer(dir, 8))[0]
    def cadena(self, s):
        d = self.nuevo(max(len(s) + 1, 0x100)); self.escribir(d, s.encode() + b'\0', False); return d      # como en un monton de verdad: detras hay mas memoria
    def fox_cadena(self, s):
        rec = self.nuevo(0x10); self.u64(rec, self.cadena(s)); return rec


def mat_srt(S, Q, T, conj=False):
    x, y, z, w = Q
    if conj: x, y, z = -x, -y, -z
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)],
                  [2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x)],
                  [2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)]])
    M = np.zeros((4, 4)); M[:3, :3] = R * np.array(S[:3])[:, None]; M[3, :3] = T[:3]; M[3, 3] = 1
    return M


class Mundo:
    """arma la memoria del Editar > Apariencia con la escena del motor"""
    def __init__(self, conj=False, con_ayudas=True, con_limite=False, huesos=1, con_visor=True, extras=False, en_cola=()):
        self.m = m = Mem(); self.conj = conj
        self.modelos, self.luces, self.ents_ds = {}, {}, []
        self.rangos = {}                 # objeto de escena -> rangos que el modulo puede pisar mientras siga en su escena
        self.en_cola = set(en_cola)      # nombres que nacen en la cola de altas (el motor los pasa a su lista al cuadro siguiente)
        self.auto_vaciar = True
        # --- exe: cabecera, tablas de funciones, RTTI, comienzo de las funciones
        m.nuevo(0x1000, BASE); m.u32(BASE + 0x3c, 0x100); m.u32(BASE + 0x108, 0x62787C6A); m.u32(BASE + 0x150, EXE_FIN - BASE)
        tablas = sorted(set(VT.values()))
        tramos = []                               # tablas vecinas comparten region (si no, se pisarian entre si)
        for vt in tablas:
            if tramos and vt - 8 <= tramos[-1][1]: tramos[-1][1] = vt + 0xf8
            else: tramos.append([vt - 8, vt + 0xf8])
        for a, b in tramos: m.nuevo(b - a, a)
        for a, v in RANURAS: m.u64(a, v)
        rva = 0x2f00000
        for vt in tablas:
            if vt in RTTI:
                col = m.nuevo(0x18, 0x142e00000 + (vt & 0xfffff)); m.u32(col, 1); m.u32(col + 0xc, rva)
                m.nuevo(0x100, BASE + rva); m.escribir(BASE + rva + 0x10, b'.?AV' + RTTI[vt].encode() + b'@@\0', False)
                m.u64(vt - 8, col); rva += 0x100
            if not any(a == vt for a, _ in RANURAS): m.u64(vt, 0x141000000 + (vt & 0xffff))
        m.nuevo(0x40, FN_COLOR); m.escribir(FN_COLOR, bytes.fromhex('488b41380f28020f294040488b0148ff6078'), False)
        m.nuevo(0x40, FN_MATRIZ); m.escribir(FN_MATRIZ, bytes.fromhex('40534883ec200f2802488bd90f294140'), False)
        m.nuevo(8, G_ESCENAS); m.nuevo(8, G_VISOR)
        # --- escenas
        self.esc = [self._escena(0x1111222233334444), self._escena(0xaaaabbbbccccdddd)]
        m.u64(G_ESCENAS, self.esc[0]); m.u64(self.esc[0], self.esc[1])
        # --- archivo de escena (DataSet) y sus entidades
        self.ds = m.nuevo(0x100); m.u64(self.ds, VT['dataset'])
        self.hds = m.nuevo(0x20); m.u64(self.hds + 0x18, self.ds)
        # --- jugador: modelos del juego (sin entidad), uno con huesos animados
        for i, n in enumerate(['face_high.fmdl', 'hair_high.fmdl', 'uniform.fmdl', 'boots.fmdl']):
            self._modelo(self.esc[0], None, (0, 0, 0), archivo='/Assets/pes16/model/character/' + n, huesos=30 if i == 0 else 0, animado=True)
        self._modelo(self.esc[0], None, (0, 0, 0))     # sin nombre de ningun tipo
        if extras:      # un modelo que el JUEGO tiene oculto a su manera: solo la mascara de vistas, sin el bit0 de +0x1b4
            self._modelo(self.esc[0], None, (0, 0, 0), archivo='/Assets/pes16/model/character/oculto_del_juego.fmdl', mascara=0xffffffff)
        # --- objetos del motor
        self._objeto('zfx_Obj_01', (0.6, 1.0, 0.0), huesos=huesos, limite=con_limite)
        self._objeto('zfx_Obj_02', (-0.6, 1.0, 0.0), S=(2, 2, 2), Q=(0, 0.3826834, 0, 0.9238795), huesos=huesos, limite=con_limite)
        if con_ayudas:
            for i in range(22): self._objeto('zfx_Grilla_%02d' % (i + 1), (i - 5 if i < 11 else 0, 0.005, 0 if i < 11 else i - 16), S=(45, 0.03, 0.03), oculto=True)
            for i in range(26): self._objeto('zfx_Luz_%02d' % (i + 1), (0, -50, 0), S=(0.4, 0.4, 0.4), oculto=True)
        # --- luces: las 26 de Default con sus valores reales + un sol
        d = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'luces_default.json')))
        for l in d: self._luz(l)
        self._luz(dict(nombre='Sol_prueba', T=[0.3, -0.8, 0.5], Q=[0, 0, 0, 1], en=True, lumen=0, color=[3, 3, 3, 1], sombra=True), clase='sol')
        if extras:
            self._luz(dict(nombre='Punto_prueba', T=[1.0, 2.0, 3.0], Q=[0, 0, 0, 1], en=True, lumen=0, color=[2, 2, 2, 1], sombra=False), clase='punto')
            self._luz(dict(nombre='Local_prueba', T=[-1.0, 2.0, 3.0], Q=[0, 0, 0, 1], en=True, lumen=0, color=[2, 2, 2, 1], sombra=False), clase='local')
            self._luz(dict(nombre='Luz_base', T=[0.0, 0.0, 0.0], Q=[0, 0, 0, 1], en=True, lumen=0, color=[1, 1, 1, 1], sombra=False), clase='luz')
            # una luz de OTRA escena grafica (los marcadores de foco no la tienen que contar)
            self._luz(dict(nombre='Foco_de_otra_escena', T=[9.0, 9.0, 9.0], Q=[0, 0, 0, 1], en=True, lumen=0, color=[1, 1, 1, 1], sombra=False), esc=self.esc[1])
        for k in range(3):
            o = self._obj_escena(self.esc[1], 0x60, TIPO['otro']); m.u64(o, VT['otro'])
        loc = self._entidad('CP0000', VT['locator'], (0, 0.12, 0), (1, 1, 1), (0, 0, 0, 1))
        self._mapa()
        # --- visor y camara
        self.cam = None
        if con_visor:
            a, b, impl, nodo, cam = m.nuevo(0x100), m.nuevo(0x100), m.nuevo(0x600), m.nuevo(0x100), m.nuevo(0x120)
            m.u64(G_VISOR, a); m.u64(a + 0x78, b); m.u64(b + 0x90, impl); m.u64(impl, VT['visor']); m.u64(impl + 0xd8, nodo)
            m.u64(nodo, VT['nodo']); m.u64(nodo + 0x60, cam); m.u64(cam, VT['cam'])
            m.f(cam + 0x10, 0.0, 1.2, 6.5, 0.0, 1.2, 0.0, 0.0, 1.0, 0.0, 0.1, 500.0); m.u32(cam + 0x3c, 0); m.f(cam + 0x40, 0.09, 1.7777)
            m.f(cam + 0x48, 1.5, 2.5); m.u8(cam + 0x58, 1)
            self.cam = cam; m.permitidas.append((cam + 0x10, cam + 0x60))

    def _escena(self, h):
        """fox::gr::Scene como la deja su ctor (FUN_1416916b0): +0x28 cantidad de listas, +0x30 arreglo de punteros a lista
        { +0 primero, +8 ultimo, +0x10 cantidad }, +0x38 / +0x40 / +0x48 la cola de altas (misma forma, vacia)"""
        m = self.m
        e = m.nuevo(0x60); rec = m.nuevo(0x20); m.u64(e + 0x20, rec); m.u64(rec + 0x10, h)
        arr = m.nuevo(N_LISTAS * 8)
        for i in range(N_LISTAS): m.u64(arr + 8 * i, m.nuevo(0x18))
        m.u32(e + 0x28, N_LISTAS); m.u32(e + 0x2c, N_LISTAS); m.u64(e + 0x30, arr)
        return e

    def _lista(self, esc, tipo): return self.m.r64(self.m.r64(esc + 0x30) + (tipo - 1) * 8)

    def _enlazar(self, lst, o):
        """al final de la lista (todos con la misma prioridad), como FUN_141691e20; prende bit0 de +0x24"""
        m = self.m; ult = m.r64(lst + 8)
        m.u64(o + 0x10, ult); m.u64(o + 0x18, 0)
        if ult: m.u64(ult + 0x18, o)
        else: m.u64(lst, o)
        m.u64(lst + 8, o); m.u32(lst + 0x10, m.r32(lst + 0x10) + 1); m.u32(o + 0x24, m.r32(o + 0x24) | 1)

    def _desenlazar(self, lst, o):
        """FUN_141692340: lo saca de la lista, apaga bit0 y baja la cantidad"""
        m = self.m; ant, sig = m.r64(o + 0x10), m.r64(o + 0x18)
        if ant: m.u64(ant + 0x18, sig)
        else: m.u64(lst, sig)
        if sig: m.u64(sig + 0x10, ant)
        else: m.u64(lst + 8, ant)
        m.u64(o + 0x10, 0); m.u64(o + 0x18, 0); m.u32(o + 0x24, m.r32(o + 0x24) & ~1 & 0xffffffff); m.u32(lst + 0x10, m.r32(lst + 0x10) - 1)

    def _obj_escena(self, esc, n, tipo, en_cola=False):
        """alta de un objeto: FUN_1416927c0 lo pone en la cola de altas (flags 3); el vaciado de cada cuadro lo pasa a la
        lista de su tipo (flags 1). Por defecto nace ya pasado, que es como esta todo lo que se ve en pantalla"""
        m = self.m
        o = m.nuevo(n); m.u64(o + 0x28, esc); m.u32(o + 0x30, tipo)
        if en_cola:
            m.u32(o + 0x24, 2); self._enlazar(esc + 0x38, o)
        else:
            self._enlazar(self._lista(esc, tipo), o)
        return o

    def vaciar_colas(self):
        """lo que hace el motor en cada cuadro con cada escena (FUN_1416928b0)"""
        m = self.m
        for esc in self.esc:
            if m.leer(esc, 0x50) is None: continue
            for _ in range(10000):
                o = m.r64(esc + 0x38)
                if not o or m.leer(o, 0x38) is None or not m.r32(esc + 0x48): break
                tipo = m.r32(o + 0x30)
                self._desenlazar(esc + 0x38, o); m.u32(o + 0x24, m.r32(o + 0x24) & ~2 & 0xffffffff)
                if 1 <= tipo <= N_LISTAS and m.leer(m.r64(esc + 0x30) + (tipo - 1) * 8, 8) is not None and m.leer(self._lista(esc, tipo), 0x18) is not None:
                    self._enlazar(self._lista(esc, tipo), o)

    def sacar(self, o):
        """el juego da de baja un objeto (FUN_141692420, lo primero que hace el destructor): desde aca el modulo no puede
        escribirle nada, ni a el ni a lo que cuelga de el"""
        m = self.m; esc = m.r64(o + 0x28)
        if not m.r32(o + 0x24) & 1: return
        if m.r32(o + 0x24) & 2:
            self._desenlazar(esc + 0x38, o); m.u32(o + 0x24, m.r32(o + 0x24) & ~2 & 0xffffffff)
        else:
            self._desenlazar(self._lista(esc, m.r32(o + 0x30)), o)
        fuera = self.rangos.pop(o, [])
        m.permitidas = [r for r in m.permitidas if r not in fuera]

    def en_alguna_lista(self, obj):
        m = self.m
        for esc in self.esc:
            if m.leer(esc, 0x50) is None: continue
            cabezas = [m.r64(esc + 0x38)]
            arr, n = m.r64(esc + 0x30), m.r32(esc + 0x28)
            for i in range(n if n <= 256 else 0):
                if m.leer(arr + 8 * i, 8) is not None and m.leer(m.r64(arr + 8 * i), 8) is not None: cabezas.append(m.r64(m.r64(arr + 8 * i)))
            for o in cabezas:
                k = 0
                while o and k < 5000 and m.leer(o + 0x18, 8) is not None:
                    if o == obj: return True
                    o = m.r64(o + 0x18); k += 1
        return False

    def _modelo(self, esc, M, pos, archivo=None, huesos=0, animado=False, limite=False, oculto=False, mascara=None, nombre=None):
        m = self.m
        o = self._obj_escena(esc, 0x200, TIPO['modelo'], en_cola=((nombre or archivo or '') in self.en_cola)); m.u64(o, VT['modelo'])
        if M is None: M = mat_srt((1, 1, 1), (0, 0, 0, 1), pos)
        fl = [float(x) for x in M.flatten()]
        m.f(o + 0x40, *fl); m.f(o + 0xa0, *fl)
        m.f(o + 0x80, -0.11, -0.11, -0.11, 0, 0.11, 0.11, 0.11, 0)
        c3 = m.nuevo(0x40); m.f(c3, *fl); m.u64(o + 0x198, c3)
        self.rangos[o] = [(o + 0x40, o + 0x80), (o + 0xa0, o + 0xe0), (o + 0x1ac, o + 0x1b8), (o + 0x1c4, o + 0x1c8), (c3, c3 + 0x40)]
        m.permitidas += self.rangos[o]
        if limite: m.u64(o + 0x1a0, m.nuevo(0x40))
        m.u32(o + 0x1ac, 0xffffffff if oculto else 0); m.u32(o + 0x1b0, 0x18); m.u32(o + 0x1b4, 0x10 | (1 if oculto else 0))
        if mascara is not None: m.u32(o + 0x1ac, mascara)
        m.f(o + 0x1bc, 1.0); m.f(o + 0x1c4, 1.0); m.f(o + 0x1cc, -1.0, -1.0); m.f(o + 0x1e0, 2.0); m.u8(o + 0x1f1, 7)
        if huesos:
            hb = m.nuevo(huesos * 0x40); m.u64(o + 0xe0, hb); m.u32(o + 0xf8, huesos)
            m.permitidas.append((hb, hb + huesos * 0x40)); self.rangos[o].append((hb, hb + huesos * 0x40))
            for i in range(huesos):
                if animado: B = mat_srt((1, 1, 1), (0.1 * i % 1, 0, 0, 0.9), (0.01 * i, 1 + 0.02 * i, 0))
                else:
                    B = M.copy(); bind = np.array([0.01 * (i + 1), 0.02 * (i + 1), -0.03 * (i + 1)]); B[3, :3] = bind @ M[:3, :3] + M[3, :3]
                m.f(hb + i * 0x40, *[float(x) for x in B.flatten()])
        if archivo:
            f = m.nuevo(0x40); rec = m.nuevo(0x10); m.u64(rec, m.cadena(archivo)); m.u64(f + 0x30, rec); m.u64(o + 0x118, f)
        self.modelos[o] = dict(nombre=archivo)
        return o

    def _entidad(self, nombre, vt, T, S, Q, n=0x230, flags=7):
        m = self.m
        e = m.nuevo(n); m.u64(e, vt); m.u64(e + 0x28, m.fox_cadena(nombre)); m.u64(e + 0x30, self.hds)
        te = m.nuevo(0x60); m.u64(te, VT['te']); m.u64(e + 0x78, te)
        self.rangos_ent = [(te + 0x30, te + 0x60), (e + 0xc0, e + 0x100), (e + 0xb8, e + 0xbc)]
        m.permitidas += self.rangos_ent
        m.f(te + 0x30, S[0], S[1], S[2], 0, Q[0], Q[1], Q[2], Q[3], T[0], T[1], T[2], 0)
        M = mat_srt(S, Q, T, self.conj)
        m.f(e + 0xc0, *[float(x) for x in M.flatten()]); m.u32(e + 0xb8, flags); m.u32(e + 0xbc, 3)
        self.ents_ds.append((nombre, e))
        return e, te, M

    def _objeto(self, nombre, T, S=(1, 1, 1), Q=(0, 0, 0, 1), huesos=1, limite=False, oculto=False):
        m = self.m
        e, te, M = self._entidad(nombre, VT['ent_modelo'], T, S, Q, n=0x190, flags=6 if oculto else 7)
        de_ent = self.rangos_ent
        o = self._modelo(self.esc[0], M, T, huesos=huesos, limite=limite, oculto=oculto, nombre=nombre)
        c = m.nuevo(0xf0); m.u64(c, VT['cuerpo']); h = m.nuevo(0x20); m.u64(h + 0x18, e); m.u64(c + 0x30, h)
        m.permitidas += [(c + 0x80, c + 0x82), (c + 0xa0, c + 0xe0)]
        self.rangos[o] += de_ent + [(c + 0x80, c + 0x82), (c + 0xa0, c + 0xe0)]
        m.u8(c + 0x80, 0 if oculto else 1); m.u8(c + 0x81, 1); m.u64(c + 0x88, o); m.f(c + 0xa0, *[float(x) for x in M.flatten()])
        fn = m.nuevo(0x28); m.u64(fn, VT['functor']); m.u64(fn + 0x10, c)
        arr = m.nuevo(0x20); m.u64(arr, fn); m.u64(o + 0x108, arr); m.u32(o + 0x100, 1); m.u32(o + 0x104, 4)
        self.modelos[o] = dict(nombre=nombre, ent=e, te=te, cuerpo=c)
        return o

    def _luz(self, l, clase='foco', esc=None):
        """cada clase con su tamano exacto (pasarse de largo al leer falla, como al final de una region) y sus campos:
        solo el foco tiene giro (+0x90) y cono (+0xa0); en el sol y el punto +0x90 es el puntero a su bloque de dibujo"""
        m = self.m; sol = (clase == 'sol')
        o = self._obj_escena(esc or self.esc[0], TAM_LUZ[clase], TIPO['luz'], en_cola=(l['nombre'] in self.en_cola)); m.u64(o, VT[clase])
        p = m.nuevo(0x100); m.u64(o + 0x38, p); m.u64(o + 0x50, m.fox_cadena(l['nombre']))
        m.u64(p, VT['dg_' + clase]); m.permitidas.append((p + 0x64, p + 0x68)); self.rangos[o] = [(p + 0x64, p + 0x68)]
        if clase == 'foco':
            m.f(o + 0x80, 0.0, 10.0); m.f(o + 0x90, *l['Q']); m.f(o + 0xa0, 0.5, 0.6)
            m.u64(o + 0xd8, m.nuevo(0xb0)); m.u64(o + 0xe0, m.nuevo(0x20))
        elif clase == 'sol':
            m.f(o + 0x80, 256.0, 1.0); m.u64(o + 0x90, m.nuevo(0x40)); m.u64(o + 0x98, 0x1234567890abcdef)
        elif clase == 'punto':
            m.f(o + 0x80, 0.005, 7.5); m.u64(o + 0x90, m.nuevo(0x40)); m.u64(o + 0x98, m.nuevo(0x20)); m.u64(o + 0xa0, 0xfedcba0987654321)
        elif clase == 'local':
            m.f(o + 0x80, 0.005, 5.0)
        c = l['color']; k = l['lumen'] / 3.14159 if l['lumen'] else 1.0
        m.f(p + 0x40, c[0] * k, c[1] * k, c[2] * k, 1.0); m.u32(p + 0x64, (1 if l['en'] else 0) | (0x10 if l['sombra'] else 0)); m.f(p + 0x70, *l['T'], 1.0)
        self.luces[o] = dict(nombre=l['nombre'], p=p, clase=clase)
        if clase == 'foco' and esc is None:
            e, te, M = self._entidad(l['nombre'], VT['ent_foco'], l['T'], (1, 1, 1), l['Q'])
            m.u8(e + 0x168, 1 if l['en'] else 0); m.f(e + 0x1c0, 10.0, 0.0); m.f(e + 0x1d0, *c, 1.0); m.f(e + 0x1e0, 6500.0); m.f(e + 0x1e8, l['lumen'])
            m.f(e + 0x1f0, 60.0, 70.0); m.u8(e + 0x229, 1 if l['sombra'] else 0)

    def _mapa(self):
        m = self.m; n = len(self.ents_ds)
        nodos, valores = m.nuevo(n * 0x18), m.nuevo(n * 8)
        orden = list(range(n)); random.Random(7).shuffle(orden)
        for k, i in enumerate(orden):
            nombre, e = self.ents_ds[i]
            m.u64(nodos + i * 0x18, m.fox_cadena(nombre))
            m.u32(nodos + i * 0x18 + 0xc, orden[k + 1] if k + 1 < n else 0xffffffff)
            m.u64(valores + i * 8, e)
        m.u32(self.ds + 0x78, orden[0]); m.u64(self.ds + 0x90, nodos); m.u64(self.ds + 0x98, valores)

    def por_nombre(self, nombre):
        return next((o, d) for o, d in self.modelos.items() if d['nombre'] == nombre)

    def luz(self, nombre):
        return next((o, d) for o, d in self.luces.items() if d['nombre'] == nombre)


LUA_ENTORNO = r'''
local ffi_real = require("ffi")
local py = ...
SIM = { izq = false, der = false, med = false, x = 500, y = 400, frente = true, tick = 1000, hilo = 77, logs = {}, archivos = {}, llamadas = {},
        existe = {}, set_cursor = 0 }
local Cf = {}
function Cf.mtr_GetCurrentProcess() return ffi_real.cast("void *", -1) end
function Cf.mtr_ReadProcessMemory(h, dir, buf, n, got)
    assert(type(dir) == "number" and dir == math.floor(dir), "direccion no entera")
    assert(n >= 1 and n <= 0x1000, "lectura de tamaño raro: " .. tostring(n))
    local s = py.leer(dir, n)
    if not s then return 0 end
    ffi_real.copy(buf, s, n); got[0] = n
    return 1
end
function Cf.mtr_WriteProcessMemory(h, dir, buf, n, got)
    assert(type(dir) == "number" and dir == math.floor(dir), "direccion no entera")
    assert(n >= 1 and n <= 0x1000)
    if not py.escribir(dir, ffi_real.string(buf, n)) then return 0 end
    got[0] = n
    return 1
end
function Cf.mtr_GetAsyncKeyState(vk)
    local b = (vk == 1 and SIM.izq) or (vk == 2 and SIM.der) or (vk == 4 and SIM.med)
    return b and -32768 or 0
end
function Cf.mtr_GetCursorPos(pt) pt[0] = SIM.x; pt[1] = SIM.y return 1 end
function Cf.mtr_SetCursorPos(x, y) SIM.x = x; SIM.y = y; SIM.set_cursor = SIM.set_cursor + 1 return 1 end
function Cf.mtr_GetForegroundWindow() return ffi_real.cast("void *", 0x1234) end
function Cf.mtr_GetWindowThreadProcessId(h, pid) pid[0] = SIM.frente and 4242 or 9 return 1 end
function Cf.mtr_GetCurrentProcessId() return 4242 end
function Cf.mtr_GetCurrentThreadId() return SIM.hilo end
function Cf.mtr_GetClientRect(h, rc) rc[0] = 0; rc[1] = 0; rc[2] = 1920; rc[3] = 1080 return 1 end
function Cf.mtr_GetTickCount() return SIM.tick end
function Cf.mtr_MultiByteToWideChar(cp, fl, s, n, w, wn)
    if w ~= nil then for i = 1, #s do w[i - 1] = string.byte(s, i) end w[#s] = 0 end
    return #s + 1
end
local abiertos, nh = {}, 100
function Cf.mtr_CreateFileW(w, acc, comp, seg, disp, fl, pl)
    local t = {}; local i = 0
    while w[i] ~= 0 do t[#t + 1] = string.char(w[i]); i = i + 1 end
    local ruta = table.concat(t)
    if SIM.sin_archivo then return ffi_real.cast("void *", -1) end
    if disp == 2 or not SIM.archivos[ruta] then SIM.archivos[ruta] = "" end
    nh = nh + 1; abiertos[nh] = ruta
    return ffi_real.cast("void *", nh)
end
function Cf.mtr_SetFilePointer(h, d, alto, metodo) return #SIM.archivos[abiertos[tonumber(ffi_real.cast("intptr_t", h))]] + (SIM.tam_extra or 0) end
function Cf.mtr_WriteFile(h, buf, n, hecho, ov)
    local ruta = abiertos[tonumber(ffi_real.cast("intptr_t", h))]
    SIM.archivos[ruta] = SIM.archivos[ruta] .. string.sub(buf, 1, n); hecho[0] = n
    return 1
end
function Cf.mtr_CloseHandle(h) abiertos[tonumber(ffi_real.cast("intptr_t", h))] = nil return 1 end

local ffi_falso = { C = Cf, cdefs = 0 }
function ffi_falso.cdef(s) ffi_falso.cdefs = ffi_falso.cdefs + 1; ffi_real.cdef((string.gsub(s, "mtr_", "mtr" .. ffi_falso.cdefs .. "_"))) end   -- que al menos compile como C
function ffi_falso.cast(tipo, v)
    if type(tipo) == "string" and string.find(tipo, "(", 1, true) then
        -- una funcion del juego: devuelvo una imitacion que comprueba como la llaman
        local dir = v
        return function(obj, buf)
            assert(type(obj) == "number", "el objeto va como numero")
            if type(buf) == "table" then buf = rawget(buf, "__real") end
            local p = tonumber(ffi_real.cast("uintptr_t", buf))
            assert(p % 16 == 0, "el buffer para movaps no esta alineado a 16")
            SIM.llamadas[#SIM.llamadas + 1] = { dir = dir, obj = obj }
            py.llamada(dir, obj, ffi_real.string(buf, 64))
        end
    end
    if tipo == "float *" then
        -- LuaJIT devuelve un valor ROTO (nil, false o un objeto falso) al leer por float* unos bits que son un NaN negativo
        -- (por ejemplo el entero -1). Segun la version cierra el juego: aca cualquier lectura asi es un error del modulo.
        local real, u = ffi_real.cast(tipo, v), ffi_real.cast("uint32_t *", v)
        return setmetatable({ __real = real }, {
            __index = function(_, i)
                local b = u[i]
                if b > 0xff800000 then error(string.format("float leido sin mirar los bits: %08x (LuaJIT devuelve un valor roto)", b), 2) end
                return real[i]
            end,
            __newindex = function(_, i, x) real[i] = x end })
    end
    return ffi_real.cast(tipo, v)
end
for _, k in ipairs({ "new", "string", "copy", "typeof", "sizeof", "fill" }) do ffi_falso[k] = ffi_real[k] end

local permitidos = { assert = assert, ipairs = ipairs, pairs = pairs, math = math, string = string, table = table,
    tonumber = tonumber, tostring = tostring, type = type, ffi = ffi_falso,
    os = { clock = os.clock, time = os.time, date = os.date },
    log = function(s) assert(type(s) == "string"); SIM.logs[#SIM.logs + 1] = s end,
    io = { open = function(ruta, modo)
        assert(modo == "rb", "io.open solo lee en el Sider de Agustin")
        local n = string.match(ruta, "([^\\]+)$")
        if SIM.existe[n] then return { close = function() end, read = function() return "" end } end
        return nil
    end },
    memory = {},
}
local env = setmetatable({}, { __index = function(_, k)
    local v = permitidos[k]
    if v == nil then
        if k == "pcall" or k == "ffi" then return nil end       -- el modulo pregunta por estos y esta bien que no esten
        error("el modulo usa el global '" .. tostring(k) .. "' que el Sider de Agustin no da", 2)
    end
    return v
end, __newindex = function(_, k) error("el modulo crea el global '" .. tostring(k) .. "'", 2) end })
SIM.ffi_falso, SIM.permitidos = ffi_falso, permitidos

function cargar(fuente, ctx)
    local f = assert(loadstring(fuente, "=MotorTiempoReal.lua"))
    setfenv(f, env)
    local m = f()
    local reg = {}
    ctx = ctx or { sider_dir = "C:\\sider\\" }
    ctx.register = function(ev, fn) reg[ev] = fn end
    m.init(ctx)
    return m, reg, ctx
end
'''


class Sim:
    def __init__(self, fuente, mundo, existe=('Default.fpkd', 'Default.fpk'), sin_ffi=False, ctx=None):
        self.w = mundo; self.mem = mundo.m
        self.lua = L.LuaRuntime(encoding=None, unpack_returned_tuples=True)
        self.llamadas = []
        py = self.lua.table_from({b'leer': self._leer, b'escribir': self._escribir, b'llamada': self._llamada})
        self.lua.execute(LUA_ENTORNO.encode(), py)
        g = self.lua.globals()
        self.SIM = g[b'SIM']
        for n in existe: self.SIM[b'existe'][n.encode()] = True
        if sin_ffi: self.SIM[b'permitidos'][b'ffi'] = None
        self.m, self.reg, self.ctx = g[b'cargar'](fuente, ctx)
        self.texto = ''; self.cuadros = 0

    def _leer(self, dir, n): return self.mem.leer(int(dir), int(n))
    def _escribir(self, dir, s): return self.mem.escribir(int(dir), s)

    def _llamada(self, dir, obj, buf):
        dir, obj = int(dir), int(obj); m = self.mem
        self.llamadas.append((dir, obj))
        v = struct.unpack('<16f', buf)
        # lo que el modulo TIENE que garantizar antes de llamar: el objeto existe, es de la clase y esta en una escena
        assert m.leer(obj, 0x40) is not None, 'llamada sobre memoria que no existe'
        n = m.lecturas; viva = self.w.en_alguna_lista(obj); m.lecturas = n
        assert viva, 'llamada sobre un objeto que no esta en ninguna escena'
        def pon(dir, *vals):       # campos internos rotos a proposito por el escenario "roto": el juego real no los tendria asi
            m.escribir(dir, struct.pack('<%df' % len(vals), *vals), False)
        if dir == FN_COLOR:
            assert m.r64(obj) in (VT['foco'], VT['sol'], VT['luz'], VT['local'], VT['punto']), 'color sobre algo que no es luz'
            p = m.r64(obj + 0x38); pon(p + 0x40, *v[:4])
            # la actualizacion de cada clase (ranura +0x78) escribe en el bloque de dibujo SIN mirar si existe
            vt = m.r64(obj)
            if vt == VT['foco']:
                blo = m.r64(obj + 0xd8); assert m.leer(blo, 0xb0) is not None, 'el juego se cierra: foco sin bloque de dibujo'
                pon(blo + 0x30, v[0] / math.pi, v[1] / math.pi)
            elif vt in (VT['sol'], VT['punto']):
                blo = m.r64(obj + 0x90); assert m.leer(blo, 0x40) is not None, 'el juego se cierra: luz sin bloque de dibujo'
                pon(blo + 0x20, v[0] / math.pi, v[1] / math.pi, v[2] / math.pi, 1.0)
        elif dir == FN_MATRIZ:
            assert m.r64(obj) == VT['modelo'], 'matriz sobre algo que no es modelo'
            pon(obj + 0x40, *v); pon(obj + 0xa0, *v); c3 = m.r64(obj + 0x198)
            if c3: pon(c3, *v)
            m.u32(obj + 0x1b0, (m.r32(obj + 0x1b0) & 0xfffffff7) | 4)
        else:
            raise AssertionError('llamada a una funcion desconocida %x' % dir)

    def cuadro(self, n=1, ms=16):
        for _ in range(n):
            self.SIM[b'tick'] += ms
            if self.w.auto_vaciar and self.cuadros > 0:      # el motor vacia la cola de altas en cada cuadro
                n = self.mem.lecturas
                try: self.w.vaciar_colas()
                except (AssertionError, TypeError, struct.error): pass      # listas rotas a proposito (escenario "roto"): el motor falso no sigue
                self.mem.lecturas = n                                       # (no son lecturas del modulo)
            self.cuadros += 1
            t = self.reg[b'overlay_on'](self.ctx)
            self.texto = t.decode('latin1') if t else ''
            for linea in self.texto.split('\n'):
                assert len(linea) <= 112, 'renglon de %d letras: %s' % (len(linea), linea)
            assert self.texto.count('\n') <= 14, 'demasiados renglones: %d' % self.texto.count('\n')
            assert not self.w.m.malas, 'escritura fuera de lugar: %s' % [(hex(d), n) for d, n in self.w.m.malas[:5]]
        return self.texto

    def tecla(self, *ks):
        for k in ks:
            self.reg[b'key_down'](self.ctx, {'0': 0x30, '2': 0x32, '3': 0x33, '4': 0x34, '5': 0x35, '6': 0x36, '7': 0x37, '8': 0x38, '9': 0x39}[k] if isinstance(k, str) else k)
            self.cuadro()

    def raton(self, izq=False, der=False, med=False, dx=0, dy=0):
        self.SIM[b'izq'], self.SIM[b'der'], self.SIM[b'med'] = izq, der, med
        self.SIM[b'x'] += dx; self.SIM[b'y'] += dy
        self.cuadro()

    def arrastre(self, boton, dx, dy, pasos=5):
        kw = {b: True for b in boton.split('+')}
        self.raton(**kw)                                 # aprieta
        if boton == 'izq': self.cuadro(9)                # el izquierdo solo recien es herramienta a los 120 ms
        for _ in range(pasos): self.raton(dx=dx // pasos, dy=dy // pasos, **kw)
        self.raton()                                     # suelta
        self.cuadro()

    def recargar(self, fuente):
        """Sider vuelve a cargar el modulo con el juego abierto: mismo estado de Lua, mismo ctx"""
        self.m, self.reg, self.ctx = self.lua.globals()[b'cargar'](fuente, self.ctx)

    def logs(self): return [x.decode('latin1') for x in self.SIM[b'logs'].values()]
    def registro(self):
        a = self.SIM[b'archivos']
        return b''.join(list(a.values())).decode('latin1')
    def pagina(self, n):
        for _ in range(8):
            if ('[%s]' % ['ESCENA', 'LUCES', 'ENTIDADES', 'OPCIONES', 'REGISTRO'][n - 1]) in self.texto: return
            self.tecla('6')
        raise AssertionError('no llego a la pagina %d' % n)
    def opcion(self, texto):
        self.pagina(4)
        for _ in range(40):
            if any(l.split(' | ')[-1].startswith('> ' + texto) for l in self.texto.split('\n')): return
            self.tecla('0')
        raise AssertionError('no encontre la opcion ' + texto)
    def cursor_en(self, nombre, pag=1):
        self.pagina(pag)
        for _ in range(200):
            if any(l.split(' | ')[-1].startswith('>') and nombre in l.split(' | ')[-1] for l in self.texto.split('\n')): return
            self.tecla('0')
        raise AssertionError('no encontre en la lista: ' + nombre)


def cerca(a, b, tol=1e-4):
    a, b = np.array(a, dtype=float).flatten(), np.array(b, dtype=float).flatten()
    assert a.shape == b.shape and np.all(np.abs(a - b) <= tol * (1 + np.abs(b))), '\n%s\n!=\n%s' % (a, b)


def estado_objeto(w, nombre):
    """lee de la memoria falsa todo lo que el motor usa de un objeto y comprueba que sea coherente entre si"""
    m = w.m; o, d = w.por_nombre(nombre)
    te = m.rf(d['te'] + 0x30, 12); S, Q, T = te[0:3], te[4:8], te[8:11]
    M = np.array(m.rf(d['ent'] + 0xc0, 16)).reshape(4, 4)
    for donde in (d['cuerpo'] + 0xa0, o + 0x40, o + 0xa0, m.r64(o + 0x198)):
        cerca(m.rf(donde, 16), M, 1e-6)
    cerca(M, mat_srt(S, Q, T, w.conj), 2e-4)
    n = m.r32(o + 0xf8)
    if n:
        hb = m.r64(o + 0xe0)
        for i in range(n):
            Bm = np.array(m.rf(hb + i * 0x40, 16)).reshape(4, 4)
            bind = np.array([0.01 * (i + 1), 0.02 * (i + 1), -0.03 * (i + 1)])
            esp = M.copy(); esp[3, :3] = bind @ M[:3, :3] + M[3, :3]
            cerca(Bm, esp, 3e-4)
    oculto = m.r32(o + 0x1b4) & 1
    assert (m.r32(o + 0x1ac) == 0xffffffff) == bool(oculto)
    # lo mismo que calcularia el motor si resincroniza (FUN_141906600): entidad habilitada y cuerpo visible
    hab = (m.r32(d['ent'] + 0xb8) & 1) == 1 and (m.r32(d['ent'] + 0xbc) & 3) == 3
    assert m.leer(d['cuerpo'] + 0x80, 1)[0] == (1 if hab else 0), 'cuerpo+0x80 distinto de lo que pondria el motor'
    assert (hab and m.leer(d['cuerpo'] + 0x81, 1)[0] == 1) == (not oculto), 'al resincronizar el motor cambiaria si se ve o no: ' + nombre
    return dict(S=np.array(S), Q=np.array(Q), T=np.array(T), M=M, oculto=bool(oculto), flags=m.r32(o + 0x1b0))


# ------------------------------------------------------------------------------------------------ escenarios
def e_basico(F):
    w = Mundo(); s = Sim(F, w)
    assert any('listo: ENTREGANDO' in x for x in s.logs()), s.logs()
    k = s.m[b'make_key'](s.ctx, b'common\\render\\model\\light\\#Win\\Default.fpkd')
    assert k == b'mtr01|Default.fpkd' and s.m[b'get_filepath'](s.ctx, b'x', k) == b'C:\\sider\\content\\motor-tiempo-real\\escena\\Default.fpkd'
    assert s.m[b'make_key'](s.ctx, b'common\\otra\\cosa.fpk') is None
    t = s.cuadro(3)
    assert '[ESCENA]' in t and 'zfx_Obj_01' in t and 'OBJETO 1 de 7 (+48 de ayuda)' in t, t
    assert '~face_high.fmdl' in t and 'modelo_' in t, t
    assert not w.m.escrituras, 'escribio sin que nadie tocara nada: %s' % w.m.escrituras[:5]
    r = s.registro()
    assert 'exe reconocido: sello 0x62787c6a' in r and 'VOLCADO (primera vez' in r and 'entidad 0x' in r and 'camara del visor 0x' in r, r[:3000]
    assert 'convencion de giro' in r and '-> normal (comprobada con' in r, [x for x in r.split('\n') if 'giro' in x]
    assert r.count('luz E1') == 27 and r.count('modelo E1') == 55, (r.count('luz E1'), r.count('modelo E1'))
    assert 'otros objetos de escena: 3 de clase Decal' in r
    s.pagina(2); assert 'LUZ 1 de 27   prendidas 9' in s.texto, s.texto
    s.pagina(3); assert 'ENTIDAD 1 de 77' in s.texto, s.texto
    s.pagina(4); assert 'Camara libre' in s.texto
    s.pagina(5); assert 'REGISTRO' in s.texto
    lect = w.m.lecturas; s.pagina(1); s.cuadro(600)
    print('   lecturas por cuadro en reposo: %.1f' % ((w.m.lecturas - lect) / 600.0))
    assert (w.m.lecturas - lect) / 600.0 < 60
    assert not w.m.escrituras
    print(s.texto)


def e_mover(F):
    for conj in (False, True):
        for huesos in (0, 1, 3):
            w = Mundo(conj=conj, huesos=huesos); s = Sim(F, w); s.cuadro(2)
            a0 = estado_objeto(w, 'zfx_Obj_01'); b0 = estado_objeto(w, 'zfx_Obj_02')
            # mover en el plano de la pantalla con el mouse: camara en +z mirando a -z -> derecha = +x, arriba = +y
            s.cursor_en('zfx_Obj_01'); s.tecla('8')
            s.arrastre('izq', 200, -100)
            a1 = estado_objeto(w, 'zfx_Obj_01')
            k = 2 * 6.5 * math.tan(0.045) / 1080
            cerca(a1['T'], a0['T'] + np.array([200 * k, 100 * k, 0]), 1e-4); cerca(a1['S'], a0['S']); cerca(a1['Q'], a0['Q'])
            assert a1['flags'] & 4 and not a1['flags'] & 8
            cerca(estado_objeto(w, 'zfx_Obj_02')['T'], b0['T'])
            # marcar el segundo tambien y mover los dos sobre el eje Y
            s.cursor_en('zfx_Obj_02'); s.tecla('8', '3', '3')
            assert 'EJE Y' in s.texto and 'marcados 2' in s.texto, s.texto
            s.arrastre('izq', 0, -300)
            a2 = estado_objeto(w, 'zfx_Obj_01'); b2 = estado_objeto(w, 'zfx_Obj_02')
            cerca(a2['T'], a1['T'] + np.array([0, 300 * k, 0]), 2e-4); cerca(b2['T'], b0['T'] + np.array([0, 300 * k, 0]), 2e-4)
            # rotar sobre Y: el giro nuevo tiene que ser el viejo con 2 radianes mas alrededor de Y del mundo
            s.tecla('4'); assert 'HERRAMIENTA ROTAR' in s.texto
            s.arrastre('izq', 200, 0)
            b3 = estado_objeto(w, 'zfx_Obj_02')
            Ry = mat_srt((1, 1, 1), (0, math.sin(1.0), 0, math.cos(1.0)), (0, 0, 0), False)[:3, :3]
            cerca(b3['M'][:3, :3], b2['M'][:3, :3] @ Ry, 2e-3); cerca(b3['T'], b2['T'])
            # escalar parejo (eje PANTALLA)
            s.tecla('4', '3', '3'); assert 'HERRAMIENTA ESCALAR   EJE PANTALLA' in s.texto, s.texto
            s.arrastre('izq', 100, 0)
            b4 = estado_objeto(w, 'zfx_Obj_02'); cerca(b4['S'], b3['S'] * math.exp(0.5), 1e-3)
            # de a pasos con el teclado
            s.opcion('Eje de las herramientas'); s.tecla('8'); assert 'eje X' in s.texto
            s.opcion('Mover lo marcado de a 5 cm'); s.tecla('3', '3', '4')
            b5 = estado_objeto(w, 'zfx_Obj_02'); cerca(b5['T'], b4['T'] + np.array([0.05, 0, 0]), 1e-4)
            s.opcion('Rotar lo marcado de a 5 grados'); s.tecla('3')
            s.opcion('Escalar lo marcado de a 5 %'); s.tecla('4')
            b6 = estado_objeto(w, 'zfx_Obj_02'); cerca(b6['S'], b5['S'] * np.array([0.95, 1, 1]), 1e-4)
            # ocultar y mostrar lo marcado
            s.pagina(1); s.tecla('7'); assert estado_objeto(w, 'zfx_Obj_01')['oculto'] and estado_objeto(w, 'zfx_Obj_02')['oculto']
            assert w.m.leer(w.por_nombre('zfx_Obj_01')[1]['cuerpo'] + 0x81, 1) == b'\0'
            s.tecla('7'); assert not estado_objeto(w, 'zfx_Obj_01')['oculto']
            # dejar todo como estaba
            s.opcion('Dejar todo como estaba'); s.tecla('8')
            a9 = estado_objeto(w, 'zfx_Obj_01'); b9 = estado_objeto(w, 'zfx_Obj_02')
            cerca(a9['T'], a0['T']); cerca(b9['T'], b0['T']); cerca(b9['S'], b0['S']); cerca(b9['Q'], b0['Q']); cerca(b9['M'], b0['M'], 1e-5)
            assert not w.m.malas, w.m.malas[:5]
            assert not s.llamadas, 'sin limites no hay que llamar al juego'
    # con limites para el descarte: una llamada por objeto al soltar, con el buffer alineado
    w = Mundo(con_limite=True); s = Sim(F, w); s.cuadro(2)
    s.cursor_en('zfx_Obj_01'); s.arrastre('izq', 50, 0)
    o = w.por_nombre('zfx_Obj_01')[0]
    assert s.llamadas == [(FN_MATRIZ, o)], s.llamadas
    estado_objeto(w, 'zfx_Obj_01')
    # un modelo del juego no se mueve (solo se oculta)
    s.cursor_en('~face_high.fmdl'); n = len(w.m.escrituras); s.arrastre('izq', 50, 0)
    assert len(w.m.escrituras) == n and 'nada para mover' in s.texto, s.texto
    s.tecla('7'); cara = next(o for o, d in w.modelos.items() if d['nombre'] and 'face_high' in d['nombre'])
    assert w.m.r32(cara + 0x1b4) & 1 and w.m.r32(cara + 0x1ac) == 0xffffffff
    s.tecla('7'); assert not w.m.r32(cara + 0x1b4) & 1
    assert not w.m.malas
    print('   registro de un movimiento:', [x for x in s.registro().split('\r\n') if x.startswith('MOVER')][:1])


def e_luces(F):
    w = Mundo(); s = Sim(F, w); s.cuadro(2); m = w.m
    s.cursor_en('SpotLight_Key ', 2)
    o, d = w.luz('SpotLight_Key'); p = d['p']
    assert m.r32(p + 0x64) == 0x10 and '[off] Foco  SpotLight_Key ' in s.texto, s.texto
    s.tecla('8'); assert m.r32(p + 0x64) == 0x11 and 'LUZ' in s.texto and 'prendidas 10' in s.texto, s.texto
    c0 = m.rf(p + 0x40, 4)
    s.tecla('3'); cerca(m.rf(p + 0x40, 4), [c0[0] * 1.25, c0[1] * 1.25, c0[2] * 1.25, c0[3]]); assert s.llamadas == [(FN_COLOR, o)]
    s.tecla('4', '4'); cerca(m.rf(p + 0x40, 4), [c0[0] * 0.8, c0[1] * 0.8, c0[2] * 0.8, c0[3]], 1e-5)
    s.tecla('7'); prend = [x['nombre'] for q, x in w.luces.items() if m.r32(x['p'] + 0x64) & 1]
    assert prend == ['SpotLight_Key'] and '(SOLA)' in s.texto, prend
    s.tecla('7'); prend = [x['nombre'] for q, x in w.luces.items() if m.r32(x['p'] + 0x64) & 1]
    assert len(prend) == 10 and '(SOLA)' not in s.texto, prend
    # enfocar la camara en la luz
    s.tecla('2'); cerca(m.rf(w.cam + 0x1c, 3), m.rf(p + 0x70, 3), 1e-5)
    # marcadores: uno por foco, detras del foco, a la vista
    s.opcion('Marcador en cada foco'); s.tecla('8'); s.cuadro(40)
    usados = 0
    for i in range(26):
        e = estado_objeto(w, 'zfx_Luz_%02d' % (i + 1))
        if not e['oculto']:
            usados += 1
            dmin = min(np.linalg.norm(e['T'] - np.array(m.rf(x['p'] + 0x70, 3))) for q, x in w.luces.items())
            assert abs(dmin - 0.18) < 1e-3, dmin
    assert usados == 26, usados
    s.tecla('8'); s.cuadro(40); assert all(estado_objeto(w, 'zfx_Luz_%02d' % (i + 1))['oculto'] for i in range(26))
    s.opcion('Grilla de orientacion'); s.tecla('8'); assert not any(estado_objeto(w, 'zfx_Grilla_%02d' % (i + 1))['oculto'] for i in range(22))
    s.opcion('Dejar todo como estaba'); s.tecla('8'); s.cuadro(40)
    assert all(estado_objeto(w, 'zfx_Grilla_%02d' % (i + 1))['oculto'] for i in range(22))
    assert m.r32(p + 0x64) == 0x10; cerca(m.rf(p + 0x40, 4), c0, 1e-5)
    assert not m.malas
    # sin ayudas en la escena: lo dice y no rompe
    w = Mundo(con_ayudas=False); s = Sim(F, w); s.cuadro(2)
    s.opcion('Grilla de orientacion'); s.tecla('8'); assert 'no trae la grilla' in s.texto, s.texto
    s.opcion('Marcador en cada foco'); s.tecla('8'); assert 'no trae marcadores' in s.texto, s.texto


def e_camara(F):
    w = Mundo(); s = Sim(F, w); s.cuadro(2); m = w.m; cam = w.cam
    orig = m.leer(cam + 0x10, 0x50)
    s.arrastre('der', 100, 0)       # orbita: 0.5 rad
    pos, obj = np.array(m.rf(cam + 0x10, 3)), np.array(m.rf(cam + 0x1c, 3))
    cerca(obj, [0, 1.2, 0]); cerca(pos, [6.5 * math.sin(-0.5), 1.2, 6.5 * math.cos(-0.5)], 1e-4); cerca(m.rf(cam + 0x28, 3), [0, 1, 0])
    cerca(m.rf(cam + 0x40, 1), [0.09]); assert int(s.SIM[b'set_cursor']) >= 5
    s.arrastre('der', -100, 0)
    cerca(m.rf(cam + 0x10, 3), [0, 1.2, 6.5], 1e-4)
    s.arrastre('med', 100, 0)       # paneo: la escena sigue al mouse -> la camara va a la izquierda (-x)
    k = 2 * 6.5 * math.tan(0.045) / 1080
    cerca(m.rf(cam + 0x1c, 3), [-100 * k, 1.2, 0], 1e-4); cerca(m.rf(cam + 0x10, 3), [-100 * k, 1.2, 6.5], 1e-4)
    s.arrastre('izq+der', 0, 100)   # zoom: se aleja
    cerca(np.linalg.norm(np.array(m.rf(cam + 0x10, 3)) - np.array(m.rf(cam + 0x1c, 3))), 6.5 * math.exp(0.5), 1e-4)
    s.opcion('Vista'); s.tecla('8'); assert 'FRENTE' in s.texto
    s.tecla('8', '8'); assert 'DERECHA' in s.texto
    d = np.array(m.rf(cam + 0x10, 3)) - np.array(m.rf(cam + 0x1c, 3)); cerca(d / np.linalg.norm(d), [1, 0, 0], 1e-4)
    s.tecla('4'); assert 'ATRAS' in s.texto
    s.opcion('Proyeccion'); alto = lambda: np.linalg.norm(np.array(m.rf(cam + 0x10, 3)) - np.array(m.rf(cam + 0x1c, 3))) * math.tan(m.rf(cam + 0x40, 1)[0] / 2)
    h0 = alto(); s.tecla('8'); assert 'PERSPECTIVA' in s.texto; cerca(m.rf(cam + 0x40, 1), [0.6]); cerca(alto(), h0, 1e-4)
    s.tecla('8'); assert 'ORTOGRAFICA' in s.texto; cerca(m.rf(cam + 0x40, 1), [0.02]); cerca(alto(), h0, 1e-4)
    dist = np.linalg.norm(np.array(m.rf(cam + 0x10, 3)) - np.array(m.rf(cam + 0x1c, 3)))
    cerca_, lejos = m.rf(cam + 0x34, 2); assert lejos > dist + 100 and cerca_ < dist, (cerca_, lejos, dist)
    s.tecla('8'); assert 'LENTE DEL JUEGO' in s.texto; cerca(m.rf(cam + 0x40, 1), [0.09]); cerca(m.rf(cam + 0x34, 2), [0.1, 500.0])
    s.opcion('Lente (campo de vision)'); s.tecla('3'); cerca(m.rf(cam + 0x40, 1), [0.09 * 1.2]); cerca(alto(), h0, 1e-4)
    s.opcion('Distancia de la camara'); s.tecla('4')
    # enfocar un objeto
    s.cursor_en('zfx_Obj_02'); s.tecla('2'); cerca(m.rf(cam + 0x1c, 3), [-0.6, 1.0, 0.0], 1e-5)
    # volver a la del juego: byte por byte
    s.opcion('Camara libre'); assert '> Camara libre (mouse der. gira) SI' in s.texto, s.texto
    s.tecla('8'); assert m.leer(cam + 0x10, 0x50) == orig and 'Camara libre (mouse der. gira) no' in s.texto
    n = len(m.escrituras); s.cuadro(30); assert len(m.escrituras) == n, 'sigue escribiendo la camara despues de soltarla'
    # el visor cambia de camara mientras la tengo: la suelto sin escribir en la vieja
    s.arrastre('der', 10, 0); m.u64(cam, 0x142000000); n = len(m.escrituras); s.cuadro(40)
    assert len(m.escrituras) == n and any('la suelto' in x for x in s.logs()), s.logs()[-3:]
    # fuera del visor: no hay camara y lo dice
    w = Mundo(con_visor=False); s = Sim(F, w); s.cuadro(2)
    s.arrastre('der', 100, 0); assert 'no encuentro la camara del visor' in s.texto and not w.m.escrituras, s.texto
    s.cursor_en('zfx_Obj_01'); s.arrastre('izq', 100, 0)      # mover sin camara: ejes del mundo
    cerca(estado_objeto(w, 'zfx_Obj_01')['T'], [0.6 + 100 * 0.002, 1.0, 0.0], 1e-4)
    # con la ventana del juego atras, el mouse no hace nada
    w = Mundo(); s = Sim(F, w); s.cuadro(2); s.SIM[b'frente'] = False
    s.arrastre('der', 100, 0); s.arrastre('izq', 100, 0); assert not w.m.escrituras
    # mouse apagado en opciones
    s.SIM[b'frente'] = True; s.opcion('Mouse'); s.tecla('8'); s.arrastre('der', 100, 0); assert not w.m.escrituras


def e_seguro(F):
    # otro exe: no escribe nada, ni una vez
    w = Mundo(); w.m.u64(0x142a72268 + 48, 0x1419057e1); s = Sim(F, w); t = s.cuadro(3)
    assert 'solo anda con PES2021.exe 1.07.02' in t, t
    for k in '0987654326': s.tecla(k)
    s.arrastre('izq', 50, 50); s.arrastre('der', 50, 50)
    assert not w.m.escrituras and not s.llamadas
    # funciones del juego cambiadas: sigue todo menos el brillo y el aviso
    w = Mundo(con_limite=True); w.m.escribir(FN_COLOR, b'\xe9', False); w.m.escribir(FN_MATRIZ + 3, b'\x00', False); s = Sim(F, w); s.cuadro(2)
    s.cursor_en('SpotLight_Key ', 2); s.tecla('3'); assert 'el brillo esta apagado' in s.texto, s.texto
    s.cursor_en('zfx_Obj_01'); s.arrastre('izq', 50, 0); estado_objeto(w, 'zfx_Obj_01'); assert not s.llamadas
    # sin ffi
    s = Sim(F, Mundo(), sin_ffi=True); assert 'le falta ffi' in s.cuadro(), s.texto
    s.tecla('0', '8')
    # apagado.txt: ni entrega ni toca
    w = Mundo(); s = Sim(F, w, existe=('Default.fpkd', 'Default.fpk', 'apagado.txt'))
    lect = w.m.lecturas
    assert s.m[b'make_key'](s.ctx, b'light\\Default.fpkd') is None and 'apagado por' in s.cuadro()
    s.tecla('8'); assert w.m.lecturas == lect
    # solo_lectura.txt: lista pero no escribe
    w = Mundo(); s = Sim(F, w, existe=('Default.fpkd', 'Default.fpk', 'solo_lectura.txt')); s.cuadro(2)
    s.cursor_en('zfx_Obj_01'); s.tecla('8', '7'); s.arrastre('izq', 50, 0); s.arrastre('der', 50, 0)
    s.cursor_en('SpotLight_Key ', 2); s.tecla('8', '3', '7')
    s.opcion('Grilla de orientacion'); s.tecla('8'); s.opcion('Marcador en cada foco'); s.tecla('8'); s.cuadro(40)
    assert not w.m.escrituras and not s.llamadas, w.m.escrituras[:3]
    assert 'SOLO LECTURA' in s.texto
    # sin_llamadas.txt
    w = Mundo(con_limite=True); s = Sim(F, w, existe=('Default.fpkd', 'Default.fpk', 'sin_llamadas.txt')); s.cuadro(2)
    s.cursor_en('zfx_Obj_01'); s.arrastre('izq', 50, 0); s.cursor_en('SpotLight_Key ', 2); s.tecla('3')
    assert not s.llamadas and w.m.escrituras
    # no puede escribir el archivo de registro: sigue andando
    w = Mundo(); s = Sim(F, w); s.SIM[b'sin_archivo'] = True; s.cuadro(5); s.tecla('0', '8')
    assert any('no pude escribir' in x for x in s.logs())
    # registro muy largo: empieza de nuevo
    w = Mundo(); s = Sim(F, w); s.SIM[b'tam_extra'] = 3000000; s.cuadro(3)
    # recarga del modulo con el juego abierto (mismo ctx): recuerda como estaba todo y lo devuelve
    w = Mundo(); s = Sim(F, w); s.cuadro(2); a0 = estado_objeto(w, 'zfx_Obj_01'); orig = w.m.leer(w.cam + 0x10, 0x50)
    s.cursor_en('zfx_Obj_01'); s.arrastre('izq', 80, 30); s.arrastre('der', 80, 30)
    s.recargar(F); s.cuadro(2); assert '[ESCENA]' in s.texto
    s.opcion('Dejar todo como estaba'); s.tecla('8')
    cerca(estado_objeto(w, 'zfx_Obj_01')['T'], a0['T']); assert w.m.leer(w.cam + 0x10, 0x50) == orig


def e_listas(F):
    """las escenas como las deja el motor (cola de altas, listas por tipo, bajas), las clases de luz y lo que corrigio la 0.2.1"""
    BOTAS = '/Assets/pes16/model/character/boots.fmdl'
    # 1. tres objetos nacen en la cola de altas: aparecen ya en el primer cuadro, una sola vez, y siguen despues del vaciado
    w = Mundo(extras=True, en_cola=('zfx_Obj_02', 'SpotLight_Key', BOTAS)); s = Sim(F, w); m = w.m
    assert m.r32(w.esc[0] + 0x48) == 3 and m.r32(w.por_nombre('zfx_Obj_02')[0] + 0x24) == 3
    t = s.cuadro(1); assert ' de 8 (+48 de ayuda)' in t and 'zfx_Obj_02' in t and '~boots.fmdl' in t, t
    r = s.registro(); assert 'listas del motor comprobadas' in r and 'en cola 3' in r and 'NO coincide' not in r, r[:1500]
    s.cuadro(40); assert m.r32(w.esc[0] + 0x48) == 0 and m.r32(w.por_nombre('zfx_Obj_02')[0] + 0x24) == 1
    assert ' de 8 (+48 de ayuda)' in s.texto and s.texto.count('zfx_Obj_02') == 1 and '> [ ] ~boots'.replace('> [', '>[') in s.texto, s.texto   # el cursor sigue al mismo objeto
    s.pagina(2); assert ' de 31   prendidas' in s.texto, s.texto          # 26 focos + sol + punto + local + luz base + 1 de otra escena
    # 2. cada clase de luz con sus campos (el sol no tiene posicion ni alcance; solo el foco tiene cono)
    s.cursor_en('Sol_prueba', 2); assert 'dir    0.300  -0.800  0.500' in s.texto and 'sin alcance' in s.texto and 'cono   no tiene' in s.texto, s.texto
    n = len(m.escrituras); s.tecla('2'); assert 'no tiene posicion' in s.texto and len(m.escrituras) == n, s.texto
    s.cursor_en('Punto_prueba', 2); assert 'pos    1.000  2.000  3.000' in s.texto and 'alcance 7.5 m' in s.texto and 'cono   no tiene' in s.texto, s.texto
    o, d = w.luz('Punto_prueba'); c0 = m.rf(d['p'] + 0x40, 4); s.tecla('3')
    cerca(m.rf(d['p'] + 0x40, 4), [c0[0] * 1.25, c0[1] * 1.25, c0[2] * 1.25, c0[3]]); assert s.llamadas[-1] == (FN_COLOR, o)
    s.cursor_en('Local_prueba', 2); assert 'alcance 5.0 m' in s.texto and 'cono   no tiene' in s.texto, s.texto
    s.cursor_en('SpotLight_Key ', 2); assert 'cono   57 / 69 grados' in s.texto and 'alcance 10.0 m' in s.texto, s.texto
    # un foco al que le falta su bloque de dibujo: el brillo no llama al juego (la funcion del juego escribiria en nulo)
    o, d = w.luz('SpotLight_Key'); m.u64(o + 0xd8, 0); n = len(s.llamadas); s.tecla('3')
    assert len(s.llamadas) == n and 'bloque de dibujo' in s.texto, s.texto
    # 3. marcadores: solo las luces de la escena de los marcadores (26 focos + punto + local = 28 candidatas, 26 marcadores)
    s.opcion('Marcador en cada foco'); s.tecla('8'); s.cuadro(40)
    for i in range(26):
        e = estado_objeto(w, 'zfx_Luz_%02d' % (i + 1)); assert not e['oculto']
        assert np.linalg.norm(e['T'] - np.array([9.0, 9.0, 9.0])) > 1.0, 'un marcador fue a la luz de otra escena'
    s.opcion('Dejar todo como estaba'); s.tecla('8'); s.cuadro(40)
    # 4. un modelo que el JUEGO tiene oculto solo con la mascara: se ve como oculto, se puede mostrar y vuelve EXACTO
    oc = next(o for o, d in w.modelos.items() if d['nombre'] and 'oculto_del_juego' in d['nombre'])
    s.cursor_en('~oculto_del_juego'); fila = next(l for l in s.texto.split('\n') if '>' in l and 'oculto_del_juego' in l)
    assert fila.rstrip().endswith('oc'), fila
    s.tecla('7'); assert m.r32(oc + 0x1ac) == 0 and not m.r32(oc + 0x1b4) & 1
    s.opcion('Dejar todo como estaba'); s.tecla('8')
    assert m.r32(oc + 0x1ac) == 0xffffffff and not m.r32(oc + 0x1b4) & 1, (hex(m.r32(oc + 0x1ac)), hex(m.r32(oc + 0x1b4)))
    s.cursor_en('~oculto_del_juego'); s.tecla('7', '7')      # mostrar y volver a ocultar a mano: oculto del todo
    assert m.r32(oc + 0x1ac) == 0xffffffff and m.r32(oc + 0x1b4) & 1
    s.opcion('Dejar todo como estaba'); s.tecla('8'); assert m.r32(oc + 0x1ac) == 0xffffffff and not m.r32(oc + 0x1b4) & 1
    # 5. la escala de un objeto girado es el largo de cada fila, no la diagonal
    s.cursor_en('zfx_Obj_02'); assert 'escala 2.00 2.00 2.00' in s.texto, s.texto
    assert not m.malas
    # 6. el juego da de baja un objeto (como empieza su destructor): ni una escritura ni una llamada sobre el
    w = Mundo(con_limite=True); s = Sim(F, w); s.cuadro(3); m = w.m
    o, d = w.por_nombre('zfx_Obj_01'); s.cursor_en('zfx_Obj_01'); s.tecla('8')
    w.sacar(o); n = len(m.escrituras)
    s.raton(izq=True); s.cuadro(9); s.raton(izq=True, dx=30); s.raton(); s.tecla('7')
    assert len(m.escrituras) == n and not s.llamadas and not m.malas, (m.escrituras[n:], s.llamadas)
    s.cuadro(40); assert ' de 6 (+48 de ayuda)' in s.texto and 'zfx_Obj_01' not in s.texto, s.texto
    o, d = w.luz('SpotLight_Key'); s.cursor_en('SpotLight_Key ', 2); w.sacar(o); n = len(m.escrituras); f0 = m.r32(d['p'] + 0x64)
    s.tecla('8'); assert len(m.escrituras) == n and not s.llamadas and m.r32(d['p'] + 0x64) == f0
    s.tecla('3', '8')        # (el cursor ya esta sobre otra luz, que si existe: esas si se tocan; la que se fue, nunca)
    assert (FN_COLOR, o) not in s.llamadas and m.r32(d['p'] + 0x64) == f0 and not m.malas and 'SpotLight_Key ' not in s.texto, s.texto
    # 7. una lista cuya cantidad no coincide con lo recorrido: lista todo pero no escribe sobre modelos ni luces, y lo dice
    w = Mundo(); m = w.m; lst = w._lista(w.esc[0], TIPO['modelo']); m.u32(lst + 0x10, m.r32(lst + 0x10) + 1)
    s = Sim(F, w); s.cuadro(3); assert 'OBJETO 1 de 7 (+48 de ayuda)' in s.texto, s.texto
    s.cursor_en('zfx_Obj_01'); s.tecla('8', '7'); s.arrastre('izq', 60, 0)
    s.cursor_en('SpotLight_Key ', 2); s.tecla('8', '3')
    s.opcion('Grilla de orientacion'); s.tecla('8'); s.cuadro(200)
    assert not m.escrituras and not s.llamadas, m.escrituras[:3]
    assert 'las listas del motor no coinciden' in s.texto, s.texto
    r = s.registro(); assert 'las listas del motor NO coinciden' in r and 'cantidad del motor 83, recorridos 82' in r, [x for x in r.split('\r\n') if 'escena' in x][:6]
    s.arrastre('der', 50, 0); assert m.escrituras and all(w.cam <= a < w.cam + 0x60 for a, _ in m.escrituras)      # la camara si anda
    m.u32(lst + 0x10, m.r32(lst + 0x10) - 1); s.cuadro(40)          # (era pasajero: en cuanto coincide una vez, anda)
    s.cursor_en('zfx_Obj_01'); s.tecla('7'); assert estado_objeto(w, 'zfx_Obj_01')['oculto']
    # 8. un ciclo en una lista (el ultimo apunta al primero): cada objeto una sola vez, no se cuelga, y queda en solo lectura
    w = Mundo(); m = w.m; lst = w._lista(w.esc[0], TIPO['modelo']); m.u64(m.r64(lst + 8) + 0x18, m.r64(lst))
    s = Sim(F, w); lect = m.lecturas; s.cuadro(3)
    assert 'OBJETO 1 de 7 (+48 de ayuda)' in s.texto and m.lecturas - lect < 2000, (s.texto, m.lecturas - lect)
    # 9. cantidad de listas basura y arreglo basura: no lista nada de esa escena y no rompe
    w = Mundo(); m = w.m; m.u32(w.esc[0] + 0x28, 0xffffffff); m.u64(w.esc[1] + 0x30, 0x7ff812345678)
    s = Sim(F, w); s.cuadro(3); assert 'no hay modelos' in s.texto and not m.escrituras, s.texto
    # 10. el gesto de acercar (izq + der) no mueve lo marcado, se suelte primero el que se suelte y se apriete primero el que sea
    for herr in (0, 1, 2):
        w = Mundo(con_limite=True); s = Sim(F, w); s.cuadro(3); m = w.m
        s.cursor_en('zfx_Obj_01'); s.tecla('8')
        for _ in range(herr): s.tecla('4')
        a0 = estado_objeto(w, 'zfx_Obj_01')
        s.raton(izq=True, der=True)
        for _ in range(10): s.raton(izq=True, der=True, dx=5, dy=7)
        for _ in range(4): s.raton(izq=True, dx=6, dy=3)          # se solto antes el derecho, con el mouse en movimiento
        s.raton(); s.cuadro(3)
        s.raton(izq=True, dx=4); s.raton(izq=True, dx=4)           # se aprieta antes el izquierdo, 2 cuadros
        for _ in range(6): s.raton(izq=True, der=True, dy=5)
        s.raton(der=True, dy=5); s.raton(); s.cuadro(3)
        a1 = estado_objeto(w, 'zfx_Obj_01'); cerca(a1['T'], a0['T'], 1e-7); cerca(a1['S'], a0['S'], 1e-7); cerca(a1['Q'], a0['Q'], 1e-7)
        assert not s.llamadas and all(w.cam <= a < w.cam + 0x60 for a, _ in m.escrituras), 'el gesto de acercar toco un objeto'
    # con un arrastre en curso no se cambia de herramienta ni de eje
    s.tecla('5', '6'); s.raton(izq=True); s.cuadro(9); s.raton(izq=True, dx=20); s.tecla('4', '3')
    assert 'solta el mouse' in s.texto, s.texto
    s.raton()
    # 11. un pedido que falla no se reintenta ni se anota en cada cuadro (camara que no conozco + boton derecho sostenido)
    w = Mundo(); s = Sim(F, w); s.cuadro(3); w.m.u32(w.cam + 0x3c, 1); n = len(s.logs())
    for _ in range(120): s.raton(der=True, dx=1)
    assert len(s.logs()) - n <= 2 and not w.m.escrituras, s.logs()[n:n + 5]
    s.raton(); n = len(s.logs()); s.cuadro(3); s.pagina(1)
    for _ in range(120): s.raton(izq=True, dx=1)                    # herramienta sobre un modelo del juego: "nada para mover"
    assert len(s.logs()) - n <= 2 and not w.m.escrituras
    s.raton()
    # 12. los archivos de apagado valen con el juego abierto (se miran cada 60 llamadas)
    w = Mundo(con_limite=True); s = Sim(F, w); s.cuadro(3); m = w.m
    s.cursor_en('zfx_Obj_01'); s.tecla('8'); s.arrastre('izq', 30, 0); assert m.escrituras and s.llamadas
    s.SIM[b'existe'][b'sin_llamadas.txt'] = True; s.cuadro(70); n = len(s.llamadas); s.arrastre('izq', 30, 0); assert len(s.llamadas) == n
    s.SIM[b'existe'][b'solo_lectura.txt'] = True; s.cuadro(70); n = len(m.escrituras)
    s.arrastre('izq', 30, 0); s.tecla('7'); s.arrastre('der', 30, 0); assert len(m.escrituras) == n and 'SOLO LECTURA' in s.registro()
    s.SIM[b'existe'][b'apagado.txt'] = True; s.cuadro(70); assert 'apagado por' in s.texto, s.texto
    lect = m.lecturas; s.tecla('7', '8'); s.cuadro(30); assert m.lecturas == lect and len(m.escrituras) == n
    for k in (b'apagado.txt', b'solo_lectura.txt', b'sin_llamadas.txt'): s.SIM[b'existe'][k] = None
    s.cuadro(70); assert '[ESCENA]' in s.texto; s.arrastre('izq', 30, 0); assert len(m.escrituras) > n
    # 13. panel compacto en OPCIONES: se ve sobre que opcion esta el cursor
    s.opcion('Panel'); s.tecla('8'); assert 'panel compacto' in s.texto and '> Panel compacto' in s.texto, s.texto
    s.tecla('0'); assert '> Dejar todo como estaba' in s.texto, s.texto
    print('   cola de altas, listas por tipo, bajas, clases de luz, compuerta de listas, gestos del mouse, apagado en vivo: bien')


def _renacer(w, o, archivo=None, mascara=0, bit=0):
    """el motor destruye un Model y el asignador le da la MISMA direccion a uno nuevo (ctor FUN_141696f60: todo en cero,
    sin functores), que entra a la lista de modelos. Con 'archivo' es otro .fmdl; sin el, el mismo archivo"""
    m = w.m; esc = m.r64(o + 0x28); viejo = m.r64(o + 0x118)
    if m.r32(o + 0x24) & 1: w.sacar(o)
    m.reg[o][:] = bytes(0x200)
    m.u64(o, VT['modelo']); m.u32(o + 0x30, TIPO['modelo']); m.u64(o + 0x28, esc)
    m.f(o + 0x40, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1); m.f(o + 0xa0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    m.f(o + 0x1bc, 1.0); m.f(o + 0x1c4, 1.0)
    if archivo:
        f = m.nuevo(0x40); rec = m.nuevo(0x10); m.u64(rec, m.cadena(archivo)); m.u64(f + 0x30, rec); m.u64(o + 0x118, f)
    else:
        m.u64(o + 0x118, viejo)
    m.u32(o + 0x1ac, mascara); m.u32(o + 0x1b4, 0x10 | bit)
    w._enlazar(w._lista(esc, TIPO['modelo']), o)
    w.rangos[o] = [(o + 0x1ac, o + 0x1b8)]; m.permitidas += w.rangos[o]


def e_repaso(F):
    """lo que encontro la revision de la 0.2.1: lista leida a medio cambiar, reponer sin poder escribir, 'dejarla sola' sin
    poder escribir, direccion reusada por otro modelo, escena ajena que no coincide"""
    BOTAS = '/Assets/pes16/model/character/boots.fmdl'
    fila = lambda s, n: [l.split('|')[-1].strip() for l in s.texto.split('\n') if n in l.split('|')[-1] and '[' in l.split('|')[-1]]
    # 1. una lista a medio cambiar durante UNA lectura (el motor apaga y prende el bit0 al mover un objeto): no se pierden las
    #    marcas ni el cursor y no se escribe nada (los marcadores no se esconden y vuelven)
    for cual, saltos in (('modelo', 0), ('luz', 5)):
        w = Mundo(); s = Sim(F, w); s.cuadro(3); m = w.m
        s.cursor_en('zfx_Obj_02'); s.tecla('8'); s.opcion('Marcador en cada foco'); s.tecla('8'); s.cuadro(40); s.pagina(1)
        antes = fila(s, 'zfx_Obj_02'); assert antes and antes[0].startswith('>[x]'), s.texto
        o = m.r64(w._lista(w.esc[0], TIPO[cual]))
        for _ in range(saltos): o = m.r64(o + 0x18)
        arm = {'n': 1}; leer0 = m.leer
        def leer(dir, n, o=o, arm=arm, leer0=leer0):
            d = leer0(dir, n)
            if d is not None and dir == o and n == 0x38 and arm['n'] > 0:
                arm['n'] -= 1
                b = bytearray(d); struct.pack_into('<I', b, 0x24, struct.unpack_from('<I', b, 0x24)[0] & 0xfffffffe)
                return bytes(b)
            return d
        m.leer = leer
        ne = len(m.escrituras); s.cuadro(40)
        assert arm['n'] == 0, 'la lectura a medio cambiar no llego a pasar'
        assert fila(s, 'zfx_Obj_02') == antes and len(m.escrituras) == ne and not m.malas, (cual, fila(s, 'zfx_Obj_02'), len(m.escrituras) - ne)
        m.leer = leer0
    # 2. "Dejar todo como estaba" sin poder escribir: no dice que repuso ni olvida los originales
    w = Mundo(extras=True, con_limite=True); s = Sim(F, w); s.cuadro(5); m = w.m
    _, dl = w.luz('SpotLight_Key'); f0 = m.r32(dl['p'] + 0x64); c0 = m.rf(dl['p'] + 0x40, 4)
    s.cursor_en('SpotLight_Key ', 2); s.tecla('8', '3', '3')
    _, dm = w.por_nombre('zfx_Obj_01'); T0 = m.rf(dm['te'] + 0x50, 3)
    s.cursor_en('zfx_Obj_01'); s.tecla('8'); s.arrastre('izq', 60, 0); s.tecla('7')
    assert m.r32(dl['p'] + 0x64) != f0 and m.rf(dl['p'] + 0x40, 4) != c0 and m.rf(dm['te'] + 0x50, 3) != T0
    s.SIM[b'existe'][b'solo_lectura.txt'] = True; s.cuadro(70); ne = len(m.escrituras)
    s.opcion('Dejar todo como estaba'); s.tecla('8')
    assert 'en SOLO LECTURA no se repone nada' in s.texto and len(m.escrituras) == ne, s.texto
    s.SIM[b'existe'][b'solo_lectura.txt'] = None; s.SIM[b'existe'][b'sin_llamadas.txt'] = True; s.cuadro(70)
    s.opcion('Dejar todo como estaba'); s.tecla('8')
    assert 'NO repuse 1' in s.texto and m.rf(dl['p'] + 0x40, 4) != c0, s.texto        # (el brillo necesita la llamada)
    cerca(m.rf(dm['te'] + 0x50, 3), T0, 1e-6); assert not estado_objeto(w, 'zfx_Obj_01')['oculto']
    s.SIM[b'existe'][b'sin_llamadas.txt'] = None; s.cuadro(70)
    s.opcion('Dejar todo como estaba'); s.tecla('8')
    assert 'NO repuse' not in s.texto and m.r32(dl['p'] + 0x64) == f0, s.texto
    cerca(m.rf(dl['p'] + 0x40, 4), c0, 1e-3)
    # 3. "dejarla sola" y prender / apagar sin poder escribir: ni lo anota ni muestra (SOLA)
    w = Mundo(); m = w.m; lst = w._lista(w.esc[0], TIPO['modelo']); m.u32(lst + 0x10, m.r32(lst + 0x10) + 1)     # compuerta cerrada
    s = Sim(F, w); s.cuadro(3); s.cursor_en('SpotLight_Key ', 2); n = len(s.logs()); s.tecla('7')
    assert not m.escrituras and '(SOLA)' not in s.texto and not [x for x in s.logs()[n:] if 'queda sola' in x], s.texto
    s.tecla('8'); assert not m.escrituras
    w = Mundo(); m = w.m; s = Sim(F, w); s.cuadro(3); s.SIM[b'existe'][b'solo_lectura.txt'] = True; s.cuadro(70)
    s.cursor_en('SpotLight_Key ', 2); n = len(s.logs()); s.tecla('7')
    assert not m.escrituras and '(SOLA)' not in s.texto and 'no pude tocar las luces' in s.texto, s.texto
    s.tecla('8'); assert not m.escrituras and 'no pude tocar esa luz' in s.texto, s.texto
    s.SIM[b'existe'][b'solo_lectura.txt'] = None; s.cuadro(70); s.tecla('7'); assert '(SOLA)' in s.texto and m.escrituras, s.texto
    s.tecla('7'); assert '(SOLA)' not in s.texto
    # 4. la direccion de un objeto marcado pasa a OTRO modelo entre dos exploraciones: la tecla de ocultar no lo toca, y
    #    despues no hereda ni la marca ni el nombre
    for nombre in ('zfx_Obj_01', BOTAS):
        w = Mundo(con_limite=True); s = Sim(F, w); s.cuadro(3); m = w.m
        o, d = w.por_nombre(nombre); s.cursor_en(nombre.split('/')[-1]); s.tecla('8')
        _renacer(w, o, '/Assets/pes16/model/character/hair_NUEVO.fmdl')
        if d.get('cuerpo'): m.u64(d['cuerpo'], VT['otro'])                 # el cuerpo viejo ya fue destruido
        ne = len(m.escrituras); s.tecla('7')
        assert len(m.escrituras) == ne and m.r32(o + 0x1ac) == 0 and m.r32(o + 0x1b4) & 1 == 0, 'oculto al modelo nuevo'
        s.cuadro(80); s.pagina(1)
        f = fila(s, 'hair_NUEVO'); assert f and '[ ]' in f[0] and not fila(s, nombre.split('/')[-1]), s.texto
        s.opcion('Dejar todo como estaba'); s.tecla('8'); s.cuadro(5)
        assert m.r32(o + 0x1ac) == 0 and m.r32(o + 0x1b4) & 1 == 0 and not m.malas
    # 5. un modelo del juego que toque y que el motor destruye: si renace en la misma direccion (mismo archivo) con el panel
    #    abierto, "Dejar todo como estaba" no le pone el estado del anterior
    for buscado, toques, nace in (('oculto_del_juego', 1, (0, 0)), ('face_high', 1, (0, 0)), ('face_high', 1, (0xffffffff, 0)), ('face_high', 2, (0xffffffff, 0))):
        w = Mundo(extras=True); s = Sim(F, w); m = w.m; s.cuadro(3)
        o = [a for a, d in w.modelos.items() if d['nombre'] and buscado in d['nombre']][0]
        s.cursor_en('~' + buscado)
        for _ in range(toques): s.tecla('7')
        w.sacar(o); s.cuadro(80)                                           # una exploracion ve que ya no esta
        _renacer(w, o, None, *nace); s.cuadro(80)
        s.opcion('Dejar todo como estaba'); s.tecla('8'); s.cuadro(5)
        assert (m.r32(o + 0x1ac), m.r32(o + 0x1b4) & 1) == nace, (buscado, toques, nace, hex(m.r32(o + 0x1ac)))
    # 6. una escena ajena (sin modelos ni luces) cuya cantidad no coincide no deja al resto en solo lectura; queda anotada
    w = Mundo(); m = w.m; lst = w._lista(w.esc[1], TIPO['otro']); m.u32(lst + 0x10, m.r32(lst + 0x10) + 1)
    s = Sim(F, w); s.cuadro(300)
    r = s.registro(); assert 'listas del motor comprobadas' in r and 'sin modelos ni luces NO coincide' in r, [x for x in r.split('\r\n') if 'escena' in x][:6]
    assert 'no coinciden' not in s.texto, s.texto
    s.cursor_en('zfx_Obj_01'); s.tecla('7'); assert estado_objeto(w, 'zfx_Obj_01')['oculto'] and not m.malas
    # 7. panel compacto: el renglon de estado entra entero en el peor caso (ESCALAR, PANTALLA, camara del juego)
    s.tecla('4', '4'); s.opcion('Panel'); s.tecla('8')
    l2 = s.texto.split('\n')[1]; assert l2.endswith('(ver OPCIONES)') and len(l2) + 7 <= 112, (len(l2), l2)
    print('   lista a medio cambiar, reponer y "dejarla sola" sin poder escribir, direccion reusada, escena ajena: bien')


def e_roto(F):
    """la memoria cambia por debajo (el juego descarga cosas, listas con ciclos, punteros basura): nunca un error de Lua
    y nunca una escritura fuera de un objeto conocido"""
    rnd = random.Random(int(os.environ.get('MTR_SEMILLA', '1234')))
    teclas = '0023456789'
    for vuelta in range(60):
        w = Mundo(conj=rnd.random() < 0.3, con_limite=rnd.random() < 0.5, huesos=rnd.choice([0, 1, 2]), con_visor=rnd.random() < 0.8)
        s = Sim(F, w); m = w.m
        for paso in range(260):
            r = rnd.random()
            if r < 0.45: s.tecla(rnd.choice(teclas))
            elif r < 0.70:
                s.raton(izq=rnd.random() < 0.5, der=rnd.random() < 0.4, med=rnd.random() < 0.2, dx=rnd.randint(-40, 40), dy=rnd.randint(-40, 40))
            elif r < 0.76: s.cuadro(rnd.randint(1, 40))
            elif r < 0.80:
                # un arrastre entero con la herramienta o con la camara
                s.arrastre(rnd.choice(['izq', 'izq', 'der', 'med', 'izq+der']), rnd.randint(-80, 80), rnd.randint(-80, 80))
            elif r < 0.84:
                # el motor da de baja un objeto de escena como corresponde, o carga uno nuevo (entra por la cola de altas)
                if rnd.random() < 0.6:
                    vivos = [o for o in list(w.modelos) + list(w.luces) if m.leer(o, 0x38) is not None and m.r32(o + 0x24) & 1
                             and m.leer(m.r64(o + 0x28), 0x50) is not None and m.r64(o + 0x28) in w.esc]
                    if vivos:
                        o = rnd.choice(vivos)
                        try: w.sacar(o)
                        except (AssertionError, TypeError, struct.error): pass      # las listas ya estaban rotas por otro paso
                elif m.leer(w.esc[0], 0x50) is not None:
                    nombre = '/Assets/nuevo_%d.fmdl' % paso; w.en_cola.add(nombre)
                    try: w._modelo(w.esc[0], None, (1, 2, 3), archivo=nombre)
                    except (AssertionError, TypeError, struct.error): pass
            elif r < 0.92:
                # el juego descarga algo: desaparece una region cualquiera que no sea del exe
                bases = [b for b in m.reg if not (BASE <= b < EXE_FIN)]
                if bases: del m.reg[rnd.choice(bases)]
            else:
                # basura en un puntero o en un valor de algun objeto
                bases = [b for b in m.reg if not (BASE <= b < EXE_FIN)]
                if bases:
                    b = rnd.choice(bases); d = m.reg[b]; o = rnd.randrange(0, max(1, len(d) - 8), 8)
                    d[o:o + 8] = struct.pack('<Q', rnd.choice([0, 0xffffffffffffffff, 0x7ff8000000000000, 0xffffffd8ffffffdc, 0xfff80000ffc00001, 0x7f800000ff800000, 0xbf800000ffffff9c, rnd.choice(bases), rnd.getrandbits(47), 0x140000000 + rnd.getrandbits(24), b]))[:len(d) - o]
        assert not m.malas, 'vuelta %d: escritura fuera de lugar %s' % (vuelta, [(hex(a), n) for a, n in m.malas[:5]])
    print('   60 vueltas de 260 pasos con la memoria rompiendose: sin errores y sin escrituras fuera de lugar')


ESCENARIOS = dict(basico=e_basico, mover=e_mover, luces=e_luces, camara=e_camara, seguro=e_seguro, listas=e_listas, repaso=e_repaso, roto=e_roto)

if __name__ == '__main__':
    fuente = open(sys.argv[1], 'rb').read()
    for n in (sys.argv[2:] or list(ESCENARIOS)):
        print('== ' + n)
        ESCENARIOS[n](fuente)
        print('   bien')
