-- PES21 FX: mod grafico para PES 2021 (reemplaza a 3DMigoto).
-- Este modulo solo carga PES21FX.dll desde modules\PES21FX\ y le dice donde esta su carpeta.
-- Necesita en sider.ini:  luajit.ext.enabled = 1   y   lua.module = "PES21FX.lua"
-- En Sider "ffi" es una variable global (no hay require).
local ffi = ffi
if type(ffi) ~= "table" and type(require) == "function" then ffi = require("ffi") end

local m = { version = "0.1.1" }
local keep  -- referencia a la DLL para que no se descargue nunca

local function load_dll(path)
    -- 1) ffi.load si existe
    if type(ffi.load) == "function" then
        local ok, lib = pcall(ffi.load, path)
        if ok and lib then
            local ok2, fn = pcall(function() return lib.PES21FX_Init end)
            if ok2 and fn then return lib, fn end
        end
    end
    -- 2) LoadLibraryA + GetProcAddress
    pcall(ffi.cdef, [[
        void* LoadLibraryA(const char* name);
        void* GetProcAddress(void* mod, const char* name);
        unsigned long GetLastError(void);
    ]])
    local h = ffi.C.LoadLibraryA(path)
    if h == nil then return nil, "LoadLibrary fallo (error " .. tostring(ffi.C.GetLastError()) .. ")" end
    local p = ffi.C.GetProcAddress(h, "PES21FX_Init")
    if p == nil then return nil, "no se encontro PES21FX_Init" end
    return h, ffi.cast("int (*)(const char*)", p)
end

function m.init(ctx)
    if type(ffi) ~= "table" then
        log("PES21FX: ffi no disponible (poner luajit.ext.enabled = 1 en sider.ini)")
        return
    end
    local dir = ctx.sider_dir .. "modules\\PES21FX\\"
    local lib, init = load_dll(dir .. "PES21FX.dll")
    if not lib then
        log("PES21FX: no se pudo cargar PES21FX.dll: " .. tostring(init))
        return
    end
    keep = { lib, init }
    m.keep = keep
    local r = tonumber(init(dir))
    if r == 2 then
        log("PES21FX " .. m.version .. ": activo (log en modules\\PES21FX\\PES21FX.log, menu con Insert)")
    else
        log("PES21FX: fallo al iniciar (codigo " .. tostring(r) .. "), mira modules\\PES21FX\\PES21FX.log")
    end
end

return m
