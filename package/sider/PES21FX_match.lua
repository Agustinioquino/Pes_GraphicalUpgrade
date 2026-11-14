--[[
    PES21 FX - puente con Sider (presets por estadio)
    Le avisa al mod grafico (d3d11.dll de PES21 FX) en que estadio y con que condiciones se juega,
    escribiendo PES21FX\match.ini. El mod lo lee y aplica PES21FX\presets\stadium_<id>.ini si existe.
    Instalacion: copiar a la carpeta modules de Sider y agregar en sider.ini:
        lua.module = "PES21FX_match.lua"
]]

local m = {}
local last = ""
local keep = { stadium = -1, tod = -1, weather = -1, season = -1 }

-- (02/11) en este Sider io.open devuelve nil sin error en los eventos del partido (tambien a otros modulos):
-- si falla se escribe con las funciones de Windows por medio de ffi (necesita luajit.ext.enabled = 1)
local K -- kernel32, se prepara la primera vez
local function ffi_setup()
    if K ~= nil then return K end
    K = false
    if type(ffi) ~= "table" or type(ffi.cdef) ~= "function" then return K end
    -- sin pcall: el entorno de los modulos de Sider no lo tiene (igual que edit21_db)
    ffi.cdef[[
        int fxm_MultiByteToWideChar(unsigned int cp, unsigned long flags, const char *s, int n, uint16_t *w, int wn) __asm__("MultiByteToWideChar");
        void *fxm_CreateFileW(const uint16_t *name, unsigned long access, unsigned long share, void *sec, unsigned long disp, unsigned long flags, void *tmpl) __asm__("CreateFileW");
        int fxm_WriteFile(void *h, const char *buf, unsigned long n, unsigned long *written, void *ov) __asm__("WriteFile");
        int fxm_CloseHandle(void *h) __asm__("CloseHandle");
        unsigned long fxm_GetLastError(void) __asm__("GetLastError");
    ]]
    K = ffi.C
    return K
end

