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
import json, math, os, random, re, struct, sys, zlib

import numpy as np
import lupa.luajit21 as L

BASE = 0x140000000
VT = dict(modelo=0x142a0a880, luz=0x142a0ad48, sol=0x142a0add8, local=0x142a0ae68, punto=0x142a0aef8, foco=0x142a0af88,
          ent_modelo=0x142a721e8, cuerpo=0x142a72268, functor=0x142a72358, cam=0x142aa5368, visor=0x142ba51e0,
          nodo=0x142c7a730, ent_foco=0x142a48360, te=0x14255ea00, dataset=0x142a01100, otro=0x142a01200, locator=0x142a01300,
          dg_luz=0x142a226a0, dg_sol=0x142a226b0, dg_local=0x142a226c0, dg_punto=0x142a226d0, dg_foco=0x142a226e0,
          # render (0.3): fox::gr::Render y las clases de etapa (plugin) con su direccion real (tablas/rtti.txt)
          render=0x142a07c58, plug_base=0x142a07bc0, plug_2d=0x142a0b6b8, plug_def=0x142a0c8d8, plug_geo=0x142a0c970,
          plug_shading=0x142a0cc88, plug_dof=0x142a0e688, plug_post=0x142a10040, plug_sombra=0x142a11eb0, plug_ton=0x142a13548,
          plug_ssao=0x142a14f00)
RTTI = {VT['modelo']: 'Model@gr@fox', VT['foco']: 'SpotLightSource@gr@fox', VT['ent_modelo']: 'StadiumModel@rd@cobra',
        VT['cuerpo']: 'StadiumModelBody@rd@cobra', VT['cam']: 'CameraImpl@camera@rc@cobra', VT['nodo']: 'Node@?A0xc19a98f1@impl@scene@draw',
        VT['ent_foco']: 'SpotLight@grx@fox', VT['te']: 'TransformEntity@fox', VT['dataset']: 'DataSet@fox',
        VT['otro']: 'Decal@gr@fox', VT['locator']: 'Locator@fox', VT['sol']: 'DirectionalLightSource@gr@fox',
        VT['dg_foco']: 'SpotLightSource@dg@gr@fox', VT['dg_sol']: 'DirectionalLightSource@dg@gr@fox',
        VT['plug_base']: 'RenderPlugin@gr@fox', VT['plug_2d']: 'Plugin2D@gr@fox', VT['plug_def']: 'PluginDeferredRendering@gr@fox',
        VT['plug_geo']: 'PluginDeferredGeometry@gr@fox', VT['plug_shading']: 'PluginDeferredShading@gr@fox',
        VT['plug_dof']: 'PluginDepthOfField@gr@fox', VT['plug_post']: 'PluginPostFilter@gr@fox', VT['plug_sombra']: 'PluginShadow@gr@fox',
        VT['plug_ton']: 'PluginTonemap@gr@fox', VT['plug_ssao']: 'PluginScreenSpaceAmbientOcclusion@gr@fox'}
RANURAS = [(0x142a0a880, 0x141697bd0), (0x142a0af88, 0x14169ed80), (0x142a0ad48, 0x14169ec20), (0x142a0add8, 0x14169eba0),
           (0x142a0ae68, 0x14169ec80), (0x142a0aef8, 0x14169ece0), (0x142a721e8, 0x141905750), (0x142a72268 + 48, 0x1419057e0),
           (0x142a72268 + 120, 0x1419064f0), (0x142a72358, 0x141905310), (0x142aa5368, 0x1419fcd70), (0x142ba51e0, 0x141ee73f0),
           (0x14255ea00, 0x1402bc920), (0x142a226a0, 0x14171d6e0), (0x142a226b0, 0x14171d620), (0x142a226c0, 0x14171d740),
           (0x142a226d0, 0x14171d7a0), (0x142a226e0, 0x14171d8c0),
           (0x142a07c58, 0x14168aae0), (0x142a07bc0 + 8 * 16, 0x14168c810)]
FN_COLOR, FN_MATRIZ = 0x14169f2e0, 0x14169de80
G_ESCENAS, G_VISOR, G_RENDER = 0x14374d958, 0x1438040d0, 0x14374ced8
G_CANDADO = 0x143756438       # candado del cuadro: { int cuenta, int giro, int hilo duenio }
# la pestana del menu de Editar (0.4): lo que el modulo lee y las TRES direcciones del exe que puede pisar
PE = dict(VT=0x1427012b0, ARMAR=0x140c29340, RANURA=0x142701520, FN11=0x141b9f460, FN12=0x1420df440, NOMBRES=0x1434c44a0,
          VACIO=0x142700fd3, TEXTOS=0x1427011c0, TEXTO12=0x1427011ec, SIN=0x14350a9a0, NOSTRING=0x142972e40, ID=0x7ffe0001)
PE_TEXTOS = [0x200026, 0x200027, 0x1a02a6, 0x1a00f1, 0x1d0013, 0x200064, 0x4b003c, 0x200033, 0x200059, 0x190055, 0x190056]
PE_NOMBRES = ['Player', 'Team', 'Coach', 'Representative', 'Transfer', 'Stadium', 'LeagueCup', 'LeagueOrganize', 'ExternalData',
              'DataSave', 'DataLoad']
