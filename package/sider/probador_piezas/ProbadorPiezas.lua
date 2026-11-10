-- ProbadorPiezas v1.1 - PES 2021 / Sider  (PES21 FX, 10/11/2026)
-- (1.1) Sin pasos obligatorios: anota TODOS los archivos desde que abre el juego y los va
-- guardando solo en vistos.txt; las piezas ocultadas se recuerdan en marcas.txt entre sesiones.
-- Muestra cada modelo (.fmdl) que el juego pide y deja OCULTARLO o REEMPLAZARLO por otro
-- para probar piezas y objetos de los jugadores. Reemplaza a SinCamiseta (no usar los dos juntos).
--
-- Como funciona: Sider avisa cada archivo que el juego va a abrir. Este modulo anota los
-- .fmdl y, si la pieza esta marcada, entrega un modelo vacio (oculta) u otro archivo (reemplazo).
-- El juego guarda los modelos ya cargados: despues de cambiar algo hay que hacer que los
-- recargue (salir y volver a entrar al jugador en Edit, o empezar otro partido).
-- Limite: las piezas del CUERPO (torso, pechera bibs, ropa de entrenamiento...) no se piden
-- sueltas: vienen dentro de common_package.fpk, que se carga una vez al abrir el juego.
-- Esas no aparecen sueltas en la lista; el paquete entero si.
--
-- Uso: overlay de Sider, pasar a este modulo con [ y ] :
--   9 / 0  = pieza anterior / siguiente          7 / 8 = saltar 10
--   1      = ocultar / mostrar la pieza elegida (por nombre de archivo)
--   2      = ocultar / mostrar TODAS las que empiezan igual (ej. shirt*, sleeve*)
--   3      = quitar todas las marcas
--   4      = guardar un resumen (veces, marca) en content\probador-piezas\resumen.txt
--            (vistos.txt se escribe solo, sin tocar nada)
--   5      = volver a leer content\probador-piezas\reglas.txt
--   6      = anotar solo .fmdl / todos los archivos (fpk, ftex, bin...)
-- reglas.txt (opcional), una por linea, sin distinguir mayusculas:
--   nombre_o_comienzo* = OCULTAR
--   nombre.fmdl = ruta\relativa\a\content\probador-piezas\otro.fmdl
--   # comentario

local m = { version = "1.1" }
local DIR, EMPTY, VISTOS, REGLAS, MARCAS
local pendientes, guardados = {}, 0
local seen, order = {}, {}        -- nombre base -> {n = veces, full = ruta pedida}
local hide_name, hide_prefix = {}, {}
local repl_name, repl_prefix = {}, {}
local sel, todo, n_reglas, aviso = 1, true, 0, ""
local MAX = 20000

local function basename(f)
    return string.lower(string.match(f, "([^\\/]+)$") or f)
end

local function prefix_of(b)
    return string.match(b, "^(%a+)") or b
end

