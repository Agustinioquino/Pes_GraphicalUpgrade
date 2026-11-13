-- PES21FX_redes.lua 1.0 (13/11/2026) - fisica de las redes de los arcos, personalizable, para PES 2021 1.07.02 con Sider.
-- Reemplaza a reactive_nets_physics.lua (Godmode_ON / DevPlays): trae sus valores como preset "reactiva" y suma otros.
-- NO usar los dos a la vez: comentar  lua.module = "reactive_nets_physics.lua"  en sider.ini.
--
-- Como mueve la red el juego (lectura del exe: decompilado/hallazgos/20261113_redes_viento_exe.cpp.md):
--   cada paso de 1/60 s, cada punto suelto: velocidad = velocidad * conservar (* golpe si lo arrastro la pelota) - peso * dt,
--   despues "pasadas" vueltas de hilos (no se dejan estirar mas que largo * largo_hilos, con su blandura) y la pelota
--   (que empuja segun la zona del arco). Al cargar la red, cada hilo toma su largo de reposo = distancia * holgura y su
--   rigidez (rigidez_*). Este modulo cambia esos numeros en memoria (bloque de 40 floats en 0x143525550 + 2 valores mas).
-- Los valores marcados "carga" se usan al cargar la red: rigen desde el partido siguiente. Los demas cambian en vivo.
-- El viento en la red y el despertarla cuando se duerme los hace la DLL de PES21FX (redes.cpp): Lua no puede entrar en
-- cada punto 60 veces por segundo.
--
-- Teclas (con el overlay de Sider abierto en este modulo): 7 preset anterior, 8 preset siguiente (hasta el partido siguiente),
-- 9 releer PES21FX_redes.ini. En el ini: preset general, preset por estadio ([estadios] id = preset), presets propios
-- ([preset nombre] con base = otro y los valores a cambiar), [personal] y red mojada con lluvia (lluvia = 1).

local m = { version = "1.0" }

-- nombre, desplazamiento desde el comienzo del bloque, valor del juego, "vivo" / "carga", tipo
local P = {
    { "conservar",          0x04, 0.95,   "vivo" },   -- velocidad que se conserva por paso (mas = la onda dura mas)
    { "peso",               0x08, 6.0,    "vivo" },   -- m/s2 hacia abajo (mas = cuelga y vuelve con cuerpo)
    { "golpe",              0x0c, 1.0,    "vivo" },   -- lo que conserva un punto que arrastro la pelota (menos = el golpe se apaga)
    { "freno_desde",        0x10, 10.0,   "vivo" },   -- saltos de velocidad (m2/s2) desde donde se frenan
    { "freno_hasta",        0x14, 50.0,   "vivo" },   -- ... y hasta donde (freno completo)
    { "largo_hilos",        0x3c, 1.0,    "vivo" },   -- multiplica el largo de reposo de todos los hilos
    { "largo_hilos_pelota", 0x40, 0.8,    "vivo" },   -- largo de los hilos junto a un punto fijo o tocado (menos = tira mas)
    { "largo_hilos_pelota2",0x44, 1.0,    "vivo" },   -- idem, el otro lado del hilo
    { "inversa_masa",       0x48, 100.0,  "vivo" },   -- cuanto se corrige un punto comun (mas = mas liviano)
    { "inversa_masa_fijo",  0x4c, 10.0,   "vivo" },   -- punto que sigue al arco
    { "inversa_masa_otro",  0x50, 10.0,   "vivo" },   -- otro tipo de punto
    { "blandura",           0x54, 1.0,    "vivo" },   -- blandura de los hilos (mas = mas elasticos, la onda viaja)
    { "blandura_pelota",    0x58, 0.0001, "vivo" },   -- blandura junto a un punto fijo o tocado
    { "blandura_pelota2",   0x5c, 0.001,  "vivo" },   -- idem, el otro caso
    { "rigidez_max",        0x6c, 0.95,   "carga" },  -- tope de rigidez de un hilo
    { "rigidez_atado",      0x70, 0.3,    "carga" },  -- rigidez de un hilo atado al arco (por largo)
    { "rigidez_suelto",     0x74, 2.2,    "carga" },  -- rigidez de un hilo suelto (por largo)
    { "rigidez_min",        0x78, 0.02,   "carga" },  -- rigidez minima
    { "holgura",            0x7c, 1.0,    "carga" },  -- largo de reposo = distancia del modelo * holgura (mas = red mas suelta)
    { "zona_fondo",         0x84, 0.8,    "vivo" },   -- la pelota empuja menos cerca del fondo del arco (fraccion)
    { "zona_ancho",         0x88, 0.2,    "vivo" },   -- ... cerca de los palos
    { "zona_alto",          0x8c, 0.5,    "vivo" },   -- ... cerca del travesano
    { "pelota_mezcla_a",    0x90, 0.1,    "vivo" },   -- choque pelota-red (mezcla)
    { "pelota_mezcla_b",    0x94, 0.8,    "vivo" },
    { "pelota_margen2",     0x98, 0.02,   "vivo" },   -- margen del choque (m)
    { "pelota_margen",      0x9c, 0.05,   "vivo" },   -- radio extra de la pelota contra la red (m)
    { "pasadas",           -0x84c, 2,     "vivo", "u32" },   -- vueltas de hilos + pelota por paso (mas = mas tensa, mas CPU)
    { "estiramiento",      -0x850, 1.2,   "vivo" },   -- un hilo no se estira mas que esto (veces su largo)
}
local BLOCK = 0x143525550
local NB = 160   -- 40 floats

