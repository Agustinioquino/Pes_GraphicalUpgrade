-- MotorTiempoReal.lua 0.4 (PES21 FX, 11/11/2026) - modulo de Sider.
-- "Motor en tiempo real" del Editar: un editor en vivo de lo que dibuja el Fox Engine. Todo dentro del juego y todo
-- por Sider (sin DLL): lee la lista de escenas graficas del motor y muestra, con nombre, los modelos y las luces;
-- deja marcar uno o muchos objetos, moverlos, rotarlos, escalarlos, ocultarlos; prender, apagar y dar brillo a cada
-- foco; mover la camara del visor como en Blender (orbitar, paneo, acercar, vistas, lente larga = ortografica);
-- mostrar la grilla y un marcador en cada foco; y anota todo lo que lee y escribe en
--   content\motor-tiempo-real\registro.txt
--
-- COMO SE LLEGA: el menu de Editar gana un boton 12, "Motor en tiempo real" (seccion 3b: la pestana). En esta version
-- lleva a la misma pantalla que "Jugadores" (elegir jugador > Apariencia es el estudio 3D, donde trabaja el panel); el
-- paso directo al estudio queda para la version siguiente.
-- COMO SE USA (en Editar > jugador > Apariencia; tambien lista lo que hay en un partido):
--   Espacio abre el panel de Sider, 1 pasa de modulo hasta llegar a este.
--   6 / 5 pagina siguiente / anterior: ESCENA, LUCES, ENTIDADES, OPCIONES, REGISTRO.
--   0 / 9 baja / sube en la lista.  8 accion principal.  7 accion secundaria.  4 / 3 menos / mas.  2 enfocar.
--   Mouse (el juego no lo usa): boton derecho gira la vista, boton del medio la corre (paneo),
--   izquierdo + derecho juntos acerca y aleja, izquierdo solo aplica la herramienta a lo marcado.
--
-- SEGURIDAD: la memoria se lee y se escribe con ReadProcessMemory / WriteProcessMemory sobre el propio juego,
-- que devuelven error en vez de cerrar el juego si una direccion ya no existe. Antes de escribir se comprueba
-- que el exe sea el 1.07.02 (tablas de funciones) y que cada objeto siga siendo de la clase esperada. Nunca
-- se escribe dentro del exe, salvo las TRES direcciones fijas de la pestana del menu de Editar (seccion 3b), cada una
-- con su valor anterior comprobado. Solo se llaman dos funciones del juego (color de una luz y matriz de un modelo),
-- las dos con su comienzo comprobado byte a byte.
--   content\motor-tiempo-real\apagado.txt       = el modulo no entrega la escena ni toca nada
--   content\motor-tiempo-real\solo_lectura.txt  = lista todo pero no escribe nada
--   content\motor-tiempo-real\sin_llamadas.txt  = escribe datos pero no llama funciones del juego
--   content\motor-tiempo-real\sin_pestana.txt   = no agrega el boton al menu de Editar (se mira al entrar al Editar)
-- (los tres primeros se miran cada segundo mientras el panel esta abierto; todos se pueden crear o borrar con el juego abierto)
--
-- LISTAS DEL MOTOR (fox::gr::Scene): un objeto recien cargado entra a una cola de altas (escena+0x38, cantidad +0x48) y
-- en el cuadro siguiente el motor (FUN_1416928b0) lo pasa a la lista de su tipo: [escena+0x30] = arreglo de [escena+0x28]
-- punteros a lista { +0 primero, +8 ultimo, +0x10 cantidad }. Lo que se dibuja esta en esas listas por tipo.
-- Objeto +0x24: bit0 = enlazado (en la cola o en su lista), bit1 = todavia en la cola. Antes de dejar escribir, el modulo
-- comprueba una vez que lo que recorre coincide con la cantidad que lleva el motor en cada lista.
--
-- Offsets leidos en el exe decompilado (PES2021.exe 1.07.02): ver memoria del proyecto motor-tiempo-real-exe.

local m = { version = "0.4" }
local VERSION = m.version
local DIR
-- lo que hay que recordar aunque Sider recargue el modulo con el juego abierto (queda en ctx)
local G = { luz_orig = {}, tf_orig = {}, oc_orig = {}, et_orig = {}, cam = false }
-- todo lo de la pagina RENDER vive en una sola tabla (Lua admite 200 variables sueltas por archivo y ya hay muchas)
local RX = {}
-- y todo lo de la pestana del menu de Editar en otra (seccion 3b)
local PE = {}

---------------------------------------------------------------------------------------------
-- 1. ENTREGA DE LA ESCENA (igual que la 0.1, probada en el juego)
---------------------------------------------------------------------------------------------
local ARCH = { ["default.fpkd"] = "Default.fpkd", ["default.fpk"] = "Default.fpk" }
local pedidos, entregas = {}, {}
local activo, motivo = false, "sin iniciar"

local function existe(path)
    local f = io.open(path, "rb")
    if f then f:close() return true end
    return false
end

local function estado()
    if existe(DIR .. "apagado.txt") then return false, "apagado.txt presente" end
    for _, n in pairs(ARCH) do
        if not existe(DIR .. "escena\\" .. n) then return false, "falta escena\\" .. n end
    end
    return true, "escena lista"
end

local function cual(filename)
    local f = string.lower(filename)
    if not string.find(f, "light", 1, true) then return end
    local b = string.match(f, "([^\\/]+)$") or f
    return ARCH[b]
end

function m.make_key(ctx, filename)
    if PE.es(filename, PE.ARCH) then return PE.pedido(ctx, filename) end
    if PE.es(filename, PE.ARCH_SALIDA) then PE.salida(filename) end
    local n = cual(filename)
    if not n then return end
    pedidos[n] = (pedidos[n] or 0) + 1
    -- el estado se revisa en cada pedido de la escena: prender o apagar no pide reiniciar Sider
    local a, mot = estado()
    if a ~= activo or mot ~= motivo then
        activo, motivo = a, mot
        log("MotorTiempoReal: " .. (a and "ENTREGANDO" or "sin entregar") .. " (" .. mot .. ")")
    end
    if pedidos[n] <= 3 then log("MotorTiempoReal: el juego pidio " .. filename .. " (pedido " .. pedidos[n] .. ")") end
    if not activo then return end
    return "mtr01|" .. n
end

