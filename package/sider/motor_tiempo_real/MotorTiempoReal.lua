-- MotorTiempoReal.lua 0.2 (PES21 FX, 10/11/2026) - modulo de Sider.
-- "Motor en tiempo real" del Editar: un editor en vivo de lo que dibuja el Fox Engine. Todo dentro del juego y todo
-- por Sider (sin DLL): lee la lista de escenas graficas del motor y muestra, con nombre, los modelos y las luces;
-- deja marcar uno o muchos objetos, moverlos, rotarlos, escalarlos, ocultarlos; prender, apagar y dar brillo a cada
-- foco; mover la camara del visor como en Blender (orbitar, paneo, acercar, vistas, lente larga = ortografica);
-- mostrar la grilla y un marcador en cada foco; y anota todo lo que lee y escribe en
--   content\motor-tiempo-real\registro.txt
--
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
-- se escribe dentro del exe. Solo se llaman dos funciones del juego (color de una luz y matriz de un modelo),
-- las dos con su comienzo comprobado byte a byte.
--   content\motor-tiempo-real\apagado.txt       = el modulo no entrega la escena ni toca nada
--   content\motor-tiempo-real\solo_lectura.txt  = lista todo pero no escribe nada
--   content\motor-tiempo-real\sin_llamadas.txt  = escribe datos pero no llama funciones del juego
--
-- Offsets leidos en el exe decompilado (PES2021.exe 1.07.02): ver memoria del proyecto motor-tiempo-real-exe.

local m = { version = "0.2" }
local VERSION = m.version
local DIR
-- lo que hay que recordar aunque Sider recargue el modulo con el juego abierto (queda en ctx)
local G = { luz_orig = {}, tf_orig = {}, oc_orig = {}, cam = false }

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
local BASE, EXE_FIN = 0x140000000, 0x150000000
local DIR_MIN, DIR_MAX = 0x10000, 0x7ffffffe0000

