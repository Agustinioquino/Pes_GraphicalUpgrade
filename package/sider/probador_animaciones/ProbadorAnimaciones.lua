-- ProbadorAnimaciones v2.2 - PES 2021 1.07.02 / Sider 7  (PES21 FX, 10/11/2026)
-- Probador de animaciones (.gani) con el menu Editar como visor. Version simple: dos paginas.
--
-- CUERPO: el visor del Editar carga los 162 festejos de gol TODOS JUNTOS al entrar al jugador.
--   Este modulo le entrega, en lugar de cada festejo, una animacion de la lista: 162 por tanda.
--   Despues se miran desde el propio juego: Editar > jugador > Movimientos > Festejo de gol.
--   El festejo numero N muestra la animacion (primera de la tanda + N - 1); el overlay dice
--   cual es la que esta puesta en el visor en ese momento.
-- CARA: una animacion de cara a la vez, en lugar de la cara de reposo del visor.
--
-- El juego carga las animaciones una sola vez y las guarda hasta que salis de la pantalla
-- "Jugadores" de Editar. Si elegis la tanda ANTES de entrar a Jugadores no hace falta nada mas.
-- Si la cambias despues, retrocede con Atras hasta el menu principal de Editar y volve a entrar
-- al jugador (volver solo a la lista de jugadores no alcanza). El overlay avisa solo:
-- PENDIENTE hasta que el juego las vuelve a pedir, APLICADO cuando ya las cargo.
--
-- Teclas (overlay de Sider abierto y este modulo elegido; en tu Sider el overlay se abre con la
-- barra espaciadora y se pasa de modulo con la tecla 1):
--   0 = tanda / animacion SIGUIENTE     9 = anterior      8 / 7 = grupo siguiente / anterior
--   5 = volver al original              6 = pagina cuerpo <-> cara
--   4 = pedirle a Sider que recargue al jugador (la recarga de kserv / kitpos)
--
-- Que toca: no cambia ningun archivo ni escribe memoria. Solo elige que archivo se entrega
-- mientras estas en Editar, y lee del visor que festejo esta puesto. Fuera de Editar no hace nada.
-- Anota solo en sider.log cada .gani distinto que pide el juego ("el juego pide ...").
--
-- Listas: content\probador-animaciones\cara.txt, cuerpo.txt y partido.txt. Primera linea
-- "raiz = carpeta completa", despues una ruta por linea relativa a esa carpeta. El modulo las
-- reparte solo: caras a CARA, cuerpos a CUERPO; manos, ojos y publico quedan afuera.

-- Sider no les da pcall a los modulos (sider.log del 10/11: "attempt to call global 'pcall'").
-- Si no esta, la funcion se llama directo: un error queda anotado por Sider en sider.log.
local pcall = pcall or function(f, ...) return true, f(...) end

local m = { version = "2.2" }
local TANDA = 162

-- direcciones de PES2021.exe 1.07.02 (base 0x140000000), leidas del decompilado. Solo lectura.
local EMV_PTR = 0x1438040d0      -- puntero a edit::ModelView (visor del menu Editar)
local IMPL_VT = 0x142ba51e0      -- vtable de draw::util::TaskModelView::Impl
local O_ANIM_TIPO, O_ANIM_ID, ID_FESTEJO_0 = 0x3c0, 0x3c4, 0x2c

-- orden de los 162 festejos en el menu (tabla 0x143536b80 del exe): dml_goal_celebrate_<numero>
local FESTEJOS =
    "0006 0007 0008 0009 0052 0002 0081 0109 0118 0098 0088 0070 0128 0069 0114 0116 0028 0115 0030 0133 " ..
    "0021 0132 0110 0084 0061 0125 0124 0054 0103 0082 0107 0106 0053 0122 0087 0100 0117 0031 0093 0092 " ..
    "0134 0113 0112 0111 0131 0074 0096 0095 0094 0090 0089 0211 0130 0129 0286 0108 0077 0075 0080 0126 " ..
    "0099 0120 0022 0119 0102 0073 0072 0020 0127 0071 0068 0209 0105 0067 0066 0018 0064 0104 0213 0055 " ..
    "0101 0123 0057 0058 0060 0059 0051 0208 0175 0214 0173 0174 0180 0176 0078 0135 0091 0079 0063 0121 " ..
    "0156 0097 0076 0056 0216 0086 0085 0083 0279 0177 0178 0179 0143 0142 0141 0140 0139 0138 0137 0191 " ..
    "0192 0136 0003 0001 0232 0233 0231 0314 0016 0019 0029 0236 0235 0015 0017 0298 0014 0010 0012 0013 " ..
    "0267 0268 0035 0266 0269 0270 0263 0271 0272 0273 0274 0262_a 0284 0285 0287 0288 0289 0290 0291 " ..
    "0293 0294 0297"

