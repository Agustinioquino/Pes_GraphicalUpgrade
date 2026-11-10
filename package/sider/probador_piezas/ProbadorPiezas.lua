-- ProbadorPiezas v3.0 - PES 2021 1.07.02 / Sider 7  (PES21 FX, 10/11/2026)
-- Previsualizador de modelos (.fmdl) sobre el jugador del menu Editar. Version simple:
-- UNA lista y UN modelo elegido a la vez; al cambiar, el jugador se recarga solo.
--
-- Como entra el modelo (lo decide solo):
--   * PRENDA APARTE (lo normal): usa el mecanismo del propio juego para vestir tecnicos y
--     cinematicas. Le pide al visor del Editar que arme al jugador "de traje" y, cuando el juego
--     pide el modelo del traje, entrega el modelo elegido. Asi el modelo sale con SUS texturas,
--     sin camiseta ni short, y el juego apaga las partes del cuerpo que lo atravesarian.
--   * pieza de uniforme del catalogo (cuello, mangas, short, camiseta): en su lugar natural.
--   * COMO CAMISETA (tecla 6): el metodo viejo; el juego le pinta la textura del kit encima.
-- Los primeros renglones de la lista son el VESTUARIO DEL JUEGO sin modelo del catalogo:
-- pechera sobre el uniforme, tecnico de traje / de camisa / de buzo.
--
-- Teclas (overlay de Sider abierto y este modulo elegido; en tu Sider el overlay se abre con la
-- barra espaciadora y se pasa de modulo con la tecla 1):
--   0 = siguiente        9 = anterior        8 / 7 = grupo siguiente / anterior
--   5 = volver al original                   6 = prenda aparte <-> como camiseta
--   4 = recargar el jugador ahora            3 = sin camiseta (camiseta, cuello y mangas)
--   2 = pagina siguiente de materiales y texturas del modelo
--
-- Que toca: no cambia ningun archivo. Con "prenda aparte" o "pechera" escribe 1 byte en la
-- memoria del visor del Editar (tipo de vestuario) y llama a la funcion del propio visor que
-- pone al jugador, para que lo rearme. Si existe content\probador-piezas\sin_llamada.txt no
-- llama a esa funcion: usa solo la recarga de Sider (la de kserv / kitpos).
--
-- Catalogo: carpeta de la primera linea de content\probador-piezas\catalogo_raiz.txt (o
-- content\probador-piezas\catalogo\), con indice.txt (una ruta relativa por linea; una linea
-- [nombre] abre un grupo) y, al lado de cada modelo, "<modelo>.fmdl.txt" con sus materiales
-- (tecla 2). indice.txt y esos .txt los arma catalogo_indice.py.

local m = { version = "3.0" }
local POR_PAGINA = 14

-- direcciones de PES2021.exe 1.07.02 (base 0x140000000), leidas del decompilado
local EMV_PTR  = 0x1438040d0     -- puntero a edit::ModelView (visor del menu Editar)
local IMPL_VT  = 0x142ba51e0     -- vtable de draw::util::TaskModelView::Impl
local FN_PONER = 0x141eea520     -- Impl: poner jugador (id, tipo, kit, botines, ...)
local O_BIBS, O_SUB = 0x5e, 0xa7 -- Impl: pechera (0/1/2) y vestuario de tecnico (3 = ninguno, 0/1/2)
local O_TIPO, O_ID, O_KIND, O_KIT = 0x370, 0x354, 0x378, 0x388

local DIR, CAT, EMPTY
local lista = {}                 -- { nombre, grupo, ruta (nil = vestuario del juego), sub, bibs }
local sel = 0                    -- 0 = original
local como_camiseta, sin_camiseta, sin_llamada = false, false, false
local sirve, prenda = {}, nil    -- lugar de uniforme -> archivo ; archivo a dar por el traje
local quiero_sub, quiero_bibs, turno = 3, 0, 0
local pedidos, pedido_prenda = {}, 0
local via_txt, nota, recarga_txt = "", "", ""
local info, info_de, pagina = {}, nil, 1

local CAMISETA, CUELLO, MANGAS, SHORT = 1, 2, 3, 4
local NOMBRE = { "camiseta", "cuello", "mangas", "short" }
local PATS = {
    { "^shirt" },
    { "^collar_", "^referee_collar_" },
    { "^sleeve", "^cap_sleeve", "^s_inner" },
    { "^pants_%d", "^pants%.fmdl$" },
}
-- modelos que el juego pide para el vestuario de tecnico (parts\...): ahi entra la prenda aparte
local TRAJES = { ["manager.fmdl"] = true, ["polo.fmdl"] = true, ["training_suit.fmdl"] = true }

