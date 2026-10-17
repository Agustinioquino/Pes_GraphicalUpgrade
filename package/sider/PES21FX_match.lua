--[[
    PES21 FX - puente con Sider (presets por estadio)
    Le avisa al mod grafico (d3d11.dll de PES21 FX) en que estadio y con que condiciones se juega,
    escribiendo PES21FX\match.ini. El mod lo lee y aplica PES21FX\presets\stadium_<id>.ini si existe.
    Instalacion: copiar a la carpeta modules de Sider y agregar en sider.ini:
        lua.module = "PES21FX_match.lua"
]]

local m = {}
local last = ""

local function num(v, def)
    local n = tonumber(v)
    if n == nil then return def end
    return n
end

local function write_match(ctx, o)
    o = o or {}
    local stadium  = num(o.stadium,   num(ctx.stadium, -1))
    local tod      = num(o.timeofday, -1)   -- 0 = dia, 1 = noche
    local weather  = num(o.weather,   -1)   -- 0 = despejado, 1 = lluvia
    local season   = num(o.season,    -1)   -- 0 = verano, 1 = invierno
    local tid      = num(ctx.tournament_id, -1)
    local text = string.format(
        "[Match]\r\nstadium = %d\r\ntimeofday = %d\r\nweather = %d\r\nseason = %d\r\ntournament = %d\r\nhome = %d\r\naway = %d\r\n",
        stadium, tod, weather, season, tid, num(ctx.home_team, -1), num(ctx.away_team, -1))
    if text == last then return end
    -- 1) PES21FX junto al exe (cwd del juego); 2) relativo a la carpeta de Sider
    local paths = { "PES21FX\\match.ini" }
    if ctx.sider_dir then
        local sd = tostring(ctx.sider_dir)
        if sd:sub(-1) ~= "\\" and sd:sub(-1) ~= "/" then sd = sd .. "\\" end
        table.insert(paths, sd .. "..\\PES21FX\\match.ini")
    end
    for _, p in ipairs(paths) do
        local f = io.open(p, "wb")
        if f then
            f:write(text); f:close(); last = text
            log(string.format("PES21FX_match: estadio %d, dia/noche %d, clima %d -> %s", stadium, tod, weather, p))
            return
        end
    end
    log("PES21FX_match: no pude escribir match.ini (no existe la carpeta PES21FX?)")
end

function m.set_stadium(ctx, options)
    write_match(ctx, options)
    return nil   -- no cambia el estadio
end

function m.set_conditions(ctx, options)
    write_match(ctx, options)
    return nil
end

function m.after_set_conditions(ctx)
    write_match(ctx, nil)
end

function m.init(ctx)
    ctx.register("set_stadium", m.set_stadium)
    ctx.register("set_conditions", m.set_conditions)
    ctx.register("after_set_conditions", m.after_set_conditions)
    log("PES21FX_match: listo (presets por estadio)")
end

return m