PE_FILAS = 17                       # filas c_menu_0..16 de la pantalla editSettingList (leido del archivo del juego, 11/11/2026)
PE_ARCHIVO = b'common\\script\\flow\\Edit\\EditMenu.json'
PE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'package', 'sider', 'motor_tiempo_real', 'pestana')
FN_ACTIVAR = 0x14168c810            # ranura 16 de la clase base de las etapas: mov [rcx+0x48], dl
ACTIVAR_PROPIO = dict(plug_ton=0x1416cdb80, plug_dof=0x1416ae420)      # clases con interruptor propio (no se tocan)
# etapas del render en el orden del arreglo del gestor (a proposito, hijas antes que su padre): nombre, padre, clase, activa,
# prioridad, lista (execMode)
ETAPAS = [('SSAO', 'SHADING_PASS', 'plug_ssao', 0, 5, 2), ('GEOMETRY_PASS', 'DEFERRED', 'plug_geo', 1, 10, 2),
          ('DEFERRED', '', 'plug_def', 1, 100, 2), ('SUN_SHADOW', 'DEFERRED', 'plug_sombra', 1, 20, 2),
          ('SHADING_PASS', 'DEFERRED', 'plug_shading', 1, 30, 2), ('POSTFILTER', '', 'plug_post', 1, 200, 1),
          ('DOF', 'POSTFILTER', 'plug_dof', 1, 10, 1), ('TONEMAP', 'POSTFILTER', 'plug_ton', 1, 20, 1),
          ('DRAW2D', '', 'plug_2d', 1, -5, 1), ('SIN_PADRE', 'NO_EXISTE', 'plug_base', 1, 0, 3), ('', '', 'plug_base', 1, 7, 0)]
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
        self.exe_ok = {}         # dir -> n: las unicas escrituras que el modulo puede hacer DENTRO del exe (la pestana)
        self.prot = {}           # dir -> proteccion de su pagina (2 = solo lectura: hay que abrirla con VirtualProtect; 4 = abierta)
        self.falla = set()       # direcciones donde WriteProcessMemory falla (para probar la marcha atras)
        self.sin_permiso = set() # direcciones donde VirtualProtect falla
        self.exe_escr = []       # (dir, datos) que el modulo escribio dentro del exe

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
            if d is None: self.malas.append((dir, len(datos)))
            elif BASE <= dir < EXE_FIN:
                if self.exe_ok.get(dir) != len(datos): self.malas.append((dir, len(datos)))
                elif self.prot.get(dir, 4) != 4 or dir in self.falla: return False      # pagina de solo lectura: Windows no escribe
                else: self.exe_escr.append((dir, bytes(datos)))
            elif not any(a <= dir and dir + len(datos) <= b for a, b in self.permitidas): self.malas.append((dir, len(datos)))
            elif dir in self.falla: return False
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
    def __init__(self, conj=False, con_ayudas=True, con_limite=False, huesos=1, con_visor=True, extras=False, en_cola=(), con_render=True):
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
        for k, vt in VT.items():                 # la ranura 16 de cada clase de etapa: la de la base, o la propia
            if k.startswith('plug_'): m.u64(vt + 0x80, ACTIVAR_PROPIO.get(k, FN_ACTIVAR))
        m.nuevo(0x40, FN_COLOR); m.escribir(FN_COLOR, bytes.fromhex('488b41380f28020f294040488b0148ff6078'), False)
        m.nuevo(0x40, FN_MATRIZ); m.escribir(FN_MATRIZ, bytes.fromhex('40534883ec200f2802488bd90f294140'), False)
        m.nuevo(8, G_ESCENAS); m.nuevo(8, G_VISOR); m.nuevo(8, G_RENDER); m.nuevo(0x10, G_CANDADO)
        self.gestor = self.render = None; self.etapas = {}
        if con_render: self._render()
        self._pestana()
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

    def _render(self):
        """gestor de render como lo deja el motor: +0x10 cantidad de etapas, +0x18 arreglo de punteros (lugar para 0x80),
        +0x20 lista de renders, +0x28 render principal (fox::gr::Render: +0xd0 distinto de cero mientras dibuja, +0xd8 modo).
        Etapa: +0x30 prioridad, +0x38 nombre, +0x40 indice, +0x44 tipo, +0x48 activa, +0x4c lista, +0x50 nombre del padre.
        El modulo solo puede pisar el byte +0x48 de cada etapa y el modo del render principal (+0xd8)"""
        m = self.m
        g, ren, arr = m.nuevo(0x50), m.nuevo(0xf8), m.nuevo(0x80 * 8)
        m.u64(G_RENDER, g); m.u32(g + 0x10, len(ETAPAS)); m.u64(g + 0x18, arr); m.u64(g + 0x20, ren); m.u64(g + 0x28, ren)
        m.u64(ren, VT['render']); m.u32(ren + 0xd8, 2); m.permitidas.append((ren + 0xd8, ren + 0xdc))
        vacio = m.fox_cadena('')                  # el motor usa UNA cadena vacia compartida
        for i, (nombre, padre, clase, activa, prio, modo) in enumerate(ETAPAS):
            p = m.nuevo(0x60); m.u64(p, VT[clase]); m.u32(p + 0x30, prio & 0xffffffff)
            m.u64(p + 0x38, m.fox_cadena(nombre) if nombre else vacio); m.u32(p + 0x40, i); m.u32(p + 0x44, 3)
            m.u8(p + 0x48, activa); m.u32(p + 0x4c, modo); m.u64(p + 0x50, m.fox_cadena(padre) if padre else vacio)
            m.u64(arr + 8 * i, p); m.permitidas.append((p + 0x48, p + 0x49))
            self.etapas[nombre] = p
        self.gestor, self.render, self.arr_etapas = g, ren, arr

    def etapa_activa(self, nombre): return self.m.leer(self.etapas[nombre] + 0x48, 1)[0]

    def _pestana(self):
        """lo del menu de Editar como esta en el exe 1.07.02: tabla de funciones de menu::MenuEditMenu (ranura 76 = armar,
        ranura 78 = "cuantos botones"), las funciones "return 11" y "return 12", la tabla de nombres de enlace (el lugar 12
        apunta a un texto vacio), la de numeros de texto (el lugar 12 vale 0) y el puntero al texto de reemplazo "No String".
        El modulo solo puede pisar la ranura 78, el numero de texto 12 y ese puntero; las dos primeras estan en paginas de
        solo lectura"""
        m = self.m
        def nuevo(n, dir):
            assert all(dir + n <= b or b + len(d) <= dir for b, d in m.reg.items()), 'region encimada %x' % dir
            return m.nuevo(n, dir)
        for fn, n in ((PE['FN11'], 11), (PE['FN12'], 12)):
            nuevo(0x10, fn); m.escribir(fn, bytes([0xb8, n, 0, 0, 0, 0xc3]) + b'\xcc' * 10, False)
        nuevo(87 * 8, PE['VT'])
        for i in range(87): m.u64(PE['VT'] + 8 * i, 0x140c20000 + 0x40 * i)
        m.u64(PE['VT'] + 76 * 8, PE['ARMAR']); m.u64(PE['RANURA'], PE['FN11'])
        nuevo(0x30, PE['TEXTOS'])
        for i, t in enumerate(PE_TEXTOS): m.u32(PE['TEXTOS'] + 4 * i, t)
        nuevo(8, PE['VACIO'] - 3); m.escribir(PE['VACIO'] - 3, b'abc\0\0\0\0\0', False)
        nuevo(12 * 8, PE['NOMBRES'])
        for i, t in enumerate(PE_NOMBRES):
            d = nuevo(0x20, 0x142700000 + 0x20 * i); m.escribir(d, t.encode() + b'\0', False); m.u64(PE['NOMBRES'] + 8 * i, d)
        m.u64(PE['NOMBRES'] + 8 * 11, PE['VACIO'])
        nuevo(8, PE['SIN']); m.u64(PE['SIN'], PE['NOSTRING'])
        nuevo(0x10, PE['NOSTRING']); m.escribir(PE['NOSTRING'], b'No String\0', False)
        m.exe_ok = {PE['RANURA']: 8, PE['TEXTO12']: 4, PE['SIN']: 8}
        m.prot = {PE['RANURA']: 2, PE['TEXTO12']: 2}

    def pe_estado(self):
        """(funcion de la ranura 78, numero de texto del lugar 12, puntero del texto de reemplazo)"""
        m = self.m; n = m.lecturas
        r = (m.r64(PE['RANURA']), m.r32(PE['TEXTO12']), m.r64(PE['SIN'])); m.lecturas = n
        return r

    def menu_editar(self, flujo):
        """lo que hace el JUEGO al abrir el menu de Editar con la memoria como esta ahora y este archivo de flujo, y al apretar
        cada boton (leido en menu::MenuEditMenu::vf76, FUN_140e3ec40 y el registro de enlaces del flujo). Devuelve
        [(texto del boton, pagina a la que lleva)]; falla si el juego se cerraria o se quedaria colgado"""
        m = self.m; n0 = m.lecturas
        def texto_c(p):
            t = b''
            while True:
                c = m.leer(p + len(t), 1); assert c is not None, 'el juego lee un texto en memoria que no existe (0x%x): se cierra' % p
                if c == b'\0': return t.decode('latin1')
                t += c
        def corto(k):
            x = re.match(r'\d+_(.*)$', k)
            return x.group(1) if x else k
        try:
            cod = m.leer(m.r64(PE['RANURA']), 6)
            assert cod is not None and cod[0] == 0xb8 and cod[5] == 0xc3, 'la ranura 78 no apunta a una funcion "return N": el juego se cierra'
            n = struct.unpack_from('<I', cod, 1)[0]
            assert n <= PE_FILAS and n <= 12, '%d botones: mas que las filas o que los lugares de las tablas' % n
            ev = json.loads(flujo.decode('ascii'))['events']
            botones = []
            for i in range(n):
                nombre = texto_c(m.r64(PE['NOMBRES'] + 8 * i)) or 'proceed'       # nombre vacio: el juego usa "proceed"
                tid = m.r32(PE['TEXTOS'] + 4 * i)
                assert tid != 0, 'el boton %d no tiene numero de texto' % i
                if (tid >> 16) in (0x19, 0x1a, 0x1d, 0x20, 0x4b): texto = 'texto %06x' % tid
                else: texto = texto_c(m.r64(PE['SIN']))                             # categoria que no existe: texto de reemplazo
                clave = next((k for k in ev if k == nombre or corto(k) == nombre), None)
                assert clave is not None and ev[clave]['to'], 'el boton %d (%s) no tiene enlace en el flujo: el juego se cuelga al apretarlo' % (i, nombre)
                botones.append((texto, ev[clave]['to']))
            return botones
        finally: m.lecturas = n0

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
-- la pestana: VirtualProtect solo sobre las dos direcciones de solo lectura que el modulo puede pisar; VirtualAlloc da una
-- pagina propia (nunca se libera)
SIM.reservas = 0
function Cf.mtr_VirtualProtect(dir, n, prot, antes)
    assert(type(dir) == "number" and dir == math.floor(dir), "direccion no entera")
    local a = py.proteger(dir, n, prot)
    if not a then return 0 end
    antes[0] = a
    return 1
end
function Cf.mtr_VirtualAlloc(dir, n, tipo, prot)
    assert(dir == 0 and n == 0x1000 and tipo == 0x3000 and prot == 4, "VirtualAlloc mal llamada")
    SIM.reservas = SIM.reservas + 1
    if SIM.sin_memoria then return 0 end
    return py.reservar(n)
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
function Cf.mtr_GetTickCount()
    if SIM.tick_paso then SIM.tick = SIM.tick + SIM.tick_paso end       -- el reloj avanza dentro de un cuadro (esperas del modulo)
    return SIM.tick
end
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
function Cf.mtr_CloseHandle(h)
    local n = tonumber(ffi_real.cast("intptr_t", h))
    if n == 0x7777 then SIM.map_cerrados = SIM.map_cerrados + 1 end
    abiertos[n] = nil
    return 1
end
-- la DLL grafica (motorlink.cpp): memoria compartida de 64 bytes "PES21FX_Motor_<pid>" y lo que hace una vez por cuadro
SIM.map_abiertos, SIM.map_cerrados, SIM.vistas, SIM.dll_pedidos, SIM.fila, SIM.dll_seq = 0, 0, 0, 0, { 0, 0, 0, 0 }, 0
function SIM.dll_crear(version)
    SIM.dll_buf = ffi_real.new("uint8_t[64]")
    local u = ffi_real.cast("uint32_t *", SIM.dll_buf)
    u[0] = 0x4C52544D; u[1] = version or 1
    SIM.dll_seq = u[2]
end
function SIM.dll_cuadro()
    if not SIM.dll_buf or SIM.dll_parada then return end
    local u, f = ffi_real.cast("uint32_t *", SIM.dll_buf), ffi_real.cast("float *", SIM.dll_buf)
    local function limpio(i, lo, hi)                -- NaN o fuera de rango = 0 (como Limpio() de la DLL)
        local b = u[i]
        if b > 0xff800000 or (b > 0x7f800000 and b < 0x80000000) then return 0 end
        local v = f[i]
        if v >= lo and v <= hi then return v end
        return 0
    end
    if u[2] ~= SIM.dll_seq then
        SIM.dll_seq = u[2]
        SIM.fila = { math.floor(limpio(3, 0, 10.5) + 0.5), (limpio(4, -1e30, 1e30) > 0.5) and 1 or 0, limpio(5, 0, 1), limpio(11, 0, 100) }
        SIM.dll_pedidos = SIM.dll_pedidos + 1
    end
    f[7], f[8], f[9], f[12] = SIM.fila[1], SIM.fila[2], SIM.fila[3], SIM.fila[4]
    u[10] = (SIM.fila[2] > 0.5) and 321 or 0
    u[6] = u[6] + 1
end
function Cf.mtr_OpenFileMappingA(acceso, heredar, nombre)
    assert(acceso == 0xF001F and heredar == 0 and type(nombre) == "string", "OpenFileMappingA mal llamada")
    SIM.map_intentos = (SIM.map_intentos or 0) + 1
    if not SIM.dll_buf or nombre ~= "PES21FX_Motor_4242" then return ffi_real.cast("void *", 0) end
    SIM.map_abiertos = SIM.map_abiertos + 1
    return ffi_real.cast("void *", 0x7777)
end
function Cf.mtr_MapViewOfFile(h, acceso, alto, bajo, n)
    assert(tonumber(ffi_real.cast("intptr_t", h)) == 0x7777 and acceso == 0xF001F and alto == 0 and bajo == 0 and n == 64, "MapViewOfFile mal llamada")
    if SIM.map_falla then return ffi_real.cast("void *", 0) end
    SIM.vistas = SIM.vistas + 1
    return ffi_real.cast("void *", SIM.dll_buf)