local VESTUARIO = {
    { nombre = "pechera de entrenamiento sobre el uniforme", sub = 3, bibs = 1 },
    { nombre = "pechera de suplente sobre el uniforme",      sub = 3, bibs = 2 },
    { nombre = "tecnico: traje",                             sub = 0, bibs = 0 },
    { nombre = "tecnico: camisa",                            sub = 1, bibs = 0 },
    { nombre = "tecnico: buzo de entrenamiento",             sub = 2, bibs = 0 },
}

-------------------------------------------------------------------------------------------
-- archivos
-------------------------------------------------------------------------------------------
local function basename(f)
    return string.lower(string.match(f, "([^\\/]+)$") or f)
end

local function exists(path)
    local f = io.open(path, "rb")
    if f then f:close() return true end
    return false
end

local function lugar_de(b)
    if string.find(b, "_sub", 1, true) then return nil end       -- piezas del cuerpo
    for i = 1, 4 do
        for _, p in ipairs(PATS[i]) do
            if string.match(b, p) then return i end
        end
    end
    return nil
end

-- lugar natural de un modelo del CATALOGO: solo si esta en una carpeta de uniformes
local function lugar_catalogo(ruta)
    local r = "\\" .. string.lower((string.gsub(ruta, "/", "\\")))
    if not string.find(r, "\\uniform\\", 1, true) then return nil end
    return lugar_de(basename(r))
end

-------------------------------------------------------------------------------------------
-- memoria del visor del Editar
-------------------------------------------------------------------------------------------
local function leer(addr, n)
    if type(memory) ~= "table" or type(memory.read) ~= "function" then return nil end
    local ok, s = pcall(memory.read, addr, n)
    if ok and type(s) == "string" and #s == n then return s end
    return nil
end

local function numero(s)
    local v = 0
    for i = #s, 1, -1 do v = v * 256 + string.byte(s, i) end
    return v
end

local function puntero(addr)
    local s = leer(addr, 8)
    if not s then return nil end
    local v = numero(s)
    if v < 0x10000 or v > 0x7fffffffffff then return nil end
    return v
end

local function u32(addr)
    local s = leer(addr, 4)
    return s and numero(s) or nil
end

local function u8(addr)
    local s = leer(addr, 1)
    return s and string.byte(s) or nil
end

-- devuelve la direccion del Impl del visor del Editar si esta mostrando un jugador
local function visor()
    local emv = puntero(EMV_PTR)
    if not emv then return nil, "no estas en el menu Editar" end
    local task = puntero(emv + 0x78)
    if not task then return nil, "el visor del Editar no esta abierto" end
    local impl = puntero(task + 0x90)
    if not impl then return nil, "el visor del Editar no esta abierto" end
    local vt = leer(impl, 8)
    if not vt or numero(vt) ~= IMPL_VT then return nil, "no reconozco el visor (otra version del juego?)" end
    if u32(impl + O_TIPO) ~= 0 then return nil, "el visor no esta mostrando un jugador" end
    return impl
end

local function escribir_byte(addr, v)
    if u8(addr) == v then return false end
    memory.write(addr, string.char(v))
    return true
end

-- recarga de Sider, la misma de kserv / kitpos / Face_Animation_Test
local function recarga_sider(ctx)
    if not (ctx.kits and ctx.kits.refresh) then return false end
    local ok = pcall(function()
        local team_id = ctx.kits.get_current_team(0)
        local kit_id, is_gk = ctx.kits.get_current_kit_id(0)
        if team_id and team_id ~= 0x1ffff and kit_id then
            if is_gk then
                local cfg = ctx.kits.get_gk(team_id)
                if cfg then ctx.kits.set_gk(team_id, cfg) end
            else
                local cfg = ctx.kits.get(team_id, kit_id)
                if cfg then ctx.kits.set(team_id, kit_id, cfg) end
            end
        end
        ctx.kits.refresh(0)
    end)
    return ok
end

