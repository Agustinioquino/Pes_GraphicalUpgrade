-- SinCamiseta v1.1 (debug) - PES 2021 / Sider
-- Oculta la camiseta, el cuello y las mangas (los reemplaza por un modelo vacio)
-- para mirar el cuerpo en el menu Edit.
-- (1.1, PES21 FX) La tecla 4 oculta el short: en su lugar va la pelvis de piel
-- (gluteos y frente liso, tipo Ken) soldada al torso de TorsoBody y a los muslos.
-- Son copias de pants_*_sub.fmdl de TorsoBody con la malla del short cambiada,
-- en content\sin-camiseta\sin_short\ (las arma tools/fox/pelvis/make_pelvis.py).
--
-- Uso: abri el overlay de Sider, pasa a este modulo con [ y ] y apreta:
--   1 = activar / desactivar todo (camiseta + cuello + mangas)
--   2 = solo mangas on/off      3 = solo camiseta + cuello on/off
--   4 = short on/off (la pelvis de piel en su lugar)
-- El juego guarda los modelos ya cargados: despues de cambiar, sali y volve a
-- entrar al jugador (Apariencia) para que los recargue.

local m = { version = "1.1" }
local EMPTY, SIN_SHORT_DIR
local hide_shirt, hide_sleeves, hide_shorts = false, false, false
local shorts_ok = false

local SHIRT = { "^shirt", "^collar_" }                       -- shirt, shirt_in, shirt_in_tight, shirt_out..., collar_XXX
local SLEEVE = { "^sleeve", "^cap_sleeve", "^s_inner" }      -- todas las mangas, cintas de capitan, mangas interiores
-- el short de TorsoBody esta en la malla 0 de estos archivos (las otras mallas son el torso)
local SHORTS = { ["pants_out_sub.fmdl"] = true, ["pants_in_sub.fmdl"] = true,
                 ["pants_out_inner_sub.fmdl"] = true, ["pants_out_sub_tight.fmdl"] = true }
local KEY_SHORT = "sincamiseta_sinshort_"

local function basename(f)
    return string.lower(string.match(f, "([^\\/]+)$") or f)
end

local function matches(name, list)
    for _, p in ipairs(list) do
        if string.match(name, p) then return true end
    end
    return false
end

local function hidden(filename)
    local b = basename(filename)
    if not string.match(b, "%.fmdl$") then return false end
    if hide_shirt and matches(b, SHIRT) then return true end
    if hide_sleeves and matches(b, SLEEVE) then return true end
    return false
end

function m.make_key(ctx, filename)
    if hidden(filename) then return "sincamiseta_vacio" end
    if hide_shorts and shorts_ok then
        local b = basename(filename)
        if SHORTS[b] then return KEY_SHORT .. b end
    end
end

function m.get_filepath(ctx, filename, key)
    if key == "sincamiseta_vacio" then return EMPTY end
    if type(key) == "string" and key:sub(1, #KEY_SHORT) == KEY_SHORT then return SIN_SHORT_DIR .. key:sub(#KEY_SHORT + 1) end
end

local function state(b) return b and "OCULTA" or "visible" end
local function state_m(b) return b and "OCULTO" or "visible" end

function m.key_down(ctx, vkey)
    if vkey == 0x31 then                 -- 1
        local on = not (hide_shirt and hide_sleeves)
        hide_shirt, hide_sleeves = on, on
    elseif vkey == 0x32 then             -- 2
        hide_sleeves = not hide_sleeves
    elseif vkey == 0x33 then             -- 3
        hide_shirt = not hide_shirt
    elseif vkey == 0x34 then             -- 4
        if not shorts_ok then
            log("SinCamiseta: no estan las pelvis (" .. tostring(SIN_SHORT_DIR) .. "pants_out_sub.fmdl): el short no se puede ocultar")
            return
        end
        hide_shorts = not hide_shorts
    else
        return
    end
    log(string.format("SinCamiseta: camiseta %s, mangas %s, short %s", state(hide_shirt), state(hide_sleeves), state_m(hide_shorts)))
end

function m.overlay_on(ctx)
    return string.format("SinCamiseta %s | camiseta+cuello: %s | mangas: %s | short: %s%s\n1 = todo on/off, 2 = mangas, 3 = camiseta, 4 = short | despues volve a entrar al jugador",
        m.version, state(hide_shirt), state(hide_sleeves), state_m(hide_shorts), shorts_ok and "" or " (faltan las pelvis)")
end

function m.init(ctx)
    EMPTY = ctx.sider_dir .. "content\\sin-camiseta\\vacio.fmdl"
    SIN_SHORT_DIR = ctx.sider_dir .. "content\\sin-camiseta\\sin_short\\"
    local f = io.open(SIN_SHORT_DIR .. "pants_out_sub.fmdl", "rb")
    if f then f:close(); shorts_ok = true end
    ctx.register("livecpk_make_key", m.make_key)
    ctx.register("livecpk_get_filepath", m.get_filepath)
    ctx.register("key_down", m.key_down)
    ctx.register("overlay_on", m.overlay_on)
    log("SinCamiseta " .. m.version .. " listo (modelo vacio: " .. EMPTY .. "; pelvis sin short: " .. (shorts_ok and SIN_SHORT_DIR or "NO ESTAN") .. ")")
end

return m