local function starts(b, list)
    for p, v in pairs(list) do
        if string.sub(b, 1, #p) == p then return v end
    end
    return nil
end

local function exists(path)
    local f = io.open(path, "rb")
    if f then f:close() return true end
    return false
end

local function load_rules()
    repl_name, repl_prefix, n_reglas = {}, {}, 0
    local f = io.open(REGLAS, "rb")
    if not f then return end
    local txt = f:read("*a") or ""
    f:close()
    for line in string.gmatch(txt, "[^\r\n]+") do
        if not string.match(line, "^%s*#") then
            local k, v = string.match(line, "^%s*(.-)%s*=%s*(.-)%s*$")
            if k and v and k ~= "" and v ~= "" then
                k = string.lower(k)
                local dest = (string.upper(v) == "OCULTAR") and EMPTY or (DIR .. v)
                if string.sub(k, -1) == "*" then
                    repl_prefix[string.sub(k, 1, -2)] = dest
                else
                    repl_name[k] = dest
                end
                n_reglas = n_reglas + 1
                if dest ~= EMPTY and not exists(dest) then
                    log("ProbadorPiezas: la regla '" .. k .. "' apunta a un archivo que no esta: " .. dest)
                end
            end
        end
    end
end

local function target(b)
    local r = repl_name[b] or starts(b, repl_prefix)
    if r then return r end
    if hide_name[b] or starts(b, hide_prefix) then return EMPTY end
    return nil
end

-- guarda solo: agrega al final de vistos.txt lo nuevo, de a tandas
local function flush()
    if #pendientes == 0 then return end
    local f = io.open(VISTOS, "ab")
    if not f then pendientes = {} return end
    f:write(table.concat(pendientes))
    f:close()
    guardados = guardados + #pendientes
    pendientes = {}
end

local function save_marks()
    local f = io.open(MARCAS, "wb")
    if not f then return end
    for b in pairs(hide_name) do f:write(b .. "\r\n") end
    for p in pairs(hide_prefix) do f:write(p .. "*\r\n") end
    f:close()
end

local function load_marks()
    local f = io.open(MARCAS, "rb")
    if not f then return end
    local txt = f:read("*a") or ""
    f:close()
    for line in string.gmatch(txt, "[^\r\n]+") do
        local k = string.lower((string.gsub(line, "%s+", "")))
        if k ~= "" then
            if string.sub(k, -1) == "*" then hide_prefix[string.sub(k, 1, -2)] = true else hide_name[k] = true end
        end
    end
end

function m.make_key(ctx, filename)
    local b = basename(filename)
    local es_fmdl = string.sub(b, -5) == ".fmdl"
    if todo or es_fmdl then
        local s = seen[b]
        if s then
            s.n = s.n + 1
        elseif #order < MAX then
            seen[b] = { n = 1, full = filename }
            order[#order + 1] = b
            pendientes[#pendientes + 1] = filename .. "\r\n"
            if #pendientes >= 40 then flush() end
        end
    end
    if es_fmdl and target(b) then return "probadorpiezas:" .. b end
end

function m.get_filepath(ctx, filename, key)
    if key and string.sub(key, 1, 15) == "probadorpiezas:" then
        return target(string.sub(key, 16))
    end
end

local function save_seen()
    flush()
    local f = io.open(DIR .. "resumen.txt", "wb")
    if not f then aviso = "no pude escribir " .. VISTOS return end
    f:write("# veces\tarchivo\truta pedida\tmarca\r\n")
    for _, b in ipairs(order) do
        local s = seen[b]
        local t = target(b)
        f:write(string.format("%d\t%s\t%s\t%s\r\n", s.n, b, s.full, t and (t == EMPTY and "OCULTA" or t) or ""))
    end
    f:close()
    aviso = string.format("resumen de %d archivos en resumen.txt", #order)
end

function m.key_down(ctx, vkey)
    local n = #order
    aviso = ""
    if vkey == 0x39 then sel = sel - 1
    elseif vkey == 0x30 then sel = sel + 1
    elseif vkey == 0x37 then sel = sel - 10
    elseif vkey == 0x38 then sel = sel + 10
    elseif vkey == 0x31 and n > 0 then
        local b = order[sel]
        hide_name[b] = (not hide_name[b]) or nil
        aviso = b .. (hide_name[b] and " OCULTA" or " visible") .. " (recargar el jugador)"
    elseif vkey == 0x32 and n > 0 then
        local p = prefix_of(order[sel])
        hide_prefix[p] = (not hide_prefix[p]) or nil
        aviso = p .. "* " .. (hide_prefix[p] and "OCULTAS" or "visibles") .. " (recargar el jugador)"
    elseif vkey == 0x33 then
        hide_name, hide_prefix = {}, {}
        aviso = "marcas quitadas (las reglas de reglas.txt siguen)"
    elseif vkey == 0x34 then save_seen()
    elseif vkey == 0x35 then
        load_rules()
        aviso = string.format("reglas.txt: %d reglas", n_reglas)
    elseif vkey == 0x36 then
        todo = not todo
        aviso = todo and "anotando TODOS los archivos" or "anotando solo .fmdl"
    else
        return
    end
    if vkey >= 0x31 and vkey <= 0x33 then save_marks() end
    flush()
    if sel > n then sel = n end
    if sel < 1 then sel = 1 end
    if aviso ~= "" then log("ProbadorPiezas: " .. aviso) end
end

function m.overlay_on(ctx)
    flush()
    local n = #order
    local t = { string.format("ProbadorPiezas %s | %d archivos vistos | reglas %d | %s", m.version, n, n_reglas, todo and "todos" or "solo fmdl") }
    if n == 0 then
        t[#t + 1] = "Todavia no se pidio ningun modelo: entra a un jugador o a un partido."
    else
        if sel > n then sel = n end
        local a = sel - 5
        if a < 1 then a = 1 end
        local z = a + 10
        if z > n then z = n end
        for i = a, z do
            local b = order[i]
            local tg = target(b)
            local marca = tg and (tg == EMPTY and " [OCULTA]" or " [REEMPLAZADA]") or ""
            t[#t + 1] = string.format("%s %4d  %s  x%d%s", i == sel and ">" or " ", i, b, seen[b].n, marca)
        end
    end
    t[#t + 1] = "9/0 mover, 7/8 de a 10 | 1 ocultar esta | 2 ocultar las que empiezan igual | 3 limpiar | 4 guardar lista | 5 releer reglas | 6 fmdl/todos"
    if aviso ~= "" then t[#t + 1] = aviso end
    return table.concat(t, "\n")
end

function m.init(ctx)
    DIR = ctx.sider_dir .. "content\\probador-piezas\\"
    EMPTY = DIR .. "vacio.fmdl"
    VISTOS = DIR .. "vistos.txt"
    REGLAS = DIR .. "reglas.txt"
    MARCAS = DIR .. "marcas.txt"
    local f0 = io.open(VISTOS, "ab")
    if f0 then f0:write("# --- sesion nueva ---\r\n") f0:close() end
    load_marks()
    if not exists(EMPTY) then
        log("ProbadorPiezas: FALTA " .. EMPTY .. " (modelo vacio): ocultar no va a funcionar")
    end
    load_rules()
    ctx.register("livecpk_make_key", m.make_key)
    ctx.register("livecpk_get_filepath", m.get_filepath)
    ctx.register("key_down", m.key_down)
    ctx.register("overlay_on", m.overlay_on)
    log(string.format("ProbadorPiezas %s listo (carpeta %s, reglas %d)", m.version, DIR, n_reglas))
end

return m
