-- PES21 FX: mod grafico para PES 2021 (reemplaza a 3DMigoto).
-- Este modulo solo carga PES21FX.dll desde modules\PES21FX\ y le dice donde esta su carpeta.
-- Necesita en sider.ini:  luajit.ext.enabled = 1   y   lua.module = "PES21FX.lua"
local ffi = require("ffi")
ffi.cdef[[ int PES21FX_Init(const char* baseDir); ]]

local m = { version = "0.1" }
local lib  -- se guarda la referencia para que la DLL no se descargue nunca

function m.init(ctx)
    local dir = ctx.sider_dir .. "modules\\PES21FX\\"
    local ok, l = pcall(ffi.load, dir .. "PES21FX.dll")
    if not ok then
        log("PES21FX: no se pudo cargar PES21FX.dll: " .. tostring(l))
        return
    end
    lib = l
    m.lib = l
    local r = lib.PES21FX_Init(dir)
    if r == 2 then
        log("PES21FX " .. m.version .. ": activo (log en modules\\PES21FX\\PES21FX.log, menu con Insert)")
    else
        log("PES21FX: fallo al iniciar (codigo " .. tostring(r) .. "), mira modules\\PES21FX\\PES21FX.log")
    end
end

return m
