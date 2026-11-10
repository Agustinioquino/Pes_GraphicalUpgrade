-- Rastro v4: carga la DLL de investigacion (PES21FX\rastro_v4.dll) dentro del juego. Solo eso.
-- Instalar: copiar a "Dream Patch Sider\modules" y agregar en sider.ini:  lua.module = "rastro_v4.lua"
-- Quitar: comentar esa linea. Necesita luajit.ext.enabled = 1 (ffi).
local m = {}
local ffi = require("ffi")
ffi.cdef[[
void* LoadLibraryA(const char* path);
void* GetModuleHandleA(const char* name);
unsigned long GetModuleFileNameA(void* mod, char* buf, unsigned long size);
unsigned long GetLastError(void);
]]

function m.init(ctx)
    if ffi.C.GetModuleHandleA("rastro_v4.dll") ~= nil then
        log("rastro_v4: la DLL ya estaba cargada")
        return
    end
    local buf = ffi.new("char[?]", 520)
    local n = ffi.C.GetModuleFileNameA(nil, buf, 520)
    local exe = ffi.string(buf, n)
    local dir = exe:match("^(.*)[\\/][^\\/]*$") or "."
    local path = dir .. "\\PES21FX\\rastro_v4.dll"
    local h = ffi.C.LoadLibraryA(path)
    if h == nil then
        log("rastro_v4: no pude cargar " .. path .. " (error " .. tostring(ffi.C.GetLastError()) .. ")")
    else
        log("rastro_v4: DLL de investigacion cargada desde " .. path)
    end
end

return m
