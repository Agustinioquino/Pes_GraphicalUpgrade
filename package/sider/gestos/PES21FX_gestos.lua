-- PES21FX_gestos v0.1 - PES 2021 1.07.02 / Sider 7  (PES21 FX, 12/11/2026)
-- Expresiones 2, prueba 1: le entrega al juego TABLAS DE GESTOS de cara nuevas, sin tocar ningun archivo
-- del juego ni de livecpk\root. Todo por Sider: no usa la d3d11.dll.
--
-- Como lo hace: el juego decide la cara de cada animacion de cuerpo con las tablas
-- common\anime\FHSequence\bin\Facebase.bin (clip de cara por tramo) y Faceadd.bin (gestos sumados:
-- cejas, boca, ojos). Cada vez que carga un partido las vuelve a pedir, y este modulo le entrega las de
--     content\PES21FX-gestos\tablas\
-- Con el modulo apagado (tecla 0) el juego vuelve a leer las de siempre. El cambio entra al cargar el
-- PROXIMO partido.
--
-- Teclas (overlay de Sider abierto y este modulo elegido; la tecla 1 pasa a otro modulo):
--   0 = tablas nuevas SI / NO (entra en el proximo partido)
--   9 = anotar en sider.log las tablas de emociones del juego (solo lee memoria; hacerlo durante un partido)
--
-- Que toca en el juego: nada. Solo contesta cuando el juego pide esos dos archivos, y con la tecla 9 LEE
-- cinco tablas fijas del exe (antes comprueba que sea el 1.07.02; si no, no lee nada).

local m = { version = "0.1" }

local pcall = pcall or function(f, ...) return true, f(...) end

local PREFIJO = "PES21FX-gestos|"
local TABLAS = { ["facebase.bin"] = "Facebase.bin", ["faceadd.bin"] = "Faceadd.bin" }
local ORDEN = { "facebase.bin", "faceadd.bin" }

-- direcciones de PES2021.exe 1.07.02 (base 0x140000000), leidas del codigo decompilado (FUN_141ec25f0, FUN_141ee0870)
local FN_POSE   = 0x141f061a0   -- solo para comprobar la version del exe
local EMO_FILA  = 0x142b73fd0   -- 17 filas de 4 bytes; el segundo par de bytes es la emocion (0..7) de la fila
local EMO_CLIP  = 0x14352c678   -- 17 numeros de 2 bytes: clip base de cada fila
local EMO_DUR   = 0x143803010   -- 17 x 4 float: duraciones de cada fila (se llenan con el partido cargado)
local ADD_MASK  = 0x143535440   -- 15 numeros de 4 bytes: zona de cada gesto sumado
local MASK_NOM  = 0x143535480   -- punteros al nombre de cada zona

local function de_hex(s) return (string.gsub(s, "%x%x", function(h) return string.char(tonumber(h, 16)) end)) end
local FIRMA_POSE = de_hex("4883ec38899100010000488b49784885c9750732c04883c438c3")

local DIR = ""
local G = { activo = true, pedidos = {}, entregados = {}, cambios = 0 }
local hay = {}
local aviso = ""

local function existe(path)
    local f = io.open(path, "rb")
    if f then
        f:close()
        return true
    end
    return false
end

local function leer(addr, n)
    if type(memory) ~= "table" or type(memory.read) ~= "function" then return nil end
    local s = memory.read(addr, n)
    if type(s) == "string" and #s == n then return s end
    return nil
end

local function numero(s, desde, n)
    local v = 0
    for i = desde + n - 1, desde, -1 do v = v * 256 + string.byte(s, i) end
    return v
end

local function con_signo(v, bits)
    local tope = 2 ^ bits
    if v >= tope / 2 then return v - tope end
    return v
end

local function flotante(s, desde)
    local v = numero(s, desde, 4)
    if v == 0 then return 0 end
    local signo = (v >= 0x80000000) and -1 or 1
    if signo < 0 then v = v - 0x80000000 end
    local e = math.floor(v / 0x800000)
    local f = v - e * 0x800000
    if e == 255 then return 0 / 0 end
    if e == 0 then return signo * f * 2 ^ -149 end
    return signo * (1 + f / 0x800000) * 2 ^ (e - 127)
end

-------------------------------------------------------------------------------------------
-- archivos que pide el juego
-------------------------------------------------------------------------------------------
local function cual(filename)
    local f = string.lower((string.gsub(filename, "/", "\\")))
    local b = string.match(f, "fhsequence\\bin\\([^\\]+)$")
    if b and TABLAS[b] then return b end
end

function m.make_key(ctx, filename)
    local b = cual(filename)
    if not b then return end
    G.pedidos[b] = (G.pedidos[b] or 0) + 1
    if G.activo and hay[b] then
        return PREFIJO .. b
    end
end

