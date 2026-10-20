--[[
    PES21 FX - volcador de memoria del exe (20/10). El PES2021.exe en disco esta empaquetado: para analizar el codigo
    del motor (distancias de sombras, LOD, streaming) hace falta leerlo ya desempaquetado, en memoria, con el juego corriendo.
    Este modulo, en el primer evento de Sider (al cargar), lee las secciones del exe y las escribe en la carpeta de Sider:
        PES21FX_dump\<seccion>.bin   +   PES21FX_dump\dump.txt (base, secciones, md5 no)
    Despues se puede quitar de sider.ini: solo hace falta una vez por version del juego.
    Instalacion: copiar a la carpeta modules de Sider y agregar en sider.ini:
        lua.module = "PES21FX_dump.lua"
]]

local m = {}
local done = false

local function u16(s, i) local a, b = s:byte(i, i + 1); return a + b * 256 end
local function u32(s, i) local a, b, c, d = s:byte(i, i + 3); return a + b * 256 + c * 65536 + d * 16777216 end

local function find_base()
    -- 1) base clasica sin ASLR
    local cands = { 0x140000000 }
    -- 2) Sider expone a veces la base del exe
    if memory and memory.get_base then local ok, b = pcall(memory.get_base); if ok and b then table.insert(cands, 1, b) end end
    for _, b in ipairs(cands) do
        local ok, s = pcall(memory.read, b, 2)
        if ok and s == "MZ" then return b end
    end
    -- 3) buscar la cabecera PE del exe en el rango tipico de carga (64 KB alineado)
    local ok, addr = pcall(memory.search, "MZ\x90\x00\x03\x00\x00\x00\x04\x00\x00\x00\xff\xff", 0x140000000, 0x150000000)
    if ok and addr then return addr end
    ok, addr = pcall(memory.search, "MZ\x90\x00\x03\x00\x00\x00\x04\x00\x00\x00\xff\xff", 0x7ff600000000, 0x7ff800000000)
    if ok and addr then return addr end
    return nil
end

local function dump(ctx)
    if done then return end
    done = true
    local base = find_base()
    if not base then log("PES21FX_dump: no encontre la base del exe"); return end
    local dir = tostring(ctx.sider_dir or ".")
    if dir:sub(-1) ~= "\\" and dir:sub(-1) ~= "/" then dir = dir .. "\\" end
    dir = dir .. "PES21FX_dump\\"
    os.execute('mkdir "' .. dir .. '" >nul 2>nul')
    local hdr = memory.read(base, 0x1000)
    local pe = u32(hdr, 0x3c + 1)
    if hdr:sub(pe + 1, pe + 4) ~= "PE\0\0" then log("PES21FX_dump: cabecera PE rara"); return end
    local nsec = u16(hdr, pe + 6 + 1)
    local optsz = u16(hdr, pe + 20 + 1)
    local sec0 = pe + 24 + optsz
    local info = { string.format("base = %X\r\n", base) }
    for i = 0, nsec - 1 do
        local o = sec0 + i * 40 + 1
        local name = hdr:sub(o, o + 7):gsub("%z", "")
        local vsize, va = u32(hdr, o + 8), u32(hdr, o + 12)
        local rawsz = u32(hdr, o + 16)
        local size = vsize
        if size > 0x4000000 then   -- .debug$P y similares: enormes y sin interes
            table.insert(info, string.format("%s va=%X size=%X (omitida)\r\n", name, va, size))
        else
            local fname = dir .. name:gsub("[^%w_]", "_") .. ".bin"
            local f = io.open(fname, "wb")
            if f then
                local off = 0
                while off < size do
                    local n = math.min(0x100000, size - off)
                    local ok, s = pcall(memory.read, base + va + off, n)
                    if not ok or not s then s = string.rep("\0", n) end
                    f:write(s); off = off + n
                end
                f:close()
                table.insert(info, string.format("%s va=%X size=%X raw=%X -> %s\r\n", name, va, size, rawsz, fname))
            else
                table.insert(info, string.format("%s va=%X size=%X (no pude escribir %s)\r\n", name, va, size, fname))
            end
        end
    end
    local f = io.open(dir .. "dump.txt", "wb")
    if f then f:write(table.concat(info)); f:close() end
    log("PES21FX_dump: listo en " .. dir .. " (" .. nsec .. " secciones)")
end

function m.init(ctx)
    -- en el primer evento disponible; set_stadium llega al entrar al partido (el exe ya esta desempaquetado)
    ctx.register("set_stadium", function(c, o) dump(c); return nil end)
    ctx.register("set_conditions", function(c, o) dump(c); return nil end)
    ctx.register("after_set_conditions", function(c, o) dump(c) end)
    -- si Sider soporta overlay_on o key_down, tambien al tocar cualquier cosa
    if ctx.register then pcall(ctx.register, "key_down", function(c, vk) dump(c) end) end
end

return m
