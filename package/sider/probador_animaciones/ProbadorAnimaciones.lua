-- ProbadorAnimaciones v1.0 - PES 2021 / Sider  (PES21 FX, 10/11/2026)
-- Probador de animaciones (.gani), UNA A UNA, con el modo Editar como visor.
-- Cada PAGINA es un tipo de animacion. Con una tecla pasas a la animacion siguiente de la
-- lista de esa pagina: el juego la carga EN LUGAR de la animacion que el visor pide siempre
-- (la de reposo), asi la reproduce el motor mismo. El overlay muestra numero y archivo.
-- No reemplaza ni toca ningun .gani: solo cambia cual se entrega mientras el modulo esta activo.
--
-- Pagina "cara": el visor pide edit_idle_loop_face.gani al cargar al jugador; se le entrega
--   el clip elegido de la lista content\probador-animaciones\cara.txt
-- Lista de una pagina (<pagina>.txt): primera linea "raiz = carpeta completa" y despues una
--   ruta por linea, relativa a esa carpeta (grupo\archivo.gani). Lineas con # no cuentan.
-- Paginas extra: content\probador-animaciones\paginas.txt, una por linea:
--   nombre = comienzo del nombre del archivo que pide el juego   (ej.  cuerpo = edit_idle_loop_body)
--
-- Ademas anota solo (sin teclas) cada .gani distinto que pide el juego en
-- content\probador-animaciones\vistos_gani.txt : sirve para saber que archivo pide cada pantalla.
--
-- Uso: overlay de Sider, pasar a este modulo con [ y ] :
--   0 = animacion SIGUIENTE      9 = anterior       5 / 6 = grupo (carpeta) anterior / siguiente
--   7 / 8 = pagina anterior / siguiente              1 = esa pagina vuelve a NORMAL    3 = todo normal
-- Para verla: volve a entrar al jugador (Edit > Apariencia), el visor recarga la animacion.

local m = { version = "1.0" }
local DIR, ESTADO, VISTOS
local PAGS = { { nombre = "cara", pref = "edit_idle_loop_face" } }
local pag, aviso = 1, ""
local vistos, nvistos = {}, 0

local function basename(f)
    return string.lower(string.match(f, "([^\\/]+)$") or f)
end

local function trim(s) return (string.match(s, "^%s*(.-)%s*$")) end