-- presets: cambios sobre los valores del juego
local REACTIVA = { conservar = 0.99, peso = 0.25, golpe = 0.3, largo_hilos_pelota = 0.2, largo_hilos_pelota2 = 0.9,
    blandura = 4.0, blandura_pelota = 1.5, blandura_pelota2 = 1.0, holgura = 1.05, pelota_margen = 0.0 }
local function con(base, extra) local t = {} for k, v in pairs(base) do t[k] = v end for k, v in pairs(extra) do t[k] = v end return t end
local PRESETS = {
    { "juego",    {} },
    { "reactiva", REACTIVA },                                                         -- = reactive_nets_physics.lua 1.0
    { "viva",     con(REACTIVA, { peso = 0.6, golpe = 0.6, pasadas = 3, estiramiento = 1.3 }) },   -- el gol ondula mas y vuelve
    { "tv",       con(REACTIVA, { conservar = 0.97, peso = 1.5, golpe = 0.7, holgura = 1.02, pasadas = 3 }) },   -- mas pesada y tensa
    { "personal", nil },                                                              -- [personal] del ini
}

local st = { addr = nil, outOk = false, preset = "reactiva", personal = {}, personalBase = "reactiva", last = "", found = "",
    byStadium = {}, rainOn = true, stadium = -1, weather = -1, active = "reactiva", manual = false }

local function sider_dir(ctx)
    local sd = ctx and ctx.sider_dir and tostring(ctx.sider_dir) or ""
    if sd ~= "" and sd:sub(-1) ~= "\\" and sd:sub(-1) ~= "/" then sd = sd .. "\\" end
    return sd
end

local function preset_values(name)
    local t
    for _, pr in ipairs(PRESETS) do if pr[1] == name then t = pr[2] end end
    if st.extra and st.extra[name] then t = st.extra[name] end
    if name == "personal" then
        local base = {}
        for _, pr in ipairs(PRESETS) do if pr[1] == st.personalBase and pr[2] then base = pr[2] end end
        t = con(base, st.personal)
    end
    if not t then return nil end
    local v = {}
    for _, p in ipairs(P) do v[p[1]] = p[3] end
    for k, x in pairs(t) do if v[k] ~= nil then v[k] = x end end
    -- red mojada (lluvia del partido): el nylon empapado pesa mas y frena un poco mas rapido
    if st.rainOn and st.weather == 1 then
        v.peso = v.peso * 1.35; v.conservar = v.conservar - (1.0 - v.conservar) * 0.3; v.golpe = v.golpe * 0.9
    end
    return v
end