-- le pide al visor que rearme al mismo jugador (asi vuelve a pedir todos sus modelos)
local function recarga_visor(impl)
    if sin_llamada or type(ffi) ~= "table" then return false end
    local kind = u32(impl + O_KIND)
    if not kind or kind > 3 then return false end
    local ok, err = pcall(function()
        local id = ffi.cast("uint64_t *", impl + O_ID)[0]
        local n = tonumber(ffi.cast("uint64_t *", impl + O_KIT + 0x10)[0])
        local cap = tonumber(ffi.cast("uint64_t *", impl + O_KIT + 0x18)[0])
        local kit = ""
        if n > 0 and n < 256 then
            local p = ffi.cast("const char *", impl + O_KIT)
            if cap > 15 then p = ffi.cast("const char **", impl + O_KIT)[0] end
            kit = ffi.string(p, n)
        end
        local botines = ffi.cast("int32_t *", impl + 0x3a8)[0]
        local b6 = ffi.cast("uint8_t *", impl + 0x3ac)[0]
        local f7 = ffi.cast("float *", impl + 0x3b0)[0]
        local f8 = ffi.cast("float *", impl + 0x3b4)[0]
        -- el visor no rearma si le llega el mismo jugador con el mismo tipo: le cambio el tipo
        -- guardado (0<->1, 2<->3) y le paso el verdadero, que es el que deja escrito
        local otro = (kind % 2 == 0) and (kind + 1) or (kind - 1)
        memory.write(impl + O_KIND, string.char(otro, 0, 0, 0))
        log("ProbadorPiezas: pido al visor del Editar que rearme al jugador")
        local poner = ffi.cast("void (__fastcall *)(void *, uint64_t, uint32_t, const char *, int32_t, char, float, float)", FN_PONER)
        poner(ffi.cast("void *", impl), id, kind, kit, botines, b6, f7, f8)
        log("ProbadorPiezas: visor rearmando")
    end)
    if not ok then log("ProbadorPiezas: la recarga por el visor fallo: " .. tostring(err)) end
    return ok
end

-- escribe el vestuario pedido y recarga al jugador
local function aplicar(ctx)
    pedidos, pedido_prenda = {}, 0
    local impl, por_que = visor()
    if impl then
        local ok, err = pcall(function()
            escribir_byte(impl + O_SUB, quiero_sub)
            escribir_byte(impl + O_BIBS, quiero_bibs)
        end)
        if not ok then log("ProbadorPiezas: no pude escribir el vestuario: " .. tostring(err)) end
        if recarga_visor(impl) then
            recarga_txt = "jugador recargado por el visor del Editar"
            return
        end
        -- sin la llamada: dejo el tipo guardado cambiado (0<->1, 2<->3) para que el visor rearme
        -- al jugador la proxima vez que el menu se lo vuelva a poner (recarga de Sider o cambio de pantalla)
        pcall(function()
            local kind = u32(impl + O_KIND)
            if kind and kind <= 3 then
                memory.write(impl + O_KIND, string.char((kind % 2 == 0) and (kind + 1) or (kind - 1), 0, 0, 0))
            end
        end)
    elseif quiero_sub ~= 3 or quiero_bibs ~= 0 then
        recarga_txt = "PENDIENTE: " .. por_que .. ". Entra a Editar > jugador y apreta 4"
        log("ProbadorPiezas: " .. recarga_txt)
        return
    end
    if recarga_sider(ctx) then
        recarga_txt = "recarga pedida a Sider (como kitpos)"
    else
        recarga_txt = "PENDIENTE: volve a entrar al jugador para verlo"
    end
end

-------------------------------------------------------------------------------------------
-- que se entrega
-------------------------------------------------------------------------------------------
local function planear()
    sirve, prenda, quiero_sub, quiero_bibs, via_txt, nota = {}, nil, 3, 0, "", ""
    local e = lista[sel]
    if e and not e.ruta then
        quiero_sub, quiero_bibs = e.sub, e.bibs
        via_txt = "vestuario del juego"
    elseif e then
        local archivo = CAT .. e.ruta
        local natural = lugar_catalogo(e.ruta)
        if como_camiseta then
            via_txt = "como camiseta (el juego le pinta la textura del kit)"
            sirve[CAMISETA] = archivo
            sirve[CUELLO], sirve[MANGAS], sirve[SHORT] = EMPTY, EMPTY, EMPTY
        elseif natural then
            via_txt = "pieza de uniforme, en su lugar (" .. NOMBRE[natural] .. ")"
            sirve[natural] = archivo
        else
            via_txt = "prenda aparte, con sus texturas (vestuario de tecnico)"
            -- cada cambio usa otro de los tres trajes (traje, camisa, buzo): asi el juego pide un
            -- archivo de nombre distinto al anterior y no reusa el que todavia tiene cargado
            turno = turno + 1
            prenda, quiero_sub = archivo, turno % 3
        end
    end
    if sin_camiseta and quiero_sub == 3 then
        for _, i in ipairs({ CAMISETA, CUELLO, MANGAS }) do
            if not sirve[i] then sirve[i] = EMPTY end
        end
    end