function m.get_filepath(ctx, filename, key)
    if type(key) ~= "string" or string.sub(key, 1, #PREFIJO) ~= PREFIJO then return end
    local b = string.sub(key, #PREFIJO + 1)
    if not (TABLAS[b] and hay[b]) then return end
    G.entregados[b] = (G.entregados[b] or 0) + 1
    if G.entregados[b] <= 3 then
        log("PES21FX_gestos: el juego pide " .. filename .. " -> entrego " .. DIR .. TABLAS[b])
    end
    return DIR .. TABLAS[b]
end

-------------------------------------------------------------------------------------------
-- tecla 9: leer las tablas de emociones del juego (solo lectura)
-------------------------------------------------------------------------------------------
local function volcar()
    if leer(FN_POSE, #FIRMA_POSE) ~= FIRMA_POSE then
        aviso = "este exe no es el 1.07.02 que conozco: no leo nada"
        log("PES21FX_gestos: " .. aviso)
        return
    end
    local filas, clips, dur = leer(EMO_FILA, 17 * 4), leer(EMO_CLIP, 17 * 2), leer(EMO_DUR, 17 * 16)
    if not (filas and clips and dur) then
        aviso = "no pude leer las tablas de emociones"
        log("PES21FX_gestos: " .. aviso)
        return
    end
    log("PES21FX_gestos: tabla de emociones (fila: bytes crudos | emocion | clip base | 4 duraciones)")
    for i = 0, 16 do
        local crudo = string.format("%02X %02X %02X %02X", string.byte(filas, i * 4 + 1), string.byte(filas, i * 4 + 2),
            string.byte(filas, i * 4 + 3), string.byte(filas, i * 4 + 4))
        log(string.format("PES21FX_gestos:   fila %2d: %s | emocion %d | clip %d | %.3f %.3f %.3f %.3f", i, crudo,
            con_signo(numero(filas, i * 4 + 3, 2), 16), con_signo(numero(clips, i * 2 + 1, 2), 16),
            flotante(dur, i * 16 + 1), flotante(dur, i * 16 + 5), flotante(dur, i * 16 + 9), flotante(dur, i * 16 + 13)))
    end
    local zonas = leer(ADD_MASK, 15 * 4)
    if zonas then
        local t = {}
        for i = 0, 14 do
            local z = numero(zonas, i * 4 + 1, 4)
            local nombre = "?"
            if z < 8 then
                -- el valor puede ser un puntero a un nombre o el codigo del nombre: anoto el nombre si es puntero, si no los bytes
                local p = leer(MASK_NOM + z * 8, 8)
                local dir = p and numero(p, 1, 6) or 0
                if p and string.byte(p, 7) == 0 and string.byte(p, 8) == 0 and dir > 0x140000000 and dir < 0x150000000 then
                    local s = leer(dir, 16)
                    if s then nombre = string.match(s, "^([%w_]*)") or "?" end
                elseif p then
                    nombre = string.format("%02X%02X%02X%02X%02X%02X%02X%02X", string.byte(p, 8), string.byte(p, 7), string.byte(p, 6),
                        string.byte(p, 5), string.byte(p, 4), string.byte(p, 3), string.byte(p, 2), string.byte(p, 1))
                end
            end
            t[#t + 1] = string.format("%d=%d(%s)", i, z, nombre)
        end
        log("PES21FX_gestos: zona de cada gesto sumado (indice=zona(nombre)): " .. table.concat(t, " "))
    end
    aviso = "tablas de emociones anotadas en sider.log"
end

function m.key_down(ctx, vkey)
    if vkey == 0x30 then
        G.activo = not G.activo
        G.cambios = G.cambios + 1
        aviso = G.activo and "tablas nuevas: SI (entran al cargar el proximo partido)" or "tablas nuevas: NO (las de siempre al cargar el proximo partido)"
        log("PES21FX_gestos: " .. aviso)
    elseif vkey == 0x39 then
        local ok, err = pcall(volcar)
        if not ok then
            aviso = "no pude leer: " .. tostring(err)
            log("PES21FX_gestos: " .. aviso)
        end
    end
end

function m.overlay_on(ctx)
    local t = { string.format("PES21FX gestos %s | tablas de gestos de cara nuevas: %s", m.version, G.activo and "SI" or "NO") }
    for _, b in ipairs(ORDEN) do
        t[#t + 1] = string.format("  %-13s %s | el juego la pidio %d veces, entregada %d", TABLAS[b],
            hay[b] and "lista" or "FALTA en content\\PES21FX-gestos\\tablas", G.pedidos[b] or 0, G.entregados[b] or 0)
    end
    t[#t + 1] = "0 = SI / NO (entra en el proximo partido) | 9 = anotar tablas de emociones en sider.log | (tecla 1 = otro modulo)"
    if aviso ~= "" then t[#t + 1] = aviso end
    return table.concat(t, "\n")
end

function m.init(ctx)
    DIR = ctx.sider_dir .. "content\\PES21FX-gestos\\tablas\\"
    -- si Sider recarga el modulo con el juego abierto, sigo con el mismo estado
    local g = ctx.PES21FXGestos
    if type(g) == "table" and type(g.pedidos) == "table" and type(g.entregados) == "table" then
        G = g
    else
        ctx.PES21FXGestos = G
    end
    local n = 0
    for _, b in ipairs(ORDEN) do
        hay[b] = existe(DIR .. TABLAS[b])
        if hay[b] then n = n + 1 end
    end
    ctx.register("livecpk_make_key", m.make_key)
    ctx.register("livecpk_get_filepath", m.get_filepath)
    ctx.register("key_down", m.key_down)
    ctx.register("overlay_on", m.overlay_on)
    log(string.format("PES21FX_gestos %s listo (%d de 2 tablas en %s; memoria %s)", m.version, n, DIR,
        type(memory) == "table" and "si" or "NO"))
end

return m