local DIR
local cuerpo, cara = {}, {}          -- { ruta = archivo completo, nombre, g = grupo }
local pag = 1                        -- 1 = cuerpo, 2 = cara
local base, activo = 0, false        -- primera animacion de la tanda (desde 0) y si se entrega
local sel_cara = 0                   -- 0 = cara normal
local IDX, HUECO = {}, nil           -- numero de festejo -> lugar en el menu (desde 0)
local orden, n_extra, ultimo, hueco_usado = {}, 0, -1, false
local serv, n_serv = {}, 0           -- festejos distintos entregados con la tanda actual
local serv_cara, n_serv_cara = {}, 0
local hubo_cuerpo, hubo_cara = false, false
local sanos, malos = {}, 0
local en_edit = false
local vistos, n_vistos = {}, 0
local aviso, recarga_txt = "", ""

-------------------------------------------------------------------------------------------
-- utilidades
-------------------------------------------------------------------------------------------
local function basename(f)
    return string.match(f, "([^\\/]+)$") or f
end

local function termina(s, fin)
    return string.sub(s, -#fin) == fin
end

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

local function en_editar(ctx)
    if en_edit then return true end
    if ctx and ctx.edit_mode_team ~= nil then return true end
    return puntero(EMV_PTR) ~= nil
end

-- festejo puesto ahora en el visor del Editar (lugar en el menu, desde 0), o nil
local function festejo_en_visor()
    local emv = puntero(EMV_PTR)
    local task = emv and puntero(emv + 0x78)
    local impl = task and puntero(task + 0x90)
    if not impl then return nil end
    local vt = leer(impl, 8)
    if not vt or numero(vt) ~= IMPL_VT then return nil end
    if u32(impl + O_ANIM_TIPO) ~= 1 then return nil end
    local id = u32(impl + O_ANIM_ID)
    if not id or id < ID_FESTEJO_0 or id >= ID_FESTEJO_0 + TANDA then return nil end
    return id - ID_FESTEJO_0
end

-- un .gani bueno empieza con D2 A2 FC 0B; si no, no se entrega (el juego usa el suyo)
local MAGIA = string.char(0xD2, 0xA2, 0xFC, 0x0B)
local function sano(path)
    local v = sanos[path]
    if v ~= nil then return v end
    v = false
    local f = io.open(path, "rb")
    if f then
        v = (f:read(4) == MAGIA)
        f:close()
    end
    sanos[path] = v
    if not v then
        malos = malos + 1
        if malos <= 5 then log("ProbadorAnimaciones: no entrego (falta o no es un .gani): " .. path) end
    end
    return v
end

-- recarga de Sider, la misma de kserv / kitpos / Face_Animation_Test
local function recarga_sider(ctx)
    if not (ctx and ctx.kits and ctx.kits.refresh) then return false end
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

-------------------------------------------------------------------------------------------
-- que archivo del juego es que
-------------------------------------------------------------------------------------------
-- festejo de gol del visor: ...\fixdemo\animations\dml_goal_celebrate_<n>.gani (o dml_pcc_ de PCC)
local function es_festejo(f, b)
    if not string.find(f, "fixdemo", 1, true) then return false end
    if string.sub(b, 1, 19) ~= "dml_goal_celebrate_" and string.sub(b, 1, 8) ~= "dml_pcc_" then return false end
    if termina(b, "_face.gani") or termina(b, "_eyes_target.gani") or termina(b, "hand.gani") then return false end
    return true
end

-- cara de las poses del visor: ...\foxanim\interface(load)\<pose>_face.gani
local function es_cara_visor(f, b)
    return termina(b, "_face.gani") and string.find(f, "foxanim\\interface", 1, true) ~= nil
end

-- lugar del festejo en el menu (desde 0)
local function lugar(b)
    local k = orden[b]
    if k then return k end
    local s = string.match(b, "^dml_goal_celebrate_(.-)%.gani$")
    k = s and IDX[s]
    if not k then
        local slot = tonumber(string.match(b, "^dml_pcc_b%d+_(%d+)%.gani$") or "")
        local escena = tonumber(s or "")
        if slot and slot > TANDA then
            k = slot - 1                                   -- festejos agregados por PCC
        elseif escena and escena >= 6000 then
            k = TANDA + escena - 6000                      -- el mismo festejo de PCC, por su otro nombre
        elseif s and HUECO and not hueco_usado and ultimo == HUECO - 1 then
            k, hueco_usado = HUECO, true                   -- el que no pude leer del exe
        else
            k = TANDA + n_extra
            n_extra = n_extra + 1
        end
    end
    orden[b] = k
    if k < TANDA then ultimo = k end
    return k
end

function m.make_key(ctx, filename)
    local f = string.lower(filename)
    if string.sub(f, -5) ~= ".gani" then return end
    f = string.gsub(f, "/", "\\")
    local b = basename(f)
    if not vistos[b] and n_vistos < 400 then
        vistos[b] = true
        n_vistos = n_vistos + 1
        log("ProbadorAnimaciones: el juego pide " .. filename)
    end
    if sel_cara > 0 and es_cara_visor(f, b) and en_editar(ctx) then
        local e = cara[sel_cara]
        if e and sano(e.ruta) then
            if not serv_cara[b] then
                serv_cara[b] = true
                n_serv_cara = n_serv_cara + 1
                log("ProbadorAnimaciones: cara " .. b .. " -> entrego " .. e.ruta)
            end
            return "pa2|" .. e.ruta
        end
        return
    end
    if activo and es_festejo(f, b) and en_editar(ctx) then
        local k = lugar(b)
        local e = cuerpo[base + k + 1]
        if e and sano(e.ruta) then
            if not serv[b] then
                serv[b] = true
                n_serv = n_serv + 1
                if n_serv <= 3 then
                    log(string.format("ProbadorAnimaciones: festejo %d (%s) -> entrego %s", k + 1, b, e.ruta))
                end
            end
            return "pa2|" .. e.ruta
        end
    end
end

function m.get_filepath(ctx, filename, key)
    if key and string.sub(key, 1, 4) == "pa2|" then
        return string.sub(key, 5)
    end
end

-------------------------------------------------------------------------------------------
-- listas
-------------------------------------------------------------------------------------------
-- devuelve "cara" o "cuerpo" y un orden (primero lo que usa el propio visor), o nil si no va
local function clasificar(r, de_partido)
    local b = basename(r)
    if string.find(r, "\\hand\\", 1, true) or string.find(r, "audience", 1, true)
        or string.find(r, "standsflag", 1, true) or termina(b, "hand.gani")
        or termina(b, "_eyes_target.gani") then
        return nil
    end
    local inter = 3
    if string.find(r, "\\interfaceload\\", 1, true) then inter = 1
    elseif string.find(r, "\\interface\\", 1, true) then inter = 2 end
    if termina(b, "_face.gani") then return "cara", inter end
    if string.find(r, "\\face\\animations\\base\\", 1, true) then return "cara", 4 end
    if string.find(r, "\\face\\", 1, true) then return "cara", 5 end
    if de_partido then return "cuerpo", 5 end
    if inter < 3 then return "cuerpo", inter end
    if string.find(r, "fixdemo", 1, true) then return "cuerpo", 3 end
    return "cuerpo", 4
end

local function grupo_de(rel)
    local carpeta, nombre = string.match(rel, "^(.*)[\\/]([^\\/]+)$")
    if not carpeta then return "" end
    if termina(string.lower(carpeta), "fixdemo\\animations") then
        -- carpeta gigante: la parto por el comienzo del nombre (dml_goal, dml_ceremony, ...)
        local a, c = string.match(string.lower(nombre), "^(%a+)_(%a+)")
        if a then return carpeta .. "\\" .. a .. "_" .. c .. "*" end
    end
    return carpeta
end

local function leer_listas()
    local todo, ya = { cara = {}, cuerpo = {} }, {}
    local total, afuera = 0, 0
    for _, nombre in ipairs({ "cara", "cuerpo", "partido" }) do
        local f = io.open(DIR .. nombre .. ".txt", "rb")
        if f then
            local txt = f:read("*a") or ""
            f:close()
            local raiz = ""
            for line in string.gmatch(txt, "[^\r\n]+") do
                local l = string.match(line, "^%s*(.-)%s*$")
                local r = string.match(l, "^raiz%s*=%s*(.+)$")
                if r then
                    local c = string.sub(r, -1)
                    if c ~= "\\" and c ~= "/" then r = r .. "\\" end
                    raiz = r
                elseif l ~= "" and string.sub(l, 1, 1) ~= "#" and string.lower(string.sub(l, -5)) == ".gani" then
                    l = string.gsub(l, "/", "\\")
                    local ruta = raiz .. l
                    local clave = string.lower(ruta)
                    if not ya[clave] then
                        ya[clave] = true
                        total = total + 1
                        local tipo, rango = clasificar("\\" .. string.lower(l), nombre == "partido")
                        if tipo then
                            local t = todo[tipo]
                            t[#t + 1] = { ruta = ruta, nombre = basename(l), g = grupo_de(l), rango = rango, n = #t + 1 }
                        else
                            afuera = afuera + 1
                        end
                    end
                end
            end
        else
            log("ProbadorAnimaciones: falta la lista " .. DIR .. nombre .. ".txt")
        end
    end
    local function antes(a, b2)
        if a.rango ~= b2.rango then return a.rango < b2.rango end
        return a.n < b2.n
    end
    table.sort(todo.cara, antes)
    table.sort(todo.cuerpo, antes)
    cara, cuerpo = todo.cara, todo.cuerpo
    return total, afuera
end

-------------------------------------------------------------------------------------------
-- teclas y overlay
-------------------------------------------------------------------------------------------
local function salto_grupo(lista, pos, adelante)       -- pos y resultado: indice desde 1
    local n = #lista
    local k = pos
    if k < 1 then return 1 end
    local g = lista[k].g
    if adelante then
        while k <= n and lista[k].g == g do k = k + 1 end
        if k > n then k = 1 end
    else
        while k >= 1 and lista[k].g == g do k = k - 1 end
        if k < 1 then k = n end
        local g2 = lista[k].g
        while k > 1 and lista[k - 1].g == g2 do k = k - 1 end
    end
    return k
end

local function cambio(ctx, texto)
    log("ProbadorAnimaciones: " .. texto)
    recarga_txt = recarga_sider(ctx) and "recarga pedida a Sider" or ""
end

local function tecla_cuerpo(ctx, vkey)
    local n = #cuerpo
    if vkey == 0x35 then
        if activo then
            activo = false
            serv, n_serv = {}, 0
            cambio(ctx, "cuerpo vuelve al original")
        end
        return
    end
    if n == 0 then aviso = "no hay animaciones de cuerpo: faltan cuerpo.txt y partido.txt" return end
    if not activo then
        activo = true                                   -- la primera tecla prende la tanda actual
    elseif vkey == 0x30 then
        base = base + TANDA
        if base >= n then base = 0 end
    elseif vkey == 0x39 then
        if base == 0 then base = math.floor((n - 1) / TANDA) * TANDA
        else base = math.max(0, base - TANDA) end
    elseif vkey == 0x38 then
        base = salto_grupo(cuerpo, base + 1, true) - 1
    else
        base = salto_grupo(cuerpo, base + 1, false) - 1
    end
    serv, n_serv, hubo_cuerpo = {}, 0, true
    cambio(ctx, string.format("cuerpo: tanda %d a %d de %d, empieza en %s", base + 1, math.min(base + TANDA, n), n, cuerpo[base + 1].ruta))
end

local function tecla_cara(ctx, vkey)
    local n = #cara
    if vkey == 0x35 then
        if sel_cara > 0 then
            sel_cara = 0
            serv_cara, n_serv_cara = {}, 0
            cambio(ctx, "cara vuelve a la normal")
        end
        return
    end
    if n == 0 then aviso = "no hay animaciones de cara: falta cara.txt" return end
    if vkey == 0x30 then
        sel_cara = sel_cara + 1
        if sel_cara > n then sel_cara = 1 end
    elseif vkey == 0x39 then
        sel_cara = sel_cara - 1
        if sel_cara < 1 then sel_cara = n end
    else
        sel_cara = salto_grupo(cara, sel_cara, vkey == 0x38)
    end
    serv_cara, n_serv_cara, hubo_cara = {}, 0, true
    cambio(ctx, string.format("cara: %d de %d, %s", sel_cara, n, cara[sel_cara].ruta))
end

function m.key_down(ctx, vkey)
    aviso = ""
    if vkey == 0x36 then
        pag = (pag == 1) and 2 or 1
    elseif vkey == 0x34 then
        recarga_txt = recarga_sider(ctx) and "recarga pedida a Sider" or "Sider no pudo pedir la recarga"
        log("ProbadorAnimaciones: tecla 4, " .. recarga_txt)
    elseif vkey == 0x30 or vkey == 0x39 or vkey == 0x38 or vkey == 0x37 or vkey == 0x35 then
        if pag == 1 then tecla_cuerpo(ctx, vkey) else tecla_cara(ctx, vkey) end
    end
end

local COMO = "retrocede con Atras hasta el menu principal de Editar (el que tiene Jugadores) y volve a entrar al jugador"

function m.overlay_on(ctx)
    local t = { string.format("ProbadorAnimaciones %s | cuerpo: %d animaciones | cara: %d", m.version, #cuerpo, #cara) }
    local n = #cuerpo
    local p1, p2 = (pag == 1) and "> " or "  ", (pag == 2) and "> " or "  "
    if activo and n > 0 then
        local e = cuerpo[base + 1]
        t[#t + 1] = string.format("%sCUERPO: tanda %d a %d de %d   [%s]", p1, base + 1, math.min(base + TANDA, n), n, e.g)
        t[#t + 1] = string.format("   Editar > Movimientos > Festejo de gol: el festejo N muestra la animacion %d + N", base)
        t[#t + 1] = "   festejo 1 = " .. e.nombre
        local k = festejo_en_visor()
        if k and n_serv > 0 then
            local a = cuerpo[base + k + 1]
            t[#t + 1] = string.format("   AHORA EN EL VISOR: festejo %d -> animacion %d: %s", k + 1, base + k + 1, a and a.nombre or "(fuera de la lista: sale el festejo del juego)")
        elseif k then
            t[#t + 1] = string.format("   AHORA EN EL VISOR: festejo %d (todavia sin esta tanda)", k + 1)
        end
        if n_serv > 0 then
            t[#t + 1] = string.format("   estado: APLICADO, el juego cargo %d de los %d festejos con esta tanda", n_serv, TANDA)
        else
            t[#t + 1] = "   estado: PENDIENTE, " .. COMO
        end
    else
        t[#t + 1] = p1 .. "CUERPO: original del juego" .. (hubo_cuerpo and "  (los festejos originales vuelven al salir de Jugadores y entrar de nuevo)" or "")
    end
    if sel_cara > 0 and cara[sel_cara] then
        local e = cara[sel_cara]
        t[#t + 1] = string.format("%sCARA: %d de %d   %s   [%s]", p2, sel_cara, #cara, e.nombre, e.g)
        if n_serv_cara > 0 then
            t[#t + 1] = "   estado: APLICADO, el juego ya la cargo"
        else
            t[#t + 1] = "   estado: PENDIENTE, " .. COMO
        end
    else
        t[#t + 1] = p2 .. "CARA: normal" .. (hubo_cara and "  (vuelve al salir de Jugadores y entrar de nuevo)" or "")
    end
    if pag == 1 then
        t[#t + 1] = "0 tanda siguiente | 9 anterior | 8 / 7 grupo | 5 original | 6 pasar a CARA | 4 recargar | (tecla 1 = otro modulo)"
    else
        t[#t + 1] = "0 cara siguiente | 9 anterior | 8 / 7 grupo | 5 normal | 6 pasar a CUERPO | 4 recargar | (tecla 1 = otro modulo)"
    end
    if malos > 0 then t[#t + 1] = string.format("%d archivos de la lista faltan o no son .gani: no se entregan", malos) end
    if recarga_txt ~= "" then t[#t + 1] = recarga_txt end
    if aviso ~= "" then t[#t + 1] = aviso end
    return table.concat(t, "\n")
end

function m.init(ctx)
    DIR = ctx.sider_dir .. "content\\probador-animaciones\\"
    local i = 0
    for s in string.gmatch(FESTEJOS, "%S+") do
        if s == "?" then HUECO = i else IDX[s] = i end
        i = i + 1
    end
    local total, afuera = leer_listas()
    ctx.register("livecpk_make_key", m.make_key)
    ctx.register("livecpk_get_filepath", m.get_filepath)
    ctx.register("key_down", m.key_down)
    ctx.register("overlay_on", m.overlay_on)
    pcall(ctx.register, "set_home_team_for_kits", function(c, team, edit) en_edit = (edit == true or edit == 1) end)
    pcall(ctx.register, "set_teams", function() en_edit = false end)
    log(string.format("ProbadorAnimaciones %s listo (%d en las listas: %d de cuerpo, %d de cara, %d afuera; %d festejos en la tabla; memoria %s; recarga de Sider %s)",
        m.version, total, #cuerpo, #cara, afuera, i, type(memory) == "table" and "si" or "NO",
        (ctx.kits and ctx.kits.refresh) and "si" or "NO"))
end

return m
