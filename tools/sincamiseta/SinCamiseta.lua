-- SinCamiseta v1.2 - PES 2021 / Sider
-- Oculta la camiseta, el cuello y las mangas (los reemplaza por un modelo vacio)
-- para mirar el cuerpo en el menu Edit.
-- (1.1, PES21 FX) La tecla 4 oculta el short: en su lugar va la pelvis de piel
-- (gluteos y frente liso, tipo Ken) soldada al torso de TorsoBody y a los muslos.
-- (1.2) La pelvis de verdad: las piezas del cuerpo (pants_*_sub.fmdl, con la malla
-- del short) no se piden sueltas, vienen dentro de common_package.fpk y el juego lo
-- carga UNA vez al abrirse. Por eso ahora:
--   - Menu Insert de PES21 FX > Cuerpo > "Sin short": "Sin short (pelvis)" y
--     reiniciar el juego. Al abrir, este modulo sirve
--     content\sin-camiseta\sin_short\common_package.fpk (el fpk del Global Model
--     con las 4 pelvis adentro; lo arma tools/sincamiseta/make_fpk_sin_short.py).
--   - Con la pelvis cargada, el short del uniforme (pants_XXX.fmdl) se oculta y la
--     tecla 4 lo muestra / oculta en vivo (despues hay que volver a entrar al jugador).
--
-- Uso: abri el overlay de Sider, pasa a este modulo con [ y ] y apreta:
--   1 = activar / desactivar todo (camiseta + cuello + mangas)
--   2 = solo mangas on/off      3 = solo camiseta + cuello on/off
--   4 = short on/off (con la pelvis cargada; si no, dice como cargarla)
-- El juego guarda los modelos ya cargados: despues de cambiar, sali y volve a
-- entrar al jugador (Apariencia) para que los recargue.

local m = { version = "1.2" }
local EMPTY, SIN_SHORT_DIR, FPK_FILE, INI_FILE
local hide_shirt, hide_sleeves, hide_shorts = false, false, false
local fpk_ok, fpk_on, pedido = false, false, false

local SHIRT = { "^shirt", "^collar_" }                       -- shirt, shirt_in, shirt_in_tight, shirt_out..., collar_XXX
local SLEEVE = { "^sleeve", "^cap_sleeve", "^s_inner" }      -- todas las mangas, cintas de capitan, mangas interiores
local KEY_FPK = "sincamiseta_fpk_sin_short"

local function basename(f)
    return string.lower(string.match(f, "([^\\/]+)$") or f)
end

local function matches(name, list)
    for _, p in ipairs(list) do
        if string.match(name, p) then return true end
    end
    return false
end

-- el short del uniforme: pants_007.fmdl (uniform\nocloth, uniform\cloth...), no las piezas del cuerpo (pants_*_sub)
local function is_kit_shorts(b)
    return string.match(b, "^pants_%d+.*%.fmdl$") ~= nil and not string.find(b, "_sub", 1, true)
end

local function hidden(filename)
    local b = basename(filename)
    if not string.match(b, "%.fmdl$") then return false end
    if hide_shirt and matches(b, SHIRT) then return true end
    if hide_sleeves and matches(b, SLEEVE) then return true end
    if fpk_on and hide_shorts and is_kit_shorts(b) then return true end
    return false
end

local FPK_TAIL = "model\\character\\#win\\common_package.fpk"
local function is_common_fpk(filename)
    local f = string.lower((string.gsub(filename, "/", "\\")))
    return #f >= #FPK_TAIL and string.sub(f, -#FPK_TAIL) == FPK_TAIL
end

function m.make_key(ctx, filename)
    if fpk_on and is_common_fpk(filename) then return KEY_FPK end
    if hidden(filename) then return "sincamiseta_vacio" end
end

function m.get_filepath(ctx, filename, key)
    if key == "sincamiseta_vacio" then return EMPTY end
    if key == KEY_FPK then return FPK_FILE end
