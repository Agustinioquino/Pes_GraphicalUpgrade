-- ProbadorPiezas v2.1 - PES 2021 / Sider  (PES21 FX, 10/11/2026)
-- Probador de modelos (.fmdl) de los jugadores, UNO A UNO.
-- Elegis un LUGAR del jugador (camiseta, cuello, mangas, short) y con una tecla vas pasando
-- por el CATALOGO de modelos del juego: en ese lugar se carga el modelo elegido en vez del
-- normal, y el overlay muestra su numero y su nombre de archivo.
-- Sirve para ver que es cada fmdl (pechera de entrenamiento, polo, traje, ropa de
-- entrenamiento, camiseta interior...) y encontrar los que no conoces.
--
-- Catalogo: content\probador-piezas\catalogo\  (los .fmdl que esten ahi adentro),
-- listado en content\probador-piezas\catalogo\indice.txt (una ruta relativa por linea).
--
-- Uso: overlay de Sider, pasar a este modulo con [ y ] :
--   0      = SIGUIENTE modelo del catalogo en el lugar elegido     9 = anterior
--   7 / 8  = lugar anterior / siguiente (camiseta, cuello, mangas, short, ropa de
--            entrenamiento, camiseta interior, pechera)
--   1      = ese lugar vuelve a NORMAL          2 = ese lugar OCULTO (modelo vacio)
--   3      = todo a normal
-- El juego guarda los modelos ya cargados: despues de cambiar, volve a entrar al jugador
-- (Edit > Apariencia) o empeza otro partido para que los recargue.
-- Lo elegido se recuerda entre sesiones (estado.txt). No usar junto con SinCamiseta.
-- No todos los modelos van a calzar en cualquier lugar: para eso es el probador.

local m = { version = "2.1" }
local DIR, CAT, EMPTY, ESTADO
local catalogo = {}                 -- rutas relativas dentro de catalogo\
local LUGARES = {
    { nombre = "camiseta", pats = { "^shirt" } },
    { nombre = "cuello",   pats = { "^collar_" } },
    { nombre = "mangas",   pats = { "^sleeve", "^cap_sleeve", "^s_inner" } },
    { nombre = "short",    pats = { "^pants_%d", "^pants%.fmdl$" } },
    { nombre = "ropa entr.", pats = { "^training_suit" } },      -- ropa de entrenamiento (parts\training_suit)
    { nombre = "interior", pats = { "^undershirt" } },           -- camiseta interior (parts\undershirt)
    { nombre = "pechera",  pats = { "^bibs" } },                 -- pechera (solo donde el juego ya la pide)
}
-- estado por lugar: 0 = normal, -1 = oculto, n >= 1 = modelo n del catalogo
local estado = {}
for i = 1, #LUGARES do estado[i] = 0 end
local lugar, aviso = 1, ""

local function basename(f)
    return string.lower(string.match(f, "([^\\/]+)$") or f)
end

local function exists(path)
    local f = io.open(path, "rb")
    if f then f:close() return true end
    return false
end

local function lugar_de(b)
    if string.find(b, "_sub", 1, true) then return nil end      -- piezas del cuerpo, no del uniforme
    for i, l in ipairs(LUGARES) do
        for _, p in ipairs(l.pats) do
            if string.match(b, p) then return i end
        end
    end
    return nil
end

local function destino(i)
    local e = estado[i]
    if e == -1 then return EMPTY end
    if e >= 1 and catalogo[e] then return CAT .. catalogo[e] end
    return nil
end

function m.make_key(ctx, filename)
    local b = basename(filename)
    if string.sub(b, -5) ~= ".fmdl" then return end
    local i = lugar_de(b)
    if i and estado[i] ~= 0 then return "probadorpiezas2:" .. i .. ":" .. estado[i] end
end

function m.get_filepath(ctx, filename, key)
    if key and string.sub(key, 1, 16) == "probadorpiezas2:" then
        local i = tonumber(string.match(key, "^probadorpiezas2:(%d+):"))
        if i then return destino(i) end
    end
end

local function guardar()
    local f = io.open(ESTADO, "wb")
    if not f then return end
    for i, l in ipairs(LUGARES) do
        local e = estado[i]
        local v = "normal"
        if e == -1 then v = "oculto" elseif e >= 1 and catalogo[e] then v = catalogo[e] end
        f:write(l.nombre .. " = " .. v .. "\r\n")
    end
    f:close()