function m.get_filepath(ctx, filename, key)
    if type(key) == "string" and string.sub(key, 1, #PE.CLAVE) == PE.CLAVE then return PE.entrega(filename, key) end
    if not key or string.sub(key, 1, 6) ~= "mtr01|" then return end
    local n = string.sub(key, 7)
    local p = DIR .. "escena\\" .. n
    entregas[n] = (entregas[n] or 0) + 1
    if entregas[n] <= 3 then log("MotorTiempoReal: entrego " .. p .. " por " .. filename) end
    return p
end

---------------------------------------------------------------------------------------------
-- 2. WINDOWS POR FFI: memoria sin riesgo, mouse, archivo de registro
---------------------------------------------------------------------------------------------
local BASE, EXE_FIN = 0x140000000, 0x160000000    -- el fin real se lee de la cabecera del exe (exe_conocido)
local DIR_MIN, DIR_MAX = 0x10000, 0x7ffffffe0000

-- estado general del editor (una sola tabla: Lua admite pocas variables sueltas por funcion)
local E = {
    listo = nil, sin_ffi = nil, apagado = false, solo_lectura = false, sin_llamadas = false,
    n_lect = 0, n_escr = 0, n_llam = 0, n_falla = 0, mi_pid = 0,
    pag = 1, aviso = "", compacto = false, mouse = true, cuadros = 0, t_expl = -100000, sucio = true,
    cur = { 1, 1, 1, 1, 1, 1 }, herr = 1, eje = 1, grilla = false, marcadores = false, solo = nil,
    conj = false, conj_visto = "sin verificar", volcado = false, hilo_cuadro = nil, hilo_tecla = nil,
    exe_ok = nil, exe_motivo = nil,
    listas_ok = false, listas_mal = 0, listas_dicho = false, n_obj = 0, n_puede = 0, conj_intentos = 0,
}
local C, PROC
local B8, B32, BF, B64, NR          -- lectura (un solo buffer: sacar los valores antes de leer otra cosa)
local W8, W32, WF                   -- escritura (alineado a 16: las funciones del juego usan movaps)
local PT, RC, PID, HECHO
local ancla = {}                    -- para que el recolector de Lua no se lleve los buffers

local function hx(n)
    if type(n) ~= "number" or not (n >= 0) then return "?" end
    local alto = math.floor(n / 4294967296)
    local bajo = n - alto * 4294967296
    if alto > 0 then return string.format("%x%08x", alto, bajo) end
    return string.format("%x", bajo)
end

local function de_hex(s)
    return (string.gsub(s, "%x%x", function(h) return string.char(tonumber(h, 16)) end))
end

local function preparar()
    if E.listo ~= nil then return E.listo end
    E.listo = false
    if type(ffi) ~= "table" or type(ffi.cdef) ~= "function" or type(ffi.new) ~= "function"
        or type(ffi.cast) ~= "function" or type(ffi.string) ~= "function" or type(ffi.copy) ~= "function" then
        E.sin_ffi = "a este Sider le falta ffi: pone luajit.ext.enabled = 1 en sider.ini"
        return false
    end
    -- nombres propios (mtr_) apuntando al nombre real: no choca con lo que declaren otros modulos.
    -- Solo funciones (declarar dos veces la misma funcion no da error; un struct si).
    ffi.cdef[[
        void *mtr_GetCurrentProcess(void) __asm__("GetCurrentProcess");
        int mtr_ReadProcessMemory(void *h, uintptr_t dir, void *buf, size_t n, size_t *hecho) __asm__("ReadProcessMemory");
        int mtr_WriteProcessMemory(void *h, uintptr_t dir, const void *buf, size_t n, size_t *hecho) __asm__("WriteProcessMemory");
        short mtr_GetAsyncKeyState(int vk) __asm__("GetAsyncKeyState");
        int mtr_GetCursorPos(int32_t *pt) __asm__("GetCursorPos");
        int mtr_SetCursorPos(int x, int y) __asm__("SetCursorPos");
        void *mtr_GetForegroundWindow(void) __asm__("GetForegroundWindow");
        uint32_t mtr_GetWindowThreadProcessId(void *hwnd, uint32_t *pid) __asm__("GetWindowThreadProcessId");
        uint32_t mtr_GetCurrentProcessId(void) __asm__("GetCurrentProcessId");
        uint32_t mtr_GetCurrentThreadId(void) __asm__("GetCurrentThreadId");
        int mtr_GetClientRect(void *hwnd, int32_t *rc) __asm__("GetClientRect");
        uint32_t mtr_GetTickCount(void) __asm__("GetTickCount");
        int mtr_MultiByteToWideChar(uint32_t cp, uint32_t flags, const char *s, int n, uint16_t *w, int wn) __asm__("MultiByteToWideChar");
        void *mtr_CreateFileW(const uint16_t *nombre, uint32_t acceso, uint32_t compartir, void *seg, uint32_t disp, uint32_t flags, void *plantilla) __asm__("CreateFileW");
        int mtr_WriteFile(void *h, const char *buf, uint32_t n, uint32_t *hecho, void *ov) __asm__("WriteFile");
        uint32_t mtr_SetFilePointer(void *h, int32_t dist, int32_t *alto, uint32_t metodo) __asm__("SetFilePointer");
        int mtr_CloseHandle(void *h) __asm__("CloseHandle");
        void *mtr_OpenFileMappingA(uint32_t acceso, int heredar, const char *nombre) __asm__("OpenFileMappingA");
        void *mtr_MapViewOfFile(void *h, uint32_t acceso, uint32_t alto, uint32_t bajo, size_t n) __asm__("MapViewOfFile");
        int mtr_UnmapViewOfFile(void *p) __asm__("UnmapViewOfFile");
        int mtr_VirtualProtect(uintptr_t dir, size_t n, uint32_t prot, uint32_t *antes) __asm__("VirtualProtect");
        uintptr_t mtr_VirtualAlloc(uintptr_t dir, size_t n, uint32_t tipo, uint32_t prot) __asm__("VirtualAlloc");
    ]]
    C = ffi.C
    PROC = C.mtr_GetCurrentProcess()
    E.mi_pid = C.mtr_GetCurrentProcessId()
    local function alineado(n)
        local crudo = ffi.new("uint8_t[?]", n + 0x40)
        ancla[#ancla + 1] = crudo
        local d = tonumber(ffi.cast("uintptr_t", crudo))
        return d + (16 - d % 16) % 16
    end
    local d = alineado(0x1000)
    B8, B32, BF, B64 = ffi.cast("uint8_t *", d), ffi.cast("uint32_t *", d), ffi.cast("float *", d), ffi.cast("uint64_t *", d)
    d = alineado(0x1000)
    W8, W32, WF = ffi.cast("uint8_t *", d), ffi.cast("uint32_t *", d), ffi.cast("float *", d)
    NR = ffi.new("size_t[1]")
    PT, RC, PID, HECHO = ffi.new("int32_t[2]"), ffi.new("int32_t[4]"), ffi.new("uint32_t[1]"), ffi.new("uint32_t[1]")
    E.listo = true
    return true
end

local function reloj() return C and C.mtr_GetTickCount() or 0 end

-- lee n bytes (hasta 0x1000) al buffer; false si la direccion no existe (no cierra el juego)
local function leer(dir, n)
    if type(dir) ~= "number" or not (dir >= DIR_MIN and dir < DIR_MAX) then return false end
    E.n_lect = E.n_lect + 1
    if C.mtr_ReadProcessMemory(PROC, dir, B8, n, NR) == 0 then return false end
    return tonumber(NR[0]) == n
end
-- puntero guardado en el buffer en el offset o (multiplo de 8), o nil si no parece un puntero
local function P(o)
    local v = tonumber(B64[o / 8])
    if v >= DIR_MIN and v < DIR_MAX then return v end
    return nil
end
local function rptr(dir) if leer(dir, 8) then return P(0) end return nil end
local function ru32(dir) if leer(dir, 4) then return B32[0] end return nil end
local function finito(v) return v == v and v > -1e9 and v < 1e9 end
-- float del buffer en el indice i (de a 4 bytes). OJO con LuaJIT: leer por un "float *" unos bits que son un NaN negativo (por
-- ejemplo el entero -1 = 0xffffffff, o cualquier entero negativo chico) NO da un numero: da un valor roto (nil, false o un objeto
-- falso que puede cerrar el juego al usarlo). Por eso miro los bits como entero antes de leerlo como float.
local NAN = 0 / 0
local function fl(i)
    local u = B32[i]
    if u > 0xff800000 or (u > 0x7f800000 and u < 0x80000000) then return NAN end
    return BF[i]
end

local function cadena(dir, max)
    max = max or 64
    if type(dir) ~= "number" or not (dir >= DIR_MIN and dir < DIR_MAX) then return nil end
    local s, hecho = "", 0
    while hecho < max do
        local n = max - hecho
        local hasta = 0x1000 - (dir + hecho) % 0x1000      -- cada lectura llega como mucho al final de su pagina
        if n > hasta then n = hasta end
        if not leer(dir + hecho, n) then
            if hecho == 0 then return nil end
            break
        end
        local k = 0
        while k < n and B8[k] ~= 0 do k = k + 1 end
        s = s .. ffi.string(B8, k)
        if k < n then break end
        hecho = hecho + n
    end
    return s
end
local function limpio(s, max)
    if not s or s == "" then return nil end
    for i = 1, #s do
        local c = string.byte(s, i)
        if c < 32 or c > 126 then return nil end
    end
    if #s > max then s = string.sub(s, 1, max) end
    return s
end
-- fox::String: el campo guarda un puntero a un registro y el registro empieza con el puntero al texto
local function fox_cadena(dir_campo, max)
    local rec = rptr(dir_campo)
    local p = rec and rptr(rec)
    return p and limpio(cadena(p, max or 64), max or 64) or nil
end

-- registro: se junta en memoria y se escribe al archivo de a tandas
local reg = { cola = {}, n = 0, ult = {}, ruta = nil, falla = false, primero = true }
local function R(s, importante)
    reg.n = reg.n + 1
    reg.cola[reg.n] = s
    local u = reg.ult
    u[#u + 1] = s
    if #u > 60 then table.remove(u, 1) end
    if importante then log("MotorTiempoReal: " .. s) end
end

local function a_archivo(ruta, texto, al_final)
    local n = C.mtr_MultiByteToWideChar(65001, 0, ruta, -1, nil, 0)
    if n <= 0 then return false end
    local w = ffi.new("uint16_t[?]", n + 1)
    C.mtr_MultiByteToWideChar(65001, 0, ruta, -1, w, n)
    -- 0x40000000 escribir, 1 compartir lectura, 4 abrir o crear / 2 crear de nuevo, 0x80 normal
    local h = C.mtr_CreateFileW(w, 0x40000000, 1, nil, al_final and 4 or 2, 0x80, nil)
    if h == nil or tonumber(ffi.cast("intptr_t", h)) == -1 then return false end
    local tam = 0
    if al_final then tam = C.mtr_SetFilePointer(h, 0, nil, 2) end
    local r = C.mtr_WriteFile(h, texto, #texto, HECHO, nil)
    C.mtr_CloseHandle(h)
    return r ~= 0, tam
end

local function volcar_registro()
    if reg.n == 0 or not C or not reg.ruta then return end
    local texto = table.concat(reg.cola, "\r\n", 1, reg.n) .. "\r\n"
    reg.cola, reg.n = {}, 0
    if reg.falla then return end
    local ok, tam = a_archivo(reg.ruta, texto, true)
    if ok and reg.primero and tam > 2000000 then ok = a_archivo(reg.ruta, texto, false) end   -- muy largo: empieza de nuevo
    reg.primero = false
    if not ok then
        reg.falla = true
        log("MotorTiempoReal: no pude escribir " .. reg.ruta .. " (sigo anotando solo en sider.log lo importante)")
    end
end

-- escribe los n primeros bytes del buffer de escritura. Nunca dentro del exe.
local function poner(dir, n)
    if E.solo_lectura then return false end
    if type(dir) ~= "number" or not (dir >= DIR_MIN and dir < DIR_MAX) or (dir >= BASE and dir < EXE_FIN) then
        E.n_falla = E.n_falla + 1
        return false
    end
    E.n_escr = E.n_escr + 1
    if C.mtr_WriteProcessMemory(PROC, dir, W8, n, NR) == 0 then
        E.n_falla = E.n_falla + 1
        if E.n_falla <= 20 then R("NO pude escribir " .. n .. " bytes en 0x" .. hx(dir)) end
        return false
    end
    return true
end
local function poner_u32(dir, v) W32[0] = v return poner(dir, 4) end
local function poner_u8(dir, v) W8[0] = v return poner(dir, 1) end
local function poner_f(dir, t, n)
    for i = 1, n do WF[i - 1] = t[i] end
    return poner(dir, n * 4)
end

---------------------------------------------------------------------------------------------
-- 3. EL EXE: tiene que ser el 1.07.02
---------------------------------------------------------------------------------------------
local G_ESCENAS = 0x14374d958       -- primera escena grafica del motor (lista enlazada)
local G_VISOR = 0x1438040d0         -- [[[G]+0x78]+0x90] = visor del Editar (draw::util::TaskModelView::Impl)
local VT_MODELO, VT_CUERPO, VT_FUNCTOR = 0x142a0a880, 0x142a72268, 0x142a72358
local VT_VISOR, VT_CAM = 0x142ba51e0, 0x142aa5368
local VT_TE = 0x14255ea00           -- fox::TransformEntity (escala +0x30, giro +0x40, posicion +0x50)
-- bloque de datos de cada luz (fox::gr::dg::*LightSource), el que lee el dibujo: luz+0x38 apunta a uno de estos
local VT_DG = { [0x142a226a0] = true, [0x142a226b0] = true, [0x142a226c0] = true, [0x142a226d0] = true, [0x142a226e0] = true }
local VT_LUZ = { [0x142a0ad48] = "Luz", [0x142a0add8] = "Sol", [0x142a0ae68] = "Local", [0x142a0aef8] = "Punto", [0x142a0af88] = "Foco" }
local RANURAS = {   -- direccion de una tabla de funciones del exe -> la funcion que tiene que haber ahi
    { 0x142a0a880, 0x141697bd0 }, { 0x142a0af88, 0x14169ed80 }, { 0x142a0ad48, 0x14169ec20 }, { 0x142a0add8, 0x14169eba0 },
    { 0x142a0ae68, 0x14169ec80 }, { 0x142a0aef8, 0x14169ece0 }, { 0x142a721e8, 0x141905750 },
    { 0x142a72268 + 8 * 6, 0x1419057e0 }, { 0x142a72268 + 8 * 15, 0x1419064f0 }, { 0x142a72358, 0x141905310 },
    { 0x142aa5368, 0x1419fcd70 }, { 0x142ba51e0, 0x141ee73f0 }, { 0x14255ea00, 0x1402bc920 },
    { 0x142a226a0, 0x14171d6e0 }, { 0x142a226b0, 0x14171d620 }, { 0x142a226c0, 0x14171d740 }, { 0x142a226d0, 0x14171d7a0 },
    { 0x142a226e0, 0x14171d8c0 },
}
-- funciones del juego que se llaman (pocas, chicas y con el comienzo comprobado)
local FN_COLOR, FIRMA_COLOR = 0x14169f2e0, de_hex("488b41380f28020f294040488b0148ff6078")      -- luz: color + aviso
local FN_MATRIZ, FIRMA_MATRIZ = 0x14169de80, de_hex("40534883ec200f2802488bd90f294140")        -- modelo: matriz + limites
local c_color, c_matriz

local function exe_conocido()
    if E.exe_ok ~= nil then return E.exe_ok end
    if not preparar() then
        E.exe_ok, E.exe_motivo = false, E.sin_ffi
        return false
    end
    local e = leer(BASE + 0x3c, 4) and B32[0]
    local sello = e and e < 0x1000 and leer(BASE + e + 8, 4) and B32[0]
    -- tamano del exe en memoria (SizeOfImage): ahi adentro nunca se escribe y ningun objeto del juego vive ahi
    local tam = e and e < 0x1000 and leer(BASE + e + 0x50, 4) and B32[0]
    if tam and tam >= 0x1000000 and tam <= 0x40000000 then EXE_FIN = BASE + tam end
    for i = 1, #RANURAS do
        local r = RANURAS[i]
        if not leer(r[1], 8) or tonumber(B64[0]) ~= r[2] then
            E.exe_ok = false
            E.exe_motivo = "este modulo solo anda con PES2021.exe 1.07.02 (no coincide la tabla 0x" .. hx(r[1]) .. "). No se toco nada"
            R(E.exe_motivo, true)
            return false
        end
    end
    E.exe_ok = true
    R(string.format("exe reconocido: sello 0x%s (el del 1.07.02 es 0x62787c6a), %d tablas de funciones iguales, exe hasta 0x%s",
        sello and hx(sello) or "?", #RANURAS, hx(EXE_FIN)), true)
    if leer(FN_COLOR, #FIRMA_COLOR) and ffi.string(B8, #FIRMA_COLOR) == FIRMA_COLOR then
        c_color = ffi.cast("void (__fastcall *)(uintptr_t, float *)", FN_COLOR)
    else
        R("la funcion de color de luz (0x14169f2e0) no empieza como espero: el brillo de las luces queda apagado", true)
    end
    if leer(FN_MATRIZ, #FIRMA_MATRIZ) and ffi.string(B8, #FIRMA_MATRIZ) == FIRMA_MATRIZ then
        c_matriz = ffi.cast("void (__fastcall *)(uintptr_t, float *)", FN_MATRIZ)
    else
        R("la funcion de matriz de modelo (0x14169de80) no empieza como espero: no aviso al motor de los limites", true)
    end
    return true
end

---------------------------------------------------------------------------------------------
-- 3b. LA PESTANA: boton 12 "Motor en tiempo real" en el menu de Editar (sin DLL)
---------------------------------------------------------------------------------------------
-- Leido en el exe 1.07.02 (11/11/2026). El menu de Editar es menu::MenuEditMenu (pagina "Edit/EditMenu"), una de las 59
-- pantallas que usan la lista comun del Editar (menu::MenuEditCmnSettingList, pantalla editSettingList.bin, hasta 17
-- filas). Al abrirse pregunta "cuantos botones" a la ranura 78 de su tabla de funciones (0x142701520, que apunta a
-- 0x141b9f460 = "return 11") y por cada uno lee un nombre de enlace (tabla 0x1434c44a0) y un numero de texto (tabla
-- 0x1427011c0). Las dos tablas ya traen un lugar 12 sin usar: nombre "" (0x1434c44f8 -> 0x142700fd3) y texto 0
-- (0x1427011ec). A donde lleva cada boton lo dice el archivo de flujo common\script\flow\Edit\EditMenu.json, que el
-- juego vuelve a leer cada vez que entra a ese menu; un boton de nombre vacio dispara el enlace "proceed".
-- La pestana son entonces TRES escrituras dentro del exe (las unicas de este modulo ahi) y un archivo entregado por Sider:
--   1. la ranura 78 pasa a apuntar a 0x1420df440, una funcion del juego que es solo "return 12";
--   2. el texto del lugar 12 pasa a ser un numero de una categoria de textos que no existe: para esos el juego muestra
--      el texto al que apunta 0x14350a9a0 (de fabrica "No String");
--   3. ese puntero pasa a apuntar a "Motor en tiempo real" (una pagina de memoria pedida a Windows que no se libera
--      nunca: el juego la puede leer cuando quiera, tambien si Sider recarga este modulo);
--   4. content\motor-tiempo-real\pestana\EditMenu.json = el del juego mas el enlace "proceed" (y EditMenu_menuserver.json =
--      el del mod Menu-server mas el mismo enlace).
-- Se pone cuando Sider esta por entregar ese archivo (o sea, al entrar al menu de Editar), se saca al salir del modo
-- Editar (cuando el juego pide el flujo Edit\EditDataAutoSaveProcess.json) y solo se pone si cada valor esta como
-- en el exe de fabrica o como lo dejo este mismo modulo. Si falta el archivo, si algo no coincide, o si existe
-- sin_pestana.txt o apagado.txt, no se escribe nada y, si estaba puesta, se saca: el menu queda con sus 11 botones.
-- Si la pestana se puso alguna vez desde que se abrio el juego, mi archivo de flujo se sigue entregando aunque la pestana
-- este sacada (el juego recuerda el cursor del menu; con 11 botones el enlace de mas no hace nada).
-- Con solo_lectura.txt no se escribe nada: queda como este (puesta o sin poner).
-- Efecto secundario conocido: si en algun otro lugar del juego faltaba un texto y decia "No String", ahi tambien dira
-- "Motor en tiempo real".
PE.VT, PE.FN_ARMAR = 0x1427012b0, 0x140c29340
PE.RANURA, PE.FN_11, PE.FN_12 = 0x142701520, 0x141b9f460, 0x1420df440
PE.NOMBRE, PE.NOMBRE_ERA = 0x1434c44f8, 0x142700fd3
PE.TEXTO, PE.ID = 0x1427011ec, 0x7ffe0001
PE.SIN_TEXTO, PE.SIN_TEXTO_ERA = 0x14350a9a0, 0x142972e40
PE.ETIQUETA = "Motor en tiempo real"
PE.ARCH, PE.CLAVE = "script\\flow\\edit\\editmenu.json", "mtrpe1|"
-- el flujo que el juego lee al SALIR del modo Editar (el "return" del menu de Editar): ahi la pestana se saca
PE.ARCH_SALIDA = "script\\flow\\edit\\editdataautosaveprocess.json"
-- dos copias del archivo de flujo, las dos con el enlace nuevo: la del juego y la del mod Menu-server (igual, con otro fondo)
PE.JSON, PE.JSON_MS = "EditMenu.json", "EditMenu_menuserver.json"
PE.puesta, PE.estado, PE.pedidos, PE.dicho = false, "el juego todavia no pidio el menu de Editar", 0, nil

-- el nombre que pide el juego termina en esa ruta? (sin importar mayusculas ni el sentido de las barras)
function PE.es(filename, ruta)
    if type(filename) ~= "string" or #filename < #ruta then return false end
    local f = string.gsub(string.lower(string.sub(filename, -#ruta)), "/", "\\")
    return f == ruta
end

-- lee todo lo que la pestana toca. Devuelve { cuenta, id, sin, mia } o nil y el motivo (algo no es lo que conozco)
function PE.mirar()
    if not exe_conocido() then return nil, E.exe_motivo or "no pude preparar el modulo" end
    if not leer(PE.FN_12, 8) or B32[0] ~= 0x00000cb8 or B8[4] ~= 0 or B8[5] ~= 0xc3 then
        return nil, "la funcion 0x1420df440 del juego no es la que espero"
    end
    local armar, cuenta = rptr(PE.VT + 76 * 8), rptr(PE.RANURA)
    if armar ~= PE.FN_ARMAR or (cuenta ~= PE.FN_11 and cuenta ~= PE.FN_12) then
        return nil, "la tabla del menu de Editar no es la que conozco"
    end
    local nom = rptr(PE.NOMBRE)
    if nom ~= PE.NOMBRE_ERA or not leer(nom, 1) or B8[0] ~= 0 then return nil, "el lugar 12 de los nombres del menu no esta vacio" end
    local id = ru32(PE.TEXTO)
    if id ~= 0 and id ~= PE.ID then return nil, "el lugar 12 de los textos del menu no esta vacio" end
    local sin, mia = rptr(PE.SIN_TEXTO), false
    if sin == PE.SIN_TEXTO_ERA then
        if not leer(sin, 10) or ffi.string(B8, 10) ~= "No String\0" then return nil, "el texto 'No String' del juego no esta donde espero" end
    else
        -- no es el de fabrica: solo vale si apunta a mi etiqueta (la puso este modulo, quiza antes de que Sider lo recargara)
        local n = #PE.ETIQUETA + 1
        if not sin or (sin >= BASE and sin < EXE_FIN) or not leer(sin, n) or ffi.string(B8, n) ~= PE.ETIQUETA .. "\0" then
            return nil, "el texto de reemplazo del juego ('No String') ya lo cambio otro"
        end
        mia = true
    end
    return { cuenta = cuenta, id = id, sin = sin, mia = mia }
end

-- escribe los n primeros bytes del buffer de escritura en una de las tres direcciones de la pestana (y en ninguna otra).
-- protegida = pagina de solo lectura del exe: se abre con VirtualProtect y se deja como estaba.
function PE.poner(dir, n, protegida)
    if dir ~= PE.RANURA and dir ~= PE.TEXTO and dir ~= PE.SIN_TEXTO then return false end
    G.pe_toco = true
    E.n_escr = E.n_escr + 1
    local ok
    if protegida then
        if C.mtr_VirtualProtect(dir, n, 4, HECHO) == 0 then return false end
        local antes = HECHO[0]
        ok = C.mtr_WriteProcessMemory(PROC, dir, W8, n, NR) ~= 0
        C.mtr_VirtualProtect(dir, n, antes, HECHO)
    else
        ok = C.mtr_WriteProcessMemory(PROC, dir, W8, n, NR) ~= 0
    end
    return ok
end
function PE.poner_ptr(dir, v, protegida)
    local alto = math.floor(v / 4294967296)
    W32[0], W32[1] = v - alto * 4294967296, alto
    return PE.poner(dir, 8, protegida)
end

-- memoria propia con la etiqueta (una pagina; no se libera). nil si Windows no la da.
function PE.etiqueta()
    local n = #PE.ETIQUETA + 1
    local p = G.pe_etiqueta
    if type(p) == "number" and leer(p, n) and ffi.string(B8, n) == PE.ETIQUETA .. "\0" then return p end
    p = tonumber(C.mtr_VirtualAlloc(0, 0x1000, 0x3000, 4))        -- reservar y comprometer, lectura y escritura
    if not p or p < DIR_MIN or p >= DIR_MAX then return nil end
    ffi.copy(W8, PE.ETIQUETA, n - 1)
    W8[n - 1] = 0
    if C.mtr_WriteProcessMemory(PROC, p, W8, n, NR) == 0 then return nil end
    if not leer(p, n) or ffi.string(B8, n) ~= PE.ETIQUETA .. "\0" then return nil end
    G.pe_etiqueta = p
    return p
end

-- saca la pestana. v = lo que devolvio PE.mirar(). Primero la cantidad de botones: si no vuelve a 11, el texto se queda
-- (mientras haya 12 botones, el 12 tiene que tener su texto)
function PE.sacar(v)
    if v.cuenta == PE.FN_12 and not PE.poner_ptr(PE.RANURA, PE.FN_11, true) then return false end
    local ok = true
    if v.id ~= 0 then
        W32[0] = 0
        ok = PE.poner(PE.TEXTO, 4, true) and ok
    end
    if v.mia then ok = PE.poner_ptr(PE.SIN_TEXTO, PE.SIN_TEXTO_ERA, false) and ok end
    return ok
end

-- deja la pestana puesta (quiero = true) o sacada. Devuelve true si quedo como se pidio. PE.estado dice que paso.
function PE.asegurar(quiero, motivo)
    local v, por_que = PE.mirar()
    if not v then
        -- no entiendo como esta: lo unico que hago es volver la cantidad de botones a la de fabrica, si la habia cambiado
        if C and E.exe_ok and rptr(PE.RANURA) == PE.FN_12 and rptr(PE.VT + 76 * 8) == PE.FN_ARMAR then PE.poner_ptr(PE.RANURA, PE.FN_11, true) end
        PE.puesta, PE.estado = false, "no se puso: " .. por_que
        return false
    end
    local estaba = (v.cuenta == PE.FN_12 and v.id == PE.ID and v.mia)
    if not quiero then
        local tocada = (v.cuenta == PE.FN_12 or v.id ~= 0 or v.mia)
        if tocada and not PE.sacar(v) then
            PE.puesta, PE.estado = false, "no pude sacarla del todo (" .. motivo .. ")"
            return false
        end
        PE.puesta, PE.estado = false, "sin poner: " .. motivo
        return true
    end
    if not estaba then
        local et = v.mia and v.sin or PE.etiqueta()
        local ok = et and true or false
        -- orden: el texto primero y la cantidad de botones al final (nunca 12 botones sin su texto)
        if ok and not v.mia then ok = PE.poner_ptr(PE.SIN_TEXTO, et, false) end
        if ok and v.id ~= PE.ID then
            W32[0] = PE.ID
            ok = PE.poner(PE.TEXTO, 4, true)
        end
        if ok and v.cuenta ~= PE.FN_12 then ok = PE.poner_ptr(PE.RANURA, PE.FN_12, true) end
        local w = ok and PE.mirar() or nil
        if not (w and w.cuenta == PE.FN_12 and w.id == PE.ID and w.mia) then
            w = PE.mirar()
            if w then PE.sacar(w) end
            PE.puesta, PE.estado = false, et and "no se puso: no pude escribir en el exe" or "no se puso: Windows no me dio memoria para el texto"
            return false
        end
    end
    PE.puesta, PE.estado = true, "puesta (boton 12 del menu de Editar)"
    return true
end

-- como esta ahora, SIN escribir nada (y sin leer nada si este modulo nunca la puso)
function PE.quieta(motivo)
    local v = G.pe_toco and PE.mirar() or nil
    local esta = (v and v.cuenta == PE.FN_12 and v.id == PE.ID and v.mia) and true or false
    PE.puesta = esta
    PE.estado = (esta and "puesta, y queda asi: " or "sin poner: ") .. motivo
    return esta
end

-- deja la pestana como corresponde en este momento y dice si hay que entregar mi EditMenu.json.
-- de_ruta = Sider esta pidiendo la ruta del archivo (ahi se que el que va a leer el juego es el mio)
function PE.resolver(de_ruta, archivo)
    local hay = existe(DIR .. "pestana\\" .. archivo)
    if existe(DIR .. "solo_lectura.txt") then PE.quieta("solo_lectura.txt presente")
    else
        local motivo
        if existe(DIR .. "apagado.txt") then motivo = "apagado.txt presente"
        elseif existe(DIR .. "sin_pestana.txt") then motivo = "sin_pestana.txt presente"
        elseif not hay then motivo = "falta pestana\\" .. archivo end
        if motivo then
            if not G.pe_toco then PE.puesta, PE.estado = false, "sin poner: " .. motivo
            elseif not PE.asegurar(false, motivo) then PE.quieta("no pude sacarla (" .. motivo .. ")") end
        elseif not de_ruta and not (G.pe_dada and G.pe_dada[PE.clave(archivo)]) then
            -- la pestana se pone recien cuando Sider me pide la ruta del archivo: asi nunca hay 12 botones con el archivo de
            -- flujo de otro. Si Sider ya tiene guardada la ruta de esta clave (no vuelve a preguntar), se pone en el pedido.
            PE.estado = G.pe_fallo or "espero a que Sider me pida el archivo"
            return true
        elseif PE.asegurar(true, "") then G.pe_fallo = nil
        else G.pe_fallo = PE.estado end
    end
    -- mi archivo va siempre que la pestana este puesta, y tambien si se puso alguna vez desde que se abrio el juego: el
    -- juego recuerda en que boton quedo el cursor de cada menu, y si quedo en el 12, aceptar ahi tiene que seguir teniendo
    -- su enlace aunque el boton ya no se vea. Con 11 botones el enlace de mas no hace nada.
    return hay and (PE.puesta or G.pe_toco == true)
end

-- la clave de Sider para una de las dos copias. Sider guarda por clave lo que le contesto (tambien los "no tengo"): por eso
-- la clave lleva un numero que cambia cada vez que una entrega falla, y asi la vez siguiente vuelve a preguntar.
function PE.clave(archivo)
    return PE.CLAVE .. (G.pe_gen or 0) .. "|" .. archivo
end

function PE.decir(que, ok, archivo)
    local d = (ok and "1" or "0") .. archivo .. PE.estado
    if d ~= PE.dicho then
        PE.dicho = d
        R(string.format("pestana: %s (pedido %d) -> %s; %s", que, PE.pedidos, PE.estado,
            ok and ("va pestana\\" .. archivo) or "va el archivo de siempre"), true)
    end
    if C then volcar_registro() end
end

-- el juego pidio el archivo de flujo del menu de Editar (esta entrando a ese menu). Devuelve la clave de Sider si el
-- archivo lo entrego yo, o nil (el juego usa el de siempre, con sus 11 botones).
-- El mod Menu-server (MenuServer.lua) entrega su propia copia de este archivo cuando Sider tiene torneo y equipos
-- cargados: con esa misma condicion entrego la copia hecha a partir de la suya, asi el fondo del menu no cambia.
function PE.pedido(ctx, filename)
    PE.pedidos = PE.pedidos + 1
    local archivo = PE.JSON
    if type(ctx) == "table" and ctx.tournament_id and ctx.home_team and ctx.away_team then archivo = PE.JSON_MS end
    local ok = PE.resolver(false, archivo)
    PE.decir("el juego pidio " .. filename, ok, archivo)
    if ok then return PE.clave(archivo) end
    return nil
end

-- el juego sale del modo Editar: la pestana se saca (vuelve sola la proxima vez que se entra al menu de Editar). Asi los
-- tres valores del exe solo estan cambiados mientras se esta dentro del Editar, y no puede quedar un boton 12 puesto si
-- mas adelante otro modulo pasa a entregar el archivo de flujo del menu.
function PE.salida(filename)
    if not G.pe_toco or existe(DIR .. "solo_lectura.txt") then return end
    PE.asegurar(false, "el juego salio del modo Editar")
    PE.decir("el juego pidio " .. filename, false, "")
end

-- Sider pide la ruta del archivo (key = la clave que di en PE.pedido)
function PE.entrega(filename, key)
    local archivo = string.match(key, "|%d+|(.+)$")
    if archivo ~= PE.JSON and archivo ~= PE.JSON_MS then return nil end
    local ok = PE.resolver(true, archivo)
    PE.decir("Sider pidio la ruta de " .. filename, ok, archivo)
    if ok then
        G.pe_dada = G.pe_dada or {}
        G.pe_dada[key] = true               -- Sider ya tiene la ruta de esta clave: no va a volver a pedirla
        return DIR .. "pestana\\" .. archivo
    end
    G.pe_gen = (G.pe_gen or 0) + 1          -- Sider anoto que para esta clave no tengo archivo: la proxima vez uso otra
    return nil
end

-- nombre de clase de un objeto C++ por su tabla de funciones (RTTI del exe): ".?AVModel@gr@fox@@" -> fox::gr::Model
local clases = {}
local function clase_de(vt)
    if not vt then return nil end
    local c = clases[vt]
    if c ~= nil then return c or nil end
    clases[vt] = false
    if vt < BASE or vt >= EXE_FIN then return nil end
    local col = rptr(vt - 8)
    if not col or col < BASE or col >= EXE_FIN or not leer(col, 0x18) or B32[0] ~= 1 then return nil end
    local rva = B32[3]
    if rva < 0x1000 or rva > 0x10000000 then return nil end
    local s = limpio(cadena(BASE + rva + 0x10, 120), 120)
    if not s or (string.sub(s, 1, 4) ~= ".?AV" and string.sub(s, 1, 4) ~= ".?AU") then return nil end
    s = string.gsub(string.sub(s, 5), "@@$", "")
    local partes = {}
    for p in string.gmatch(s, "[^@]+") do table.insert(partes, 1, p) end
    local nombre = table.concat(partes, "::")
    if #nombre > 60 then nombre = string.sub(nombre, 1, 60) end
    clases[vt] = nombre
    return nombre
end
local function clase_corta(vt)
    local c = clase_de(vt)
    if not c then return vt and ("0x" .. hx(vt)) or "?" end
    return string.match(c, "([^:]+)$") or c
end

---------------------------------------------------------------------------------------------
-- 4. CUENTAS: matrices del motor (fila x matriz, traslacion en la ultima fila) y cuaterniones
---------------------------------------------------------------------------------------------
local function mat_srt(s, q, t, conj)
    local x, y, z, w = q[1], q[2], q[3], q[4]
    if conj then x, y, z = -x, -y, -z end
    local xx, yy, zz, xy, xz, yz, wx, wy, wz = x * x, y * y, z * z, x * y, x * z, y * z, w * x, w * y, w * z
    return {
        s[1] * (1 - 2 * (yy + zz)), s[1] * (2 * (xy + wz)), s[1] * (2 * (xz - wy)), 0,
        s[2] * (2 * (xy - wz)), s[2] * (1 - 2 * (xx + zz)), s[2] * (2 * (yz + wx)), 0,
        s[3] * (2 * (xz + wy)), s[3] * (2 * (yz - wx)), s[3] * (1 - 2 * (xx + yy)), 0,
        t[1], t[2], t[3], 1,
    }
end
local function inv3(a)   -- inversa de la parte 3x3 (filas 1-3) o nil
    local a1, a2, a3, b1, b2, b3, c1, c2, c3 = a[1], a[2], a[3], a[5], a[6], a[7], a[9], a[10], a[11]
    local det = a1 * (b2 * c3 - b3 * c2) - a2 * (b1 * c3 - b3 * c1) + a3 * (b1 * c2 - b2 * c1)
    if not (math.abs(det) > 1e-12) then return nil end
    local d = 1 / det
    return {
        (b2 * c3 - b3 * c2) * d, (a3 * c2 - a2 * c3) * d, (a2 * b3 - a3 * b2) * d,
        (b3 * c1 - b1 * c3) * d, (a1 * c3 - a3 * c1) * d, (a3 * b1 - a1 * b3) * d,
        (b1 * c2 - b2 * c1) * d, (a2 * c1 - a1 * c2) * d, (a1 * b2 - a2 * b1) * d,
    }
end
local function q_mul(a, b)
    return {
        a[4] * b[1] + a[1] * b[4] + a[2] * b[3] - a[3] * b[2],
        a[4] * b[2] - a[1] * b[3] + a[2] * b[4] + a[3] * b[1],
        a[4] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[4],
        a[4] * b[4] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
    }
end
local function q_eje(e, ang)
    local s = math.sin(ang / 2)
    return { e[1] * s, e[2] * s, e[3] * s, math.cos(ang / 2) }
end
local function q_norm(q)
    local l = math.sqrt(q[1] * q[1] + q[2] * q[2] + q[3] * q[3] + q[4] * q[4])
    if not (l > 1e-9) then return { 0, 0, 0, 1 } end
    return { q[1] / l, q[2] / l, q[3] / l, q[4] / l }
end
local function norm3(x, y, z)
    local l = math.sqrt(x * x + y * y + z * z)
    if not (l > 1e-9 and l < 1e9) then return nil end
    return { x / l, y / l, z / l }
end

---------------------------------------------------------------------------------------------
-- 5. EXPLORAR: escenas graficas del motor, modelos, luces, entidades
---------------------------------------------------------------------------------------------
local escenas, modelos, luces, ayudas, otros = {}, {}, {}, {}, {}
local por_dir, marcados = {}, {}
local ents = { lista = {}, t = -100000, ds = {} }
local N = { expl = 0 }

local function leer_modelo(o, r)
    if not leer(o, 0x200) then return false end
    r.x, r.y, r.z = fl(0x70 / 4), fl(0x74 / 4), fl(0x78 / 4)
    r.caja = { fl(0x80 / 4), fl(0x84 / 4), fl(0x88 / 4), fl(0x90 / 4), fl(0x94 / 4), fl(0x98 / 4) }
    r.dir_huesos_ahora, r.huesos = P(0xe0), B32[0xf8 / 4]
    r.n_fun, r.fun, r.archivo = B32[0x100 / 4], P(0x108), P(0x118)
    r.copia3, r.limite = P(0x198), P(0x1a0)
    r.mascara, r.b1b0, r.b1b4 = B32[0x1ac / 4], B32[0x1b0 / 4], B32[0x1b4 / 4]
    -- el dibujo mira la mascara de vistas (+0x1ac); el bit0 de +0x1b4 es "oculto a la fuerza". El juego esconde sus propios
    -- modelos solo con la mascara; los objetos de escena (StadiumModelBody) llevan las dos cosas juntas
    r.oculto = (r.b1b4 % 2) == 1 or r.mascara ~= 0
    r.esc_media = fl(0x1c4 / 4)
    r.lod_cerca, r.lod_lejos, r.lod_poli, r.descarte = fl(0x1cc / 4), fl(0x1d0 / 4), fl(0x1e0 / 4), B8[0x1f1]
    -- escala = largo de cada fila de la matriz (la diagonal sola vale unicamente si el objeto no esta girado)
    r.e1 = math.sqrt(fl(16) ^ 2 + fl(17) ^ 2 + fl(18) ^ 2)
    r.e2 = math.sqrt(fl(20) ^ 2 + fl(21) ^ 2 + fl(22) ^ 2)
    r.e3 = math.sqrt(fl(24) ^ 2 + fl(25) ^ 2 + fl(26) ^ 2)
    return true
end

-- de quien es el modelo: modelo -> functor del cuerpo -> StadiumModelBody -> entidad -> nombre
local function resolver_modelo(o, r)
    r.resuelto, r.fun_res, r.nfun_res = true, r.fun, r.n_fun
    r.cuerpo, r.ent, r.te, r.vt_ent, r.nombre, r.padre, r.ds, r.asa = nil, nil, nil, nil, nil, nil, nil, nil
    r.reposo, r.dir_huesos = nil, nil
    local n = r.n_fun
    if r.fun and n >= 1 and n <= 16 then
        for i = 0, n - 1 do
            local f = rptr(r.fun + 8 * i)
            if f and leer(f, 0x18) and P(0) == VT_FUNCTOR then
                local c = P(0x10)
                if c and rptr(c) == VT_CUERPO and rptr(c + 0x88) == o then
                    local h = rptr(c + 0x30)
                    local e = h and rptr(h + 0x18)
                    if e and leer(e, 0x80) then
                        local vt, hds, hpadre, te = P(0), P(0x30), P(0x70), P(0x78)
                        if vt and vt >= BASE and vt < EXE_FIN then
                            if te and rptr(te) ~= VT_TE then te = nil end      -- otra clase de transformacion: no se mueve
                            r.cuerpo, r.ent, r.te, r.vt_ent, r.asa = c, e, te, vt, h
                            r.nombre = fox_cadena(e + 0x28, 40)
                            r.padre = hpadre and rptr(hpadre + 0x18) or nil
                            r.ds = hds and rptr(hds + 0x18) or nil
                            break
                        end
                    end
                end
            end
        end
    end
    if not r.nombre and r.archivo then      -- INFERIDO: File +0x30 = registro de la ruta, que empieza con el texto
        local rec = rptr(r.archivo + 0x30)
        local p = rec and rptr(rec)
        local s = p and limpio(cadena(p, 120), 120)
        if s and (string.find(s, "/", 1, true) or string.find(s, ".", 1, true)) then
            r.ruta = s
            r.nombre = "~" .. string.sub(string.match(s, "([^/]+)$") or s, 1, 38)
        end
    end
    if not r.nombre then r.nombre = "modelo_" .. hx(o) end
    r.nuestro = string.sub(r.nombre, 1, 4) == "zfx_"
    r.ayuda = (string.sub(r.nombre, 1, 11) == "zfx_Grilla_" and "grilla") or (string.sub(r.nombre, 1, 8) == "zfx_Luz_" and "luz") or nil
    r.movible = (r.ent ~= nil and r.te ~= nil and r.cuerpo ~= nil and r.padre == nil)
end

-- cada clase de luz mide distinto (Luz 0x58, Local 0x90, Sol 0xa0, Punto 0xb0, Foco 0xf0): solo el foco tiene giro y cono
-- en +0x90..+0xa7 (en el sol y en el punto ahi hay punteros) y el sol no tiene alcance. Los campos que una clase no tiene
-- quedan en numeros neutros, nunca en nil (se muestran con string.format)
local function leer_luz(o, r)
    local foco = (r.clase == "Foco")
    local con_alcance = foco or r.clase == "Punto" or r.clase == "Local"
    if not leer(o, foco and 0xa8 or (con_alcance and 0x88 or 0x58)) then return false end
    local p, nom = P(0x38), P(0x50)
    r.alc_int, r.alcance, r.q, r.umbra, r.penumbra = 0, 0, { 0, 0, 0, 1 }, 0, 0
    if con_alcance then r.alc_int, r.alcance = fl(0x80 / 4), fl(0x84 / 4) end
    if foco then
        r.q = { fl(0x90 / 4), fl(0x94 / 4), fl(0x98 / 4), fl(0x9c / 4) }
        r.umbra, r.penumbra = fl(0xa0 / 4), fl(0xa4 / 4)
    end
    if not p or (p >= BASE and p < EXE_FIN) or not leer(p, 0x80) then return false end
    r.p, r.vt_p = p, P(0)
    r.p_ok = (r.vt_p ~= nil and VT_DG[r.vt_p] == true)       -- si no es una clase conocida se muestra pero no se toca
    r.color = { fl(0x40 / 4), fl(0x44 / 4), fl(0x48 / 4), fl(0x4c / 4) }
    r.flags = B32[0x64 / 4]
    r.prendida = (r.flags % 2) == 1
    r.sombra = (math.floor(r.flags / 16) % 2) == 1
    r.x, r.y, r.z = fl(0x70 / 4), fl(0x74 / 4), fl(0x78 / 4)
    if r.nombre == nil or r.nom_rec ~= nom then
        r.nom_rec = nom
        local t = nom and rptr(nom)
        r.nombre = (t and limpio(cadena(t, 40), 40)) or ("luz_" .. hx(o))
    end
    return true
end

local function pasada()
    N.expl = N.expl + 1
    local id = N.expl
    local nesc, nm, nl, na, no = {}, {}, {}, {}, {}
    local primera = rptr(G_ESCENAS)
    local e, ne = primera, 0
    while e and ne < 64 do
        ne = ne + 1
        if not leer(e, 0x50) then break end
        local sig, rec, n_listas, arreglo, cola, n_cola = P(0), P(0x20), B32[0x28 / 4], P(0x30), P(0x38), B32[0x48 / 4]
        local hash = "?"
        if rec and leer(rec + 0x10, 8) then hash = string.format("%08x%08x", B32[1], B32[0]) end
        local esc = { dir = e, hash = hash, cuenta = 0, n = 0, n_mod = 0, n_luz = 0, n_listas = 0, n_cola = 0, raro = 0, tope = false, ok = false }
        nesc[ne] = esc
        -- cabezas a recorrer: la cola de altas y cada lista por tipo que no este vacia. El buffer de lectura es uno solo:
        -- primero se copian todas las direcciones y recien despues se recorren los objetos
        local cab, ncab = {}, 0
        if cola and n_cola >= 1 and n_cola <= 100000 then
            ncab = 1
            cab[1] = cola
            esc.cuenta, esc.n_cola = n_cola, n_cola
        elseif cola or n_cola ~= 0 then
            esc.raro = esc.raro + 1                     -- cola con cabeza y sin cantidad, o al reves
        end
        if arreglo and (arreglo < BASE or arreglo >= EXE_FIN) and n_listas >= 1 and n_listas <= 256 and leer(arreglo, n_listas * 8) then
            local lst = {}
            for i = 1, n_listas do lst[i] = P((i - 1) * 8) or false end
            for i = 1, n_listas do
                if lst[i] and leer(lst[i], 0x18) then
                    local primero, cuantos = P(0), B32[4]
                    if primero and cuantos >= 1 and cuantos <= 100000 then
                        ncab = ncab + 1
                        cab[ncab] = primero
                        esc.cuenta = esc.cuenta + cuantos
                        esc.n_listas = esc.n_listas + 1
                    elseif primero or cuantos ~= 0 then
                        esc.raro = esc.raro + 1
                    end
                end
            end
        else
            esc.raro = esc.raro + 1                     -- una escena sin su arreglo de listas no es lo que conozco
        end
        local k = 0
        for ci = 1, ncab do
            local o = cab[ci]
            while o do
                if k >= 4096 then esc.tope = true break end
                k = k + 1
                if not leer(o, 0x38) then esc.raro = esc.raro + 1 break end
                local vt, sgte = P(0), P(0x18)
                -- enlazado (bit0) y de esta escena: si no, el motor lo saco mientras yo recorria
                if B32[0x24 / 4] % 2 ~= 1 or P(0x28) ~= e then esc.raro = esc.raro + 1 break end
                local r = por_dir[o]
                if not r or r.vt ~= vt then
                    r = { dir = o, vt = vt }
                    por_dir[o] = r
                    if vt == VT_MODELO then r.tipo = "modelo"
                    elseif vt and VT_LUZ[vt] then r.tipo, r.clase = "luz", VT_LUZ[vt]
                    else r.tipo, r.clase = "otro", clase_corta(vt) end
                end
                if r.visto ~= id then                   -- (nunca dos veces en la misma pasada)
                    r.visto, r.esc = id, ne
                    esc.n = esc.n + 1
                    if r.tipo == "modelo" then
                        if leer_modelo(o, r) then
                            if not r.resuelto or r.fun ~= r.fun_res or r.n_fun ~= r.nfun_res or r.archivo ~= r.arch_res then
                                r.nom_antes = r.resuelto and r.nombre or nil
                                resolver_modelo(o, r)
                                r.arch_res = r.archivo
                                -- la misma direccion con otro nombre = otro objeto (el juego reuso la memoria): no hereda la marca
                                if r.nom_antes and r.nom_antes ~= r.nombre then marcados[o] = nil end
                            end
                            esc.n_mod = esc.n_mod + 1
                            if r.ayuda then na[#na + 1] = r else nm[#nm + 1] = r end
                        end
                    elseif r.tipo == "luz" then
                        if leer_luz(o, r) then
                            esc.n_luz = esc.n_luz + 1
                            nl[#nl + 1] = r
                        end
                    else
                        no[#no + 1] = r
                    end
                else
                    esc.raro = esc.raro + 1             -- repetido: el motor lo cambio de lista en el medio, o hay un ciclo
                end
                if sgte == cab[ci] then esc.raro = esc.raro + 1 break end
                o = sgte
            end
            if esc.tope then break end
        end
        esc.ok = (not esc.tope) and esc.raro == 0 and esc.n == esc.cuenta
        if sig == primera then break end
        e = sig
    end
    return nesc, nm, nl, na, no, ne, id
end

local function explorar()
    local nesc, nm, nl, na, no, ne, id = pasada()
    -- con las listas ya comprobadas: si una lista se corto a mitad (el motor la estaba cambiando), se recorre otra vez
    if E.listas_ok then
        local cortada = false
        for i = 1, ne do
            local x = nesc[i]
            if x and not x.tope and x.raro > 0 and x.cuenta > 0 then cortada = true end
        end
        if cortada then nesc, nm, nl, na, no, ne, id = pasada() end
    end
    -- primera vez: lo recorrido tiene que coincidir con la cantidad que lleva el motor en cada lista. Hasta que eso pase una
    -- vez, el modulo lista pero no escribe sobre modelos ni luces (si mi lectura del motor fuera equivocada, no toco nada)
    local total = 0
    for i = 1, ne do
        if nesc[i] then total = total + nesc[i].n end
    end
    E.n_obj = total
    if not E.listas_ok then
        local bien, con_algo = true, 0
        for i = 1, ne do
            local x = nesc[i]
            if x and not x.tope and (x.n_mod + x.n_luz > 0 or x.cuenta > 0) then
                if x.ok then
                    if x.n_mod + x.n_luz > 0 then con_algo = con_algo + 1 end
                else
                    bien = false
                end
            end
        end
        if bien and con_algo > 0 then
            E.listas_ok = true
            R(string.format("listas del motor comprobadas (%d objetos en %d escenas: lo recorrido coincide con la cantidad de cada lista)", total, ne), true)
        elseif bien then
            E.listas_mal = 0                            -- todavia no hay modelos ni luces contra los que comprobar
        else
            E.listas_mal = E.listas_mal + 1             -- (seis pasadas seguidas = 3 segundos: no es un cambio pasajero)
            if E.listas_mal == 6 and not E.listas_dicho then
                E.listas_dicho = true
                R("las listas del motor NO coinciden con lo que recorro: sigo en solo lectura para modelos y luces", true)
                for i = 1, ne do
                    local x = nesc[i]
                    if x then
                        R(string.format("  escena %d 0x%s: listas con algo %d, en cola %d, cantidad del motor %d, recorridos %d, cosas raras %d%s",
                            i, hx(x.dir), x.n_listas, x.n_cola, x.cuenta, x.n, x.raro, x.tope and " (tope de 4096)" or ""))
                    end
                end
            end
        end
    end
    for d, r in pairs(por_dir) do
        if r.visto ~= id then
            por_dir[d] = nil
            marcados[d] = nil
        end
    end
    -- el cursor sigue sobre el mismo objeto aunque la lista cambie
    local function seguir(vieja, nueva, i)
        local r = vieja[E.cur[i]]
        if r then
            for k = 1, #nueva do
                if nueva[k] == r then E.cur[i] = k return end
            end
        end
        if E.cur[i] > #nueva then E.cur[i] = (#nueva > 0) and #nueva or 1 end
    end
    seguir(modelos, nm, 1)
    seguir(luces, nl, 2)
    escenas, modelos, luces, ayudas, otros = nesc, nm, nl, na, no
    E.t_expl, E.sucio = reloj(), false
end

-- todas las entidades con nombre del archivo de escena: DataSet +0x70 = mapa nombre -> entidad
local function listar_entidades()
    local vistos, lista = {}, {}
    local dss = {}
    for i = 1, #modelos + #ayudas do
        local r = (i <= #modelos) and modelos[i] or ayudas[i - #modelos]
        if r.ds and not vistos[r.ds] then
            vistos[r.ds] = true
            dss[#dss + 1] = r.ds
        end
    end
    local comprobar = 0
    for k = 1, #dss do
        local ds = dss[k]
        if #dss > 4 and k > 4 then break end
        if leer(ds + 0x70, 0x30) then
            local i, nodos, valores, n = B32[2], P(0x20), P(0x28), 0
            while nodos and valores and i ~= 0xffffffff and i < 0x100000 and n < 4096 do
                n = n + 1
                if not leer(nodos + i * 0x18, 0x18) then break end
                local clave, sig = P(0), B32[3]
                local e = rptr(valores + i * 8)
                local vt = e and rptr(e)
                if vt and vt >= BASE and vt < EXE_FIN then
                    local t = clave and rptr(clave)
                    local it = { dir = e, vt = vt, ds = ds, flags = 0, clase = clase_corta(vt),
                        nombre = (t and limpio(cadena(t, 48), 48)) or ("entidad_" .. hx(e)) }
                    -- solo las clases con transformacion tienen posicion (su +0x78 apunta a un TransformEntity)
                    if leer(e, 0x100) then
                        local hpadre, te, flags = P(0x70), P(0x78), B32[0xb8 / 4]
                        local x, y, z = fl(0xf0 / 4), fl(0xf4 / 4), fl(0xf8 / 4)
                        local mundo = {}
                        for j = 1, 16 do mundo[j] = fl(0xc0 / 4 + j - 1) end
                        if te and string.find(clase_corta(rptr(te)), "TransformEntity", 1, true) then
                            it.x, it.y, it.z, it.con_tf, it.te, it.padre, it.flags, it.mundo = x, y, z, true, te, hpadre, flags, mundo
                        end
                    end
                    lista[#lista + 1] = it
                end
                i = sig
            end
        end
    end
    -- convencion de giro: se compara una entidad girada del propio juego con mi cuenta (normal y conjugada)
    if E.conj_visto == "sin verificar" then
        for k = 1, #lista do
            local it = lista[k]
            if it.con_tf and not (it.padre and rptr(it.padre + 0x18)) and leer(it.te, 0x60) then
                local S, Q, T = { fl(12), fl(13), fl(14) }, { fl(16), fl(17), fl(18), fl(19) }, { fl(20), fl(21), fl(22) }
                if math.abs(Q[4]) < 0.999 and finito(Q[1]) and finito(S[1]) then
                    local a, b, da, db = mat_srt(S, Q, T, false), mat_srt(S, Q, T, true), 0, 0
                    for j = 1, 12 do
                        da = math.max(da, math.abs(a[j] - it.mundo[j]))
                        db = math.max(db, math.abs(b[j] - it.mundo[j]))
                    end
                    comprobar = comprobar + 1
                    if da < 0.01 and da <= db then
                        E.conj, E.conj_visto = false, "normal (comprobada con " .. it.nombre .. ")"
                    elseif db < 0.01 then
                        E.conj, E.conj_visto = true, "conjugada (comprobada con " .. it.nombre .. ")"
                    end
                    R(string.format("convencion de giro: %s da %.5f con la normal y %.5f con la conjugada -> %s",
                        it.nombre, da, db, E.conj_visto), true)
                    if E.conj_visto ~= "sin verificar" or comprobar >= 4 then break end
                end
            end
        end
    end
    if comprobar > 0 and E.conj_visto == "sin verificar" then
        E.conj_intentos = E.conj_intentos + 1
        if E.conj_intentos >= 3 then E.conj_visto = "sin comprobar (uso la normal, la que arma el motor)" end
    end
    -- ordenadas como un arbol de escena: por clase y despues por nombre
    table.sort(lista, function(x, y)
        if x.clase ~= y.clase then return x.clase < y.clase end
        if x.nombre ~= y.nombre then return x.nombre < y.nombre end
        return x.dir < y.dir
    end)
    ents.lista, ents.t, ents.ds = lista, reloj(), dss
    if E.cur[3] > #lista then E.cur[3] = (#lista > 0) and #lista or 1 end
end

---------------------------------------------------------------------------------------------
-- 6. OBJETOS: mover, rotar, escalar, ocultar (sobre entidades StadiumModel: las que cargo el motor desde la escena)
---------------------------------------------------------------------------------------------
-- el objeto sigue en su escena? (el juego pudo sacarlo o destruirlo desde la ultima exploracion)
-- rapido: misma tabla de funciones, enlazado (bit0 de +0x24: el destructor de un objeto empieza por sacarlo de su escena
-- y apagar ese bit) y apuntando a la escena donde lo encontre. Nada de esto vale hasta haber comprobado las listas una vez
local function en_escena(r, vt)
    local e = escenas[r.esc]
    if not E.listas_ok or not e or not leer(r.dir, 0x30) then return false end
    return P(0) == vt and (B32[0x24 / 4] % 2) == 1 and P(0x28) == e.dir
end
-- a fondo (antes de llamar una funcion del juego): ademas tiene que estar en la lista donde lo tiene el motor, elegida
-- como la elige el motor al darlo de baja (FUN_141692420): bit1 = la cola de altas; si no, la lista de su tipo (+0x30)
local function viva(r, vt)
    if not en_escena(r, vt) then return false end
    local e = escenas[r.esc].dir
    if not leer(r.dir, 0x38) then return false end
    local en_cola, tipo = (math.floor(B32[0x24 / 4] / 2) % 2) == 1, B32[0x30 / 4]
    local primero
    if en_cola then
        primero = rptr(e + 0x38)
    else
        if not leer(e, 0x38) then return false end
        local n, arreglo = B32[0x28 / 4], P(0x30)
        if not arreglo or n > 256 or tipo < 1 or tipo > n then return false end
        local lst = rptr(arreglo + 8 * (tipo - 1))
        primero = lst and rptr(lst)
    end
    local o, k = primero, 0
    while o and k < 4096 do
        if o == r.dir then return true end
        k = k + 1
        o = rptr(o + 0x18)
        if o == primero then break end
    end
    return false
end

local function leer_tf(r)
    if not r.te or not leer(r.te, 0x60) then return nil end
    local S, Q, T = { fl(12), fl(13), fl(14), fl(15) }, { fl(16), fl(17), fl(18), fl(19) }, { fl(20), fl(21), fl(22), fl(23) }
    for i = 1, 4 do
        if not (finito(S[i]) and finito(Q[i]) and finito(T[i])) then return nil end
    end
    return S, Q, T
end

-- posicion de cada hueso respecto del modelo (los modelos de escena llevan sus huesos pegados a la matriz del modelo)
local function huesos_reposo(r)
    if r.reposo ~= nil then return r.reposo end
    local n = r.huesos
    if n < 1 or n > 64 then
        r.reposo = false
        return false
    end
    local base = rptr(r.dir + 0xe0)
    if not base or (base >= BASE and base < EXE_FIN) or not leer(r.dir + 0x40, 0x40) then return false end
    local Wm = {}
    for i = 1, 16 do Wm[i] = fl(i - 1) end
    local inv = inv3(Wm)
    if not inv or not leer(base, n * 0x40) then return false end
    local t = {}
    for i = 0, n - 1 do
        local o = i * 16
        for k = 0, 11 do
            if not (math.abs(fl(o + k) - Wm[k + 1]) <= 0.001 * (1 + math.abs(Wm[k + 1]))) then return false end   -- hueso animado: no lo toco
        end
        local dx, dy, dz = fl(o + 12) - Wm[13], fl(o + 13) - Wm[14], fl(o + 14) - Wm[15]
        if not (finito(dx) and finito(dy) and finito(dz)) then return false end
        t[i + 1] = { dx * inv[1] + dy * inv[4] + dz * inv[7], dx * inv[2] + dy * inv[5] + dz * inv[8], dx * inv[3] + dy * inv[6] + dz * inv[9] }
    end
    r.reposo, r.dir_huesos = t, base
    return t
end

-- el juego pudo descargar el objeto: antes de cada escritura se comprueba TODA la cadena otra vez
-- (modelo en su escena -> cuerpo -> entidad -> transformacion), cada eslabon con su tabla de funciones
local function cuerpo_sigue(r)
    return r.cuerpo ~= nil and rptr(r.cuerpo) == VT_CUERPO and rptr(r.cuerpo + 0x88) == r.dir
end
local function sigue_siendo(r)
    if not en_escena(r, VT_MODELO) or not cuerpo_sigue(r) then return false end
    if not r.ent or not r.te or not r.asa then return false end
    if rptr(r.cuerpo + 0x30) ~= r.asa or rptr(r.asa + 0x18) ~= r.ent then return false end
    if not leer(r.ent, 0x80) or P(0) ~= r.vt_ent or P(0x78) ~= r.te then return false end
    return rptr(r.te) == VT_TE
end
-- los huesos siguen pegados a la matriz del modelo? (se mira justo antes de pisarlos)
local function huesos_pegados(r)
    local n = r.reposo and #r.reposo or 0
    if n < 1 or n > 64 or rptr(r.dir + 0xe0) ~= r.dir_huesos or ru32(r.dir + 0xf8) ~= n or not leer(r.dir + 0x40, 0x40) then return false end
    local Wm = {}
    for i = 1, 12 do Wm[i] = fl(i - 1) end
    if not leer(r.dir_huesos, n * 0x40) then return false end
    for i = 0, n - 1 do
        for k = 0, 11 do
            if not (math.abs(fl(i * 16 + k) - Wm[k + 1]) <= 0.001 * (1 + math.abs(Wm[k + 1]))) then return false end
        end
    end
    return true
end

-- pone escala / giro / posicion nuevos: entidad (TransformEntity + matriz mundo), cuerpo y modelo, como lo deja
-- StadiumModelBody al sincronizar (FUN_141906600 + FUN_14169de80), pero solo con datos
local function aplicar_tf(r, S, Q, T)
    if not r.movible then return false, "no es un objeto de la escena (solo se puede ocultar)" end
    if not sigue_siendo(r) then
        r.resuelto = false
        return false, "el objeto ya no esta"
    end
    local con_huesos = false
    if r.huesos > 0 then
        if r.reposo and (#r.reposo ~= r.huesos or rptr(r.dir + 0xe0) ~= r.dir_huesos) then r.reposo = nil end
        if not huesos_reposo(r) or not huesos_pegados(r) then return false, "tiene huesos animados" end
        con_huesos = true
    end
    if G.tf_orig[r.ent] == nil or G.tf_orig[r.ent].nombre ~= r.nombre then
        local s0, q0, t0 = leer_tf(r)
        if not s0 then return false, "no pude leer su transformacion" end
        G.tf_orig[r.ent] = { nombre = r.nombre, S = s0, Q = q0, T = t0 }
    end
    local M = mat_srt(S, Q, T, E.conj)
    for i = 1, 16 do
        if not finito(M[i]) then return false, "cuenta invalida" end
    end
    -- la tercera copia de la matriz vive fuera del modelo: solo se pisa si ahora mismo es igual a la del modelo
    local c3 = rptr(r.dir + 0x198)
    if c3 then
        local ahora = leer(r.dir + 0x40, 0x40) and ffi.string(B8, 0x40)
        if not ahora or (c3 >= BASE and c3 < EXE_FIN) or not leer(c3, 0x40) or ffi.string(B8, 0x40) ~= ahora then
            if not r.c3_raro then
                r.c3_raro = true
                R(string.format("%s: la 3ra copia de la matriz (0x%s) no es igual a la del modelo: no la toco", r.nombre, hx(c3)), true)
            end
            c3 = nil
        end
    end
    for i = 1, 4 do WF[i - 1], WF[3 + i], WF[7 + i] = S[i], Q[i], T[i] end
    if not poner(r.te + 0x30, 0x30) then return false, "no pude escribir" end
    for i = 1, 16 do WF[i - 1] = M[i] end
    poner(r.ent + 0xc0, 0x40)
    poner(r.cuerpo + 0xa0, 0x40)
    poner(r.dir + 0x40, 0x40)
    poner(r.dir + 0xa0, 0x40)
    if c3 then poner(c3, 0x40) end
    if con_huesos then
        local n = #r.reposo
        for i = 0, n - 1 do
            local o, b = i * 16, r.reposo[i + 1]
            for k = 0, 11 do WF[o + k] = M[k + 1] end
            WF[o + 12] = b[1] * M[1] + b[2] * M[5] + b[3] * M[9] + M[13]
            WF[o + 13] = b[1] * M[2] + b[2] * M[6] + b[3] * M[10] + M[14]
            WF[o + 14] = b[1] * M[3] + b[2] * M[7] + b[3] * M[11] + M[15]
            WF[o + 15] = 1
        end
        poner(r.dir_huesos, n * 0x40)
    end
    local v = ru32(r.dir + 0x1b0)
    if v then
        if math.floor(v / 8) % 2 == 1 then v = v - 8 end
        if math.floor(v / 4) % 2 == 0 then v = v + 4 end
        poner_u32(r.dir + 0x1b0, v)
    end
    WF[0] = (math.sqrt(M[1] * M[1] + M[2] * M[2] + M[3] * M[3]) + math.sqrt(M[5] * M[5] + M[6] * M[6] + M[7] * M[7])
        + math.sqrt(M[9] * M[9] + M[10] * M[10] + M[11] * M[11])) / 3
    poner(r.dir + 0x1c4, 4)
    r.M, r.pend = M, true
    r.x, r.y, r.z = M[13], M[14], M[15]
    return true
end

-- al terminar de mover: si el modelo tiene limites para el descarte por vista, se le avisa al motor una vez
local function avisar_motor(r)
    if not r.pend then return end
    r.pend = false
    if not r.limite or not c_matriz or not r.M or E.solo_lectura or E.sin_llamadas then return end
    if not viva(r, VT_MODELO) or rptr(r.dir + 0x1a0) ~= r.limite then return end
    for i = 1, 16 do WF[i - 1] = r.M[i] end
    c_matriz(r.dir, WF)
    E.n_llam = E.n_llam + 1
end

local function ocultar(r, oc, exacto)
    if not en_escena(r, VT_MODELO) then return false end
    -- sigue siendo el modelo que explore (mismos functores y mismo archivo)? si la direccion paso a otro, no lo toco
    if not leer(r.dir + 0x100, 0x20) or B32[0] ~= r.nfun_res or P(8) ~= r.fun_res or P(0x18) ~= r.archivo then
        E.sucio = true
        return false
    end
    if not leer(r.dir + 0x1ac, 0xc) then return false end
    local masc, v = B32[0], B32[2]
    if G.oc_orig[r.dir] == nil or G.oc_orig[r.dir].nombre ~= r.nombre then
        G.oc_orig[r.dir] = { nombre = r.nombre, oculto = (v % 2) == 1 or masc ~= 0, masc = masc, bit = v % 2 }
    end
    if cuerpo_sigue(r) then
        -- para el motor (FUN_141906600) un objeto se ve si su entidad esta habilitada (flags bit0, estado 3) Y su cuerpo es
        -- visible. Las ayudas vienen apagadas desde el archivo (flags 6): al mostrarlas se prende tambien la entidad, asi
        -- no las vuelve a esconder el motor si resincroniza
        if not oc and sigue_siendo(r) and leer(r.ent + 0xb8, 8) then
            local f, estado = B32[0], B32[1]
            if f % 2 == 0 then poner_u32(r.ent + 0xb8, f + 1) end
            if estado % 4 == 3 then poner_u8(r.cuerpo + 0x80, 1) end
        end
        poner_u8(r.cuerpo + 0x81, oc and 0 or 1)
    end
    -- un modelo del juego (sin cuerpo de escena) vuelve a su mascara y a su bit de antes, tal cual estaban
    local masc_n, bit_n = oc and 0xffffffff or 0, oc and 1 or 0
    if exacto and exacto.masc and exacto.bit and not r.cuerpo then masc_n, bit_n = exacto.masc, exacto.bit end
    poner_u32(r.dir + 0x1ac, masc_n)
    local ok = poner_u32(r.dir + 0x1b4, v - v % 2 + bit_n)
    if ok then r.oculto, r.mascara, r.b1b4 = (bit_n == 1 or masc_n ~= 0), masc_n, v - v % 2 + bit_n end
    return ok
end

local function seleccion()
    local t = {}
    for i = 1, #modelos do
        if marcados[modelos[i].dir] then t[#t + 1] = modelos[i] end
    end
    if #t == 0 and modelos[E.cur[1]] then t[1] = modelos[E.cur[1]] end
    return t
end

---------------------------------------------------------------------------------------------
-- 7. CAMARA DEL VISOR (cobra::rc::camera::CameraImpl: +0x10 posicion, +0x1c objetivo, +0x28 arriba, +0x34 cerca,
--    +0x38 lejos, +0x3c proyeccion, +0x40 campo de vision en radianes). El visor no la reescribe cada cuadro.
---------------------------------------------------------------------------------------------
local cam = { activa = false, vista = 1, modo = 1 }
local VISTAS = { "LIBRE", "FRENTE", "ATRAS", "DERECHA", "IZQUIERDA", "ARRIBA", "ABAJO" }
local MODOS = { "LENTE DEL JUEGO", "PERSPECTIVA", "ORTOGRAFICA (lente larga)" }

local function cam_buscar()
    local a = rptr(G_VISOR)
    local b = a and rptr(a + 0x78)
    local impl = b and rptr(b + 0x90)
    if not impl or rptr(impl) ~= VT_VISOR then return nil end
    local nodo = rptr(impl + 0xd8)
    local c = nodo and rptr(nodo + 0x60)
    if not c or rptr(c) ~= VT_CAM then return nil end
    return c, nodo, impl
end

local function alto_ventana()
    local h = C.mtr_GetForegroundWindow()
    if h ~= nil and C.mtr_GetClientRect(h, RC) ~= 0 then
        local a = RC[3] - RC[1]
        if a >= 200 and a <= 10000 then return a end
    end
    return 1080
end

local function cam_soltar(por_que)
    if cam.activa then R("camara: la suelto (" .. por_que .. ")", true) end
    cam.activa = false
end

local function cam_activar()
    local c, nodo, impl = cam_buscar()
    if not c then
        E.aviso = "no encuentro la camara del visor: entra a Editar > un jugador > Apariencia"
        return false
    end
    if not leer(c, 0x60) then return false end
    local px, py, pz, tx, ty, tz = fl(4), fl(5), fl(6), fl(7), fl(8), fl(9)
    local cerca, lejos, proy, fov, asp = fl(13), fl(14), B32[15], fl(16), fl(17)
    local orig = ffi.string(B8 + 0x10, 0x50)
    local dx, dy, dz = px - tx, py - ty, pz - tz
    local D = math.sqrt(dx * dx + dy * dy + dz * dz)
    local rara = not (D > 0.01 and D < 100000) or not (fov > 0.001 and fov < 3.2) or proy ~= 0
    if cam.anotada ~= c or cam.anotada_rara ~= rara then
        cam.anotada, cam.anotada_rara = c, rara
        R(string.format("camara del visor 0x%s (nodo 0x%s clase %s, visor 0x%s): pos %.3f %.3f %.3f objetivo %.3f %.3f %.3f cerca %.3f lejos %.1f proyeccion %d campo %.4f aspecto %.3f",
            hx(c), hx(nodo), clase_corta(rptr(nodo)), hx(impl), px, py, pz, tx, ty, tz, cerca, lejos, proy, fov, asp), true)
    end
    if rara then
        E.aviso = "la camara del visor tiene valores que no conozco: no la toco (anotado en el registro)"
        return false
    end
    if not G.cam or G.cam.dir ~= c then G.cam = { dir = c, orig = orig } end
    cam.dir, cam.pivote, cam.dist = c, { tx, ty, tz }, D
    local sy = dy / D
    if sy > 1 then sy = 1 elseif sy < -1 then sy = -1 end
    cam.pitch, cam.yaw = math.asin(sy), math.atan2(dx, dz)
    cam.yaw0, cam.fov, cam.fov_juego, cam.cerca, cam.lejos = cam.yaw, fov, fov, cerca, lejos
    cam.cerca_w, cam.lejos_w, cam.vista, cam.modo, cam.activa = nil, nil, 1, 1, true
    return true
end

local function cam_escribir()
    if not cam.activa then return end
    local c = cam.dir
    if cam_buscar() ~= c then              -- tiene que seguir siendo LA camara del visor, no solo parecer una camara
        cam_soltar("el visor cambio de camara")
        return
    end
    if cam.pitch > 1.55 then cam.pitch = 1.55 elseif cam.pitch < -1.55 then cam.pitch = -1.55 end
    if cam.dist < 0.05 then cam.dist = 0.05 elseif cam.dist > 3000 then cam.dist = 3000 end
    local cp, sp = math.cos(cam.pitch), math.sin(cam.pitch)
    local ox, oy, oz = cp * math.sin(cam.yaw), sp, cp * math.cos(cam.yaw)
    local p = cam.pivote
    WF[0], WF[1], WF[2] = p[1] + ox * cam.dist, p[2] + oy * cam.dist, p[3] + oz * cam.dist
    WF[3], WF[4], WF[5] = p[1], p[2], p[3]
    WF[6], WF[7], WF[8] = 0, 1, 0
    poner(c + 0x10, 36)
    -- cerca / lejos: solo se cambian si la camara quedo mas cerca que el plano cercano o mas lejos que el lejano
    local cerca, lejos = cam.cerca, cam.lejos
    if cam.dist * 0.5 < cerca then cerca = math.max(0.01, cam.dist * 0.2) end
    if cam.dist + 100 > lejos then lejos = cam.dist + 500 end
    if cerca ~= cam.cerca or lejos ~= cam.lejos or cam.cerca_w then
        WF[0], WF[1] = cerca, lejos
        poner(c + 0x34, 8)
        cam.cerca_w, cam.lejos_w = cerca, lejos
    end
    WF[0] = cam.fov
    poner(c + 0x40, 4)
end

local function cam_restaurar()
    local g = G.cam
    cam.activa = false
    if not g then return false end
    if cam_buscar() ~= g.dir then          -- el visor ya tiene otra camara (o se cerro): la vieja no se toca
        G.cam = false
        return false
    end
    -- solo lo que este modulo escribe: posicion, objetivo, arriba, cerca y lejos (+0x10..+0x3b) y el campo de vision (+0x40)
    ffi.copy(W8, g.orig, 0x2c)
    local ok = poner(g.dir + 0x10, 0x2c)
    ffi.copy(W8, string.sub(g.orig, 0x31, 0x34), 4)
    ok = poner(g.dir + 0x40, 4) and ok
    R("camara: vuelve a la del juego" .. (ok and "" or " (NO pude escribir)"), true)
    G.cam = false
    return ok
end

-- cambia el campo de vision sin cambiar cuanto ocupa en pantalla lo que se ve (se aleja o acerca la camara)
local function cam_lente(fov)
    if fov < 0.01 then fov = 0.01 elseif fov > 2.0 then fov = 2.0 end
    local h = cam.dist * math.tan(cam.fov / 2)
    cam.fov = fov
    cam.dist = h / math.tan(fov / 2)
end

local function cam_vista(i)
    cam.vista = i
    local pi = math.pi
    if i == 2 then cam.yaw, cam.pitch = cam.yaw0, 0
    elseif i == 3 then cam.yaw, cam.pitch = cam.yaw0 + pi, 0
    elseif i == 4 then cam.yaw, cam.pitch = cam.yaw0 + pi / 2, 0
    elseif i == 5 then cam.yaw, cam.pitch = cam.yaw0 - pi / 2, 0
    elseif i == 6 then cam.yaw, cam.pitch = cam.yaw0, 1.55
    elseif i == 7 then cam.yaw, cam.pitch = cam.yaw0, -1.55 end
end

local function cam_enfocar(x, y, z, radio)
    if not (finito(x) and finito(y) and finito(z)) then return false end
    if not cam.activa and not cam_activar() then return false end
    cam.pivote = { x, y, z }
    if radio and radio > 0.001 and radio < 500 then
        local d = 1.6 * radio / math.tan(cam.fov / 2)
        if d < 0.15 then d = 0.15 elseif d > 2500 then d = 2500 end
        cam.dist = d
    end
    cam_escribir()
    return true
end

-- ejes de la pantalla en el mundo y metros por pixel a la distancia d (o a la del objetivo)
local function base_camara(hacia)
    local b = { der = { 1, 0, 0 }, arr = { 0, 1, 0 }, ade = { 0, 0, -1 }, k = 0.002 }
    local c = cam.activa and cam.dir or cam_buscar()
    if not c or not leer(c, 0x48) then return b end
    local px, py, pz, tx, ty, tz, fov = fl(4), fl(5), fl(6), fl(7), fl(8), fl(9), fl(16)
    local f = norm3(tx - px, ty - py, tz - pz)
    if not f or not (fov > 0.001 and fov < 3.2) then return b end
    local d = math.sqrt((tx - px) ^ 2 + (ty - py) ^ 2 + (tz - pz) ^ 2)
    if hacia then
        local dd = (hacia[1] - px) * f[1] + (hacia[2] - py) * f[2] + (hacia[3] - pz) * f[3]
        if dd > 0.05 then d = dd end
    end
    local der = norm3(-f[3], 0, f[1])            -- adelante x arriba del mundo (el mundo del juego es diestro)
    if not der then return b end
    b.der, b.ade = der, f
    b.arr = { der[2] * f[3] - der[3] * f[2], der[3] * f[1] - der[1] * f[3], der[1] * f[2] - der[2] * f[1] }
    b.k = 2 * d * math.tan(fov / 2) / alto_ventana()
    return b
end

---------------------------------------------------------------------------------------------
-- 8. HERRAMIENTAS (mover / rotar / escalar lo marcado) con el mouse o de a pasos con el teclado
---------------------------------------------------------------------------------------------
local HERR = { "MOVER", "ROTAR", "ESCALAR" }
local EJES = { "PANTALLA", "X", "Y", "Z" }
local EJE_V = { false, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }
local arr = nil     -- arrastre en curso

local function herr_inicio()
    local sel, t = seleccion(), {}
    for i = 1, #sel do
        local r = sel[i]
        if r.movible then
            local S, Q, T = leer_tf(r)
            if S then t[#t + 1] = { r = r, S = S, Q = Q, T = T } end
        end
    end
    if #t == 0 then
        E.aviso = "nada para " .. string.lower(HERR[E.herr]) .. ": marca con 8 un objeto de la escena (los zfx_)"
        return nil
    end
    return { objs = t, tx = 0, ty = 0, base = base_camara(t[1].T), fallo = nil }
end

-- cx / cy = pixeles acumulados del mouse; paso = +1 / -1 para un paso fijo del teclado
local function herr_aplicar(a, cx, cy, paso)
    local b, eje = a.base, E.eje
    for i = 1, #a.objs do
        local o = a.objs[i]
        local S, Q, T = { o.S[1], o.S[2], o.S[3], o.S[4] }, { o.Q[1], o.Q[2], o.Q[3], o.Q[4] }, { o.T[1], o.T[2], o.T[3], o.T[4] }
        if E.herr == 1 then
            local e, c = EJE_V[eje], 0
            if paso then
                e = e or b.der
                c = 0.05 * paso
            elseif e then
                local ar = e[1] * b.der[1] + e[2] * b.der[2] + e[3] * b.der[3]
                local au = e[1] * b.arr[1] + e[2] * b.arr[2] + e[3] * b.arr[3]
                local den = ar * ar + au * au
                if den < 0.05 then den = 0.05 end
                c = (cx * ar - cy * au) * b.k / den
            end
            if e then
                T[1], T[2], T[3] = T[1] + e[1] * c, T[2] + e[2] * c, T[3] + e[3] * c
            else
                T[1] = T[1] + (b.der[1] * cx - b.arr[1] * cy) * b.k
                T[2] = T[2] + (b.der[2] * cx - b.arr[2] * cy) * b.k
                T[3] = T[3] + (b.der[3] * cx - b.arr[3] * cy) * b.k
            end
        elseif E.herr == 2 then
            local ang = paso and (paso * math.pi / 36) or (cx * 0.01)
            local d = q_eje(EJE_V[eje] or b.ade, ang)
            if E.conj then
                Q = q_mul(Q, { -d[1], -d[2], -d[3], d[4] })
            else
                Q = q_mul(d, Q)
            end
            Q = q_norm(Q)
        else
            local f = paso and (1 + 0.05 * paso) or math.exp(cx * 0.005)
            if eje == 1 then
                S[1], S[2], S[3] = S[1] * f, S[2] * f, S[3] * f
            else
                S[eje - 1] = S[eje - 1] * f
            end
            for k = 1, 3 do
                if math.abs(S[k]) < 0.001 then S[k] = 0.001 elseif math.abs(S[k]) > 10000 then S[k] = 10000 end
            end
        end
        local ok, por_que = aplicar_tf(o.r, S, Q, T)
        if ok then
            o.ult = { S, Q, T }
        elseif not a.fallo then
            a.fallo = o.r.nombre .. ": " .. tostring(por_que)
            E.aviso = "no se movio " .. a.fallo
            R("no se movio " .. a.fallo)
        end
    end
end

local function herr_fin(a)
    for i = 1, #a.objs do
        local o = a.objs[i]
        if o.ult then
            local S, Q, T = o.ult[1], o.ult[2], o.ult[3]
            R(string.format("%s %s (%s): pos %.3f %.3f %.3f  escala %.3f %.3f %.3f  giro %.4f %.4f %.4f %.4f%s",
                HERR[E.herr], o.r.nombre, EJES[E.eje], T[1], T[2], T[3], S[1], S[2], S[3], Q[1], Q[2], Q[3], Q[4],
                o.r.limite and " (aviso al motor)" or ""), true)
            avisar_motor(o.r)
        end
    end
    E.sucio = true
end

---------------------------------------------------------------------------------------------
-- 9. LUCES
---------------------------------------------------------------------------------------------
local function luz_valida(r)
    return r.vt ~= nil and VT_LUZ[r.vt] ~= nil and r.p_ok and en_escena(r, r.vt) and rptr(r.dir + 0x38) == r.p
        and rptr(r.p) == r.vt_p
end
local function luz_recordar(r)
    if G.luz_orig[r.dir] == nil or G.luz_orig[r.dir].nombre ~= r.nombre then
        G.luz_orig[r.dir] = { nombre = r.nombre, prendida = r.prendida, color = { r.color[1], r.color[2], r.color[3], r.color[4] } }
    end
end
local function luz_prender(r, si)
    if not luz_valida(r) then return false end
    local v = ru32(r.p + 0x64)
    if not v then return false end
    luz_recordar(r)
    if si and v % 2 == 0 then v = v + 1 elseif not si and v % 2 == 1 then v = v - 1 else return true end
    if not poner_u32(r.p + 0x64, v) then return false end
    r.prendida, r.flags = si, v
    return true
end
local function luz_color(r, c)
    if not c_color then
        E.aviso = "el brillo esta apagado: la funcion de color del juego no es la que conozco"
        return false
    end
    if E.solo_lectura or E.sin_llamadas then
        E.aviso = "el brillo necesita llamar al juego y esta apagado por archivo (solo_lectura / sin_llamadas)"
        return false
    end
    if not luz_valida(r) or not viva(r, r.vt) then
        E.aviso = "esa luz ya no esta en la escena"
        return false
    end
    -- la funcion del juego termina en la actualizacion propia de cada clase (ranura +0x78), que escribe en un bloque que
    -- cuelga de la luz sin mirar si existe: foco [+0xd8] (0xb0 bytes), punto y sol [+0x90] (0x40 bytes)
    local blo, tam = nil, 0
    if r.clase == "Foco" then blo, tam = rptr(r.dir + 0xd8), 0xb0
    elseif r.clase == "Punto" or r.clase == "Sol" then blo, tam = rptr(r.dir + 0x90), 0x40 end
    if tam > 0 and (not blo or (blo >= BASE and blo < EXE_FIN) or not leer(blo, tam)) then
        E.aviso = "esa luz no tiene su bloque de dibujo: no le cambio el brillo"
        return false
    end
    for i = 1, 4 do
        if not finito(c[i]) or math.abs(c[i]) > 1000000 then
            E.aviso = "brillo fuera de rango: no lo cambio"
            return false
        end
    end
    luz_recordar(r)
    for i = 1, 4 do WF[i - 1] = c[i] end
    c_color(r.dir, WF)
    E.n_llam = E.n_llam + 1
    r.color = { c[1], c[2], c[3], c[4] }
    return true
end
local function luz_fuerza(r) return math.max(r.color[1], r.color[2], r.color[3]) end

-- marcadores: un objeto zfx_Luz_NN por foco, puesto un poco por detras del foco (hacia afuera de la escena)
-- para que no le tape su propia luz ni su sombra
local function marcadores_poner()
    -- se marcan las luces de la misma escena grafica que los marcadores (hay otras escenas con luces propias); si en esa
    -- escena no hubiera ninguna luz, valen las de todas
    local k, esc_m, en_esa = 0, nil, 0
    for j = 1, #ayudas do
        if ayudas[j].ayuda == "luz" then esc_m = ayudas[j].esc break end
    end
    for i = 1, #luces do
        if luces[i].esc == esc_m then en_esa = en_esa + 1 end
    end
    for i = 1, #luces do
        local l = luces[i]
        if (l.esc == esc_m or en_esa == 0) and l.clase ~= "Sol" and l.clase ~= "Luz" and finito(l.x) and finito(l.y) and finito(l.z) then
            local mo
            for j = 1, #ayudas do
                if ayudas[j].ayuda == "luz" and not ayudas[j].usado then mo = ayudas[j] break end
            end
            if not mo then break end
            k = k + 1
            mo.usado = true
            local afuera = norm3(l.x, l.y - 1, l.z) or { 0, 1, 0 }
            local x, y, z = l.x + afuera[1] * 0.18, l.y + afuera[2] * 0.18, l.z + afuera[3] * 0.18
            local S, Q, T = nil, nil, nil
            if mo.movible then S, Q, T = leer_tf(mo) end
            if S then
                -- lo que va al punto es el CENTRO de la caja del modelo (como lo arma escena_objetos.py), no su origen
                local c = mo.caja
                local cx, cy, cz = (c[1] + c[4]) / 2 * S[1], (c[2] + c[5]) / 2 * S[2], (c[3] + c[6]) / 2 * S[3]
                if finito(cx) and finito(cy) and finito(cz) and math.abs(cx) < 5 and math.abs(cy) < 5 and math.abs(cz) < 5 then
                    x, y, z = x - cx, y - cy, z - cz
                end
                if math.abs(T[1] - x) > 0.002 or math.abs(T[2] - y) > 0.002 or math.abs(T[3] - z) > 0.002 then
                    T[1], T[2], T[3] = x, y, z
                    if aplicar_tf(mo, S, Q, T) then avisar_motor(mo) end
                end
            end
            if mo.oculto then ocultar(mo, false) end
        end
    end
    local sobran = 0
    for j = 1, #ayudas do
        local a = ayudas[j]
        if a.ayuda == "luz" then
            if not a.usado then
                sobran = sobran + 1
                if not a.oculto then ocultar(a, true) end
            end
            a.usado = nil
        end
    end
    return k, sobran
end
local function ayudas_ver(clase, ver)
    local n = 0
    for j = 1, #ayudas do
        local a = ayudas[j]
        if a.ayuda == clase then
            n = n + 1
            if a.oculto == ver then ocultar(a, not ver) end
        end
    end
    return n
end

---------------------------------------------------------------------------------------------
-- 10. VOLCADO COMPLETO AL REGISTRO
---------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------
-- 10. RENDER: las etapas del render del motor (plugins) y las vistas de la DLL grafica
---------------------------------------------------------------------------------------------
-- ETAPAS. Gestor de render = [G_RENDER] (el motor lo puede destruir y volver a crear: se relee siempre). +0x10 cantidad de
-- plugins, +0x18 arreglo de punteros a todos, +0x28 render principal (fox::gr::Render: +0xd0 distinto de cero mientras
-- dibuja el cuadro, +0xd8 modo). Plugin: +0x30 prioridad, +0x38 nombre, +0x40 indice, +0x44 tipo, +0x48 ACTIVO (un byte
-- que el bucle de dibujo FUN_14168d1a0 mira en cada cuadro), +0x4c lista (execMode), +0x50 nombre del padre.
-- El propio motor prende y apaga una etapa con la ranura 16 de su tabla; en la clase base esa funcion es solo
-- "mov [rcx+0x48], dl" (FN_ACTIVAR), o sea lo mismo que escribir el byte. Seis clases traen una funcion propia (tonemap,
-- antialiasing, profundidad de campo, reduccion del G-buffer, luz de entorno, modificador de luz): hacen algo mas y NO se
-- tocan desde aca. El motor mira el byte DOS veces por cuadro, al preparar y al dibujar (las sueltas en FUN_14168d1a0, las
-- hijas en FUN_14168d120 y FUN_14168b970): se cambia solo cuando el motor no esta armando un cuadro. Eso se sabe por dos
-- datos: el candado del cuadro G_CANDADO = { int cuenta, int giro, int hilo duenio } que FUN_14168b1c0 toma al empezar el
-- cuadro y suelta al terminar (FUN_1403f7f80 / FUN_1403f8020), y render+0xd0, distinto de cero mientras recorre las etapas.
-- MODO DEL MOTOR (experimental). GrTools.SwitchRenderMode (FUN_1416d2410) solo escribe un entero en render+0xd8: 2 deferred
-- (el normal), 3 forward, 4 prepass, 5 wireframe, 6 pseudoshade = que lista de etapas corre para cada vista. En el juego de
-- venta las listas 3 a 6 estan vacias (leido en el codigo, sin probar): lo esperado es pantalla negra y sin menus. Se deja
-- probar desde el panel, y vuelve solo al normal a los 8 segundos.
-- VISTAS. El juego de venta no trae los dibujos de depuracion del Fox Engine (alambre, G-buffer): los hace la DLL grafica
-- (PES21 FX, motorlink.cpp) y se le piden por una memoria compartida de 64 bytes "PES21FX_Motor_<pid>":
--   +0x00 'MTRL', +0x04 version 1, +0x08 seq (lo subo en cada pedido), +0x0c vista, +0x10 alambre, +0x14 corte (floats),
--   +0x18 cuadros de la DLL, +0x1c / +0x20 / +0x24 lo que rige, +0x28 dibujos en alambre del ultimo cuadro,
--   +0x2c escala de profundidad pedida, +0x30 la que rige.
RX.G_RENDER, RX.VT_RENDER, RX.FN_ACTIVAR, RX.G_CANDADO = 0x14374ced8, 0x142a07c58, 0x14168c810, 0x143756438
RX.MODO_SIG = { [2] = 5, [5] = 6, [6] = 3, [3] = 4, [4] = 2 }      -- orden en que la tecla 8 recorre los modos del motor
RX.MODO_NOM = { [2] = "deferred (normal)", [3] = "forward", [4] = "prepass", [5] = "wireframe (alambre)", [6] = "pseudoshade" }
RX.exp = { hasta = nil, espera = 0 }                            -- vuelta automatica del modo experimental
RX.LISTA = { [0] = "pre", "post", "deferred", "forward", "prepass", "wireframe", "pseudoshade" }
RX.VISTA = { [0] = "Normal", "Luz difusa", "Luz especular", "Normales", "Normales de mundo", "Albedo (sin luz)", "Profundidad",
    "Oclusion", "Sombra del sol", "Solo luz (sin texturas)", "Material" }
RX.QUE = { [0] = "la imagen de siempre", "solo la luz difusa que llega", "solo los brillos", "hacia donde mira cada cara (camara)",
    "hacia donde mira cada cara (mundo)", "el color del material, sin luz", "cerca claro, lejos oscuro (bandas 20 cm)",
    "la oclusion ambiente", "la sombra del sol", "la luz sobre un modelo sin texturas", "los canales del material" }
RX.T_HERR = { [0x47] = 1, [0x52] = 2, [0x53] = 3 }              -- G mover, R rotar, S escalar (como Blender)
RX.T_EJE = { [0x58] = 2, [0x59] = 3, [0x5A] = 4 }               -- X, Y, Z (otra vez la misma = eje de la pantalla)
RX.etapas, RX.render, RX.modo, RX.mal, RX.apagadas, RX.t = {}, nil, nil, nil, 0, -100000
-- fox::gr::Render (primera ranura) y RenderPlugin (ranura 16): si no coinciden se apaga solo la pagina RENDER, no el modulo
RX.RANURAS, RX.exe = { { 0x142a07c58, 0x14168aae0 }, { 0x142a07bc0 + 8 * 16, 0x14168c810 } }, nil
RX.dll = { u32 = nil, f = nil, t = -100000, estado = "todavia no busque la DLL grafica", pend = nil, visto = nil, t_visto = 0, viva = false }

function RX.leer()
    local vistas, por_nombre = {}, {}
    RX.etapas, RX.render, RX.modo, RX.mal, RX.apagadas = {}, nil, nil, nil, 0
    if RX.exe == nil then
        RX.exe = true
        for i = 1, #RX.RANURAS do
            if rptr(RX.RANURAS[i][1]) ~= RX.RANURAS[i][2] then RX.exe = false end
        end
        if not RX.exe then R("render: las tablas de Render / RenderPlugin de este exe no son las que lei: la pagina RENDER queda sin etapas", true) end
    end
    if not RX.exe then
        RX.mal = "no reconozco el render de este exe"
        return
    end
    local g = rptr(RX.G_RENDER)
    if not g or not leer(g, 0x30) then
        RX.mal = "el motor todavia no armo su render"
        return
    end
    local n, arr, ren = B32[0x10 / 4], P(0x18), P(0x28)
    if n > 0x80 or not arr then
        RX.mal = "el gestor de render no es el que conozco"
        return
    end
    if ren and leer(ren, 0xe8) and P(0) == RX.VT_RENDER then
        RX.render, RX.modo = ren, B32[0xd8 / 4]
        if G.modo_mio and RX.modo >= 3 and RX.modo <= 6 and not RX.exp.hasta then RX.exp.hasta = reloj() + 8000 end
    end
    local dirs = {}
    if n > 0 then
        if not leer(arr, n * 8) then
            RX.mal = "no pude leer la lista de etapas"
            return
        end
        for i = 1, n do dirs[i] = P((i - 1) * 8) or false end
    end
    for i = 1, n do
        local p = dirs[i]
        if p and leer(p, 0x60) then
            local vt, prio, idx, tipo, act, modo = P(0), B32[0x30 / 4], B32[0x40 / 4], B32[0x44 / 4], B8[0x48], B32[0x4c / 4]
            local s_nom = P(0x38)
            if vt and vt >= BASE and vt < EXE_FIN and s_nom and act <= 1 and modo <= 6 then
                if prio >= 0x80000000 then prio = prio - 4294967296 end
                local e = { dir = p, vt = vt, s_nom = s_nom, prio = prio, idx = idx, tipo = tipo, modo = modo, activa = (act == 1), nivel = 0 }
                e.propio = (rptr(vt + 0x80) ~= RX.FN_ACTIVAR)
                e.nombre = fox_cadena(p + 0x38, 40) or ("etapa_" .. hx(p))
                e.padre = fox_cadena(p + 0x50, 40)
                vistas[#vistas + 1] = e
                if not por_nombre[e.nombre] then por_nombre[e.nombre] = e end
                if not e.activa then RX.apagadas = RX.apagadas + 1 end
            end
        end
    end
    -- arbol: cada etapa seguida de sus hijas (el padre se busca por nombre y gana la primera, como hace el motor)
    local hijos, sueltas = {}, {}
    for i = 1, #vistas do
        local e = vistas[i]
        local pa = e.padre and por_nombre[e.padre]
        if pa and pa ~= e then
            local h = hijos[pa]
            if not h then
                h = {}
                hijos[pa] = h
            end
            h[#h + 1] = e
        else
            if e.padre then e.huerfana = true end       -- nombra un padre que no existe: el motor nunca la ejecuta
            sueltas[#sueltas + 1] = e
        end
    end
    local lista, puesto = {}, {}
    local function rama(e, nivel)
        if puesto[e] or nivel > 6 then return end
        puesto[e] = true
        e.nivel = nivel
        lista[#lista + 1] = e
        local h = hijos[e]
        if h then
            for i = 1, #h do rama(h[i], nivel + 1) end
        end
    end
    for i = 1, #sueltas do rama(sueltas[i], 0) end
    for i = 1, #vistas do                               -- si quedara un ciclo de padres, nada se pierde
        if not puesto[vistas[i]] then rama(vistas[i], 0) end
    end
    RX.etapas = lista
end

-- espera (hasta ms milisegundos) a que el motor no este armando un cuadro: el candado del cuadro libre (o tomado por este
-- mismo hilo: Sider me llamo desde adentro del cuadro) y el render sin recorrer sus etapas (+0xd0 en cero). Lecturas
-- directas, sin pasar por leer(): son muchas y seguidas. OJO: pisa el buffer de lectura. Devuelve true, o false y el motivo.
function RX.esperar_libre(ren, ms)
    local t0, vueltas, yo = reloj(), 0, C.mtr_GetCurrentThreadId()
    while vueltas < 400000 do
        vueltas = vueltas + 1
        if C.mtr_ReadProcessMemory(PROC, RX.G_CANDADO, B8, 12, NR) == 0 then return false, "no pude leer el candado del render" end
        local cuenta, duenio = B32[0], B32[2]
        if (cuenta == 0 and duenio == 0) or duenio == yo then
            if C.mtr_ReadProcessMemory(PROC, ren + 0xd0, B8, 8, NR) == 0 then return false, "no pude leer el render principal" end
            if B32[0] == 0 and B32[1] == 0 then
                RX.exp.espera = vueltas
                return true
            end
        end
        if vueltas % 64 == 0 then
            local dt = reloj() - t0
            if dt > ms or dt < 0 then break end
        end
    end
    RX.exp.espera = vueltas
    return false, string.format("el motor no dejo de dibujar en %d ms: proba de nuevo", ms)
end

-- prende o apaga una etapa. Devuelve true, o false y el motivo.
function RX.poner(e, si)
    if E.solo_lectura then return false, "en SOLO LECTURA no se cambia ninguna etapa" end
    if e.propio then return false, "esa etapa tiene un interruptor propio del motor: desde aca no se toca" end
    if not leer(e.dir, 0x60) or P(0) ~= e.vt or P(0x38) ~= e.s_nom or B8[0x48] > 1 then
        return false, "esa etapa ya no esta: el motor rearmo su render"
    end
    local era = (B8[0x48] == 1)
    if era == si then
        local g = G.et_orig[e.dir]
        if g and g.nombre == e.nombre and g.activa == si then G.et_orig[e.dir] = nil end     -- ya esta como la tenia el juego
        e.activa = si
        return true
    end
    local ren = RX.render
    if not ren or not leer(ren, 8) or P(0) ~= RX.VT_RENDER then return false, "no encuentro el render principal del motor" end
    local libre, por_que = RX.esperar_libre(ren, 60)
    if not libre then return false, por_que, true end        -- el tercer valor: no se pudo por espera agotada
    -- la espera pudo durar varios cuadros (y piso el buffer de lectura): la etapa se mira otra vez justo antes de escribir
    if not leer(e.dir, 0x60) or P(0) ~= e.vt or P(0x38) ~= e.s_nom or B8[0x48] > 1 then
        return false, "esa etapa ya no esta: el motor rearmo su render"
    end
    era = (B8[0x48] == 1)
    if era == si then                       -- el motor la cambio mientras esperaba: no hay nada que escribir
        e.activa = si
        return true
    end
    if not poner_u8(e.dir + 0x48, si and 1 or 0) then return false, "no pude escribir en esa etapa" end
    local g = G.et_orig[e.dir]
    if g and g.nombre == e.nombre then
        if g.activa == si then G.et_orig[e.dir] = nil end        -- volvio a como la tenia el juego
    else
        G.et_orig[e.dir] = { nombre = e.nombre, activa = era }
    end
    e.activa = si
    return true
end

-- EXPERIMENTAL: modo de dibujo del render principal (render+0xd8, lo mismo que escribe GrTools.SwitchRenderMode).
-- forzar = la vuelta al normal: se hace aunque el modulo este en solo lectura (no se deja la pantalla en un modo raro).
function RX.modo_poner(modo, ms, forzar)
    if E.solo_lectura and not forzar then return false, "en SOLO LECTURA no se cambia el modo del motor" end
    local ren = RX.render
    if not ren or not leer(ren, 0xe8) or P(0) ~= RX.VT_RENDER then return false, "no encuentro el render principal del motor" end
    local era = B32[0xd8 / 4]
    if era < 2 or era > 6 then return false, "el modo actual del render no es uno que conozca: no lo toco" end
    if era ~= modo then
        local libre, por_que = RX.esperar_libre(ren, ms)
        if not libre then return false, por_que end
        -- la espera pudo durar varios cuadros (y piso el buffer de lectura): el render se mira otra vez antes de escribir
        if not leer(ren, 0xe8) or P(0) ~= RX.VT_RENDER or B32[0xd8 / 4] ~= era then
            return false, "el render cambio mientras esperaba: no lo toco"
        end
        if forzar then
            W32[0] = modo
            E.n_escr = E.n_escr + 1
            if C.mtr_WriteProcessMemory(PROC, ren + 0xd8, W8, 4, NR) == 0 then return false, "no pude escribir el modo" end
        elseif not poner_u32(ren + 0xd8, modo) then
            return false, "no pude escribir el modo"
        end
    end
    RX.modo = modo
    if modo == 2 then G.modo_mio = nil elseif era ~= modo then G.modo_mio = true end
    if modo == 2 then RX.exp.hasta = nil else RX.exp.hasta = reloj() + 8000 end
    return true
end
-- una vez por cuadro del panel: a los 8 segundos el modo experimental vuelve solo al normal (si el motor esta ocupado,
-- prueba de nuevo en el cuadro siguiente)
function RX.exp_pulso(ya)
    local x = RX.exp
    if not x.hasta then return end
    local ahora = reloj()
    if not ya and ahora < x.hasta and ahora >= x.hasta - 8000 then return end
    if RX.modo_poner(2, 4, true) then
        R("render: fin del modo experimental, el motor vuelve a deferred (el normal)", true)
        E.sucio = true
    end
end
function RX.tecla_modo(vkey)
    local quiero
    if vkey == 0x38 then quiero = RX.MODO_SIG[RX.modo or 2] or 2
    elseif vkey == 0x37 then quiero = 2
    else
        E.aviso = "8 pasa al modo siguiente (vuelve solo a los 8 s), 7 vuelve al normal"
        return
    end
    local ok, por_que = RX.modo_poner(quiero, 60, quiero == 2)
    if ok then
        R(string.format("render: MODO DEL MOTOR %d %s (render 0x%s +0xd8, espere %d vueltas)%s", quiero, RX.MODO_NOM[quiero], hx(RX.render),
            RX.exp.espera, (quiero ~= 2) and "; vuelve solo a deferred en 8 s" or ""), true)
    else
        E.aviso = por_que
    end
    E.sucio = true
end

function RX.tecla_etapa(e, vkey)
    if not e then return end
    local quiero
    if vkey == 0x38 then quiero = not e.activa
    elseif vkey == 0x37 then
        local g = G.et_orig[e.dir]
        if not (g and g.nombre == e.nombre) then
            E.aviso = "esa etapa esta como la dejo el juego"
            return
        end
        quiero = g.activa
    else
        E.aviso = "8 prende / apaga la etapa, 7 la deja como la tenia el juego"
        return
    end
    local ok, por_que = RX.poner(e, quiero)
    if ok then R(string.format("render: etapa %s %s (0x%s, espere %d vueltas)", e.nombre, quiero and "PRENDIDA" or "apagada", hx(e.dir), RX.exp.espera), true)
    else E.aviso = por_que end
    E.sucio = true
end

-- deja las etapas como las tenia el juego; devuelve cuantas repuso y cuantas no pudo
function RX.reponer()
    RX.leer()
    local hechas, falta, quedan, ocupado = 0, 0, {}, false
    for i = 1, #RX.etapas do
        local e = RX.etapas[i]
        local g = G.et_orig[e.dir]
        if g and g.nombre == e.nombre then
            -- si el motor no aflojo para una, no se vuelve a esperar por cada una de las demas (quedan para otro intento)
            local ok, por_que, agotada = false, nil, false
            if not ocupado then ok, por_que, agotada = RX.poner(e, g.activa) end
            if agotada then ocupado = true end
            if ok then hechas = hechas + 1
            else
                quedan[e.dir] = g
                falta = falta + 1
            end
        end
    end
    G.et_orig = quedan
    return hechas, falta
end

-- ---- vistas de la DLL grafica
function RX.dll_f(i)
    local u = RX.dll.u32[i]
    if u > 0xff800000 or (u > 0x7f800000 and u < 0x80000000) then return 0 end
    return RX.dll.f[i]
end
function RX.dll_abrir()
    local d = RX.dll
    if d.u32 then return true end
    local ahora = reloj()
    if ahora - d.t < 2000 and ahora >= d.t then return false end
    d.t = ahora
    local nombre = string.format("PES21FX_Motor_%d", E.mi_pid)
    local h = C.mtr_OpenFileMappingA(0xF001F, 0, nombre)
    if h == nil then
        d.estado = "la DLL grafica instalada no trae las vistas"
        return false
    end
    local p = C.mtr_MapViewOfFile(h, 0xF001F, 0, 0, 64)
    C.mtr_CloseHandle(h)                    -- la vista sigue valida sin el identificador
    if p == nil then
        d.estado = "no pude abrir la memoria de la DLL grafica"
        return false
    end
    local u = ffi.cast("uint32_t *", p)
    if u[0] ~= 0x4C52544D or u[1] ~= 1 then
        C.mtr_UnmapViewOfFile(p)
        d.estado = "la DLL grafica habla otra version del enlace"
        return false
    end
    d.u32, d.f = u, ffi.cast("float *", p)
    d.estado = "enlazada"
    R("vistas: enlace con la DLL grafica abierto (" .. nombre .. ")", true)
    return true
end
-- lo que rige: vista, alambre, corte, escala. Si hay un pedido mio que la DLL todavia no tomo, vale el pedido.
function RX.dll_valores()
    local d = RX.dll
    if d.pend and d.u32[6] == d.pend then return math.floor(RX.dll_f(3) + 0.5), RX.dll_f(4), RX.dll_f(5), RX.dll_f(11) end
    d.pend = nil
    local v = math.floor(RX.dll_f(7) + 0.5)
    if v < 0 or v > 10 then v = 0 end
    return v, RX.dll_f(8), RX.dll_f(9), RX.dll_f(12)
end
function RX.dll_pedir(v, a, c, k)
    if E.solo_lectura then
        E.aviso = "en SOLO LECTURA no se cambia la vista"
        return false
    end
    if not RX.dll_abrir() then
        E.aviso = RX.dll.estado
        return false
    end
    local d = RX.dll
    d.f[3], d.f[4], d.f[5], d.f[11] = v, a, c, k
    d.pend = d.u32[6]
    d.u32[2] = d.u32[2] + 1
    return true
end
-- una vez por cuadro del panel: la DLL esta viva si su cuenta de cuadros avanza
function RX.dll_pulso()
    local d = RX.dll
    if not d.u32 then return end
    local s, ahora = d.u32[6], reloj()
    if s ~= d.visto then
        d.visto, d.t_visto, d.viva = s, ahora, true
    elseif ahora - d.t_visto > 2000 or ahora < d.t_visto then
        d.viva = false
    end
end
function RX.tecla_vista(fila, vkey)
    if not RX.dll_abrir() then
        E.aviso = RX.dll.estado
        return
    end
    local v, a, c, k = RX.dll_valores()
    if fila == 1 then
        if vkey == 0x38 or vkey == 0x33 then v = (v + 1) % 11
        elseif vkey == 0x34 then v = (v - 1) % 11
        elseif vkey == 0x37 then v = 0
        else return end
    elseif fila == 2 then
        if vkey == 0x38 then a = (a > 0.5) and 0 or 1
        elseif vkey == 0x37 then a = 0
        else
            E.aviso = "el alambre se prende y se apaga con 8"
            return
        end
    else
        local hay = (c > 0.005)
        if vkey == 0x38 then c = hay and 0 or 0.5
        elseif vkey == 0x33 then c = hay and math.min(0.95, c + 0.05) or 0.5
        elseif vkey == 0x34 then c = hay and math.max(0.05, c - 0.05) or 0.5
        elseif vkey == 0x37 then c = 0
        else return end
    end
    if RX.dll_pedir(v, a, c, k) then
        R(string.format("vista: %s, alambre %s, pantalla partida %s", RX.VISTA[v], (a > 0.5) and "SI" or "no",
            (c > 0.005) and string.format("%.0f %%", c * 100) or "no"), true)
    end
end
function RX.volcar()
    RX.leer()
    R(string.format("render: gestor [0x%s], render principal %s, modo %s, %d etapas (%d apagadas)%s", hx(RX.G_RENDER),
        RX.render and ("0x" .. hx(RX.render)) or "NO", tostring(RX.modo), #RX.etapas, RX.apagadas, RX.mal and (" | " .. RX.mal) or ""))
    for i = 1, #RX.etapas do
        local e = RX.etapas[i]
        R(string.format("etapa %2d 0x%s %s%-32s %s lista %s prio %d tipo %d indice %d padre %s tabla 0x%s%s%s", i, hx(e.dir),
            string.rep("  ", e.nivel), e.nombre, e.activa and "PRENDIDA" or "apagada ", RX.LISTA[e.modo] or "?", e.prio, e.tipo, e.idx,
            e.padre or "-", hx(e.vt), e.propio and " INTERRUPTOR PROPIO (no se toca)" or "", e.huerfana and " PADRE INEXISTENTE (no corre)" or ""))
    end
    local d = RX.dll
    if RX.dll_abrir() then
        RX.dll_pulso()
        local v, a, c, k = RX.dll_valores()
        R(string.format("vistas: DLL enlazada (cuadros %d, %s), vista %d %s, alambre %s (%d dibujos), corte %.2f, escala %g", d.u32[6],
            d.viva and "viva" or "sin avanzar", v, RX.VISTA[v] or "?", (a > 0.5) and "SI" or "no", d.u32[10], c, k))
    else
        R("vistas: " .. d.estado)
    end
end

local function volcar_todo(por_que)
    R(string.format("===== VOLCADO (%s) version %s: %d escenas, %d modelos + %d de ayuda, %d luces, %d otros; lecturas %d escrituras %d llamadas %d fallas %d; giro %s",
        por_que, VERSION, #escenas, #modelos, #ayudas, #luces, #otros, E.n_lect, E.n_escr, E.n_llam, E.n_falla, E.conj_visto), true)
    R(string.format("hilos: cuadro %s, tecla %s, proceso %d; listas del motor %s", tostring(E.hilo_cuadro), tostring(E.hilo_tecla), E.mi_pid,
        E.listas_ok and "comprobadas" or "SIN COMPROBAR (solo lectura para modelos y luces)"))
    for i = 1, #escenas do
        local e = escenas[i]
        R(string.format("escena %d: 0x%s nombre(hash) %s, listas con algo %d, en cola %d, cuenta %d, recorridos %d (%d modelos, %d luces), raras %d%s -> %s",
            i, hx(e.dir), e.hash, e.n_listas, e.n_cola, e.cuenta, e.n, e.n_mod, e.n_luz, e.raro, e.tope and " TOPE" or "", e.ok and "coincide" or "NO coincide"))
    end
    for i = 1, #modelos + #ayudas do
        local r = (i <= #modelos) and modelos[i] or ayudas[i - #modelos]
        R(string.format("modelo E%d 0x%s %-28s pos %.3f %.3f %.3f escala %.3f %.3f %.3f caja %.2f %.2f %.2f / %.2f %.2f %.2f huesos %d limite %s copia3 %s oculto %s (1ac %s 1b0 %s 1b4 %s) lod %.1f %.1f %.1f descarte %d cuerpo %s entidad %s (%s) te %s padre %s%s",
            r.esc, hx(r.dir), r.nombre, r.x, r.y, r.z, r.e1, r.e2, r.e3, r.caja[1], r.caja[2], r.caja[3], r.caja[4], r.caja[5], r.caja[6],
            r.huesos, r.limite and hx(r.limite) or "no", r.copia3 and hx(r.copia3) or "no", tostring(r.oculto), hx(r.mascara), hx(r.b1b0), hx(r.b1b4),
            r.lod_cerca, r.lod_lejos, r.lod_poli, r.descarte, r.cuerpo and hx(r.cuerpo) or "-", r.ent and hx(r.ent) or "-",
            r.vt_ent and clase_corta(r.vt_ent) or "-", r.te and hx(r.te) or "-", r.padre and hx(r.padre) or "no",
            r.ruta and (" archivo " .. r.ruta) or ""))
    end
    for i = 1, #luces do
        local l = luces[i]
        R(string.format("luz E%d 0x%s %-6s %-24s %s pos %.3f %.3f %.3f color %.2f %.2f %.2f %.2f flags 0x%s sombra %s alcance %.2f / %.2f giro %.4f %.4f %.4f %.4f conos %.3f %.3f datos 0x%s (%s)%s",
            l.esc, hx(l.dir), l.clase, l.nombre, l.prendida and "PRENDIDA" or "apagada", l.x, l.y, l.z, l.color[1], l.color[2], l.color[3], l.color[4],
            hx(l.flags), tostring(l.sombra), l.alc_int, l.alcance, l.q[1], l.q[2], l.q[3], l.q[4], l.umbra, l.penumbra, hx(l.p),
            clase_corta(l.vt_p), l.p_ok and "" or " CLASE DE DATOS DESCONOCIDA: no la toco"))
    end
    local cuenta = {}
    for i = 1, #otros do cuenta[otros[i].clase] = (cuenta[otros[i].clase] or 0) + 1 end
    for c, n in pairs(cuenta) do R(string.format("otros objetos de escena: %d de clase %s", n, c)) end
    listar_entidades()
    for i = 1, #ents.lista do
        local it = ents.lista[i]
        R(string.format("entidad 0x%s %-22s %-30s flags 0x%s%s", hx(it.dir), it.clase, it.nombre, hx(it.flags),
            it.con_tf and string.format(" pos %.3f %.3f %.3f", it.x, it.y, it.z) or ""))
    end
    local c, nodo, impl = cam_buscar()
    if c and leer(c, 0x120) then
        local t = {}
        for i = 0, 0x120 / 4 - 1 do t[#t + 1] = string.format("%08x", B32[i]) end
        R("camara del visor 0x" .. hx(c) .. " (0x120 bytes, de a 4): " .. table.concat(t, " "))
    else
        R("camara del visor: no hay (fuera de Editar > Apariencia)")
    end
    RX.volcar()
    R("===== fin del volcado")
end

---------------------------------------------------------------------------------------------
-- 11. PAGINAS: que muestra cada una y que hace cada tecla
---------------------------------------------------------------------------------------------
local PAGS = { "ESCENA", "LUCES", "ENTIDADES", "OPCIONES", "REGISTRO", "RENDER" }
local FILAS, A_IZQ, A_DER = 10, 46, 56

local function col(s, n)
    local l = #s
    if l > n then return string.sub(s, 1, n) end
    return s .. string.rep(" ", n - l)
end
local function ventana(n, cur)
    local ini = cur - math.floor(FILAS / 2)
    if ini > n - FILAS + 1 then ini = n - FILAS + 1 end
    if ini < 1 then ini = 1 end
    return ini
end
local function mover_cursor(i, n, d)
    if n <= 0 then E.cur[i] = 1 return end
    local c = E.cur[i] + d
    if c > n then c = (d == 1) and 1 or n end
    if c < 1 then c = (d == -1) and n or 1 end
    E.cur[i] = c
end

-- opciones (pagina 4): { texto, valor(), accion(8), menos / mas (4 / 3) }
local OPC
local function paso_herr(signo)
    local a = herr_inicio()
    if not a then return end
    herr_aplicar(a, 0, 0, signo)
    herr_fin(a)
end
local function restaurar_todo()
    if E.solo_lectura then                  -- no se puede escribir: no se repone ni se olvida ningun original
        E.aviso = "en SOLO LECTURA no se repone nada: borra solo_lectura.txt y volve a elegir esta opcion"
        return
    end
    local nl, no, nt, falta = 0, 0, 0, 0
    local ql, qt, qo = {}, {}, {}           -- originales que no se pudieron reponer: quedan para otro intento
    for i = 1, #luces do
        local l, g = luces[i], G.luz_orig[luces[i].dir]
        if g and g.nombre == l.nombre then
            local ok = luz_prender(l, g.prendida)
            if ok and c_color and (l.color[1] ~= g.color[1] or l.color[2] ~= g.color[2] or l.color[3] ~= g.color[3]) then ok = luz_color(l, g.color) end
            if ok then nl = nl + 1 else ql[l.dir] = g falta = falta + 1 end
        end
    end
    for i = 1, #modelos + #ayudas do
        local r = (i <= #modelos) and modelos[i] or ayudas[i - #modelos]
        local g = r.ent and G.tf_orig[r.ent]
        if g and g.nombre == r.nombre then
            if aplicar_tf(r, g.S, g.Q, g.T) then
                avisar_motor(r)
                nt = nt + 1
            else
                qt[r.ent] = g
                falta = falta + 1
            end
        end
        local go = G.oc_orig[r.dir]
        if go and go.nombre == r.nombre and (r.oculto ~= go.oculto
            or (not r.cuerpo and go.masc and go.bit and (r.mascara ~= go.masc or r.b1b4 % 2 ~= go.bit))) then
            if ocultar(r, go.oculto, go) then no = no + 1 else qo[r.dir] = go falta = falta + 1 end
        end
    end
    G.luz_orig, G.tf_orig, G.oc_orig = ql, qt, qo
    E.solo, E.grilla, E.marcadores = nil, false, false
    if G.cam then cam_restaurar() end
    local ne, fe = RX.reponer()                 -- etapas del render como las tenia el juego
    falta = falta + fe
    if RX.exp.hasta then RX.exp_pulso(true) end -- modo del motor: al normal
    if RX.dll_abrir() then                      -- vista normal, sin alambre ni pantalla partida (solo si habia algo puesto)
        local v, a, c = RX.dll_valores()
        if v ~= 0 or a > 0.5 or c > 0.005 then RX.dll_pedir(0, 0, 0, 0) end
    end
    E.sucio = true
    E.aviso = string.format("todo como estaba: %d luces, %d objetos en su lugar, %d visibles / ocultos, %d etapas del render%s", nl, nt, no, ne,
        (falta > 0) and string.format(" | NO repuse %d: probar de nuevo", falta) or "")
    R(E.aviso, true)
end

OPC = {
    { "Camara libre (mouse der. gira)", function() return cam.activa and "SI" or "no" end,
        function() if cam.activa then cam_restaurar() else cam_activar() end end },
    { "Proyeccion", function() return cam.activa and MODOS[cam.modo] or "-" end,
        function()
            if not cam.activa and not cam_activar() then return end
            cam.modo = cam.modo % 3 + 1
            cam_lente((cam.modo == 1) and cam.fov_juego or (cam.modo == 2) and 0.6 or 0.02)
            R(string.format("camara: %s (campo %.4f rad, distancia %.2f m)", MODOS[cam.modo], cam.fov, cam.dist), true)
        end },
    { "Vista", function() return cam.activa and VISTAS[cam.vista] or "-" end,
        function()
            if not cam.activa and not cam_activar() then return end
            cam_vista(cam.vista % #VISTAS + 1)
        end,
        function(s)
            if not cam.activa and not cam_activar() then return end
            cam_vista((cam.vista - 1 + s) % #VISTAS + 1)
        end },
    { "Lente (campo de vision)", function() return cam.activa and string.format("%.1f grados", math.deg(cam.fov)) or "-" end, nil,
        function(s)
            if not cam.activa and not cam_activar() then return end
            cam_lente(cam.fov * ((s > 0) and 1.2 or 1 / 1.2))
        end },
    { "Distancia de la camara", function() return cam.activa and string.format("%.2f m", cam.dist) or "-" end, nil,
        function(s)
            if not cam.activa and not cam_activar() then return end
            cam.dist = cam.dist * ((s > 0) and 1.15 or 1 / 1.15)
        end },
    { "Grilla de orientacion", function() return E.grilla and "SI" or "no" end,
        function()
            E.grilla = not E.grilla
            local n = ayudas_ver("grilla", E.grilla)
            if n == 0 then
                E.aviso = "esta escena no trae la grilla (objetos zfx_Grilla_)"
                E.grilla = false
            end
            R(string.format("grilla %s (%d objetos)", E.grilla and "a la vista" or "oculta", n), true)
            E.sucio = true
        end },
    { "Marcador en cada foco", function() return E.marcadores and "SI" or "no" end,
        function()
            E.marcadores = not E.marcadores
            if E.marcadores then
                local k, sobran = marcadores_poner()
                if k == 0 and sobran == 0 then
                    E.aviso = "esta escena no trae marcadores (objetos zfx_Luz_)"
                    E.marcadores = false
                end
                R(string.format("marcadores de focos: %d puestos, %d sin usar", k, sobran), true)
            else
                ayudas_ver("luz", false)
                R("marcadores de focos ocultos", true)
            end
            E.sucio = true
        end },
    { "Mover lo marcado de a 5 cm", function() return "eje " .. EJES[E.eje] end, nil,
        function(s) local h = E.herr E.herr = 1 paso_herr(s) E.herr = h end },
    { "Rotar lo marcado de a 5 grados", function() return "eje " .. EJES[E.eje] end, nil,
        function(s) local h = E.herr E.herr = 2 paso_herr(s) E.herr = h end },
    { "Escalar lo marcado de a 5 %", function() return "eje " .. EJES[E.eje] end, nil,
        function(s) local h = E.herr E.herr = 3 paso_herr(s) E.herr = h end },
    { "Eje de las herramientas", function() return EJES[E.eje] end,
        function() E.eje = E.eje % #EJES + 1 end },
    { "Desmarcar todo", function()
            local n = 0
            for _ in pairs(marcados) do n = n + 1 end
            return n .. " marcados"
        end,
        function() marcados = {} end },
    { "Mouse", function() return E.mouse and "SI" or "no" end, function() E.mouse = not E.mouse end },
    { "Panel", function() return E.compacto and "compacto" or "completo" end, function() E.compacto = not E.compacto end },
    { "Dejar todo como estaba", function() return "" end, restaurar_todo },
    { "Anotar todo en el registro", function() return "" end,
        function()
            volcar_todo("pedido con la tecla")
            E.aviso = "anotado en content\\motor-tiempo-real\\registro.txt"
        end },
    { "Pestana en Editar", function() return PE.puesta and "puesta" or "no" end,
        function() E.aviso = "pestana: " .. PE.estado .. " (se saca creando sin_pestana.txt)" end },
}

local function tecla(vkey)
    local pag = E.pag
    E.aviso = ""
    if RX.T_HERR[vkey] or RX.T_EJE[vkey] then
        -- como en Blender: G mover, R rotar, S escalar; X / Y / Z fijan el eje (la misma letra otra vez = eje de la pantalla)
        if arr then
            E.aviso = "solta el mouse antes de cambiar de herramienta o de eje"
            return
        end
        if RX.T_HERR[vkey] then E.herr = RX.T_HERR[vkey]
        elseif RX.eje_abajo ~= vkey then        -- la misma letra sin soltarla es la repeticion del teclado: no cuenta como "otra vez"
            RX.eje_abajo = vkey
            E.eje = (E.eje == RX.T_EJE[vkey]) and 1 or RX.T_EJE[vkey]
        end
        if pag ~= 1 then
            E.pag = 1
            E.sucio = true
        end
        E.aviso = string.format("%s en el eje %s: marca objetos con 8 y arrastra con el boton izquierdo del mouse", HERR[E.herr], EJES[E.eje])
        return
    end
    if vkey == 0x36 then
        E.pag = pag % #PAGS + 1
        E.sucio = true
    elseif vkey == 0x35 then
        E.pag = (pag - 2) % #PAGS + 1
        E.sucio = true
    elseif pag == 1 then
        local r = modelos[E.cur[1]]
        if vkey == 0x30 then mover_cursor(1, #modelos, 1)
        elseif vkey == 0x39 then mover_cursor(1, #modelos, -1)
        elseif vkey == 0x38 then
            if r then marcados[r.dir] = (not marcados[r.dir]) or nil end
        elseif vkey == 0x37 then
            local sel = seleccion()
            if #sel > 0 then
                local oc, hechos = not sel[1].oculto, 0
                for i = 1, #sel do
                    if ocultar(sel[i], oc) then hechos = hechos + 1 end
                end
                R(string.format("%s %d de %d objeto(s): %s%s", oc and "oculto" or "muestro", hechos, #sel, sel[1].nombre, (#sel > 1) and " y mas" or ""), true)
                if hechos < #sel then E.aviso = string.format("solo pude con %d de %d (los otros ya no estan o esta en solo lectura)", hechos, #sel) end
                E.sucio = true
            end
        elseif vkey == 0x34 or vkey == 0x33 then
            -- con un arrastre en curso no se cambia de herramienta ni de eje (reinterpretaria todo el arrastre)
            if arr then E.aviso = "solta el mouse antes de cambiar de herramienta o de eje"
            elseif vkey == 0x34 then E.herr = E.herr % #HERR + 1
            else E.eje = E.eje % #EJES + 1 end
        elseif vkey == 0x32 and r then
            local c = r.caja
            local radio = 0.5 * math.sqrt((c[4] - c[1]) ^ 2 + (c[5] - c[2]) ^ 2 + (c[6] - c[3]) ^ 2) * math.max(math.abs(r.e1), math.abs(r.e2), math.abs(r.e3))
            if not finito(radio) then radio = nil end
            if cam_enfocar(r.x, r.y, r.z, radio) then R("camara: enfoco " .. r.nombre) end
        end
    elseif pag == 2 then
        local l = luces[E.cur[2]]
        if vkey == 0x30 then mover_cursor(2, #luces, 1)
        elseif vkey == 0x39 then mover_cursor(2, #luces, -1)
        elseif vkey == 0x38 and l then
            local si = not l.prendida
            if luz_prender(l, si) then R(string.format("luz %s: %s", l.nombre, si and "PRENDIDA" or "apagada"), true)
            else E.aviso = "no pude tocar esa luz (solo lectura, listas sin comprobar o ya no esta)" end
        elseif vkey == 0x37 and l then
            -- cambiadas = las que de verdad cambie; pend = las que habia que cambiar y no pude (solo lectura, listas sin
            -- comprobar, luz que ya no esta). Si no cambie ninguna y quedaron pendientes, no anoto ni muestro nada hecho
            local antes, cambiadas, pend = {}, 0, 0
            for i = 1, #luces do
                local x = luces[i]
                local era, q = x.prendida, (x == l)
                antes[x.dir] = era
                if E.solo then q = E.solo[x.dir] end
                if q ~= nil then
                    if not luz_prender(x, q) then
                        if era ~= q then pend = pend + 1 end
                    elseif x.prendida ~= era then
                        cambiadas = cambiadas + 1
                    end
                end
            end
            if cambiadas == 0 and pend > 0 then
                E.aviso = "no pude tocar las luces (solo lectura, listas sin comprobar o ya no estan)"
            elseif E.solo then
                E.solo = nil
                R("luces: vuelven las que estaban prendidas", true)
            else
                E.solo = antes
                R("luces: queda sola " .. l.nombre, true)
            end
        elseif (vkey == 0x34 or vkey == 0x33) and l then
            local f = (vkey == 0x33) and 1.25 or 0.8
            local c = l.color
            if luz_fuerza(l) < 0.0001 and f > 1 then c = { 1, 1, 1, c[4] } end   -- estaba en negro: arranca en blanco
            -- tope: 2000, o 8 veces lo que traia la luz si ya era mas fuerte (en la prueba 2 un sol llego a 815663)
            local g = G.luz_orig[l.dir]
            local base = (g and g.nombre == l.nombre) and math.max(g.color[1], g.color[2], g.color[3]) or luz_fuerza(l)
            local tope = math.max(2000, 8 * (finito(base) and base or 0))
            if f > 1 and math.max(c[1], c[2], c[3]) * f > tope then
                E.aviso = string.format("tope de brillo (%.0f): no lo subo mas", tope)
            elseif luz_color(l, { c[1] * f, c[2] * f, c[3] * f, c[4] }) then
                R(string.format("luz %s: color %.3f %.3f %.3f", l.nombre, l.color[1], l.color[2], l.color[3]), true)
            end
        elseif vkey == 0x32 and l then
            if l.clase == "Sol" or l.clase == "Luz" then
                E.aviso = "esa luz no tiene posicion (es una direccion): no hay donde enfocar"
            elseif cam_enfocar(l.x, l.y, l.z, nil) then R("camara: enfoco la luz " .. l.nombre) end
        end
        E.sucio = true
    elseif pag == 3 then
        local n = #ents.lista
        local it = ents.lista[E.cur[3]]
        if vkey == 0x30 then mover_cursor(3, n, 1)
        elseif vkey == 0x39 then mover_cursor(3, n, -1)
        elseif vkey == 0x38 then E.cur[3] = math.min(math.max(n, 1), E.cur[3] + 10)
        elseif vkey == 0x37 then E.cur[3] = math.max(1, E.cur[3] - 10)
        elseif vkey == 0x32 and it and it.con_tf then
            if cam_enfocar(it.x, it.y, it.z, nil) then R("camara: enfoco la entidad " .. it.nombre) end
        end
    elseif pag == 4 then
        local o = OPC[E.cur[4]]
        if vkey == 0x30 then mover_cursor(4, #OPC, 1)
        elseif vkey == 0x39 then mover_cursor(4, #OPC, -1)
        elseif vkey == 0x38 and o and o[3] then o[3]()
        elseif (vkey == 0x34 or vkey == 0x33) and o and o[4] then o[4]((vkey == 0x33) and 1 or -1)
        elseif vkey == 0x38 or vkey == 0x34 or vkey == 0x33 then
            E.aviso = (o and o[4]) and "esta opcion se cambia con 4 (menos) y 3 (mas)" or "esta opcion se usa con 8"
        end
        cam_escribir()
    elseif pag == 5 then
        if vkey == 0x38 then
            volcar_todo("pedido con la tecla")
            E.aviso = "anotado en content\\motor-tiempo-real\\registro.txt"
        end
    elseif pag == 6 then
        local n, cur = 4 + #RX.etapas, E.cur[6]
        if cur > n then                         -- la lista se achico: el cursor es el que muestra el panel
            cur = n
            E.cur[6] = n
        end
        if vkey == 0x30 then mover_cursor(6, n, 1)
        elseif vkey == 0x39 then mover_cursor(6, n, -1)
        elseif cur <= 3 then RX.tecla_vista(cur, vkey)
        elseif cur == 4 then RX.tecla_modo(vkey)
        else RX.tecla_etapa(RX.etapas[cur - 4], vkey) end
    end
end

-- mouse: se mira en cada cuadro (el juego no usa el mouse). Solo con la ventana del juego al frente.
-- modo = lo que se esta haciendo; pedido = lo que piden los botones (si un pedido falla, modo queda en "nada" y NO se
-- reintenta hasta que cambien los botones); cam = hubo un boton de camara en este gesto; t_izq = cuando se apreto el izquierdo
local raton = { x = 0, y = 0, modo = nil, pedido = nil, ix = 0, iy = 0, cam = false, t_izq = nil }
local function arrastre_fin(modo)
    if modo == "herr" and arr then
        herr_fin(arr)
        arr = nil
    end
end
local function entrada()
    if not E.mouse then
        if raton.modo then arrastre_fin(raton.modo) end
        raton.modo, raton.pedido, raton.cam, raton.t_izq = nil, nil, false, nil
        return
    end
    PID[0] = 0
    C.mtr_GetWindowThreadProcessId(C.mtr_GetForegroundWindow(), PID)
    local frente = (PID[0] == E.mi_pid)
    C.mtr_GetCursorPos(PT)
    local x, y = PT[0], PT[1]
    local izq = frente and C.mtr_GetAsyncKeyState(1) < 0
    local der = frente and C.mtr_GetAsyncKeyState(2) < 0
    local med = frente and C.mtr_GetAsyncKeyState(4) < 0
    -- despues de un gesto de camara el izquierdo no es herramienta hasta soltar todos los botones (al soltar "izq+der" nunca
    -- se sueltan los dos en el mismo cuadro), y el izquierdo solo recien es herramienta a los 120 ms (al apretar tampoco)
    if der or med then raton.cam = true elseif not izq then raton.cam = false end
    if not izq then raton.t_izq = nil end
    local quiere = nil
    if izq and der then quiere = "zoom" elseif der then quiere = "orbita" elseif med then quiere = "paneo"
    elseif izq and E.pag == 1 and not raton.cam then
        local t = reloj()
        if not raton.t_izq then raton.t_izq = t end
        local pasaron = t - raton.t_izq
        if raton.pedido == "herr" or pasaron >= 120 or pasaron < 0 then quiere = "herr" end
    end
    if quiere ~= raton.pedido then
        if raton.modo then arrastre_fin(raton.modo) end
        raton.pedido, raton.modo, raton.ix, raton.iy = quiere, quiere, x, y
        if quiere == "herr" then
            arr = herr_inicio()
            if not arr then raton.modo = "nada" end
        elseif quiere and not cam.activa and not cam_activar() then
            raton.modo = "nada"
        end
        raton.x, raton.y = x, y
        return
    end
    if not quiere or raton.modo == "nada" then
        raton.x, raton.y = x, y
        return
    end
    local dx, dy = x - raton.x, y - raton.y
    if dx == 0 and dy == 0 then return end
    if quiere == "orbita" then
        cam.yaw, cam.pitch, cam.vista = cam.yaw - dx * 0.005, cam.pitch + dy * 0.005, 1
    elseif quiere == "paneo" then
        local b, p = base_camara(nil), cam.pivote
        for i = 1, 3 do p[i] = p[i] + (-b.der[i] * dx + b.arr[i] * dy) * b.k end
    elseif quiere == "zoom" then
        cam.dist = cam.dist * math.exp(dy * 0.005)
    elseif quiere == "herr" and arr then
        arr.tx, arr.ty = arr.tx + dx, arr.ty + dy
        herr_aplicar(arr, arr.tx, arr.ty, nil)
    end
    -- el cursor vuelve al punto donde empezo el arrastre: se puede arrastrar sin llegar al borde de la pantalla
    C.mtr_SetCursorPos(raton.ix, raton.iy)
    raton.x, raton.y = raton.ix, raton.iy
end

local function filas_pagina()
    local izq, der, pie = {}, {}, ""
    local pag = E.pag
    if pag == 1 then
        local n, cur = #modelos, E.cur[1]
        local ini = ventana(n, cur)
        for i = ini, math.min(n, ini + FILAS - 1) do
            local r = modelos[i]
            der[#der + 1] = string.format("%s%s %-24s %7.2f %7.2f %7.2f %s", (i == cur) and ">" or " ", marcados[r.dir] and "[x]" or "[ ]",
                string.sub(r.nombre, 1, 24), r.x, r.y, r.z, r.oculto and "oc" or "")
        end
        local r = modelos[cur]
        local nm = 0
        for _ in pairs(marcados) do nm = nm + 1 end
        izq[1] = string.format("OBJETO %d de %d (+%d de ayuda)  marcados %d", (n > 0) and cur or 0, n, #ayudas, nm)
        if r then
            izq[2] = r.nombre
            izq[3] = string.format("clase  %s", r.vt_ent and clase_corta(r.vt_ent) or "Model (del juego)")
            izq[4] = string.format("pos    %.3f  %.3f  %.3f", r.x, r.y, r.z)
            izq[5] = string.format("escala %.2f %.2f %.2f   huesos %d", r.e1, r.e2, r.e3, r.huesos)
            izq[6] = string.format("caja   %.2f x %.2f x %.2f m", r.caja[4] - r.caja[1], r.caja[5] - r.caja[2], r.caja[6] - r.caja[3])
            izq[7] = string.format("escena %d (%s)  0x%s", r.esc, escenas[r.esc] and string.sub(escenas[r.esc].hash, 9) or "?", hx(r.dir))
            izq[8] = r.movible and "se puede mover, rotar y escalar" or "del juego: solo ocultar / mostrar"
        else
            izq[2] = "no hay modelos en las escenas del motor"
        end
        izq[9] = string.format("HERRAMIENTA %s   EJE %s", HERR[E.herr], EJES[E.eje])
        izq[10] = "mouse izq. arrastra = " .. string.lower(HERR[E.herr]) .. " lo marcado"
        pie = "0/9 baja/sube | 8 marcar | 7 ocultar/mostrar | 4 o G R S herramienta | 3 o X Y Z eje | 2 enfocar camara"
    elseif pag == 2 then
        local n, cur = #luces, E.cur[2]
        local ini = ventana(n, cur)
        local prend = 0
        for i = 1, n do
            if luces[i].prendida then prend = prend + 1 end
        end
        for i = ini, math.min(n, ini + FILAS - 1) do
            local l = luces[i]
            der[#der + 1] = string.format("%s%s %-5s %-18s %6.2f %6.2f %6.2f %5.0f", (i == cur) and ">" or " ", l.prendida and "[ON ]" or "[off]",
                l.clase, string.sub(l.nombre, 1, 18), l.x, l.y, l.z, luz_fuerza(l))
        end
        local l = luces[cur]
        izq[1] = string.format("LUZ %d de %d   prendidas %d%s", (n > 0) and cur or 0, n, prend, E.solo and "   (SOLA)" or "")
        if l then
            izq[2] = l.nombre
            izq[3] = string.format("tipo   %s   %s", l.clase, l.prendida and "PRENDIDA" or "apagada")
            local sin_pos = (l.clase == "Sol" or l.clase == "Luz")
            izq[4] = string.format("%s    %.3f  %.3f  %.3f", sin_pos and "dir" or "pos", l.x, l.y, l.z)
            izq[5] = string.format("color  %.2f  %.2f  %.2f", l.color[1], l.color[2], l.color[3])
            izq[6] = sin_pos and string.format("sin alcance   sombra %s", l.sombra and "si" or "no")
                or string.format("alcance %.1f m   sombra %s", l.alcance, l.sombra and "si" or "no")
            izq[7] = (l.clase == "Foco") and string.format("cono   %.0f / %.0f grados", math.deg(l.umbra) * 2, math.deg(l.penumbra) * 2)
                or "cono   no tiene"
            izq[8] = string.format("escena %d   0x%s", l.esc, hx(l.dir))
        else
            izq[2] = "no hay luces en las escenas del motor"
        end
        pie = "0/9 baja/sube | 8 prender/apagar | 7 dejarla sola / volver | 4/3 menos/mas brillo | 2 enfocar camara"
    elseif pag == 3 then
        local n, cur = #ents.lista, E.cur[3]
        local ini = ventana(n, cur)
        for i = ini, math.min(n, ini + FILAS - 1) do
            local it = ents.lista[i]
            der[#der + 1] = string.format("%s %-20s %-33s", (i == cur) and ">" or " ", string.sub(it.clase, 1, 20), string.sub(it.nombre, 1, 33))
        end
        local it = ents.lista[cur]
        izq[1] = string.format("ENTIDAD %d de %d (archivo de escena)", (n > 0) and cur or 0, n)
        if it then
            izq[2] = it.nombre
            izq[3] = "clase  " .. (clase_de(it.vt) or it.clase)
            izq[4] = it.con_tf and string.format("pos    %.3f  %.3f  %.3f", it.x, it.y, it.z) or "sin posicion"
            izq[5] = string.format("%s0x%s", it.con_tf and ("flags  0x" .. hx(it.flags) .. "   ") or "", hx(it.dir))
            if it.clase == "SpotLight" and leer(it.dir, 0x230) then
                izq[6] = string.format("enable %d  sombra %d  lumen %.0f", B8[0x168], B8[0x229], fl(0x1e8 / 4))
                izq[7] = string.format("color  %.2f %.2f %.2f  temp %.0f", fl(0x1d0 / 4), fl(0x1d4 / 4), fl(0x1d8 / 4), fl(0x1e0 / 4))
                izq[8] = string.format("cono   %.0f / %.0f grados  alcance %.1f", fl(0x1f0 / 4), fl(0x1f4 / 4), fl(0x1c0 / 4))
            elseif it.clase == "StadiumModel" and leer(it.dir, 0x180) then
                izq[6] = string.format("dinamico %d  descarte %d", B8[0x151], B32[0x170 / 4])
                izq[7] = string.format("lod    %.1f / %.1f / %.1f", fl(0x158 / 4), fl(0x154 / 4), fl(0x15c / 4))
            end
        else
            izq[2] = "hace falta un objeto zfx_ en la escena"
            izq[3] = "(de su archivo se saca la lista)"
        end
        pie = "0/9 baja/sube | 8/7 salta 10 | 2 enfocar camara"
    elseif pag == 4 then
        local n, cur = #OPC, E.cur[4]
        local ini = ventana(n, cur)
        for i = ini, math.min(n, ini + FILAS - 1) do
            local o = OPC[i]
            der[#der + 1] = string.format("%s %-30s %s", (i == cur) and ">" or " ", o[1], o[2]())
        end
        izq[1] = "OPCIONES"
        izq[2] = string.format("camara %s", cam.activa and "LIBRE (mia)" or "del juego")
        if cam.activa then
            izq[3] = string.format("mira a %.2f  %.2f  %.2f", cam.pivote[1], cam.pivote[2], cam.pivote[3])
            izq[4] = string.format("dist %.2f m  lente %.1f grados", cam.dist, math.deg(cam.fov))
        end
        izq[6] = string.format("lecturas %d  escrituras %d", E.n_lect, E.n_escr)
        izq[7] = string.format("llamadas %d  fallas %d", E.n_llam, E.n_falla)
        izq[8] = "giro: " .. string.sub(E.conj_visto, 1, 38)
        izq[9] = E.solo_lectura and "SOLO LECTURA (archivo solo_lectura.txt)" or (E.sin_llamadas and "sin llamadas al juego (archivo)" or "")
        pie = "0/9 baja/sube | 8 usar o cambiar | 4/3 menos/mas"
    elseif pag == 6 then
        local et, d = RX.etapas, RX.dll
        local n, cur = 4 + #et, E.cur[6]
        if cur > n then cur = n end
        local ini = ventana(n, cur)
        local v, a, c = 0, 0, 0
        if d.u32 then v, a, c = RX.dll_valores() end
        for i = ini, math.min(n, ini + FILAS - 1) do
            local s
            if i == 1 then s = string.format("    %-18s %s", "Vista", d.u32 and (RX.VISTA[v] or "?") or "-")
            elseif i == 2 then s = string.format("    %-18s %s", "Alambre", d.u32 and ((a > 0.5) and "SI" or "no") or "-")
            elseif i == 3 then s = string.format("    %-18s %s", "Pantalla partida", d.u32 and ((c > 0.005) and string.format("%.0f %%", c * 100) or "no") or "-")
            elseif i == 4 then s = string.format("    %-18s %s", "Modo motor (exp.)", RX.modo and (RX.MODO_NOM[RX.modo] or tostring(RX.modo)) or "-")
            else
                local e = et[i - 4]
                s = string.format("%s %s%s", e.propio and "[=]" or (e.activa and "[x]" or "[ ]"), string.rep("  ", e.nivel), e.nombre)
            end
            der[#der + 1] = ((i == cur) and ">" or " ") .. s
        end
        izq[1] = string.format("RENDER  %d etapas, %d apagadas", #et, RX.apagadas)
        if cur <= 3 then
            izq[2] = (cur == 1) and "VISTA DE LA IMAGEN" or (cur == 2) and "ALAMBRE (mallas en lineas)" or "PANTALLA PARTIDA"
            izq[3] = d.u32 and (d.viva and "la dibuja la DLL grafica: enlazada" or "DLL grafica enlazada (sin cuadros nuevos)")
                or string.sub(d.estado, 1, A_IZQ)
            if d.u32 then
                if cur == 1 then
                    izq[4] = string.format("%d de 10: %s", v, RX.VISTA[v] or "?")
                    izq[5] = string.sub(RX.QUE[v] or "", 1, A_IZQ)
                    izq[7] = "vale en el Editar y en el partido"
                elseif cur == 2 then
                    izq[4] = string.format("%s   dibujos en alambre: %d", (a > 0.5) and "PRENDIDO" or "apagado", d.u32[10])
                    izq[5] = "jugador, estadio y pasto en lineas;"
                    izq[6] = "la luz y los menus quedan como estan"
                else
                    izq[4] = (c > 0.005) and string.format("la vista a la izquierda del %.0f %%", c * 100) or "la vista ocupa toda la pantalla"
                    izq[5] = "a la derecha queda la imagen normal"
                end
            else
                izq[4] = "(las vistas las trae la DLL grafica nueva)"
                izq[5] = "sin esa DLL sirven igual las etapas"
                izq[6] = "del render de mas abajo"
            end
            if RX.mal then izq[8] = string.sub(RX.mal, 1, A_IZQ) end
            izq[9] = "con el shader mod en \"Ver original\""
            izq[10] = "las vistas no se ven (el alambre si)"
            pie = (cur == 1) and "0/9 baja/sube | 8 o 3 vista siguiente | 4 anterior | 7 imagen normal"
                or (cur == 2) and "0/9 baja/sube | 8 prender/apagar | 7 apagar"
                or "0/9 baja/sube | 8 mitad / nada | 4/3 corre el corte | 7 sin corte"
        elseif cur == 4 then
            izq[2] = "MODO DEL MOTOR (EXPERIMENTAL, sin DLL)"
            izq[3] = string.format("ahora: %s", RX.modo and (RX.MODO_NOM[RX.modo] or tostring(RX.modo)) or "no encuentro el render")
            izq[4] = "es el interruptor propio del Fox Engine"
            izq[5] = "al juego de venta le faltan las piezas"
            izq[6] = "de los otros modos: lo esperado es"
            izq[7] = "pantalla negra y sin menus"
            izq[8] = "vuelve solo a los 8 s CON ESTE PANEL ABIERTO"
            if RX.exp.hasta then izq[9] = string.format("VUELVE AL NORMAL EN %d s", math.max(0, math.floor((RX.exp.hasta - reloj()) / 1000 + 0.999))) end
            if RX.mal then izq[10] = string.sub(RX.mal, 1, A_IZQ) end
            pie = "0/9 baja/sube | 8 modo siguiente (vuelve solo a los 8 s) | 7 volver al normal ya"
        else
            local e = et[cur - 4]
            if e then
                izq[2] = string.sub(e.nombre, 1, A_IZQ)
                izq[3] = e.propio and "INTERRUPTOR PROPIO del motor: no se toca"
                    or string.format("%s%s", e.activa and "PRENDIDA" or "apagada", G.et_orig[e.dir] and "   (cambiada por mi)" or "")
                izq[4] = e.huerfana and ("padre inexistente: " .. string.sub(e.padre, 1, 24)) or ("dentro de  " .. (e.padre or "(suelta)"))
                izq[5] = string.format("lista  %s   prioridad %d", RX.LISTA[e.modo] or "?", e.prio)
                izq[6] = string.format("tipo %d  indice %d  0x%s", e.tipo, e.idx, hx(e.dir))
                izq[7] = string.format("clase  %s", clase_corta(e.vt))
            end
            izq[9] = RX.mal and string.sub(RX.mal, 1, A_IZQ) or string.format("modo del motor: %s", RX.LISTA[RX.modo or -1] or "?")
            izq[10] = "apagar DRAW2D esconde los menus del juego"
            pie = "0/9 baja/sube | 8 prender/apagar la etapa | 7 dejarla como la tenia el juego"
        end
    else
        local u = reg.ult
        for i = math.max(1, #u - FILAS + 1), #u do der[#der + 1] = u[i] end
        izq[1] = "REGISTRO (lo ultimo)"
        izq[2] = "archivo completo:"
        izq[3] = "content\\motor-tiempo-real\\registro.txt"
        izq[4] = reg.falla and "NO pude escribir el archivo" or ""
        pie = "8 anotar TODO (escenas, modelos, luces, entidades, camara)"
        -- esta pagina muestra renglones largos: van enteros, sin columna izquierda
        local t = {}
        for i = 1, #der do t[i] = string.sub(der[i], 1, A_IZQ + A_DER + 3) end
        return nil, t, pie
    end
    return izq, der, pie
end

local function cabecera()
    local t = {}
    for i = 1, #PAGS do t[i] = (i == E.pag) and ("[" .. PAGS[i] .. "]") or string.lower(PAGS[i]) end
    return string.format("MotorTiempoReal %s | %s | 6/5 pagina | 1 = otro modulo", VERSION, table.concat(t, " "))
end

---------------------------------------------------------------------------------------------
-- 12. EVENTOS DE SIDER
---------------------------------------------------------------------------------------------
local function puede()
    -- los tres archivos de apagado se vuelven a mirar cada 60 llamadas (un segundo con el panel abierto): se pueden crear
    -- o borrar con el juego abierto. Va por cuenta de llamadas y no por reloj porque apagado no hay reloj.
    E.n_puede = E.n_puede + 1
    if E.n_puede % 60 == 0 then
        local a, s, l = existe(DIR .. "apagado.txt"), existe(DIR .. "solo_lectura.txt"), existe(DIR .. "sin_llamadas.txt")
        if a ~= E.apagado or s ~= E.solo_lectura or l ~= E.sin_llamadas then
            E.apagado, E.solo_lectura, E.sin_llamadas = a, s, l
            log("MotorTiempoReal: archivos de apagado ahora: " .. (a and "APAGADO " or "") .. (s and "SOLO LECTURA " or "")
                .. (l and "sin llamadas al juego" or "") .. ((a or s or l) and "" or "ninguno"))
        end
    end
    if E.apagado then return false, "apagado por content\\motor-tiempo-real\\apagado.txt" end
    if not exe_conocido() then return false, E.exe_motivo or "no pude preparar el modulo" end
    return true
end

function m.key_down(ctx, vkey)
    local ok = puede()
    if not ok then return end
    if not E.hilo_tecla then E.hilo_tecla = C.mtr_GetCurrentThreadId() end
    local n0 = reg.n
    tecla(vkey)
    if E.solo_lectura and reg.n > n0 then R("(SOLO LECTURA: lo anterior no se escribio en el juego)", true) end
    volcar_registro()
end

function m.overlay_on(ctx)
    local ok, por_que = puede()
    if not ok then
        if C and RX.exp.hasta then RX.exp_pulso(true) end      -- lo unico que se hace apagado: sacar el modo experimental
        return string.format("MotorTiempoReal %s | (tecla 1 = otro modulo)\n%s", VERSION, por_que)
    end
    E.cuadros = E.cuadros + 1
    if not E.hilo_cuadro then E.hilo_cuadro = C.mtr_GetCurrentThreadId() end
    local ahora = reloj()
    local viejo = ahora - E.t_expl
    -- con muchisimos objetos (un partido) se explora mas espaciado
    if not raton.modo and (E.sucio or viejo > ((E.n_obj > 1500) and 2000 or 500) or viejo < 0) then
        explorar()
        if E.pag == 6 then
            RX.leer()
            RX.dll_abrir()
        end
        if E.pag == 3 and (ahora - ents.t > 2000 or ahora < ents.t) then listar_entidades() end
        if E.marcadores then marcadores_poner() end
        if cam.activa and cam_buscar() ~= cam.dir then cam_soltar("el visor ya no usa esa camara") end
        if not E.volcado and #ayudas + #modelos > 0 then
            for i = 1, #modelos do
                if modelos[i].nuestro then E.volcado = true end
            end
            if #ayudas > 0 then E.volcado = true end
            if E.volcado then volcar_todo("primera vez que veo objetos del motor") end
        end
    end
    if RX.eje_abajo and C.mtr_GetAsyncKeyState(RX.eje_abajo) >= 0 then RX.eje_abajo = nil end     -- solto la letra del eje
    entrada()
    cam_escribir()
    RX.dll_pulso()
    RX.exp_pulso()
    volcar_registro()
    local t = { cabecera() }
    if E.compacto then
        t[2] = string.format("%d modelos, %d luces | herramienta %s eje %s | camara %s | panel compacto (ver OPCIONES)",
            #modelos, #luces, HERR[E.herr], EJES[E.eje], cam.activa and "libre" or "del juego")
        local o = (E.pag == 4) and OPC[E.cur[4]] or nil
        if o then t[3] = string.format("> %s %s", o[1], o[2]()) end      -- en OPCIONES se ve sobre que opcion esta el cursor
    else
        local izq, der, pie = filas_pagina()
        for i = 1, FILAS do
            if izq then
                t[#t + 1] = col(izq[i] or "", A_IZQ) .. " | " .. string.sub(der[i] or "", 1, A_DER)
            else
                t[#t + 1] = der[i] or ""
            end
        end
        t[#t + 1] = pie
        t[#t + 1] = "mouse: derecho gira | medio corre la vista | izq+der acerca/aleja | izquierdo usa la herramienta (en ESCENA)"
    end
    if not E.listas_ok and E.listas_mal >= 6 then
        t[#t + 1] = "* las listas del motor no coinciden con lo que conozco: modelos y luces en SOLO LECTURA (ver registro.txt)"
    elseif E.aviso ~= "" then t[#t + 1] = "* " .. E.aviso end
    for i = 1, #t do                        -- un valor basura (memoria que cambio) no puede ensanchar el panel
        if #t[i] > 112 then t[i] = string.sub(t[i], 1, 112) end
    end
    return table.concat(t, "\n")
end

function m.init(ctx)
    DIR = ctx.sider_dir .. "content\\motor-tiempo-real\\"
    reg.ruta = DIR .. "registro.txt"
    activo, motivo = estado()
    E.apagado = existe(DIR .. "apagado.txt")
    E.solo_lectura = existe(DIR .. "solo_lectura.txt")
    E.sin_llamadas = existe(DIR .. "sin_llamadas.txt")
    -- si Sider recarga el modulo con el juego abierto, sigo sabiendo como estaba todo antes de tocarlo
    local g = ctx.MotorTiempoReal2
    if type(g) == "table" and type(g.luz_orig) == "table" and type(g.tf_orig) == "table" and type(g.oc_orig) == "table" then
        G = g
        if type(G.et_orig) ~= "table" then G.et_orig = {} end       -- lo guardo la 0.2.1, que no conocia las etapas
    else
        ctx.MotorTiempoReal2 = G
    end
    ctx.register("livecpk_make_key", m.make_key)
    ctx.register("livecpk_get_filepath", m.get_filepath)
    ctx.register("key_down", m.key_down)
    ctx.register("overlay_on", m.overlay_on)
    R("---- MotorTiempoReal " .. VERSION .. " iniciado ----")
    log("MotorTiempoReal " .. VERSION .. " listo: " .. (activo and "ENTREGANDO" or "sin entregar") .. " (" .. motivo .. "); carpeta " .. DIR
        .. (E.apagado and "; APAGADO" or "") .. (E.solo_lectura and "; SOLO LECTURA" or "") .. (E.sin_llamadas and "; sin llamadas al juego" or "")
        .. "; ffi " .. ((type(ffi) == "table") and "si" or "NO"))
end

return m