end
function Cf.mtr_UnmapViewOfFile(p)
    assert(p == ffi_real.cast("void *", SIM.dll_buf), "UnmapViewOfFile de otra cosa")
    SIM.vistas = SIM.vistas - 1
    return 1
end

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
SIM.ffi_falso, SIM.permitidos, SIM.ffi_real_string, SIM.ffi_real_cast = ffi_falso, permitidos, ffi_real.string, ffi_real.cast

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
        py = self.lua.table_from({b'leer': self._leer, b'escribir': self._escribir, b'llamada': self._llamada,
                                  b'proteger': self._proteger, b'reservar': self._reservar})
        self.cache = {}                  # lo que Sider recuerda: clave del modulo -> ruta entregada (o None)
        # la otra cache de Sider (key-cache.ttl-sec = 10 en la PC): por unos segundos recuerda el resultado de cada nombre
        # de archivo y NO llama al modulo. ttl None = sin ella. t_sider = reloj de Sider, en segundos
        self.kc, self.ttl, self.t_sider = {}, None, 0.0
        self.lua.execute(LUA_ENTORNO.encode(), py)
        g = self.lua.globals()
        self.SIM = g[b'SIM']
        for n in existe: self.SIM[b'existe'][n.encode()] = True
        if sin_ffi: self.SIM[b'permitidos'][b'ffi'] = None
        self.m, self.reg, self.ctx = g[b'cargar'](fuente, ctx)
        self.texto = ''; self.cuadros = 0

    def _leer(self, dir, n): return self.mem.leer(int(dir), int(n))
    def _escribir(self, dir, s): return self.mem.escribir(int(dir), s)

    def _proteger(self, dir, n, prot):
        dir, n, prot = int(dir), int(n), int(prot); m = self.mem
        if dir not in m.prot or m.exe_ok.get(dir) != n or prot not in (2, 4):
            m.malas.append((dir, n)); return None                 # el modulo no tiene que abrir ninguna otra pagina
        if dir in m.sin_permiso: return None
        antes = m.prot[dir]; m.prot[dir] = prot
        return antes

    def _reservar(self, n):
        d = self.mem.nuevo(int(n)); self.mem.permitidas.append((d, d + int(n)))
        return d

    def pedir(self, nombre=PE_ARCHIVO):
        """lo que hace Sider cuando el juego pide un archivo: le pide la clave al modulo y, si no tiene guardada la ruta de
        esa clave, le pide la ruta y comprueba que el archivo exista. Devuelve el nombre del archivo entregado o None"""
        if self.ttl:
            g = self.kc.get(nombre)
            if g and g[1] > self.t_sider: return g[0]
        k = self.reg[b'livecpk_make_key'](self.ctx, nombre)
        assert k is None or isinstance(k, bytes), k
        clave = k if k is not None else nombre
        if clave not in self.cache:
            r = self.reg[b'livecpk_get_filepath'](self.ctx, nombre, clave)
            assert r is None or isinstance(r, bytes), r
            if r is not None:
                assert r.startswith(self.ctx[b'sider_dir'] + b'content\\motor-tiempo-real\\'), r
                r = r.rsplit(b'\\', 1)[-1]
                if not self.SIM[b'existe'][r]: r = None
            self.cache[clave] = r
        m = self.mem
        assert not m.malas, 'escritura fuera de lugar: %s' % [(hex(d), n) for d, n in m.malas[:5]]
        assert all(v == 2 for v in m.prot.values()), 'una pagina del exe quedo abierta'
        res = self.cache[clave].decode() if self.cache[clave] else None
        if self.ttl: self.kc[nombre] = (res, self.t_sider + self.ttl)
        return res

    def editar(self, nombre=PE_ARCHIVO):
        """entra al menu de Editar: Sider resuelve el archivo de flujo y el juego arma el menu. Devuelve (archivo, botones)"""
        r = self.pedir(nombre)
        ms = bool(self.ctx[b'tournament_id'] and self.ctx[b'home_team'] and self.ctx[b'away_team'])
        if r: flujo = open(os.path.join(PE_DIR, r), 'rb').read()
        else: flujo = pe_flujo_original(ms)
        b = self.w.menu_editar(flujo)
        # el juego recuerda el cursor de cada menu y lo repone sin mirar la cantidad de botones (inferido del C): si alguna vez
        # hubo 12 botones, el cursor pudo quedar en el 12, y aceptar ahi pide el enlace "proceed" aunque el boton no se vea
        if len(b) == 12: self.cursor12 = True
        elif getattr(self, 'cursor12', False) and self.SIM[b'existe'][b'EditMenu_menuserver.json' if ms else b'EditMenu.json']:
            assert 'proceed' in json.loads(flujo.decode('ascii'))['events'], \
                'el cursor recordado quedo en el boton 12 y el flujo entregado (%s) no tiene ese enlace: el juego se cuelga' % r
        return r, b

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
            self.SIM[b'dll_cuadro']()                    # la DLL grafica atiende el pedido una vez por cuadro
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
        for _ in range(9):
            if ('[%s]' % ['ESCENA', 'LUCES', 'ENTIDADES', 'OPCIONES', 'REGISTRO', 'RENDER'][n - 1]) in self.texto: return
            self.tecla('6')
        raise AssertionError('no llego a la pagina %d' % n)
    def opcion(self, texto):
        self.pagina(4)
        for _ in range(40):
            if any(l.split(' | ')[-1].startswith('> ' + texto) for l in self.texto.split('\n')): return
            self.tecla('0')
        raise AssertionError('no encontre la opcion ' + texto)
    def golpe(self, *vks):
        """varias teclas dentro del MISMO cuadro (sin dibujar entre una y otra)"""
        for vk in vks: self.reg[b'key_down'](self.ctx, vk)
        self.cuadro()
    def fila_cursor(self):
        return next((l.split(' | ')[-1] for l in self.texto.split('\n') if l.split(' | ')[-1].startswith('>')), '')
    def dll(self, i, como='I'):
        """campo i (de a 4 bytes) de la memoria compartida de la DLL"""
        b = self.lua.eval('function() return SIM.ffi_real_string(SIM.dll_buf, 64) end')()
        return struct.unpack_from('<' + como, b, 4 * i)[0]
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
    poder escribir, direccion reusada por otro modelo, parte del jugador que sale y vuelve, escena ajena que no coincide"""
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
    # 5. una parte del jugador que escondi y que el motor saca de la escena un rato y vuelve a poner (el MISMO objeto, como
    #    FUN_141a4b130): "Dejar todo como estaba" la vuelve a mostrar
    w = Mundo(extras=True); s = Sim(F, w); m = w.m; s.cuadro(3)
    o = [a for a, d in w.modelos.items() if d['nombre'] and 'face_high' in d['nombre']][0]
    s.cursor_en('~face_high'); s.tecla('7'); assert m.r32(o + 0x1ac) == 0xffffffff and m.r32(o + 0x1b4) & 1 == 1
    esc = m.r64(o + 0x28); rangos = list(w.rangos.get(o, []))
    w.sacar(o); s.cuadro(80)
    w._enlazar(w._lista(esc, TIPO['modelo']), o); w.rangos[o] = rangos; m.permitidas += rangos; s.cuadro(80)
    s.opcion('Dejar todo como estaba'); s.tecla('8'); s.cuadro(5)
    assert m.r32(o + 0x1ac) == 0 and m.r32(o + 0x1b4) & 1 == 0 and not m.malas, hex(m.r32(o + 0x1ac))
    # 6. una escena ajena cuya cantidad no coincide: todo en solo lectura, con aviso y el detalle por escena en el registro
    w = Mundo(); m = w.m; lst = w._lista(w.esc[1], TIPO['otro']); m.u32(lst + 0x10, m.r32(lst + 0x10) + 1)
    s = Sim(F, w); s.cuadro(300)
    s.cursor_en('zfx_Obj_01'); s.tecla('8', '7'); s.arrastre('izq', 60, 0); s.cursor_en('SpotLight_Key ', 2); s.tecla('8', '3')
    r = s.registro(); assert not m.escrituras and not s.llamadas and 'las listas del motor no coinciden' in s.texto, s.texto
    assert 'las listas del motor NO coinciden' in r and 'cantidad del motor 4, recorridos 3' in r, [x for x in r.split('\r\n') if 'escena' in x][:6]
    m.u32(lst + 0x10, m.r32(lst + 0x10) - 1); s.cuadro(40); s.cursor_en('zfx_Obj_01'); s.tecla('7')
    assert estado_objeto(w, 'zfx_Obj_01')['oculto'] and not m.malas                    # (en cuanto coincide una vez, anda)
    # 7. panel compacto: el renglon de estado entra entero en el peor caso (ESCALAR, PANTALLA, camara del juego)
    s.tecla('4', '4'); s.opcion('Panel'); s.tecla('8')
    l2 = s.texto.split('\n')[1]; assert l2.endswith('(ver OPCIONES)') and len(l2) + 7 <= 112, (len(l2), l2)
    print('   lista a medio cambiar, reponer y "dejarla sola" sin poder escribir, direccion reusada, escena ajena: bien')


def e_render(F):
    """0.3: pagina RENDER (etapas del render del motor y vistas de la DLL grafica), teclas G R S / X Y Z, tope de brillo"""
    etapas_reg = lambda s: [x for x in s.registro().split('\r\n') if x.startswith('etapa ')]
    n_esc = lambda m: len(m.escrituras)
    # ---------- 1. sin la DLL nueva: las etapas andan igual, las vistas dicen por que no
    w = Mundo(); s = Sim(F, w); m = w.m; s.cuadro(3)
    r = etapas_reg(s); orden = [x.split()[3] for x in r]
    assert len(r) == 11, r
    assert orden[:10] == ['DEFERRED', 'GEOMETRY_PASS', 'SUN_SHADOW', 'SHADING_PASS', 'SSAO', 'POSTFILTER', 'DOF', 'TONEMAP', 'DRAW2D', 'SIN_PADRE'], orden
    assert orden[10].startswith('etapa_') and '    SSAO' in r[4] and 'apagada' in r[4] and 'padre SHADING_PASS' in r[4], r
    assert 'INTERRUPTOR PROPIO' in r[6] and 'INTERRUPTOR PROPIO' in r[7] and 'INTERRUPTOR PROPIO' not in r[5], r[5:8]
    assert 'PADRE INEXISTENTE' in r[9] and 'prio -5 ' in r[8] and 'lista post' in r[8] and 'lista deferred' in r[0], r
    reg = s.registro()
    assert '11 etapas (1 apagadas)' in reg and 'modo 2' in reg and 'vistas: la DLL grafica instalada no trae las vistas' in reg, [x for x in reg.split('\r\n') if 'render' in x or 'vistas' in x]
    assert not m.escrituras
    s.pagina(6)
    assert 'RENDER  11 etapas, 1 apagadas' in s.texto and 'la DLL grafica instalada no trae las vistas' in s.texto, s.texto
    assert s.fila_cursor().split() == ['>', 'Vista', '-'], s.fila_cursor()
    s.tecla('8', '3', '7'); assert 'no trae las vistas' in s.texto and not m.escrituras
    s.cuadro(600); assert int(s.SIM[b'map_intentos']) <= 7, 'busca la DLL demasiado seguido: %d' % int(s.SIM[b'map_intentos'])
    # el motor mira el byte de TODA etapa dos veces por cuadro: una hija tampoco se cambia mientras arma el cuadro
    s.cursor_en('SSAO', 6); assert s.fila_cursor().startswith('>[ ]     SSAO') and 'dentro de  SHADING_PASS' in s.texto, s.texto
    m.u64(w.render + 0xd0, 0x1a0000100); s.SIM[b'tick_paso'] = 5; lect = m.lecturas
    s.tecla('8'); s.SIM[b'tick_paso'] = None
    assert w.etapa_activa('SSAO') == 0 and not m.escrituras and 'no dejo de dibujar' in s.texto, s.texto
    assert m.lecturas - lect < 20000, m.lecturas - lect
    m.u64(w.render + 0xd0, 0)
    # el candado del cuadro tomado por otro hilo: tampoco. Tomado por este mismo hilo (Sider me llama desde el cuadro): si
    m.u32(G_CANDADO, 1); m.u32(G_CANDADO + 8, 4321); s.SIM[b'tick_paso'] = 5
    s.tecla('8'); s.SIM[b'tick_paso'] = None
    assert w.etapa_activa('SSAO') == 0 and not m.escrituras and 'no dejo de dibujar' in s.texto, s.texto
    m.u32(G_CANDADO + 8, 77)
    s.tecla('8'); assert w.etapa_activa('SSAO') == 1 and m.escrituras == [(w.etapas['SSAO'] + 0x48, 1)], m.escrituras
    assert 'espere 1 vueltas' in s.registro(), [x for x in s.registro().split('\r\n') if 'render: etapa' in x]
    m.u32(G_CANDADO, 0); m.u32(G_CANDADO + 8, 0)
    assert s.fila_cursor().startswith('>[x]     SSAO') and 'PRENDIDA   (cambiada por mi)' in s.texto and '11 etapas, 0 apagadas' in s.texto, s.texto
    s.tecla('7'); assert w.etapa_activa('SSAO') == 0 and 'cambiada por mi' not in s.texto and n_esc(m) == 2
    s.tecla('7'); assert 'esta como la dejo el juego' in s.texto and n_esc(m) == 2, s.texto
    s.tecla('3'); assert '8 prende / apaga la etapa' in s.texto and n_esc(m) == 2, s.texto
    # una etapa suelta: igual, solo cuando el motor no esta dibujando (+0xd0 en cero); si no afloja en 60 ms, no se toca
    m.u64(w.render + 0xd0, 0x1a0000100)
    s.cursor_en('POSTFILTER', 6); s.SIM[b'tick_paso'] = 5; lect = m.lecturas
    s.tecla('8'); s.SIM[b'tick_paso'] = None
    assert w.etapa_activa('POSTFILTER') == 1 and n_esc(m) == 2 and 'no dejo de dibujar' in s.texto, s.texto
    assert m.lecturas - lect < 20000, m.lecturas - lect
    s.cursor_en('SIN_PADRE', 6); assert 'padre inexistente: NO_EXISTE' in s.texto, s.texto
    s.SIM[b'tick_paso'] = 5; s.tecla('8'); s.SIM[b'tick_paso'] = None
    assert w.etapa_activa('SIN_PADRE') == 1 and n_esc(m) == 2, 'una etapa de padre inexistente se trata como suelta'
    # ... y si afloja durante la espera, se cambia
    arm = {'n': 300}; leer0 = m.leer
    def leer(dir, n, arm=arm, leer0=leer0, d0=w.render + 0xd0):
        if dir == d0:
            arm['n'] -= 1
            if arm['n'] < 0: return bytes(n)
        return leer0(dir, n)
    m.leer = leer; s.cursor_en('DRAW2D', 6); arm['n'] = 300; s.SIM[b'tick_paso'] = 1; s.tecla('8'); s.SIM[b'tick_paso'] = None; m.leer = leer0
    assert w.etapa_activa('DRAW2D') == 0 and n_esc(m) == 3 and s.fila_cursor().startswith('>[ ] DRAW2D'), s.texto
    m.u64(w.render + 0xd0, 0)
    s.tecla('8'); assert w.etapa_activa('DRAW2D') == 1 and n_esc(m) == 4
    # las clases con interruptor propio no se tocan
    for nombre, clase in (('TONEMAP', 'PluginTonemap'), ('DOF', 'PluginDepthOfField')):
        s.cursor_en(nombre, 6); assert s.fila_cursor().startswith('>[=]   ' + nombre) and 'INTERRUPTOR PROPIO' in s.texto and clase in s.texto, s.texto
        s.tecla('8'); assert 'interruptor propio' in s.texto and n_esc(m) == 4 and w.etapa_activa(nombre) == 1, s.texto
        s.tecla('7'); assert n_esc(m) == 4
    s.cursor_en('etapa_', 6); assert 'dentro de  (suelta)' in s.texto and 'lista  pre   prioridad 7' in s.texto, s.texto
    # la etapa cambio por debajo entre la lista y la tecla (el motor rearmo su render): no se escribe
    s.cursor_en('GEOMETRY_PASS', 6); p = w.etapas['GEOMETRY_PASS']; m.u64(p, VT['plug_sombra'])
    s.tecla('8'); assert 'ya no esta' in s.texto and n_esc(m) == 4 and w.etapa_activa('GEOMETRY_PASS') == 1, s.texto
    m.u64(p, VT['plug_geo']); s.cuadro(40)
    # "Dejar todo como estaba" repone las etapas; tambien despues de recargar el modulo (Shift+R en Sider)
    s.cursor_en('SSAO', 6); s.tecla('8'); s.cursor_en('SUN_SHADOW', 6); s.tecla('8'); s.cursor_en('DRAW2D', 6); s.tecla('8')
    assert (w.etapa_activa('SSAO'), w.etapa_activa('SUN_SHADOW'), w.etapa_activa('DRAW2D')) == (1, 0, 0)
    s.recargar(F); s.cuadro(3); s.cursor_en('SUN_SHADOW', 6); assert 'apagada   (cambiada por mi)' in s.texto, s.texto
    # (una de las tres ya no es la misma etapa: en esa direccion hay otra con otro nombre -> no se toca)
    m.u64(w.etapas['DRAW2D'] + 0x38, m.fox_cadena('OTRA_COSA')); s.cuadro(40)
    s.opcion('Dejar todo como estaba'); s.tecla('8')
    assert (w.etapa_activa('SSAO'), w.etapa_activa('SUN_SHADOW'), w.etapa_activa('DRAW2D')) == (0, 1, 0), s.texto
    assert '2 etapas del render' in s.texto and 'NO repuse' not in s.texto, s.texto
    s.cursor_en('SUN_SHADOW', 6); assert 'cambiada por mi' not in s.texto
    assert not m.malas and all(n == 1 for _, n in m.escrituras)
    # en SOLO LECTURA no se cambia ninguna etapa
    w = Mundo(); s = Sim(F, w, existe=('Default.fpkd', 'Default.fpk', 'solo_lectura.txt')); m = w.m; s.cuadro(3)
    s.cursor_en('SUN_SHADOW', 6); s.tecla('8'); assert 'SOLO LECTURA' in s.texto and not m.escrituras, s.texto
    # ---------- 2. el gestor de render no esta, o no es el que conozco: lo dice y no rompe
    w = Mundo(con_render=False); s = Sim(F, w); s.cuadro(3); s.pagina(6)
    assert 'RENDER  0 etapas' in s.texto and 'todavia no armo su render' in s.texto, s.texto
    s.tecla('0', '0', '0', '8', '7', '9'); assert not w.m.escrituras
    for que in ('cantidad', 'arreglo', 'punteros', 'render', 'ciclo', 'repetida'):
        w = Mundo(); m = w.m
        if que == 'cantidad': m.u32(w.gestor + 0x10, 0xffffffff)
        elif que == 'arreglo': m.u64(w.gestor + 0x18, 0x7ff812345678)
        elif que == 'punteros':
            m.u64(w.arr_etapas, 0); m.u64(w.arr_etapas + 8, 0x7ff812345678); m.u64(w.etapas['DEFERRED'], 0x12345)       # nulo, ilegible, tabla basura
            m.u8(w.etapas['SUN_SHADOW'] + 0x48, 7); m.u32(w.etapas['DOF'] + 0x4c, 99)                                       # byte y lista imposibles
        elif que == 'render': m.u64(w.render, VT['otro'])
        elif que == 'ciclo':
            m.u64(w.etapas['DEFERRED'] + 0x50, m.fox_cadena('SSAO'))                 # DEFERRED > SHADING_PASS > SSAO > DEFERRED
        elif que == 'repetida': m.u64(w.etapas['POSTFILTER'] + 0x38, m.fox_cadena('DEFERRED'))
        s = Sim(F, w); s.cuadro(3); s.pagina(6); s.cuadro(40)
        if que == 'cantidad': assert 'no es el que conozco' in s.texto and '0 etapas' in s.texto, s.texto
        elif que == 'arreglo': assert 'no pude leer la lista de etapas' in s.texto, s.texto
        elif que == 'punteros': assert 'RENDER  6 etapas' in s.texto, s.texto
        elif que == 'render':
            s.cursor_en('DRAW2D', 6); s.tecla('8'); assert 'no encuentro el render principal' in s.texto and not m.escrituras, s.texto
            s.cursor_en('SSAO', 6); s.tecla('8'); assert w.etapa_activa('SSAO') == 0 and not m.escrituras      # (las hijas tambien lo necesitan)
            s.cursor_en('Modo motor', 6); assert s.fila_cursor().split()[-1] == '-', s.fila_cursor()
            s.tecla('8'); assert 'no encuentro el render principal' in s.texto and not m.escrituras, s.texto
        elif que in ('ciclo', 'repetida'): assert 'RENDER  11 etapas' in s.texto, s.texto
        for k in '0808070809999': s.tecla(k)
        assert not m.malas
    # ---------- 3. con la DLL nueva: vistas, alambre y pantalla partida por la memoria compartida
    w = Mundo(); s = Sim(F, w); m = w.m; S = s.SIM; s.cuadro(3); s.pagina(6); assert 'no trae las vistas' in s.texto
    S[b'dll_crear'](); s.cuadro(10); assert s.fila_cursor().split() == ['>', 'Vista', '-']       # se busca de nuevo cada 2 s, no en cada cuadro
    s.cuadro(200); assert s.fila_cursor().split() == ['>', 'Vista', 'Normal'] and 'DLL grafica: enlazada' in s.texto, s.texto
    assert (int(S[b'map_abiertos']), int(S[b'map_cerrados']), int(S[b'vistas'])) == (1, 1, 1)
    fila = lambda: [float(x) for x in S[b'fila'].values()]
    s.tecla('8'); assert fila() == [1, 0, 0, 0] and 'Luz difusa' in s.fila_cursor() and '1 de 10: Luz difusa' in s.texto, s.texto
    s.tecla('3'); assert fila()[0] == 2 and 'Luz especular' in s.fila_cursor()
    s.tecla('4'); assert fila()[0] == 1
    s.tecla('7'); assert fila()[0] == 0 and 'Normal' in s.fila_cursor()
    s.tecla('4'); assert fila()[0] == 10 and 'Material' in s.fila_cursor(), s.texto
    s.tecla('8'); assert fila()[0] == 0
    s.golpe(0x38, 0x38, 0x38); assert fila()[0] == 3 and 'Normales' in s.fila_cursor(), (fila(), s.texto)     # tres teclas en un cuadro
    assert int(S[b'dll_pedidos']) == 7 and s.dll(2) == 9, (int(S[b'dll_pedidos']), s.dll(2))
    vistos = set()
    for _ in range(11): s.tecla('8'); vistos.add(s.fila_cursor().strip())
    assert len(vistos) == 11 and fila()[0] == 3, vistos
    s.tecla('0'); assert s.fila_cursor().split() == ['>', 'Alambre', 'no']
    s.tecla('8'); s.cuadro(); assert fila() == [3, 1, 0, 0] and 'PRENDIDO   dibujos en alambre: 321' in s.texto and s.fila_cursor().split()[-1] == 'SI', s.texto
    s.tecla('3'); assert 'se prende y se apaga con 8' in s.texto and fila()[1] == 1
    s.tecla('0'); assert s.fila_cursor().split()[-1] == 'no' and 'toda la pantalla' in s.texto, s.texto
    s.tecla('8'); assert abs(fila()[2] - 0.5) < 1e-6 and '50 %' in s.fila_cursor() and 'izquierda del 50 %' in s.texto, s.texto
    s.tecla('3'); assert abs(fila()[2] - 0.55) < 1e-6 and '55 %' in s.fila_cursor()
    s.tecla('4', '4'); assert abs(fila()[2] - 0.45) < 1e-6
    for _ in range(25): s.tecla('4')
    assert abs(fila()[2] - 0.05) < 1e-6 and '5 %' in s.fila_cursor()
    for _ in range(25): s.tecla('3')
    assert abs(fila()[2] - 0.95) < 1e-6
    s.tecla('8'); assert fila()[2] == 0
    s.tecla('3'); assert abs(fila()[2] - 0.5) < 1e-6
    s.tecla('7'); assert fila() == [3, 1, 0, 0]
    # lo que se cambia desde el menu de la DLL se ve en el panel, y un pedido mio conserva lo demas (tambien la escala)
    S[b'fila'] = s.lua.eval('{ 5, 0, 0.25, 0.02 }'); s.cuadro(2)
    assert '25 %' in s.fila_cursor(); s.tecla('9'); assert s.fila_cursor().split()[-1] == 'no'; s.tecla('9'); assert 'Albedo' in s.fila_cursor()
    s.tecla('8'); f = fila(); assert f[0] == 6 and f[1] == 0 and abs(f[2] - 0.25) < 1e-6 and abs(f[3] - 0.02) < 1e-6, f
    # la DLL deja de avanzar (juego en pausa, o se descargo): lo dice; y un pedido pendiente se muestra como pedido
    S[b'dll_parada'] = True; s.cuadro(200); assert 'sin cuadros nuevos' in s.texto, s.texto
    s.tecla('8'); assert 'Oclusion' in s.fila_cursor() and fila()[0] == 6
    # basura en la memoria compartida (NaN, infinito, enormes): ni un error de Lua ni una vista imposible
    for bits in (0xffffffff, 0xffc00000, 0x7fc00000, 0x7f800000, 0xff800000, 0x7f7fffff, 0xc1200000, 0x41300000):
        s.lua.eval('function(b) local u = SIM.ffi_real_cast("uint32_t *", SIM.dll_buf); for _, i in ipairs({ 3, 4, 5, 7, 8, 9, 11, 12 }) do u[i] = b end; u[6] = u[6] + 1 end')(bits)
        s.cuadro(2)
        for k in '0098': s.tecla(k)
        s.lua.eval('function(b) local u = SIM.ffi_real_cast("uint32_t *", SIM.dll_buf); for _, i in ipairs({ 7, 8, 9, 12 }) do u[i] = b end end')(bits)
        s.tecla('9', '8', '9'); s.cuadro(2)
    S[b'dll_parada'] = None; s.cuadro(3); S[b'fila'] = s.lua.eval('{ 0, 0, 0, 0 }'); s.cuadro(3)
    # "Dejar todo como estaba": vista normal, sin alambre ni corte. Con todo ya en cero no pide nada
    s.pagina(6); s.cuadro(3)
    while 'Vista' not in s.fila_cursor(): s.tecla('9')
    s.tecla('8'); s.tecla('0'); s.tecla('8'); assert fila()[0] != 0 and fila()[1] == 1, fila()
    s.opcion('Dejar todo como estaba'); s.tecla('8'); s.cuadro(2); assert fila() == [0, 0, 0, 0], fila()
    n = int(S[b'dll_pedidos']); s.tecla('8'); s.cuadro(2); assert int(S[b'dll_pedidos']) == n
    # recarga del modulo con una vista puesta: "Dejar todo como estaba" la saca aunque no se haya vuelto a la pagina RENDER
    s.pagina(6)
    while 'Vista' not in s.fila_cursor(): s.tecla('9')
    s.tecla('8'); assert fila() == [1, 0, 0, 0], fila()
    s.recargar(F); s.cuadro(3); assert '[ESCENA]' in s.texto or '[RENDER]' in s.texto
    s.opcion('Dejar todo como estaba'); s.tecla('8'); s.cuadro(2); assert fila() == [0, 0, 0, 0], fila()
    # en SOLO LECTURA no se pide nada
    S[b'existe'][b'solo_lectura.txt'] = True; s.cuadro(70); s.pagina(6); n = s.dll(2); ne = len(m.escrituras)
    for k in '8038037': s.tecla(k)
    assert s.dll(2) == n and 'SOLO LECTURA' in s.texto and len(m.escrituras) == ne, s.texto
    # (en el paso de la basura el cursor paso por algunas etapas: lo unico que se escribio en el juego son bytes +0x48 de etapas)
    assert not m.malas and all((n == 1 and a - 0x48 in w.etapas.values()) or (n == 4 and a == w.render + 0xd8) for a, n in m.escrituras)
    # una DLL que habla otra version, o no se puede abrir la memoria: lo dice y no deja nada abierto
    w = Mundo(); s = Sim(F, w); S = s.SIM; S[b'dll_crear'](2); s.cuadro(3); s.pagina(6); s.cuadro(300)
    assert 'otra version del enlace' in s.texto and int(S[b'vistas']) == 0 and int(S[b'map_abiertos']) == int(S[b'map_cerrados']) >= 2, s.texto
    s.tecla('8'); assert s.dll(2) == 0
    w = Mundo(); s = Sim(F, w); S = s.SIM; S[b'dll_crear'](); S[b'map_falla'] = True; s.cuadro(3); s.pagina(6); s.cuadro(300)
    assert 'no pude abrir la memoria de la DLL' in s.texto and int(S[b'map_abiertos']) == int(S[b'map_cerrados']), s.texto
    S[b'map_falla'] = None; s.cuadro(200); assert 'enlazada' in s.texto
    # ---------- 4. teclas como Blender: G mover, R rotar, S escalar; X / Y / Z el eje (la misma otra vez = pantalla)
    w = Mundo(); s = Sim(F, w); m = w.m; s.cuadro(3); s.pagina(2)
    s.tecla(0x52); assert '[ESCENA]' in s.texto and 'HERRAMIENTA ROTAR   EJE PANTALLA' in s.texto and 'ROTAR en el eje PANTALLA' in s.texto, s.texto
    s.tecla(0x59); assert 'EJE Y' in s.texto
    s.tecla(0x59); assert 'EJE PANTALLA' in s.texto
    s.tecla(0x5A); assert 'EJE Z' in s.texto
    s.tecla(0x58); assert 'HERRAMIENTA ROTAR   EJE X' in s.texto
    s.tecla(0x47); assert 'HERRAMIENTA MOVER   EJE X' in s.texto
    s.pagina(6); s.tecla(0x53); assert '[ESCENA]' in s.texto and 'HERRAMIENTA ESCALAR   EJE X' in s.texto, s.texto
    s.tecla(0x58); assert 'HERRAMIENTA ESCALAR   EJE PANTALLA' in s.texto
    assert not m.escrituras
    a0 = estado_objeto(w, 'zfx_Obj_01'); s.cursor_en('zfx_Obj_01'); s.tecla('8'); s.arrastre('izq', 100, 0)
    cerca(estado_objeto(w, 'zfx_Obj_01')['S'], a0['S'] * math.exp(0.5), 1e-3)
    s.tecla(0x47, 0x59); s.arrastre('izq', 0, -300); k = 2 * 6.5 * math.tan(0.045) / 1080
    cerca(estado_objeto(w, 'zfx_Obj_01')['T'], a0['T'] + np.array([0, 300 * k, 0]), 2e-4)
    s.raton(izq=True); s.cuadro(9); s.raton(izq=True, dx=20); s.tecla(0x52, 0x5A)
    assert 'solta el mouse' in s.texto and 'HERRAMIENTA MOVER   EJE Y' in s.texto, s.texto
    s.raton(); s.cuadro(2)
    for vk in (0x41, 0x48, 0x54, 0x51, 0x0d, 0x20, 0x70): s.tecla(vk)        # otras teclas: nada
    assert 'HERRAMIENTA MOVER   EJE Y' in s.texto
    # ---------- 5. tope de brillo: 2000, u 8 veces lo que traia la luz
    w = Mundo(); s = Sim(F, w); m = w.m; s.cuadro(3)
    s.cursor_en('SpotLight_Key ', 2); o, d = w.luz('SpotLight_Key'); c0 = max(m.rf(d['p'] + 0x40, 3))
    for _ in range(80): s.tecla('3')
    c1 = max(m.rf(d['p'] + 0x40, 3)); tope = max(2000.0, 8 * c0)
    assert 'tope de brillo' in s.texto and tope / 1.3 < c1 <= tope * 1.0001, (c0, c1, tope, s.texto)
    n = len(s.llamadas); s.tecla('3'); assert len(s.llamadas) == n
    s.tecla('4'); assert max(m.rf(d['p'] + 0x40, 3)) < c1 and 'tope' not in s.texto
    # ---------- 6. EXPERIMENTAL: modo del motor (render +0xd8, lo que escribe GrTools.SwitchRenderMode), con vuelta sola
    w = Mundo(); s = Sim(F, w); m = w.m; S = s.SIM; s.cuadro(3); d8 = w.render + 0xd8
    modo = lambda: m.r32(d8)
    s.cursor_en('Modo motor', 6); assert s.fila_cursor().split() == ['>', 'Modo', 'motor', '(exp.)', 'deferred', '(normal)'], s.fila_cursor()
    assert 'MODO DEL MOTOR (EXPERIMENTAL' in s.texto and 'VUELVE AL NORMAL' not in s.texto, s.texto
    s.tecla('3'); assert '8 pasa al modo siguiente' in s.texto and not m.escrituras, s.texto
    s.tecla('7'); assert not m.escrituras and modo() == 2                     # ya esta en el normal: no escribe nada
    s.tecla('8'); assert modo() == 5 and m.escrituras == [(d8, 4)] and 'wireframe' in s.fila_cursor() and 'VUELVE AL NORMAL EN 8 s' in s.texto, s.texto
    assert 'MODO DEL MOTOR 5 wireframe' in s.registro() and 'vuelve solo a deferred en 8 s' in s.registro()
    vistos = []
    for _ in range(4): s.tecla('8'); vistos.append(modo())
    assert vistos == [6, 3, 4, 2] and 'VUELVE AL NORMAL' not in s.texto and 'deferred (normal)' in s.fila_cursor(), (vistos, s.texto)
    s.tecla('8'); s.tecla('7'); assert modo() == 2 and len(m.escrituras) == 7 and 'VUELVE' not in s.texto
    # vuelve solo a los 8 segundos (no antes), y lo anota
    s.tecla('8'); s.cuadro(400); assert modo() == 5 and 'VUELVE AL NORMAL EN 2 s' in s.texto, s.texto
    s.cuadro(110); assert modo() == 2 and 'fin del modo experimental' in s.registro() and 'VUELVE' not in s.texto, s.texto
    # el motor armando un cuadro (candado de otro hilo, o recorriendo etapas): no se cambia; la vuelta sola espera y reintenta
    m.u32(G_CANDADO, 1); m.u32(G_CANDADO + 8, 4321); S[b'tick_paso'] = 5; n = len(m.escrituras)
    s.tecla('8'); S[b'tick_paso'] = None
    assert modo() == 2 and len(m.escrituras) == n and 'no dejo de dibujar' in s.texto, s.texto
    m.u32(G_CANDADO, 0); m.u32(G_CANDADO + 8, 0)
    s.tecla('8'); assert modo() == 5
    m.u64(w.render + 0xd0, 0x1a0000100); S[b'tick_paso'] = 3; s.cuadro(400); S[b'tick_paso'] = None
    assert modo() == 5, 'con el motor dibujando no se escribe el modo'
    m.u64(w.render + 0xd0, 0); s.cuadro(2); assert modo() == 2, 'apenas afloja, vuelve al normal'
    # el modo cambiado y el modulo recargado (Shift+R): igual vuelve solo
    s.tecla('8'); assert modo() == 5
    s.recargar(F); s.cuadro(3); assert modo() == 5
    s.cuadro(520); assert modo() == 2, 'despues de recargar tiene que volver solo al normal'
    # "Dejar todo como estaba" lo saca ya
    s.cursor_en('Modo motor', 6); s.tecla('8'); s.tecla('8'); assert modo() == 6
    s.opcion('Dejar todo como estaba'); s.tecla('8'); assert modo() == 2, s.texto
    # SOLO LECTURA: no se entra a un modo, pero volver al normal se puede siempre (tecla 7 y vuelta sola)
    s.cursor_en('Modo motor', 6); s.tecla('8'); assert modo() == 5
    S[b'existe'][b'solo_lectura.txt'] = True; s.cuadro(70); n = len(m.escrituras)
    s.tecla('8'); assert modo() == 5 and len(m.escrituras) == n and 'SOLO LECTURA' in s.texto, s.texto
    s.tecla('7'); assert modo() == 2 and len(m.escrituras) == n + 1
    S[b'existe'][b'solo_lectura.txt'] = None; s.cuadro(70)
    s.tecla('8'); assert modo() == 5
    S[b'existe'][b'solo_lectura.txt'] = True; s.cuadro(600); assert modo() == 2, 'en solo lectura tambien vuelve solo'
    S[b'existe'][b'solo_lectura.txt'] = None; s.cuadro(70)
    # apagado.txt con un modo puesto: lo unico que hace el modulo apagado es volver al normal
    s.cursor_en('Modo motor', 6); s.tecla('8'); assert modo() == 5
    S[b'existe'][b'apagado.txt'] = True; s.cuadro(70); assert 'apagado por' in s.texto and modo() == 2, s.texto
    n = len(m.escrituras); s.cuadro(200); assert len(m.escrituras) == n
    S[b'existe'][b'apagado.txt'] = None; s.cuadro(70)
    # un modo que no conozco (otra cosa escribio ahi): se muestra y no se toca
    m.u32(d8, 9); s.cuadro(80); s.cursor_en('Modo motor', 6); assert s.fila_cursor().split()[-1] == '9', s.fila_cursor()
    n = len(m.escrituras); s.tecla('8'); s.tecla('7'); assert modo() == 9 and len(m.escrituras) == n and 'no es uno que conozca' in s.texto, s.texto
    m.u32(d8, 2); s.cuadro(80)
    assert not m.malas and all(a == d8 and n == 4 for a, n in m.escrituras), m.escrituras
    # sin render: la fila lo dice y no se escribe
    w = Mundo(con_render=False); s = Sim(F, w); s.cuadro(3); s.cursor_en('Modo motor', 6)
    assert s.fila_cursor().split()[-1] == '-' and 'no encuentro el render' in s.texto, s.texto
    s.tecla('8'); s.tecla('7'); assert not w.m.escrituras
    print('   etapas del render, vistas por la DLL, modo del motor experimental, teclas G R S / X Y Z, tope de brillo: bien')


def pe_flujo_original(menu_server=False):
    """el archivo de flujo del menu de Editar SIN la pestana: el del juego (2066 bytes, CRC 7C81894A) o el del mod Menu-server
    (2091 bytes, CRC FC5F8BE8). Sale de sacarle el enlace nuevo a los archivos del paquete: asi de paso se comprueba que
    son los originales byte por byte mas ese enlace y nada mas"""
    d = open(os.path.join(PE_DIR, 'EditMenu_menuserver.json' if menu_server else 'EditMenu.json'), 'rb').read()
    a = d.index(b',\n\t\t"proceed":{'); b = d.index(b'\n\t\t}', a) + 4
    j = json.loads(d.decode('ascii'))['events']['proceed']
    assert j == dict(to=j['to'], go='forward', clear='all', fadeStart='', fadeEnd='do', bgType='none'), j
    o = d[:a] + d[b:]
    assert (len(o), zlib.crc32(o)) == ((2091, 0xFC5F8BE8) if menu_server else (2066, 0x7C81894A)), (len(o), hex(zlib.crc32(o)))
    return o


def e_pestana(F):
    """la pestana "Motor en tiempo real" del menu de Editar: el boton 12 aparece solo cuando Sider entrega mi archivo de flujo,
    nunca quedan 12 botones con el archivo de otro, se saca sola, y las unicas escrituras en el exe son las tres previstas"""
    FAB = (PE['FN11'], 0, PE['NOSTRING'])
    CON = ('Default.fpkd', 'Default.fpk', 'EditMenu.json', 'EditMenu_menuserver.json')
    once = Mundo().menu_editar(pe_flujo_original())
    assert len(once) == 11 and once[0] == ('texto 200026', 'Edit/Player/EditPlayerMain') and once[8][1] == 'Edit/DataManagement/EditExternalDataMain'
    assert Mundo().menu_editar(pe_flujo_original(True)) == once
    destino = json.load(open(os.path.join(PE_DIR, 'EditMenu.json')))['events']['proceed']['to']
    def puesta(w, r, b, archivo='EditMenu.json'):
        assert r == archivo, r
        assert b[:11] == once and b[11] == ('Motor en tiempo real', destino), b[11:]
        e = w.pe_estado(); assert e[0] == PE['FN12'] and e[1] == PE['ID'] and e[2] not in (PE['NOSTRING'], 0), e
    def sacada(w, r, b, entrega=None):
        # entrega: una vez que la pestana se puso en esta sesion, mi archivo sigue llegando aunque este sacada (cursor recordado)
        assert r == entrega and b == once and w.pe_estado() == FAB, (r, len(b), w.pe_estado())
    MIO = 'EditMenu.json'

    # 1. primera entrada al Editar: se pone al pedir Sider la ruta; las tres escrituras, en orden (texto, numero, cantidad)
    w = Mundo(); s = Sim(F, w, existe=CON); m = w.m
    assert w.pe_estado() == FAB and s.cuadro(2)
    assert not m.exe_escr                               # dibujar el panel no pone la pestana
    k = s.reg[b'livecpk_make_key'](s.ctx, PE_ARCHIVO)
    assert k == b'mtrpe1|0|EditMenu.json' and w.pe_estado() == FAB     # con solo la clave todavia no toca nada
    puesta(w, *s.editar())
    assert [d for d, _ in m.exe_escr] == [PE['SIN'], PE['TEXTO12'], PE['RANURA']], [hex(d) for d, _ in m.exe_escr]
    assert s.SIM[b'reservas'] == 1
    assert 'pestana: ' in s.registro() and any('pestana: ' in x and 'puesta (boton 12' in x for x in s.logs()), s.logs()[-3:]
    # 1b. salir del modo Editar: se saca (primero la cantidad); al volver a entrar se pone en el pedido de la clave (Sider ya
    # tiene la ruta y no la vuelve a pedir)
    SALIDA = b'common\\script\\flow\\Edit\\EditDataAutoSaveProcess.json'
    n = len(m.exe_escr); assert s.pedir(SALIDA) is None and w.pe_estado() == FAB and m.exe_escr[n][0] == PE['RANURA']
    assert any('el juego salio del modo Editar' in x for x in s.logs()), s.logs()[-2:]
    n = len(m.escrituras); assert s.pedir(SALIDA) is None and len(m.escrituras) == n       # ya estaba sacada: nada que escribir
    puesta(w, *s.editar()); assert s.SIM[b'reservas'] == 1
    # 2. volver a entrar (Sider ya tiene la ruta): nada nuevo que escribir; con otra forma de escribir la ruta tambien
    n = len(m.escrituras); puesta(w, *s.editar()); puesta(w, *s.editar(b'COMMON/script/flow/edit/editmenu.JSON'))
    assert len(m.escrituras) == n
    s.cuadro(3); s.opcion('Pestana en Editar'); assert 'puesta' in s.fila_cursor(), s.fila_cursor()
    s.tecla('8'); assert 'pestana: puesta' in s.texto, s.texto
    # otros archivos no son asunto de la pestana
    lect = m.lecturas
    for otro in (b'common\\script\\flow\\Edit\\EditDataCheckProcess.json', b'common\\script\\flow\\Edit\\Player\\EditMenu.json.bak', b'EditMenu.json',
                 b'common\\script\\flow\\TopMenu\\TopMenu.json'):
        assert s.pedir(otro) is None
    assert m.lecturas == lect and len(m.escrituras) == n
    # 3. con torneo y equipos cargados (ahi entrega Menu-server): va la copia hecha con la suya, misma pestana
    for c in (b'tournament_id', b'home_team', b'away_team'): s.ctx[c] = 65535
    puesta(w, *s.editar(), archivo='EditMenu_menuserver.json'); assert len(m.escrituras) == n
    for c in (b'tournament_id', b'home_team', b'away_team'): s.ctx[c] = None
    # 4. sin_pestana.txt: se saca (primero la cantidad de botones) y vuelve al crear de nuevo; la memoria del texto se reusa
    s.SIM[b'existe'][b'sin_pestana.txt'] = True; n = len(m.exe_escr)
    sacada(w, *s.editar(), entrega=MIO); assert m.exe_escr[n][0] == PE['RANURA'], hex(m.exe_escr[n][0])
    assert any('sin_pestana.txt presente' in x for x in s.logs())
    sacada(w, *s.editar(), entrega=MIO); s.cuadro(2); s.opcion('Pestana en Editar'); assert ' no' in s.fila_cursor(), s.fila_cursor()
    s.SIM[b'existe'][b'sin_pestana.txt'] = None
    puesta(w, *s.editar()); assert s.SIM[b'reservas'] == 1
    # 5. apagado.txt con la pestana puesta: tambien se saca
    s.SIM[b'existe'][b'apagado.txt'] = True; sacada(w, *s.editar(), entrega=MIO); s.SIM[b'existe'][b'apagado.txt'] = None
    puesta(w, *s.editar())
    # 6. Sider recarga el modulo (mismo ctx): sigue puesta sin escribir nada; tambien si Sider olvido sus rutas
    n = len(m.escrituras); s.recargar(F); s.cuadro(2); puesta(w, *s.editar())
    s.cache = {}; puesta(w, *s.editar()); assert len(m.escrituras) == n and s.SIM[b'reservas'] == 1
    # 7. solo_lectura.txt con la pestana puesta: queda como esta y mi archivo sigue llegando; nada escrito
    s.SIM[b'existe'][b'solo_lectura.txt'] = True; s.SIM[b'existe'][b'sin_pestana.txt'] = True
    puesta(w, *s.editar()); assert s.pedir(SALIDA) is None; puesta(w, *s.editar()); assert len(m.escrituras) == n
    s.SIM[b'existe'][b'solo_lectura.txt'] = None
    sacada(w, *s.editar(), entrega=MIO)
    # 8. se borra mi archivo de flujo con la pestana puesta: se saca (y ahi ya no hay archivo mio que entregar)
    s.SIM[b'existe'][b'sin_pestana.txt'] = None; puesta(w, *s.editar())
    s.SIM[b'existe'][b'EditMenu.json'] = None; sacada(w, *s.editar())
    assert any('falta pestana\\EditMenu.json' in x for x in s.logs())

    # 9. desde el arranque, sin escribir ni una vez: sin_pestana, apagado (ni siquiera lee), solo_lectura, sin archivo, sin ffi
    for extra, lee in (('sin_pestana.txt', False), ('apagado.txt', False), ('solo_lectura.txt', False)):
        w = Mundo(); s = Sim(F, w, existe=CON + (extra,)); l0 = w.m.lecturas
        sacada(w, *s.editar()); sacada(w, *s.editar()); assert s.pedir(SALIDA) is None
        assert not w.m.escrituras and w.m.lecturas == l0, (extra, w.m.lecturas - l0)
        s.cuadro(2); assert not w.m.exe_escr
    w = Mundo(); s = Sim(F, w); sacada(w, *s.editar()); assert not w.m.escrituras
    w = Mundo(); s = Sim(F, w, existe=CON[:3])          # falta solo la copia de Menu-server
    for c in (b'tournament_id', b'home_team', b'away_team'): s.ctx[c] = 7
    sacada(w, *s.editar()); assert not w.m.escrituras
    for c in (b'tournament_id', b'home_team', b'away_team'): s.ctx[c] = None
    puesta(w, *s.editar())
    w = Mundo(); s = Sim(F, w, existe=CON, sin_ffi=True); sacada(w, *s.editar()); s.cuadro()

    # 10. el exe no es el que conozco: no se escribe nada y el registro dice por que
    def otro_exe(cambio, dice):
        w = Mundo(); cambio(w.m); antes = w.pe_estado(); s = Sim(F, w, existe=CON)
        r, b = s.editar(); assert r is None and len(b) == 11 and w.pe_estado() == antes and not w.m.escrituras, (dice, r)
        assert any(dice in x for x in s.logs()), (dice, s.logs()[-2:])
        s.cuadro(2); s.editar(); assert not w.m.escrituras
    otro_exe(lambda m: m.u64(0x142a72268 + 48, 0x1419057e1), 'solo anda con PES2021.exe 1.07.02')
    otro_exe(lambda m: m.escribir(PE['FN12'] + 1, b'\x0d', False), 'la funcion 0x1420df440 del juego no es la que espero')
    otro_exe(lambda m: m.u64(PE['VT'] + 76 * 8, PE['ARMAR'] + 0x10), 'la tabla del menu de Editar no es la que conozco')
    otro_exe(lambda m: m.u64(PE['NOMBRES'] + 88, PE['NOMBRES']), 'el lugar 12 de los nombres del menu no esta vacio')
    otro_exe(lambda m: m.escribir(PE['VACIO'], b'x', False), 'el lugar 12 de los nombres del menu no esta vacio')
    otro_exe(lambda m: m.escribir(PE['NOSTRING'], b'Sin texto\0', False), "el texto 'No String' del juego no esta donde espero")
    otro_exe(lambda m: m.u64(PE['SIN'], m.cadena('texto de otro mod')), "ya lo cambio otro")
    otro_exe(lambda m: m.u64(PE['SIN'], PE['NOSTRING'] + 3), "ya lo cambio otro")
    # la ranura 78 cambiada por otro (ni 11 ni 12) y el numero de texto 12 ocupado: tampoco
    w = Mundo(); w.m.u64(PE['RANURA'], PE['FN11'] + 0x40); s = Sim(F, w, existe=CON); assert s.pedir() is None and not w.m.escrituras
    w = Mundo(); w.m.u32(PE['TEXTO12'], 0x200026); s = Sim(F, w, existe=CON); assert s.pedir() is None and not w.m.escrituras

    # 11. Windows no deja escribir: se deshace lo hecho y el menu queda de fabrica
    def falla(preparar, dice, toco=True):
        # toco: llego a intentar una escritura en el exe. Desde ahi mi archivo se entrega igual (11 botones, el enlace de mas
        # no hace nada); si ni lo intento (sin memoria para el texto), va el archivo de siempre y Sider no se queda con el "no"
        w = Mundo(); s = Sim(F, w, existe=CON); preparar(w.m, s); e = MIO if toco else None
        sacada(w, *s.editar(), entrega=e); assert any(dice in x for x in s.logs()), (dice, s.logs()[-2:])
        sacada(w, *s.editar(), entrega=e)                   # la vez siguiente lo intenta de nuevo y vuelve a fallar
        w.m.falla.clear(); w.m.sin_permiso.clear(); s.SIM[b'sin_memoria'] = None
        puesta(w, *s.editar())                              # cuando Windows deja, se pone sola
        k = s.reg[b'livecpk_make_key'](s.ctx, PE_ARCHIVO)
        assert k == (b'mtrpe1|0|EditMenu.json' if toco else b'mtrpe1|2|EditMenu.json') and (toco or len(s.cache) >= 3), (k, list(s.cache))
    falla(lambda m, s: m.sin_permiso.add(PE['TEXTO12']), 'no pude escribir en el exe')
    falla(lambda m, s: m.sin_permiso.add(PE['RANURA']), 'no pude escribir en el exe')
    falla(lambda m, s: m.falla.add(PE['RANURA']), 'no pude escribir en el exe')
    falla(lambda m, s: m.falla.add(PE['SIN']), 'no pude escribir en el exe')
    falla(lambda m, s: s.SIM.__setitem__(b'sin_memoria', True), 'Windows no me dio memoria para el texto', toco=False)
    # 12. no se puede sacar (la cantidad de botones no vuelve a 11): mi archivo tiene que seguir llegando, el boton sigue andando
    w = Mundo(); s = Sim(F, w, existe=CON); puesta(w, *s.editar())
    s.SIM[b'existe'][b'sin_pestana.txt'] = True; w.m.sin_permiso.add(PE['RANURA'])
    r, b = s.editar(); assert r == 'EditMenu.json' and len(b) == 12 and b[11][1] == destino, (r, len(b))
    assert any('no pude sacarla' in x for x in s.logs()), s.logs()[-2:]
    w.m.sin_permiso.clear(); sacada(w, *s.editar(), entrega=MIO)
    # 12b. Sider ya tiene la ruta, sale del Editar, y al volver Windows no deja escribir: 11 botones con mi archivo (el
    # cursor pudo quedar recordado en el boton 12: su enlace sigue estando), y a la vez siguiente se pone
    w = Mundo(); s = Sim(F, w, existe=CON); puesta(w, *s.editar()); assert s.pedir(SALIDA) is None
    w.m.sin_permiso.add(PE['RANURA']); sacada(w, *s.editar(), entrega=MIO); sacada(w, *s.editar(), entrega=MIO)
    w.m.sin_permiso.clear(); puesta(w, *s.editar())
    # 13. el modulo se recarga con la pestana puesta y sin_pestana.txt: la reconoce como suya y la saca
    w = Mundo(); s = Sim(F, w, existe=CON); puesta(w, *s.editar())
    s.SIM[b'existe'][b'sin_pestana.txt'] = True; s.recargar(F); sacada(w, *s.editar(), entrega=MIO)
    # 14. con torneo y equipos, despues de haberla puesto: sacada, sigue llegando la copia de Menu-server con el enlace
    for c in (b'tournament_id', b'home_team', b'away_team'): s.ctx[c] = 9
    sacada(w, *s.editar(), entrega='EditMenu_menuserver.json')
    for c in (b'tournament_id', b'home_team', b'away_team'): s.ctx[c] = None
    # 15. Sider con su cache de claves (10 s): dentro de ese lapso no llama al modulo y repite lo que entrego la vez anterior
    w = Mundo(); s = Sim(F, w, existe=CON); s.ttl = 10; m = w.m
    puesta(w, *s.editar()); s.t_sider += 3
    n = len(m.escrituras); puesta(w, *s.editar()); assert len(m.escrituras) == n
    assert s.pedir(SALIDA) is None and w.pe_estado() == FAB       # sale del Editar: sacada
    s.t_sider += 4; sacada(w, *s.editar(), entrega=MIO)           # vuelve enseguida: Sider no pregunta -> 11 botones con mi archivo
    s.t_sider += 11; puesta(w, *s.editar())                       # pasado el lapso vuelve sola
    s.SIM[b'existe'][b'sin_pestana.txt'] = True; s.t_sider += 2
    puesta(w, *s.editar())                                        # sin_pestana.txt y Sider sin preguntar: sigue puesta, con mi archivo
    s.t_sider += 11; sacada(w, *s.editar(), entrega=MIO)
    # desde el arranque con sin_pestana.txt, que se borra enseguida: Sider recuerda el "no hay" -> 11 botones con el de siempre
    w = Mundo(); s = Sim(F, w, existe=CON + ('sin_pestana.txt',)); s.ttl = 10
    sacada(w, *s.editar()); s.SIM[b'existe'][b'sin_pestana.txt'] = None; s.t_sider += 5; sacada(w, *s.editar())
    assert not w.m.escrituras
    s.t_sider += 6; puesta(w, *s.editar())
    print('   pestana: 12 botones solo con mi archivo de flujo; se saca sola; 3 escrituras en el exe y ninguna otra')


def e_roto(F):
    """la memoria cambia por debajo (el juego descarga cosas, listas con ciclos, punteros basura): nunca un error de Lua
    y nunca una escritura fuera de un objeto conocido"""
    rnd = random.Random(int(os.environ.get('MTR_SEMILLA', '1234')))
    teclas = list('0023456789') * 3 + [0x47, 0x52, 0x53, 0x58, 0x59, 0x5A]
    for vuelta in range(60):
        w = Mundo(conj=rnd.random() < 0.3, con_limite=rnd.random() < 0.5, huesos=rnd.choice([0, 1, 2]), con_visor=rnd.random() < 0.8,
                  con_render=rnd.random() < 0.9)
        s = Sim(F, w, existe=('Default.fpkd', 'Default.fpk', 'EditMenu.json', 'EditMenu_menuserver.json')); m = w.m
        if rnd.random() < 0.6: s.SIM[b'dll_crear'](1 if rnd.random() < 0.9 else 3)      # con la DLL nueva (o una de otra version)
        if rnd.random() < 0.5:                       # el motor dibujando: el reloj avanza dentro del cuadro para que la espera corte
            s.SIM[b'tick_paso'] = 3
            if w.render: m.u64(w.render + 0xd0, 0x1a0000100)
        if vuelta % 3 == 0: s.pagina(6)              # una de cada tres vueltas arranca en la pagina RENDER
        if rnd.random() < 0.5: s.ttl = 10            # la mitad de las vueltas, Sider con su cache de claves de 10 s
        for paso in range(260):
            r = rnd.random()
            if rnd.random() < 0.04:
                # el jugador entra al menu de Editar: pase lo que pase con la memoria, o 11 botones con el archivo de siempre
                # o 12 con el mio (menu_editar falla si el juego se cerraria o un boton quedaria sin enlace)
                if rnd.random() < 0.2: s.SIM[b'existe'][b'sin_pestana.txt'] = (not s.SIM[b'existe'][b'sin_pestana.txt']) or None
                if rnd.random() < 0.1: s.cache = {}
                s.t_sider += rnd.choice([0, 1, 4, 12])
                arch, bot = s.editar(); assert len(bot) == 11 or arch, (vuelta, paso, arch, len(bot))     # 12 botones: solo con mi archivo
                if rnd.random() < 0.3: s.pedir(b'common\\script\\flow\\Edit\\EditDataAutoSaveProcess.json')     # y sale del modo Editar
            if r < 0.45: s.tecla(rnd.choice(teclas))
            elif r < 0.70:
                s.raton(izq=rnd.random() < 0.5, der=rnd.random() < 0.4, med=rnd.random() < 0.2, dx=rnd.randint(-40, 40), dy=rnd.randint(-40, 40))
            elif r < 0.76:
                s.cuadro(rnd.randint(1, 40))
                if rnd.random() < 0.3 and s.SIM[b'dll_buf'] is not None:        # basura en la memoria compartida de la DLL
                    s.lua.eval('function(i, b) SIM.ffi_real_cast("uint32_t *", SIM.dll_buf)[i] = b end')(
                        rnd.randrange(2, 16), rnd.choice([0, 0xffffffff, 0x7fc00000, 0xffc00001, 0x7f800000, 0xff800000, 0x41200000, 0xc1200000, rnd.getrandbits(32)]))
                if rnd.random() < 0.1: s.SIM[b'dll_parada'] = not s.SIM[b'dll_parada']
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


ESCENARIOS = dict(basico=e_basico, mover=e_mover, luces=e_luces, camara=e_camara, seguro=e_seguro, listas=e_listas, repaso=e_repaso,
                  render=e_render, pestana=e_pestana, roto=e_roto)

if __name__ == '__main__':
    fuente = open(sys.argv[1], 'rb').read()
    for n in (sys.argv[2:] or list(ESCENARIOS)):
        print('== ' + n)
        ESCENARIOS[n](fuente)
        print('   bien')
