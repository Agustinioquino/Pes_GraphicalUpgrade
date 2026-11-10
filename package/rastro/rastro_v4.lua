-- Rastro v4: carga la DLL de investigacion (PES21FX\rastro_v4.dll) dentro del juego. Solo eso.
-- Instalar: copiar a "Dream Patch Sider\modules" y agregar en sider.ini:  lua.module = "rastro_v4.lua"
-- Quitar: comentar esa linea. Necesita luajit.ext.enabled = 1 (ffi).
-- (10/11) este Sider no da 'require' ni 'pcall' a los modulos: se usa el ffi global. Los nombres llevan prefijo propio
-- (__asm__ apunta al simbolo real) para no chocar con declaraciones de otros modulos.
local m = {}
local ffi = ffi
if not ffi and require then ffi = require("ffi") end

function m.init(ctx)
    if not ffi then
        log("rastro_v4: este Sider no tiene ffi (luajit.ext.enabled = 1?); DLL no cargada")
        return
    end
    ffi.cdef[[
    void* rv4_LoadLibraryA(const char* path) __asm__("LoadLibraryA");
    void* rv4_GetModuleHandleA(const char* name) __asm__("GetModuleHandleA");
    unsigned long rv4_GetModuleFileNameA(void* mod, char* buf, unsigned long size) __asm__("GetModuleFileNameA");
    unsigned long rv4_GetLastError(void) __asm__("GetLastError");
    ]]
    local C = ffi.C
    if C.rv4_GetModuleHandleA("rastro_v4.dll") ~= nil then
        log("rastro_v4: la DLL ya estaba cargada")
        return
    end
    local buf = ffi.new("char[?]", 520)
    local n = C.rv4_GetModuleFileNameA(nil, buf, 520)
    local exe = ffi.string(buf, n)
    local dir = exe:match("^(.*)[\\/][^\\/]*$") or "."
    local path = dir .. "\\PES21FX\\rastro_v4.dll"
    local h = C.rv4_LoadLibraryA(path)
    if h == nil then
        log("rastro_v4: no pude cargar " .. path .. " (error " .. tostring(C.rv4_GetLastError()) .. ")")
    else
        log("rastro_v4: DLL de investigacion cargada desde " .. path)
    end
end

return m