-- estado general del editor (una sola tabla: Lua admite pocas variables sueltas por funcion)
local E = {
    listo = nil, sin_ffi = nil, apagado = false, solo_lectura = false, sin_llamadas = false,
    n_lect = 0, n_escr = 0, n_llam = 0, n_falla = 0, mi_pid = 0,
    pag = 1, aviso = "", compacto = false, mouse = true, cuadros = 0, t_expl = -100000, sucio = true,
    cur = { 1, 1, 1, 1, 1 }, herr = 1, eje = 1, grilla = false, marcadores = false, solo = nil,
    conj = false, conj_visto = "sin verificar", volcado = false, hilo_cuadro = nil, hilo_tecla = nil,
    exe_ok = nil, exe_motivo = nil,
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
    R(string.format("exe reconocido: sello 0x%s (el del 1.07.02 es 0x62787c6a), %d tablas de funciones iguales",
        sello and hx(sello) or "?", #RANURAS), true)
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
    r.oculto = (r.b1b4 % 2) == 1
    r.esc_media = fl(0x1c4 / 4)
    r.lod_cerca, r.lod_lejos, r.lod_poli, r.descarte = fl(0x1cc / 4), fl(0x1d0 / 4), fl(0x1e0 / 4), B8[0x1f1]
    r.e1, r.e2, r.e3 = fl(0x40 / 4), fl(0x54 / 4), fl(0x68 / 4)   -- diagonal de la matriz (escala si no esta girado)
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

local function leer_luz(o, r)
    if not leer(o, 0xb0) then return false end
    local p, nom = P(0x38), P(0x50)
    r.alc_int, r.alcance = fl(0x80 / 4), fl(0x84 / 4)
    r.q = { fl(0x90 / 4), fl(0x94 / 4), fl(0x98 / 4), fl(0x9c / 4) }
    r.umbra, r.penumbra = fl(0xa0 / 4), fl(0xa4 / 4)
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

local function explorar()
    N.expl = N.expl + 1
    local id = N.expl
    local nesc, nm, nl, na, no = {}, {}, {}, {}, {}
    local primera = rptr(G_ESCENAS)
    local e, ne = primera, 0
    while e and ne < 64 do
        ne = ne + 1
        if not leer(e, 0x50) then break end
        local sig, rec, primero, cuenta = P(0), P(0x20), P(0x38), B32[0x48 / 4]
        local hash = "?"
        if rec and leer(rec + 0x10, 8) then hash = string.format("%08x%08x", B32[1], B32[0]) end
        local esc = { dir = e, hash = hash, cuenta = cuenta, n = 0, n_mod = 0, n_luz = 0 }
        nesc[ne] = esc
        local o, k = primero, 0
        while o and k < 4096 do
            k = k + 1
            if not leer(o, 0x38) then break end
            local vt, sgte = P(0), P(0x18)
            local r = por_dir[o]
            if not r or r.vt ~= vt then
                r = { dir = o, vt = vt }
                por_dir[o] = r
                if vt == VT_MODELO then r.tipo = "modelo"
                elseif vt and VT_LUZ[vt] then r.tipo, r.clase = "luz", VT_LUZ[vt]
                else r.tipo, r.clase = "otro", clase_corta(vt) end
            end
            r.visto, r.esc = id, ne
            esc.n = esc.n + 1
            if r.tipo == "modelo" then
                if leer_modelo(o, r) then
                    if not r.resuelto or r.fun ~= r.fun_res or r.n_fun ~= r.nfun_res then resolver_modelo(o, r) end
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
            if sgte == primero then break end
            o = sgte
        end
        if sig == primera then break end
        e = sig
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
-- rapido: misma tabla de funciones, marcado "en escena" y apuntando a la escena donde lo encontre
local function en_escena(r, vt)
    local e = escenas[r.esc]
    if not e or not leer(r.dir, 0x30) then return false end
    return P(0) == vt and (math.floor(B32[0x24 / 4] / 2) % 2) == 1 and P(0x28) == e.dir
end
-- a fondo (antes de llamar una funcion del juego): ademas tiene que estar en la lista de la escena
local function viva(r, vt)
    if not en_escena(r, vt) then return false end
    local primero = rptr(escenas[r.esc].dir + 0x38)
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
    r.reposo = false
    local n = r.huesos
    if n < 1 or n > 64 then return false end
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
    if n < 1 or n > 64 or rptr(r.dir + 0xe0) ~= r.dir_huesos or not leer(r.dir + 0x40, 0x40) then return false end
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

local function ocultar(r, oc)
    if not en_escena(r, VT_MODELO) then return false end
    local v = ru32(r.dir + 0x1b4)
    if not v then return false end
    if G.oc_orig[r.dir] == nil or G.oc_orig[r.dir].nombre ~= r.nombre then G.oc_orig[r.dir] = { nombre = r.nombre, oculto = (v % 2) == 1 } end
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
    poner_u32(r.dir + 0x1ac, oc and 0xffffffff or 0)
    if oc and v % 2 == 0 then v = v + 1 elseif not oc and v % 2 == 1 then v = v - 1 end
    local ok = poner_u32(r.dir + 0x1b4, v)
    if ok then r.oculto = oc end
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
    R(string.format("camara del visor 0x%s (nodo 0x%s clase %s, visor 0x%s): pos %.3f %.3f %.3f objetivo %.3f %.3f %.3f cerca %.3f lejos %.1f proyeccion %d campo %.4f aspecto %.3f",
        hx(c), hx(nodo), clase_corta(rptr(nodo)), hx(impl), px, py, pz, tx, ty, tz, cerca, lejos, proy, fov, asp), true)
    if not (D > 0.01 and D < 100000) or not (fov > 0.001 and fov < 3.2) or proy ~= 0 then
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
    luz_recordar(r)
    for i = 1, 4 do
        if not finito(c[i]) then return false end
        WF[i - 1] = c[i]
    end
    c_color(r.dir, WF)
    E.n_llam = E.n_llam + 1
    r.color = { c[1], c[2], c[3], c[4] }
    return true
end
local function luz_fuerza(r) return math.max(r.color[1], r.color[2], r.color[3]) end

-- marcadores: un objeto zfx_Luz_NN por foco, puesto un poco por detras del foco (hacia afuera de la escena)
-- para que no le tape su propia luz ni su sombra
local function marcadores_poner()
    local k = 0
    for i = 1, #luces do
        local l = luces[i]
        if l.clase ~= "Sol" and finito(l.x) and finito(l.y) and finito(l.z) then
            local mo
            for j = 1, #ayudas do
                if ayudas[j].ayuda == "luz" and not ayudas[j].usado then mo = ayudas[j] break end
            end
            if not mo then break end
            k = k + 1
            mo.usado = true
            local afuera = norm3(l.x, l.y - 1, l.z) or { 0, 1, 0 }
            local x, y, z = l.x + afuera[1] * 0.18, l.y + afuera[2] * 0.18, l.z + afuera[3] * 0.18
            if mo.movible and (math.abs(mo.x - x) > 0.002 or math.abs(mo.y - y) > 0.002 or math.abs(mo.z - z) > 0.002) then
                local S, Q, T = leer_tf(mo)
                if S then
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
local function volcar_todo(por_que)
    R(string.format("===== VOLCADO (%s) version %s: %d escenas, %d modelos + %d de ayuda, %d luces, %d otros; lecturas %d escrituras %d llamadas %d fallas %d; giro %s",
        por_que, VERSION, #escenas, #modelos, #ayudas, #luces, #otros, E.n_lect, E.n_escr, E.n_llam, E.n_falla, E.conj_visto), true)
    R(string.format("hilos: cuadro %s, tecla %s, proceso %d", tostring(E.hilo_cuadro), tostring(E.hilo_tecla), E.mi_pid))
    for i = 1, #escenas do
        local e = escenas[i]
        R(string.format("escena %d: 0x%s nombre(hash) %s, cuenta %d, recorridos %d (%d modelos, %d luces)", i, hx(e.dir), e.hash, e.cuenta, e.n, e.n_mod, e.n_luz))
    end
    for i = 1, #modelos + #ayudas do
        local r = (i <= #modelos) and modelos[i] or ayudas[i - #modelos]
        R(string.format("modelo E%d 0x%s %-28s pos %.3f %.3f %.3f diag %.3f %.3f %.3f caja %.2f %.2f %.2f / %.2f %.2f %.2f huesos %d limite %s copia3 %s oculto %s (1ac %s 1b0 %s 1b4 %s) lod %.1f %.1f %.1f descarte %d cuerpo %s entidad %s (%s) te %s padre %s%s",
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
    R("===== fin del volcado")
end

---------------------------------------------------------------------------------------------
-- 11. PAGINAS: que muestra cada una y que hace cada tecla
---------------------------------------------------------------------------------------------
local PAGS = { "ESCENA", "LUCES", "ENTIDADES", "OPCIONES", "REGISTRO" }
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
    local nl, no, nt = 0, 0, 0
    for i = 1, #luces do
        local l, g = luces[i], G.luz_orig[luces[i].dir]
        if g and g.nombre == l.nombre then
            luz_prender(l, g.prendida)
            if c_color and (l.color[1] ~= g.color[1] or l.color[2] ~= g.color[2] or l.color[3] ~= g.color[3]) then luz_color(l, g.color) end
            nl = nl + 1
        end
    end
    for i = 1, #modelos + #ayudas do
        local r = (i <= #modelos) and modelos[i] or ayudas[i - #modelos]
        local g = r.ent and G.tf_orig[r.ent]
        if g and g.nombre == r.nombre and aplicar_tf(r, g.S, g.Q, g.T) then
            avisar_motor(r)
            nt = nt + 1
        end
        local go = G.oc_orig[r.dir]
        if go and go.nombre == r.nombre and r.oculto ~= go.oculto then
            ocultar(r, go.oculto)
            no = no + 1
        end
    end
    G.luz_orig, G.tf_orig, G.oc_orig = {}, {}, {}
    E.solo, E.grilla, E.marcadores = nil, false, false
    if G.cam then cam_restaurar() end
    E.sucio = true
    E.aviso = string.format("todo como estaba: %d luces, %d objetos en su lugar, %d visibles / ocultos", nl, nt, no)
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
}

local function tecla(vkey)
    local pag = E.pag
    E.aviso = ""
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
                local oc = not sel[1].oculto
                for i = 1, #sel do ocultar(sel[i], oc) end
                R(string.format("%s %d objeto(s): %s%s", oc and "oculto" or "muestro", #sel, sel[1].nombre, (#sel > 1) and " y mas" or ""), true)
                E.sucio = true
            end
        elseif vkey == 0x34 then E.herr = E.herr % #HERR + 1
        elseif vkey == 0x33 then E.eje = E.eje % #EJES + 1
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
            if luz_prender(l, si) then R(string.format("luz %s: %s", l.nombre, si and "PRENDIDA" or "apagada"), true) end
        elseif vkey == 0x37 and l then
            if E.solo then
                for i = 1, #luces do
                    local x = luces[i]
                    if E.solo[x.dir] ~= nil then luz_prender(x, E.solo[x.dir]) end
                end
                E.solo = nil
                R("luces: vuelven las que estaban prendidas", true)
            else
                E.solo = {}
                for i = 1, #luces do
                    local x = luces[i]
                    E.solo[x.dir] = x.prendida
                    luz_prender(x, x == l)
                end
                R("luces: queda sola " .. l.nombre, true)
            end
        elseif (vkey == 0x34 or vkey == 0x33) and l then
            local f = (vkey == 0x33) and 1.25 or 0.8
            local c = l.color
            if luz_fuerza(l) < 0.0001 and f > 1 then c = { 1, 1, 1, c[4] } end   -- estaba en negro: arranca en blanco
            if luz_color(l, { c[1] * f, c[2] * f, c[3] * f, c[4] }) then
                R(string.format("luz %s: color %.3f %.3f %.3f", l.nombre, l.color[1], l.color[2], l.color[3]), true)
            end
        elseif vkey == 0x32 and l then
            if cam_enfocar(l.x, l.y, l.z, nil) then R("camara: enfoco la luz " .. l.nombre) end
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
    end
end

-- mouse: se mira en cada cuadro (el juego no usa el mouse). Solo con la ventana del juego al frente.
local raton = { x = 0, y = 0, modo = nil, ix = 0, iy = 0 }
local function arrastre_fin(modo)
    if modo == "herr" and arr then
        herr_fin(arr)
        arr = nil
    end
end
local function entrada()
    if not E.mouse then
        if raton.modo then arrastre_fin(raton.modo) raton.modo = nil end
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
    local quiere = nil
    if izq and der then quiere = "zoom" elseif der then quiere = "orbita" elseif med then quiere = "paneo"
    elseif izq and E.pag == 1 then quiere = "herr" end
    if quiere ~= raton.modo then
        if raton.modo then arrastre_fin(raton.modo) end
        raton.modo, raton.ix, raton.iy = quiere, x, y
        if quiere == "herr" then
            arr = herr_inicio()
            if not arr then raton.modo = "nada" end
        elseif quiere and not cam.activa and not cam_activar() then
            raton.modo = "nada"
        end
        if raton.modo == "nada" and not quiere then raton.modo = nil end
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
        pie = "0/9 baja/sube | 8 marcar | 7 ocultar/mostrar | 4 herramienta | 3 eje | 2 enfocar camara"
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
            izq[4] = string.format("pos    %.3f  %.3f  %.3f", l.x, l.y, l.z)
            izq[5] = string.format("color  %.2f  %.2f  %.2f", l.color[1], l.color[2], l.color[3])
            izq[6] = string.format("alcance %.1f m   sombra %s", l.alcance, l.sombra and "si" or "no")
            izq[7] = string.format("cono   %.0f / %.0f grados", math.deg(l.umbra) * 2, math.deg(l.penumbra) * 2)
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
    if E.apagado then return false, "apagado por content\\motor-tiempo-real\\apagado.txt" end
    if not exe_conocido() then return false, E.exe_motivo or "no pude preparar el modulo" end
    return true
end

function m.key_down(ctx, vkey)
    local ok = puede()
    if not ok then return end
    if not E.hilo_tecla then E.hilo_tecla = C.mtr_GetCurrentThreadId() end
    tecla(vkey)
    volcar_registro()
end

function m.overlay_on(ctx)
    local ok, por_que = puede()
    if not ok then
        return string.format("MotorTiempoReal %s | (tecla 1 = otro modulo)\n%s", VERSION, por_que)
    end
    E.cuadros = E.cuadros + 1
    if not E.hilo_cuadro then E.hilo_cuadro = C.mtr_GetCurrentThreadId() end
    local ahora = reloj()
    local viejo = ahora - E.t_expl
    if not raton.modo and (E.sucio or viejo > 500 or viejo < 0) then
        explorar()
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
    entrada()
    cam_escribir()
    volcar_registro()
    local t = { cabecera() }
    if E.compacto then
        t[2] = string.format("%d modelos, %d luces | herramienta %s eje %s | camara %s | panel compacto (se cambia en OPCIONES)",
            #modelos, #luces, HERR[E.herr], EJES[E.eje], cam.activa and "libre" or "del juego")
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
    if E.aviso ~= "" then t[#t + 1] = "* " .. E.aviso end
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
