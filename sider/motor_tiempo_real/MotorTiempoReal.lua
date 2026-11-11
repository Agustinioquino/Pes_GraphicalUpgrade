-- MotorTiempoReal.lua 0.1 (PES21 FX, 10/11/2026) - modulo de Sider.
-- Primer ladrillo del "Motor en tiempo real" del Editar: entrega al juego una escena de estudio con objetos
-- agregados (entidades StadiumModel armadas con tools/fox/escena_objetos.py) para que los cargue y los dibuje
-- el propio Fox Engine. No dibuja nada, no escribe memoria, no reemplaza archivos en disco.
--
-- Que entrega: cuando el juego pide ...\model\light\#Win\Default.fpkd o Default.fpk, y existen los dos en
--   content\motor-tiempo-real\escena\Default.fpkd  y  content\motor-tiempo-real\escena\Default.fpk
-- entrega esos. Si falta alguno de los dos no entrega nada (el juego sigue con lo suyo).
-- Apagarlo sin tocar sider.ini: crear content\motor-tiempo-real\apagado.txt (cualquier contenido).
-- Registro: cada pedido y cada entrega va a sider.log con el prefijo [MotorTiempoReal.lua].
--
-- INFERIDO, se confirma con el registro: que la entrega de un modulo le gane a la carpeta livecpk\LucesEditor.

local m = {}
local VERSION = "0.1"
local pcall = pcall or function(f, ...) return true, f(...) end   -- el Sider de Agustin no da pcall
local DIR
local ARCH = { ["default.fpkd"] = "Default.fpkd", ["default.fpk"] = "Default.fpk" }
local pedidos, entregas = {}, {}
local activo, motivo = false, "sin iniciar"

local function exists(path)
    local f = io.open(path, "rb")
    if f then f:close() return true end
    return false
end

local function estado()
    if exists(DIR .. "apagado.txt") then return false, "apagado.txt presente" end
    for _, n in pairs(ARCH) do
        if not exists(DIR .. "escena\\" .. n) then return false, "falta escena\\" .. n end
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

function m.init(ctx)
    DIR = ctx.sider_dir .. "content\\motor-tiempo-real\\"
    activo, motivo = estado()
    ctx.register("livecpk_make_key", m.make_key)
    ctx.register("livecpk_get_filepath", m.get_filepath)
    log("MotorTiempoReal " .. VERSION .. " listo: " .. (activo and "ENTREGANDO" or "sin entregar") .. " (" .. motivo .. "); carpeta " .. DIR)
end

return m
