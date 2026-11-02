--[[
    PES21 FX - puente con Sider (presets por estadio)
    Le avisa al mod grafico (d3d11.dll de PES21 FX) en que estadio y con que condiciones se juega,
    escribiendo PES21FX\match.ini. El mod lo lee y aplica PES21FX\presets\stadium_<id>.ini si existe.
    Instalacion: copiar a la carpeta modules de Sider y agregar en sider.ini:
        lua.module = "PES21FX_match.lua"
]]

local m = {}
local last = ""

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

function m.init(ctx)
    ctx.register("set_stadium", m.set_stadium)
    ctx.register("set_conditions", m.set_conditions)
    ctx.register("after_set_conditions", m.after_set_conditions)
    log("PES21FX_match: listo (presets por estadio)")
end

return m
