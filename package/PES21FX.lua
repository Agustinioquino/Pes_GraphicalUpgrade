-- PES21 FX: mod grafico para PES 2021 (reemplaza a 3DMigoto).
-- Este modulo solo carga PES21FX.dll desde modules\PES21FX\ y le dice donde esta su carpeta.
-- Necesita en sider.ini:  luajit.ext.enabled = 1   y   lua.module = "PES21FX.lua"
-- Sider da "ffi" como global y no tiene require ni pcall: se usa LoadLibrary de Windows directo.

local m = { version = "0.1.2" }
local keep  -- referencia para que la DLL no se descargue nunca

function m.init(ctx)
    if type(ffi) ~= "table" or type(ffi.cdef) ~= "function" then
        log("PES21FX: ffi no disponible (poner luajit.ext.enabled = 1 en sider.ini)")
        return
    end
    -- nombres propios (fx21_) para no chocar con otros modulos que declaren las mismas funciones
    ffi.cdef[[
        void* fx21_LoadLibraryA(const char* name) __asm__("LoadLibraryA");
        void* fx21_GetProcAddress(void* mod, const char* name) __asm__("GetProcAddress");
        unsigned long fx21_GetLastError(void) __asm__("GetLastError");
    ]]
    local K = ffi.C
    local dir = ctx.sider_dir .. "modules\\PES21FX\\"
    local h = K.fx21_LoadLibraryA(dir .. "PES21FX.dll")
    if h == nil then
        log("PES21FX: no se pudo cargar PES21FX.dll (error de Windows " .. tostring(K.fx21_GetLastError()) .. ")")
        return
    end
    local p = K.fx21_GetProcAddress(h, "PES21FX_Init")
    if p == nil then
        log("PES21FX: la DLL no tiene PES21FX_Init")
        return
    end
    local init = ffi.cast("int (*)(const char*)", p)
    keep = { h, init }
    m.keep = keep
    local r = tonumber(init(dir))
    if r == 2 then
        log("PES21FX " .. m.version .. ": activo (log en modules\\PES21FX\\PES21FX.log, menu con Insert)")
    else
        log("PES21FX: fallo al iniciar (codigo " .. tostring(r) .. "), mira modules\\PES21FX\\PES21FX.log")
    end
end

return m