-- patron de 160 bytes de un preset sobre el bloque original (los floats sin nombre se toman de 'orig')
local function pattern(vals, orig)
    local out = {}
    local named = {}
    for _, p in ipairs(P) do if p[2] >= 0 then named[p[2]] = vals[p[1]] end end
    for o = 0, NB - 4, 4 do
        local v = named[o]
        out[#out + 1] = v and memory.pack("f", v) or orig:sub(o + 1, o + 4)
    end
    return table.concat(out)
end

local function read_ini(ctx)
    local f = io.open(sider_dir(ctx) .. "modules\\PES21FX_redes.ini", "rb")
    if not f then f = io.open("modules\\PES21FX_redes.ini", "rb") end
    if not f then log("PES21FX_redes: sin modules\\PES21FX_redes.ini (preset reactiva)"); return end
    local sec = ""
    st.personal = {}; st.byStadium = {}; st.extra = {}; st.extraBase = {}
    for line in f:lines() do
        line = line:gsub(";.*$", ""):gsub("^%s+", ""):gsub("%s+$", "")
        local s = line:match("^%[(.-)%]$")
        if s then sec = s:lower()
        else
            local k, v = line:match("^([%w_]+)%s*=%s*(.-)$")
            if k then
                k = k:lower()
                if sec == "" and k == "preset" then st.preset = v:lower()
                elseif sec == "" and k == "lluvia" then st.rainOn = tonumber(v) ~= 0
                elseif sec == "estadios" then local id = tonumber(k); if id then st.byStadium[id] = v:lower() end
                elseif sec:match("^preset ") then
                    local nm = sec:sub(8):gsub("%s", ""); local n = tonumber(v)
                    st.extra = st.extra or {}; st.extra[nm] = st.extra[nm] or {}
                    if k == "base" then st.extraBase = st.extraBase or {}; st.extraBase[nm] = v:lower() elseif n then st.extra[nm][k] = n end
                elseif sec == "personal" then
                    if k == "base" then st.personalBase = v:lower() else local n = tonumber(v); if n then st.personal[k] = n end end
                end
            end
        end
    end
    f:close()
    -- presets propios del ini ([preset nombre] con base = otro preset): se suman a la lista
    for nm, vals in pairs(st.extra) do
        local base = {}
        local bn = st.extraBase[nm] or "reactiva"
        for _, pr in ipairs(PRESETS) do if pr[1] == bn and pr[2] then base = pr[2] end end
        st.extra[nm] = con(base, vals)
        local have = false
        for _, pr in ipairs(PRESETS) do if pr[1] == nm then have = true end end
        if not have then PRESETS[#PRESETS + 1] = { nm, st.extra[nm] } end
    end
end

local function valid_name(n) for _, pr in ipairs(PRESETS) do if pr[1] == n then return true end end return false end

local orig   -- 160 bytes del juego (para los floats sin nombre)

local function apply(name)
    if not st.addr then return end
    local v = preset_values(name)
    if not v then log("PES21FX_redes: preset desconocido '" .. tostring(name) .. "'"); return end
    -- segunda comprobacion antes de escribir: el bloque sigue siendo uno de los nuestros (o el del juego / reactiva)
    local now = memory.read(st.addr, NB)
    local okNow = (now == st.last) or (now == orig) or (now == pattern(preset_values("reactiva"), orig))
    if not okNow then log("PES21FX_redes: el bloque de la red cambio por otro lado; no se escribe"); return end
    local blk = pattern(v, orig)
    memory.write(st.addr, blk); st.last = blk
    if st.outOk then
        for _, p in ipairs(P) do
            if p[2] < 0 then
                if p[5] == "u32" then memory.write(st.addr + p[2], memory.pack("u32", math.floor(v[p[1]] + 0.5)))
                else memory.write(st.addr + p[2], memory.pack("f", v[p[1]])) end
            end
        end
    end
    st.active = name
    log(string.format("PES21FX_redes: preset %s%s (peso %.2f, conservar %.3f, golpe %.2f, holgura %.2f, pasadas %d, estiramiento %.2f)%s",
        name, (st.rainOn and st.weather == 1) and " mojada" or "", v.peso, v.conservar, v.golpe, v.holgura, math.floor(v.pasadas + 0.5), v.estiramiento, st.outOk and "" or " (pasadas / estiramiento sin tocar)"))
end

local function find_block()
    local vanilla = {}
    for _, p in ipairs(P) do vanilla[p[1]] = p[3] end
    -- bloque del juego: los 160 bytes tal como los busca reactive_nets_physics.lua
    local game = "\xcd\xcc\xcc\x3d\x33\x33\x73\x3f\x00\x00\xc0\x40\x00\x00\x80\x3f\x00\x00\x20\x41\x00\x00\x48\x42\x6f\x12\x83\x3a\x00\x00\x00\x00\x00\x00\x00\x42\x00\x00\x80\x3f\x00\x00\x80\x3f\x00\x00\x80\x3e\xcd\xcc\xcc\x3d\xcd\xcc\x4c\x3d\x00\x00\x80\x3e\x00\x00\x80\x3f\xcd\xcc\x4c\x3f\x00\x00\x80\x3f\x00\x00\xc8\x42\x00\x00\x20\x41\x00\x00\x20\x41\x00\x00\x80\x3f\x17\xb7\xd1\x38\x6f\x12\x83\x3a\x00\x00\x80\x3f\x00\x00\x80\x3f\x00\x00\x80\x3f\x33\x33\x73\x3f\x9a\x99\x99\x3e\xcd\xcc\x0c\x40\x0a\xd7\xa3\x3c\x00\x00\x80\x3f\x00\x00\x00\x40\xcd\xcc\x4c\x3f\xcd\xcc\x4c\x3e\x00\x00\x00\x3f\xcd\xcc\xcc\x3d\xcd\xcc\x4c\x3f\x0a\xd7\xa3\x3c\xcd\xcc\x4c\x3d"
    orig = game
    local a = memory.search_process(game)
    if a then st.found = "el del juego"
    else
        a = memory.search_process(pattern(preset_values("reactiva"), game))
        if a then st.found = "el de reactive_nets_physics.lua (ya escrito)" end
    end
    if not a then log("PES21FX_redes: no se encontro el bloque de la fisica de la red (otro mod lo cambio?): no se toca nada"); return end
    st.addr = a
    -- pasadas y estiramiento: solo si el bloque esta donde lo pone el exe 1.07.02 y esos dos valores son los del juego
    local base = tonumber(a) or a
    if base == BLOCK then
        local s = memory.read(a - 0x850, 8)
        local e, n = memory.unpack("f", s:sub(1, 4)), memory.unpack("u32", s:sub(5, 8))
        st.outOk = (math.abs(e - 1.2) < 1e-6) and n == 2
    end
    log(string.format("PES21FX_redes %s: bloque encontrado en %s (%s)%s", m.version, memory.hex(a), st.found,
        st.outOk and "" or "; pasadas / estiramiento no coinciden: no se tocan"))
end

local function step_preset(d)
    local i = 1
    for k, pr in ipairs(PRESETS) do if pr[1] == st.active then i = k end end
    i = (i - 1 + d) % #PRESETS + 1
    st.manual = true
    apply(PRESETS[i][1])
end

function m.overlay_on(ctx)
    local v = preset_values(st.active) or {}
    return string.format("PES21FX Redes %s | preset: %s%s%s | peso %.2f, conservar %.3f, golpe %.2f, holgura %.2f | [7] anterior [8] siguiente [9] releer ini%s",
        m.version, st.active, (st.rainOn and st.weather == 1) and " (mojada)" or "", st.byStadium[st.stadium] and (" (estadio " .. st.stadium .. ")") or "", v.peso or 0, v.conservar or 0, v.golpe or 0, v.holgura or 0, st.addr and "" or " | SIN BLOQUE")
end

function m.key_up(ctx, vkey)
    if not st.addr then return end
    if vkey == 0x37 then step_preset(-1)
    elseif vkey == 0x38 then step_preset(1)
    elseif vkey == 0x39 then read_ini(ctx); if not valid_name(st.preset) then st.preset = "reactiva" end; st.manual = false; m.pick() end
end

-- preset que toca: el del estadio ([estadios] del ini) o el general. Al empezar cada partido vuelve a esto (las teclas 7 / 8
-- valen hasta el partido siguiente). Se escribe en set_stadium, ANTES de que cargue la red: holgura y rigidez rigen ya.
function m.pick()
    local nm = st.byStadium[st.stadium] or st.preset
    if not valid_name(nm) then log("PES21FX_redes: el preset '" .. tostring(nm) .. "' del estadio " .. st.stadium .. " no existe"); nm = st.preset end
    apply(nm)
end
function m.set_stadium(ctx, o)
    if type(o) == "table" and o.stadium then st.stadium = tonumber(o.stadium) or -1 end
    st.manual = false; m.pick()
end
function m.set_conditions(ctx, o)
    if type(o) == "table" and o.weather then st.weather = tonumber(o.weather) or -1 end
    if not st.manual then m.pick() end
end

function m.init(ctx)
    read_ini(ctx)
    if not valid_name(st.preset) then log("PES21FX_redes: preset '" .. st.preset .. "' no existe, uso reactiva"); st.preset = "reactiva" end
    find_block()
    if st.addr then st.last = memory.read(st.addr, NB); apply(st.preset) end
    ctx.register("overlay_on", m.overlay_on)
    ctx.register("key_up", m.key_up)
    ctx.register("set_stadium", m.set_stadium)
    ctx.register("set_conditions", m.set_conditions)
end

return m