local function leer_lista(p)
    p.lista, p.raiz, p.sel = {}, "", p.sel or 0
    local f = io.open(DIR .. p.nombre .. ".txt", "rb")
    if not f then return end
    local txt = f:read("*a") or ""
    f:close()
    for line in string.gmatch(txt, "[^\r\n]+") do
        local l = trim(line)
        local r = string.match(l, "^raiz%s*=%s*(.+)$")
        if r then
            local c = string.sub(r, -1)
            if c ~= "\\" and c ~= "/" then r = r .. "\\" end
            p.raiz = r
        elseif l ~= "" and string.sub(l, 1, 1) ~= "#" and string.lower(string.sub(l, -5)) == ".gani" then
            p.lista[#p.lista + 1] = l
        end
    end
end

local function leer_paginas()
    local f = io.open(DIR .. "paginas.txt", "rb")
    if f then
        local txt = f:read("*a") or ""
        f:close()
        for line in string.gmatch(txt, "[^\r\n]+") do
            local k, v = string.match(line, "^%s*([^#=]-)%s*=%s*(.-)%s*$")
            if k and k ~= "" and v ~= "" then
                local ya = false
                for _, p in ipairs(PAGS) do if p.nombre == k then p.pref = string.lower(v) ya = true end end
                if not ya then PAGS[#PAGS + 1] = { nombre = k, pref = string.lower(v) } end
            end
        end
    end
    for _, p in ipairs(PAGS) do leer_lista(p) end
end

local function pagina_de(b)
    for i, p in ipairs(PAGS) do
        if string.sub(b, 1, #p.pref) == p.pref then return i end
    end
    return nil
end

local function anotar(filename, b)
    if vistos[b] or nvistos >= 5000 then return end
    vistos[b] = true
    nvistos = nvistos + 1
    local f = io.open(VISTOS, "ab")
    if f then f:write(filename .. "\r\n") f:close() end
end

function m.make_key(ctx, filename)
    local b = basename(filename)
    if string.sub(b, -5) ~= ".gani" then return end
    anotar(filename, b)
    local i = pagina_de(b)
    if i and PAGS[i].sel >= 1 and PAGS[i].lista[PAGS[i].sel] then
        return "probadoranim1:" .. i .. ":" .. PAGS[i].sel
    end
end

function m.get_filepath(ctx, filename, key)
    if key and string.sub(key, 1, 14) == "probadoranim1:" then
        local i = tonumber(string.match(key, "^probadoranim1:(%d+):"))
        local p = i and PAGS[i]
        if p and p.sel >= 1 and p.lista[p.sel] then return p.raiz .. p.lista[p.sel] end
    end
end

local function guardar()
    local f = io.open(ESTADO, "wb")
    if not f then return end
    for _, p in ipairs(PAGS) do
        f:write(p.nombre .. " = " .. ((p.sel >= 1 and p.lista[p.sel]) or "normal") .. "\r\n")
    end
    f:close()
end

local function cargar()
    local f = io.open(ESTADO, "rb")
    if not f then return end
    local txt = f:read("*a") or ""
    f:close()
    for line in string.gmatch(txt, "[^\r\n]+") do
        local k, v = string.match(line, "^%s*(.-)%s*=%s*(.-)%s*$")
        if k and v then
            for _, p in ipairs(PAGS) do
                if p.nombre == k then
                    for n, c in ipairs(p.lista) do if c == v then p.sel = n end end
                end
            end
        end
    end
end

local function grupo_de(r) return string.match(r, "^(.*)[\\/][^\\/]+$") or "" end

local function texto(p)
    if p.sel >= 1 and p.lista[p.sel] then
        return string.format("animacion %d de %d:  %s", p.sel, #p.lista, p.lista[p.sel])
    end
    return string.format("normal   (%d en la lista)", #p.lista)
end

function m.key_down(ctx, vkey)
    local p = PAGS[pag]
    local n = #p.lista
    if vkey == 0x37 then
        pag = pag - 1
        if pag < 1 then pag = #PAGS end
        return
    elseif vkey == 0x38 then
        pag = pag + 1
        if pag > #PAGS then pag = 1 end
        return
    elseif vkey == 0x30 or vkey == 0x39 or vkey == 0x35 or vkey == 0x36 then
        if n == 0 then
            aviso = "la lista de '" .. p.nombre .. "' esta vacia (falta " .. p.nombre .. ".txt)"
            return
        end
        local e = p.sel
        if vkey == 0x30 then
            e = e + 1
            if e > n then e = 1 end
        elseif vkey == 0x39 then
            e = e - 1
            if e < 1 then e = n end
        else
            if e < 1 then e = 1 end
            local g = grupo_de(p.lista[e])
            local k = e
            if vkey == 0x36 then
                while k <= n and grupo_de(p.lista[k]) == g do k = k + 1 end
                if k > n then k = 1 end
            else
                while k >= 1 and grupo_de(p.lista[k]) == g do k = k - 1 end
                if k < 1 then k = n end
                local g2 = grupo_de(p.lista[k])
                while k > 1 and grupo_de(p.lista[k - 1]) == g2 do k = k - 1 end
            end
            e = k
        end
        p.sel = e
    elseif vkey == 0x31 then p.sel = 0
    elseif vkey == 0x33 then for _, q in ipairs(PAGS) do q.sel = 0 end
    else return end
    guardar()
    aviso = p.nombre .. " -> " .. texto(p) .. "   (volve a entrar al jugador para verla)"
    log("ProbadorAnimaciones: " .. aviso)
end

function m.overlay_on(ctx)
    local t = { "ProbadorAnimaciones " .. m.version }
    for i, p in ipairs(PAGS) do
        t[#t + 1] = string.format("%s %-8s %s", i == pag and ">" or " ", p.nombre, texto(p))
        if i == pag then t[#t + 1] = "           reemplaza a: " .. p.pref .. "*.gani" end
    end
    t[#t + 1] = "0 = siguiente | 9 = anterior | 5/6 = grupo | 7/8 = pagina | 1 normal | 3 todo normal"
    if aviso ~= "" then t[#t + 1] = aviso end
    return table.concat(t, "\n")
end

function m.init(ctx)
    DIR = ctx.sider_dir .. "content\\probador-animaciones\\"
    ESTADO = DIR .. "estado.txt"
    VISTOS = DIR .. "vistos_gani.txt"
    leer_paginas()
    cargar()
    ctx.register("livecpk_make_key", m.make_key)
    ctx.register("livecpk_get_filepath", m.get_filepath)
    ctx.register("key_down", m.key_down)
    ctx.register("overlay_on", m.overlay_on)
    log(string.format("ProbadorAnimaciones %s listo (%d paginas, cara: %d clips)", m.version, #PAGS, #PAGS[1].lista))
end

return m