end

local function cargar()
    local f = io.open(ESTADO, "rb")
    if not f then return end
    local txt = f:read("*a") or ""
    f:close()
    for line in string.gmatch(txt, "[^\r\n]+") do
        local k, v = string.match(line, "^%s*(.-)%s*=%s*(.-)%s*$")
        if k and v then
            for i, l in ipairs(LUGARES) do
                if l.nombre == k then
                    if v == "oculto" then
                        estado[i] = -1
                    else
                        for n, c in ipairs(catalogo) do
                            if c == v then estado[i] = n end
                        end
                    end
                end
            end
        end
    end
end

local function leer_catalogo()
    catalogo = {}
    local f = io.open(CAT .. "indice.txt", "rb")
    if not f then return end
    local txt = f:read("*a") or ""
    f:close()
    for line in string.gmatch(txt, "[^\r\n]+") do
        local r = string.match(line, "^%s*(.-)%s*$")
        if r ~= "" and string.sub(r, 1, 1) ~= "#" and string.lower(string.sub(r, -5)) == ".fmdl" then
            catalogo[#catalogo + 1] = r
        end
    end
end

local function texto_estado(i)
    local e = estado[i]
    if e == -1 then return "OCULTO" end
    if e >= 1 and catalogo[e] then return string.format("modelo %d de %d:  %s", e, #catalogo, catalogo[e]) end
    return "normal"
end

function m.key_down(ctx, vkey)
    local n = #catalogo
    if vkey == 0x37 then
        lugar = lugar - 1
        if lugar < 1 then lugar = #LUGARES end
    elseif vkey == 0x38 then
        lugar = lugar + 1
        if lugar > #LUGARES then lugar = 1 end
    elseif vkey == 0x30 or vkey == 0x39 then
        if n == 0 then
            aviso = "el catalogo esta vacio (falta catalogo\\indice.txt)"
            return
        end
        local e = estado[lugar]
        if e < 1 then
            e = (vkey == 0x30) and 1 or n
        elseif vkey == 0x30 then
            e = e + 1
            if e > n then e = 1 end
        else
            e = e - 1
            if e < 1 then e = n end
        end
        estado[lugar] = e
    elseif vkey == 0x31 then estado[lugar] = 0
    elseif vkey == 0x32 then estado[lugar] = -1
    elseif vkey == 0x33 then for i = 1, #LUGARES do estado[i] = 0 end
    else return end
    guardar()
    if vkey ~= 0x37 and vkey ~= 0x38 then
        aviso = LUGARES[lugar].nombre .. " -> " .. texto_estado(lugar) .. "   (volve a entrar al jugador para verlo)"
        log("ProbadorPiezas: " .. aviso)
    end
end

function m.overlay_on(ctx)
    local t = { string.format("ProbadorPiezas %s | catalogo: %d modelos", m.version, #catalogo) }
    for i, l in ipairs(LUGARES) do
        t[#t + 1] = string.format("%s %-10s %s", i == lugar and ">" or " ", l.nombre, texto_estado(i))
    end
    t[#t + 1] = "0 = siguiente modelo | 9 = anterior | 7/8 = lugar | 1 normal | 2 oculto | 3 todo normal"
    if aviso ~= "" then t[#t + 1] = aviso end
    return table.concat(t, "\n")
end

function m.init(ctx)
    DIR = ctx.sider_dir .. "content\\probador-piezas\\"
    CAT = DIR .. "catalogo\\"
    EMPTY = DIR .. "vacio.fmdl"
    ESTADO = DIR .. "estado.txt"
    leer_catalogo()
    cargar()
    if not exists(EMPTY) then log("ProbadorPiezas: FALTA " .. EMPTY) end
    ctx.register("livecpk_make_key", m.make_key)
    ctx.register("livecpk_get_filepath", m.get_filepath)
    ctx.register("key_down", m.key_down)
    ctx.register("overlay_on", m.overlay_on)
    log(string.format("ProbadorPiezas %s listo (catalogo %d modelos en %s)", m.version, #catalogo, CAT))
end

return m