end

function m.make_key(ctx, filename)
    if string.lower(string.sub(filename, -5)) ~= ".fmdl" then return end
    local f = string.lower(filename)
    local b = basename(f)
    if prenda and TRAJES[b] and string.find(f, "parts", 1, true) then
        pedido_prenda = pedido_prenda + 1
        if pedido_prenda == 1 then log("ProbadorPiezas: el juego pidio " .. filename .. " -> entrego " .. prenda) end
        return "pp3|" .. prenda
    end
    if string.find(f, "face", 1, true) then return end           -- shirt.fmdl de los tecnicos (face\real)
    local i = lugar_de(b)
    if not i then return end
    local d = sirve[i]
    if not d then return end
    pedidos[i] = (pedidos[i] or 0) + 1
    if pedidos[i] == 1 then log("ProbadorPiezas: el juego pidio " .. filename .. " -> entrego " .. d) end
    return "pp3|" .. d
end

function m.get_filepath(ctx, filename, key)
    if key and string.sub(key, 1, 4) == "pp3|" then
        return string.sub(key, 5)
    end
end

-------------------------------------------------------------------------------------------
-- catalogo
-------------------------------------------------------------------------------------------
local function grupo_de(r)
    return string.match(r, "^(.*)[\\/][^\\/]+$") or ""
end

local function leer_raiz()
    local f = io.open(DIR .. "catalogo_raiz.txt", "rb")
    if not f then return end
    local l = f:read("*l") or ""
    f:close()
    l = string.match(l, "^%s*(.-)%s*$")
    if l ~= "" then
        local c = string.sub(l, -1)
        if c ~= "\\" and c ~= "/" then l = l .. "\\" end
        CAT = l
    end
end