local function ffi_write(path, text)
    if not ffi_setup() then return false, "sin ffi" end
    local n = K.fxm_MultiByteToWideChar(65001, 0, path, -1, nil, 0)
    local wbuf = ffi.new("uint16_t[?]", n + 1)
    K.fxm_MultiByteToWideChar(65001, 0, path, -1, wbuf, n)
    local h = K.fxm_CreateFileW(wbuf, 0x40000000, 1, nil, 2, 0x80, nil)   -- GENERIC_WRITE, CREATE_ALWAYS
    if ffi.cast("intptr_t", h) == -1 then return false, "CreateFileW error " .. tostring(K.fxm_GetLastError()) end
    local wr = ffi.new("unsigned long[1]")
    local ok = K.fxm_WriteFile(h, text, #text, wr, nil)
    K.fxm_CloseHandle(h)
    if ok == 0 then return false, "WriteFile fallo" end
    return true
end

-- (08/11) escudos por equipo: modules\PES21FX_escudos.txt ("id_equipo, 0|1  ; nombre": 1 = bordado, 0 = impreso /
-- termoaplicado). Se lee una vez al iniciar (en los eventos del partido io.open puede fallar). Sin dato = el shader estima.
local crest = {}
local function load_crests(ctx)
    local sd = ctx and ctx.sider_dir and tostring(ctx.sider_dir) or ""
    if sd ~= "" and sd:sub(-1) ~= "\\" and sd:sub(-1) ~= "/" then sd = sd .. "\\" end
    local f = io.open(sd .. "modules\\PES21FX_escudos.txt", "rb")
    if not f then f = io.open("modules\\PES21FX_escudos.txt", "rb") end
    if not f then log("PES21FX_match: sin PES21FX_escudos.txt (los escudos se estiman por la textura)"); return end
    local n = 0
    for line in f:lines() do
        local id, v = line:match("^%s*(%d+)%s*,%s*([01])")
        if id then crest[tonumber(id)] = tonumber(v); n = n + 1 end
    end
    f:close()
    log(string.format("PES21FX_match: %d equipos con escudo bordado / impreso", n))
end

-- colores de la camiseta elegida (kitserver): "#rrggbb" o "" si no hay
local function kit_colors(ctx, side)
    if type(ctx.kits) ~= "table" or type(ctx.kits.get_current_kit_id) ~= "function" or type(ctx.kits.get) ~= "function" then return "", "" end
    local team = side == 0 and ctx.home_team or ctx.away_team
    if team == nil then return "", "" end
    local kid = ctx.kits.get_current_kit_id(side)
    if kid == nil then return "", "" end
    local cfg = ctx.kits.get(team, kid)
    if type(cfg) ~= "table" then return "", "" end
    local c1 = cfg.ShirtColor1 or cfg.UniColor_Color1 or ""
    local c2 = cfg.ShirtColor2 or cfg.UniColor_Color2 or ""
    return tostring(c1), tostring(c2)
end

local function num(v, def)
    local n = tonumber(v)
    if n == nil then return def end
    return n
end

local function write_match(ctx, o)
    o = o or {}
    local stadium  = num(o.stadium,   num(ctx.stadium, keep.stadium))
    -- (02/11) after_set_conditions llega sin opciones: se toman de ctx y, si no estan, se conservan las del evento anterior
    -- (antes la segunda escritura pisaba hora / clima / estacion con -1)
    local tod      = num(o.timeofday, num(ctx.timeofday, keep.tod))       -- 0 = dia, 1 = noche
    local weather  = num(o.weather,   num(ctx.weather,   keep.weather))   -- 0 = despejado, 1 = lluvia
    local season   = num(o.season,    num(ctx.season,    keep.season))    -- 0 = verano, 1 = invierno
    if stadium >= 0 then keep.stadium = stadium end
    if tod >= 0 then keep.tod = tod end
    if weather >= 0 then keep.weather = weather end
    if season >= 0 then keep.season = season end
    local tid      = num(ctx.tournament_id, -1)
    local home, away = num(ctx.home_team, -1), num(ctx.away_team, -1)
    local h1, h2 = kit_colors(ctx, 0)
    local a1, a2 = kit_colors(ctx, 1)
    local text = string.format(
        "[Match]\r\nstadium = %d\r\ntimeofday = %d\r\nweather = %d\r\nseason = %d\r\ntournament = %d\r\nhome = %d\r\naway = %d\r\n" ..
        "home_crest = %d\r\naway_crest = %d\r\nhome_color1 = %s\r\nhome_color2 = %s\r\naway_color1 = %s\r\naway_color2 = %s\r\n",
        stadium, tod, weather, season, tid, home, away,
        crest[home] or -1, crest[away] or -1, h1, h2, a1, a2)
    if text == last then return end
    -- Sider solo deja escribir dentro de su propia carpeta: se escribe ahi (PES21FX_match.ini) y el mod la busca
    -- (busca la carpeta que tenga sider.ini al lado del juego). Las otras rutas quedan por si alguna version lo permite.
    local paths = {}
    if ctx.sider_dir then
        local sd = tostring(ctx.sider_dir)
        if sd:sub(-1) ~= "\\" and sd:sub(-1) ~= "/" then sd = sd .. "\\" end
        table.insert(paths, sd .. "PES21FX_match.ini")
        table.insert(paths, sd .. "..\\PES21FX\\match.ini")
    end
    table.insert(paths, "PES21FX\\match.ini")
    local errs = {}
    for _, p in ipairs(paths) do
        local f, err = io.open(p, "wb")
        if f then
            f:write(text); f:close(); last = text
            log(string.format("PES21FX_match: estadio %d, dia/noche %d, clima %d -> %s", stadium, tod, weather, p))
            return
        end
        table.insert(errs, tostring(p) .. " (" .. tostring(err) .. ")")
    end
    for _, p in ipairs(paths) do
        local ok, err = ffi_write(p, text)
        if ok then
            last = text
            log(string.format("PES21FX_match: estadio %d, dia/noche %d, clima %d, estacion %d -> %s (ffi)", stadium, tod, weather, season, p))
            return
        end
        table.insert(errs, tostring(p) .. " (ffi: " .. tostring(err) .. ")")
    end
    log("PES21FX_match: no pude escribir match.ini. sider_dir=" .. tostring(ctx.sider_dir) .. " intentos: " .. table.concat(errs, " | "))
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

function m.set_kits(ctx)
    write_match(ctx, nil)   -- (08/11) colores de la camiseta elegida
    return nil
end

function m.init(ctx)
    load_crests(ctx)
    ctx.register("set_kits", m.set_kits)
    ctx.register("set_stadium", m.set_stadium)
    ctx.register("set_conditions", m.set_conditions)
    ctx.register("after_set_conditions", m.after_set_conditions)
    log("PES21FX_match: listo (presets por estadio)")
end

return m