end

local function state(b) return b and "OCULTA" or "visible" end
local function state_m(b) return b and "OCULTO" or "visible" end

local function short_state()
    if fpk_on then return state_m(hide_shorts) .. " (pelvis cargada)" end
    if not fpk_ok then return "visible (falta el fpk sin short)" end
    if pedido then return "visible (la pelvis se carga al reiniciar el juego)" end
    return "visible"
end

function m.key_down(ctx, vkey)
    if vkey == 0x31 then                 -- 1
        local on = not (hide_shirt and hide_sleeves)
        hide_shirt, hide_sleeves = on, on
    elseif vkey == 0x32 then             -- 2
        hide_sleeves = not hide_sleeves
    elseif vkey == 0x33 then             -- 3
        hide_shirt = not hide_shirt
    elseif vkey == 0x34 then             -- 4
        if not fpk_on then
            if not fpk_ok then
                log("SinCamiseta: no esta " .. tostring(FPK_FILE) .. " (el fpk con las pelvis): el short no se puede ocultar")
            else
                log("SinCamiseta: para ocultar el short, menu Insert de PES21 FX > Cuerpo > Sin short = Sin short (pelvis), y reinicia el juego (la pelvis va dentro de common_package.fpk, que se carga al abrir)")
            end
            return
        end
        hide_shorts = not hide_shorts
    else
        return
    end
    log(string.format("SinCamiseta: camiseta %s, mangas %s, short %s", state(hide_shirt), state(hide_sleeves), short_state()))
end

function m.overlay_on(ctx)
    local extra = ""
    if not fpk_on and fpk_ok then extra = "\nSin short: menu Insert > Cuerpo > Sin short y reiniciar el juego" end
    if fpk_on and not hide_shorts then extra = "\nCon la pelvis cargada falta la cintura del short: para el short completo apaga Sin short y reinicia" end
    return string.format("SinCamiseta %s | camiseta+cuello: %s | mangas: %s | short: %s\n1 = todo on/off, 2 = mangas, 3 = camiseta, 4 = short | despues volve a entrar al jugador%s",
        m.version, state(hide_shirt), state(hide_sleeves), short_state(), extra)
end

-- x308 del ini de PES21 FX (lo guarda el menu Insert): 1 = cargar la pelvis al abrir el juego
local function read_pedido()
    local f = io.open(INI_FILE, "rb")
    if not f then return false end
    local txt = f:read("*a") or ""
    f:close()
    for line in string.gmatch(txt, "[^\r\n]+") do
        local v = string.match(line, "^%s*x308%s*=%s*([%-%d%.]+)")
        if v then return (tonumber(v) or 0) > 0.5 end
    end
    return false
end

function m.init(ctx)
    EMPTY = ctx.sider_dir .. "content\\sin-camiseta\\vacio.fmdl"
    SIN_SHORT_DIR = ctx.sider_dir .. "content\\sin-camiseta\\sin_short\\"
    FPK_FILE = SIN_SHORT_DIR .. "common_package.fpk"
    INI_FILE = ctx.sider_dir .. "..\\PES21FX\\PES21FX.ini"
    local f = io.open(FPK_FILE, "rb")
    if f then f:close(); fpk_ok = true end
    pedido = read_pedido()
    fpk_on = fpk_ok and pedido
    hide_shorts = fpk_on
    ctx.register("livecpk_make_key", m.make_key)
    ctx.register("livecpk_get_filepath", m.get_filepath)
    ctx.register("key_down", m.key_down)
    ctx.register("overlay_on", m.overlay_on)
    log("SinCamiseta " .. m.version .. " listo (modelo vacio: " .. EMPTY .. "; sin short: " ..
        (fpk_on and ("pelvis cargada desde " .. FPK_FILE) or (fpk_ok and "fpk listo, apagado en el menu (x308)" or "NO ESTA el fpk " .. FPK_FILE)) .. ")")
end

return m