local function leer_catalogo()
    lista = {}
    for _, v in ipairs(VESTUARIO) do
        lista[#lista + 1] = { nombre = v.nombre, grupo = "VESTUARIO DEL JUEGO", sub = v.sub, bibs = v.bibs }
    end
    local f = io.open(CAT .. "indice.txt", "rb")
    if not f then return 0 end
    local txt = f:read("*a") or ""
    f:close()
    local n, grupo = 0, nil
    for line in string.gmatch(txt, "[^\r\n]+") do
        local r = string.match(line, "^%s*(.-)%s*$")
        local g = string.match(r, "^%[(.+)%]$")
        if g then
            grupo = g                                                -- [grupo] para lo que sigue
        elseif r ~= "" and string.sub(r, 1, 1) ~= "#" then
            r = string.match(r, "^[PK]|(.+)$") or r                 -- letra de indices viejos
            if string.lower(string.sub(r, -5)) == ".fmdl" then
                lista[#lista + 1] = { nombre = r, grupo = grupo or grupo_de(r), ruta = r }
                n = n + 1
            end
        end
    end
    return n
end

local function leer_info()
    local e = lista[sel]
    local r = e and e.ruta or nil
    if r == info_de then return end
    info_de, info, pagina = r, {}, 1
    if not r then return end
    local f = io.open(CAT .. r .. ".txt", "rb")
    if not f then info[1] = "(sin datos de materiales: falta " .. r .. ".txt)" return end
    local txt = f:read("*a") or ""
    f:close()
    for line in string.gmatch(txt, "[^\r\n]+") do
        info[#info + 1] = (string.gsub(line, "/Assets/pes16/model/", ""))
    end
end

-------------------------------------------------------------------------------------------
-- teclas y overlay
-------------------------------------------------------------------------------------------
local function cambio(ctx, texto)
    planear()
    log("ProbadorPiezas: " .. texto .. (via_txt ~= "" and (" | " .. via_txt) or ""))
    aplicar(ctx)
end

function m.key_down(ctx, vkey)
    local n = #lista
    if vkey == 0x30 or vkey == 0x39 or vkey == 0x38 or vkey == 0x37 then
        if vkey == 0x30 then
            sel = sel + 1
            if sel > n then sel = 1 end
        elseif vkey == 0x39 then
            sel = sel - 1
            if sel < 1 then sel = n end
        else
            local k = sel
            if k < 1 then k = 1 end
            local g = lista[k].grupo
            if vkey == 0x38 then
                while k <= n and lista[k].grupo == g do k = k + 1 end
                if k > n then k = 1 end
            else
                while k >= 1 and lista[k].grupo == g do k = k - 1 end        -- ultimo del grupo anterior
                if k < 1 then k = n end
                local g2 = lista[k].grupo
                while k > 1 and lista[k - 1].grupo == g2 do k = k - 1 end    -- su primer renglon
            end
            sel = k
        end
        como_camiseta = false
        cambio(ctx, string.format("%d de %d: %s", sel, n, lista[sel].nombre))
    elseif vkey == 0x35 then
        sel, como_camiseta = 0, false
        cambio(ctx, "vuelve al original")
    elseif vkey == 0x36 then
        local e = lista[sel]
        if not (e and e.ruta) then nota = "la tecla 6 es para un modelo del catalogo: elegi uno con 0 o 9" return end
        como_camiseta = not como_camiseta
        cambio(ctx, "cambia la forma de entrar")
    elseif vkey == 0x33 then
        sin_camiseta = not sin_camiseta
        cambio(ctx, sin_camiseta and "sin camiseta" or "con camiseta")
    elseif vkey == 0x34 then
        log("ProbadorPiezas: recarga pedida con la tecla 4")
        aplicar(ctx)
    elseif vkey == 0x32 then
        leer_info()
        pagina = pagina + 1
        if (pagina - 1) * POR_PAGINA >= #info then pagina = 1 end
    end
end

local function cargado_txt()
    if prenda then
        return pedido_prenda > 0 and "el juego ya cargo el modelo" or "el juego todavia no pidio el modelo"
    end
    local hay, falta = false, {}
    for i = 1, 4 do
        if sirve[i] then
            hay = true
            if not pedidos[i] then falta[#falta + 1] = NOMBRE[i] end
        end
    end
    if not hay then return nil end
    if #falta == 0 then return "el juego ya cargo los modelos" end
    return "el juego todavia no pidio: " .. table.concat(falta, ", ")
end

function m.overlay_on(ctx)
    local t = { string.format("ProbadorPiezas %s | %d renglones (%d modelos del catalogo)", m.version, #lista, #lista - #VESTUARIO) }
    local e = lista[sel]
    if e then
        t[#t + 1] = string.format("%d de %d  [%s]", sel, #lista, e.grupo)
        t[#t + 1] = "ELEGIDO: " .. (#e.nombre > 96 and ("..." .. string.sub(e.nombre, -93)) or e.nombre)
        t[#t + 1] = "entra: " .. via_txt
    else
        t[#t + 1] = "ELEGIDO: el original del juego" .. (sin_camiseta and "  (sin camiseta)" or "")
    end
    local c = cargado_txt()
    if recarga_txt ~= "" or c then
        t[#t + 1] = "estado: " .. recarga_txt .. ((recarga_txt ~= "" and c) and " | " or "") .. (c or "")
    end
    t[#t + 1] = "0 siguiente | 9 anterior | 8 / 7 grupo | 5 original | 6 prenda aparte / como camiseta"
    t[#t + 1] = "4 recargar jugador | 3 sin camiseta | 2 materiales | (tecla 1 = otro modulo)"
    if e and e.ruta then
        leer_info()
        if #info > 0 then
            local paginas = math.floor((#info + POR_PAGINA - 1) / POR_PAGINA)
            t[#t + 1] = string.format("--- materiales (pagina %d de %d) ---", pagina, paginas)
            for k = (pagina - 1) * POR_PAGINA + 1, math.min(pagina * POR_PAGINA, #info) do t[#t + 1] = info[k] end
        end
    end
    if nota ~= "" then t[#t + 1] = nota end
    return table.concat(t, "\n")
end

function m.init(ctx)
    DIR = ctx.sider_dir .. "content\\probador-piezas\\"
    CAT = DIR .. "catalogo\\"
    EMPTY = DIR .. "vacio.fmdl"
    leer_raiz()
    local n = leer_catalogo()
    sin_llamada = exists(DIR .. "sin_llamada.txt")
    planear()
    if not exists(EMPTY) then log("ProbadorPiezas: FALTA " .. EMPTY) end
    ctx.register("livecpk_make_key", m.make_key)
    ctx.register("livecpk_get_filepath", m.get_filepath)
    ctx.register("key_down", m.key_down)
    ctx.register("overlay_on", m.overlay_on)
    log(string.format("ProbadorPiezas %s listo (catalogo %d modelos en %s; memoria %s, ffi %s, recarga de Sider %s%s)",
        m.version, n, CAT, type(memory) == "table" and "si" or "NO", type(ffi) == "table" and "si" or "NO",
        (ctx.kits and ctx.kits.refresh) and "si" or "NO", sin_llamada and "; sin_llamada.txt presente" or ""))
end

return m
