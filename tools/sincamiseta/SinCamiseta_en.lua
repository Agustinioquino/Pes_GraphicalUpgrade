-- SinCamiseta ("shirtless") v1.2 - PES 2021 / Sider
-- Hides the shirt, the collar and the sleeves (replaces them with an empty model)
-- to look at the body in the Edit menu.
-- (1.1, PES21 FX) Key 4 hides the shorts: the skin pelvis goes in their place
-- (glutes and smooth front, Ken style) welded to the TorsoBody torso and the thighs.
-- (1.2) The real pelvis: the body pieces (pants_*_sub.fmdl, with the shorts mesh)
-- are not requested one by one, they come inside common_package.fpk and the game
-- loads it ONCE at start-up. So now:
--   - PES21 FX Insert menu > Body > "No shorts": "No shorts (pelvis)" and restart
--     the game. On start-up this module serves
--     content\sin-camiseta\sin_short\common_package.fpk (the Global Model fpk
--     with the 4 pelvises inside; built by tools/sincamiseta/make_fpk_sin_short.py).
--   - With the pelvis loaded, the kit shorts (pants_XXX.fmdl) are hidden and key 4
--     shows / hides them live (then re-enter the player).
--
-- Usage: open the Sider overlay, switch to this module with [ and ] and press:
--   1 = everything on/off (shirt + collar + sleeves)
--   2 = sleeves only on/off      3 = shirt + collar only on/off
--   4 = shorts on/off (with the pelvis loaded; otherwise it says how to load it)
-- The game keeps the models already loaded: after a change, leave and re-enter
-- the player (Appearance) so it reloads them.

local m = { version = "1.2" }
local EMPTY, SIN_SHORT_DIR, FPK_FILE, INI_FILE
local hide_shirt, hide_sleeves, hide_shorts = false, false, false
local fpk_ok, fpk_on, pedido = false, false, false

local SHIRT = { "^shirt", "^collar_" }                       -- shirt, shirt_in, shirt_in_tight, shirt_out..., collar_XXX
local SLEEVE = { "^sleeve", "^cap_sleeve", "^s_inner" }      -- all sleeves, captain armbands, inner sleeves
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

-- the kit shorts: pants_007.fmdl (uniform\nocloth, uniform\cloth...), not the body pieces (pants_*_sub)
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

local function state(b) return b and "HIDDEN" or "visible" end

local function short_state()
    if fpk_on then return state(hide_shorts) .. " (pelvis loaded)" end
    if not fpk_ok then return "visible (the no-shorts fpk is missing)" end
    if pedido then return "visible (the pelvis loads when the game restarts)" end
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
                log("SinCamiseta: " .. tostring(FPK_FILE) .. " (the fpk with the pelvises) is missing: the shorts can't be hidden")
            else
                log("SinCamiseta: to hide the shorts, PES21 FX Insert menu > Body > No shorts = No shorts (pelvis), then restart the game (the pelvis is inside common_package.fpk, loaded at start-up)")
            end
            return
        end
        hide_shorts = not hide_shorts
    else
        return
    end
    log(string.format("SinCamiseta: shirt %s, sleeves %s, shorts %s", state(hide_shirt), state(hide_sleeves), short_state()))
end

function m.overlay_on(ctx)
    local extra = ""
    if not fpk_on and fpk_ok then extra = "\nNo shorts: Insert menu > Body > No shorts, then restart the game" end
    if fpk_on and not hide_shorts then extra = "\nWith the pelvis loaded the shorts waistband is missing: for the full shorts turn off No shorts and restart" end
    return string.format("SinCamiseta %s | shirt+collar: %s | sleeves: %s | shorts: %s\n1 = all on/off, 2 = sleeves, 3 = shirt, 4 = shorts | then re-enter the player%s",
        m.version, state(hide_shirt), state(hide_sleeves), short_state(), extra)
end

-- x308 from the PES21 FX ini (saved by the Insert menu): 1 = load the pelvis when the game starts
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
    log("SinCamiseta " .. m.version .. " ready (empty model: " .. EMPTY .. "; no shorts: " ..
        (fpk_on and ("pelvis loaded from " .. FPK_FILE) or (fpk_ok and "fpk ready, off in the menu (x308)" or "fpk MISSING " .. FPK_FILE)) .. ")")
end

return m
