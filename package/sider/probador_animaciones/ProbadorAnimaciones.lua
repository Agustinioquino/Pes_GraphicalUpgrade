-- ProbadorAnimaciones v3.1 - PES 2021 1.07.02 / Sider 7  (PES21 FX, 10/11/2026)
-- Probador de animaciones (.gani) EN EL MOMENTO. Con el jugador en pantalla dentro de Editar
-- (Apariencia o cualquier pantalla donde se lo ve de pie), una tecla le cambia la animacion del
-- cuerpo o de la cara. No hay que salir ni volver a entrar.
--
-- Como lo hace: el jugador del Editar esta siempre en la pose de reposo "edit_idle_loop". Cada
-- vez que elegis una animacion, el modulo le cambia a esa pose el NOMBRE de su archivo por uno
-- nuevo (pa3_c00001, pa3_c00002, ...), le borra la marca de "ya cargada" y le pide al visor la
-- misma pose otra vez, igual que se la pide el menu. El juego va a buscar ese archivo nuevo y
-- este modulo le entrega la animacion elegida. La cara va igual (pa3_f00001, ...).
--
-- Teclas (overlay de Sider abierto y este modulo elegido; en tu Sider el overlay se abre con la
-- barra espaciadora y se pasa de modulo con la tecla 1):
--   0 = animacion SIGUIENTE     9 = anterior
--   8 / 7 = CATEGORIA siguiente / anterior      3 / 2 = grupo siguiente / anterior dentro de la lista
--   5 = volver al original      6 = pagina cuerpo <-> cara      4 = ponerla de nuevo desde el principio
--   La animacion se pone al SOLTAR la tecla: dejandola apretada solo se recorre la lista.
--
-- Nuevo en la 3.1:
--   * Solo se ponen animaciones DE JUGADOR. Antes de poner una se lee el comienzo del archivo y se
--     compara su esqueleto con el del jugador; las de objetos (copas, carteles, banderas), publico,
--     manos y ojos se saltean solas. Una de esas (una copa) fue la que cerro el juego con la 3.0.
--   * Categorias: Editar y menus, Gameplay (partido), Festejos de gol, Ceremonias y final, Entrada y
--     previa, Liga Master y myClub, Arbitro/faltas/penales/cambios, Gestos de cara y Varias.
--
-- Que toca en el juego, y solo cuando apretas una tecla con un jugador en pantalla:
--   escribe el puntero de nombre de esa pose (tabla 0x143536b00) y su marca de carga, con los
--   candados que usa el propio juego tomados SIN esperar (si estan ocupados reintenta en el cuadro
--   siguiente), y llama a la funcion con la que el menu pide una pose (0x141f061a0). Antes
--   comprueba que el exe sea el 1.07.02 que conozco; si no, no toca nada.
--   No cambia ningun archivo. La animacion elegida queda hasta que apretes 5 o cierres el juego.
--   Si existe content\probador-animaciones\sin_llamada.txt el modulo no toca nada.
-- Anota en sider.log lo que hace y cada archivo propio que le pide el juego.
--
-- Listas: content\probador-animaciones\cara.txt, cuerpo.txt y partido.txt. Primera linea
-- "raiz = carpeta completa", despues una ruta por linea relativa a esa carpeta. El modulo las
-- reparte solo: caras a CARA, cuerpos a CUERPO; manos, ojos, publico y objetos quedan afuera.

local m = { version = "3.1" }

-- direcciones de PES2021.exe 1.07.02 (base 0x140000000), confirmadas en el codigo de maquina
local EMV_PTR    = 0x1438040d0   -- puntero a edit::ModelView (visor del menu Editar)
local IMPL_VT    = 0x142ba51e0   -- vtable de draw::util::TaskModelView::Impl
local RES_PTR    = 0x1438033b0   -- puntero a las animaciones del visor (draw::ModelViewAnimeResource)
local CANDADO    = 0x1438033c0   -- su candado (CRITICAL_SECTION; en +0x28 la marca de "armado")
local FN_PROBAR  = 0x1403ff4b0   -- tomar un candado del juego SIN esperar: devuelve 0 si lo tomo
local FN_SALIR   = 0x1403ff4e0   -- soltarlo
local FN_POSE    = 0x141f061a0   -- pedir pose al visor: (ModelView, pose, tiempo, 1, 1, 0)
local function de_hex(s) return (string.gsub(s, "%x%x", function(h) return string.char(tonumber(h, 16)) end)) end
-- las tres empiezan igual; lo unico que las distingue es a que funcion de Windows llaman (bytes 23 a 26)
local FIRMA_PROBAR = de_hex("4883ec2880792800750ab8030000184883c428c3ff150e8d1302")
local FIRMA_SALIR  = de_hex("4883ec2880792800750ab8030000184883c428c3ff15ee911302")
local FIRMA_POSE   = de_hex("4883ec38899100010000488b49784885c9750732c04883c438c3")
-- las dos funciones por las que pasa el pedido despues (0x141f061a0 -> 0x141ee4fa0 -> 0x141ee9360)
local FN_PASO, FIRMA_PASO   = 0x141ee4fa0, de_hex("488b89900000004885c9741c83fa2c77")
local FN_FIJAR, FIRMA_FIJAR = 0x141ee9360, de_hex("40574883ec4048c7442420feffffff")
local c_probar, c_salir, c_pose      -- las funciones del juego ya convertidas (se convierten UNA sola vez)
local t_void, t_char, t_uptr         -- tipos de ffi, tambien pedidos una sola vez
local CERO = string.char(0, 0, 0, 0)
local POSE       = 0x2b          -- edit_idle_loop, la pose de reposo del Editar
local T_POSES    = 0x142ba19c0   -- 45 poses de 0x1c bytes: id de cuerpo, id de cara, ...
local T_NOMBRES  = 0x143536b00   -- 16 punteros a los nombres de las poses de reposo
local O_RES_EN_IMPL = 0x1e0      -- Impl guarda aca el mismo puntero de RES_PTR
local O_CANDADO_IMPL = 0x510     -- candado del visor: el juego lo tiene tomado mientras atiende un pedido

local DIR
local cuerpo, cara = {}, {}          -- { ruta = archivo completo, nombre, cat = categoria, g = grupo, sg = categoria y grupo }
local pag = 1                        -- 1 = cuerpo, 2 = cara
local vistos, n_vistos = {}, 0
-- Lo que tiene que seguir existiendo aunque Sider vuelva a cargar este modulo con el juego abierto
-- (los nombres que le di al juego y que archivo va con cada uno). En init se guarda en ctx.
--   sirve["pa3_c....gani"] = archivo elegido      nombre_de["c|archivo"] = { nombre, dir }
--   vivos = los textos de los nombres (el juego guarda un puntero a ellos)   orig = punteros originales
local G = { sirve = {}, nombre_de = {}, vivos = {}, orig = {}, serie = 0 }
-- cada carga del modulo usa nombres distintos: asi nunca repite uno que el juego ya tenga con otro archivo
local SELLO = string.format("%05x", (type(os) == "table" and type(os.time) == "function") and (os.time() % 0x100000) or 0)
local exe_ok, exe_motivo = nil, ""
local sin_llamada = false
local aviso = ""

-- los dos canales de la pose: cuerpo y cara
-- sel = donde esta parado el cursor de la lista; en_juego = cual quedo escrita en el juego (0 = el original)
-- orig0 = donde esta el nombre original en el exe (solo se usa si al mirar ahi esta ese texto)
local CUERPO = { titulo = "CUERPO", letra = "c", o_rec = 0, t_idx = 0x143536808, o_marca = 0x34, orig0 = 0x142ba18f8,
                 nombre0 = "edit_idle_loop", sel = 0, en_juego = 0, estado = "", nota = "", cuenta = 0 }
local CARA   = { titulo = "CARA", letra = "f", o_rec = 4, t_idx = 0x1435368c8, o_marca = 0x36c, orig0 = 0x142ba1928,
                 nombre0 = "edit_idle_loop_face", sel = 0, en_juego = 0, estado = "", nota = "", cuenta = 0 }

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
    local s = memory.read(addr, n)
    if type(s) == "string" and #s == n then return s end
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

local function i32(addr)
    local v = u32(addr)
    if v and v >= 0x80000000 then v = v - 0x100000000 end
    return v
end

local function bytes64(v)
    local t = {}
    for i = 1, 8 do
        t[i] = string.char(v % 256)
        v = math.floor(v / 256)
    end
    return table.concat(t)
end

-- segundos (para saber cuanto hace que no se toca una tecla); nil si este Sider no tiene os.clock
local function reloj()
    if type(os) == "table" and type(os.clock) == "function" then return os.clock() end
    return nil
end

-------------------------------------------------------------------------------------------
-- que animaciones puede usar el jugador
-------------------------------------------------------------------------------------------
-- Cada .gani trae la lista de PIEZAS del esqueleto que mueve (un numero por pieza) y, de cada pieza,
-- sus pistas, cada una con un indice y un tipo. El juego usa ese indice sin comprobar que el modelo
-- que esta en pantalla lo tenga (funcion 0x1423ae2a0): una animacion de un OBJETO (una copa, un
-- cartel) puesta al jugador lee fuera de su tabla y cierra el juego. Paso el 10/11/2026 con
-- ..._cup_lg_ita2_hi (196 pistas contra las 27 del jugador). Por eso antes de poner una animacion
-- se lee su comienzo y solo se pone si trae EXACTAMENTE las piezas del jugador (cuerpo) o de su
-- cara, con los mismos indices y tipos.
-- "pieza:indice.tipo,..." medido en los 11003 .gani del catalogo (gani_firmas.py, 10/11/2026):
-- cuerpo = 15 piezas y 27 pistas (6980 archivos); cara = 32 piezas y 59 pistas (225 archivos)
local ESQUELETO = {
    c = "D3C3FF90:0.5,1.6 39BC5D3E:2.0,3.3 E96BC45A:4.0 C917522B:5.3,6.0,7.0 602089FA:8.0 80E81915:9.3,10.0,11.0 519FC0F1:12.0 DAC1DFE3:13.0 96DBCD2D:14.0 BEDBFB94:15.0 E7CE157E:16.0 9A9200CE:17.0,18.3,19.0,20.0 600C67FC:21.0 C555E93B:22.0,23.3,24.0,25.0 A3ED8071:26.0",
    f = "D3C3FF90:0.5,1.6 D7886F00:2.0,3.3 BF968528:4.0 B3C2A0D4:5.0,6.3 13984EAC:7.0,8.3 A13BA15B:9.0,10.3 079C59E8:11.0,12.3 7503D963:13.0,14.3 D6D21686:15.0,16.3 55011CD8:17.0,18.3 3AA921A1:19.0,20.3 9027ED31:21.0,22.3 16C455FF:23.0,24.3 DE427183:25.0,26.3 2D62FB55:27.0,28.3 A03480FA:29.0 73C2EA62:30.0 59935477:31.0 CCFCAE76:32.0 102F1F71:33.0,34.3 E263223A:35.0,36.3 FB40FE50:37.0,38.3 02767479:39.0,40.3 68D6B887:41.0,42.3 998D15DC:43.0,44.3 67B10931:45.0,46.3 6338490F:47.0,48.3 412160EC:49.0,50.3 87E8F2FF:51.0,52.3 3FBCB898:53.0,54.3 528F1ED6:55.0,56.3 FB5E99C0:57.0,58.3",
}
local MAGIA = string.char(0xD2, 0xA2, 0xFC, 0x0B)
local esq_de = {}                    -- letra del canal -> { n = cuantas piezas, [1..n] = pieza, de = { [pieza] = "0.5,1.6" } }
local aptos, por_que_no = {}, {}     -- "c|archivo" -> true / false, y el motivo
local malos = 0

local function n32(s, o)             -- numero de 4 bytes en la posicion o (contando desde 0) del texto s
    local a, b, c, d = string.byte(s, o + 1, o + 4)
    if not d then return nil end
    return a + b * 256 + c * 65536 + d * 16777216
end

local function esqueleto(letra)
    local e = esq_de[letra]
    if e then return e end
    e = { n = 0, de = {} }
    for pieza, idx in string.gmatch(ESQUELETO[letra], "(%x+):([%x%.,]+)") do
        e.n = e.n + 1
        e[e.n] = tonumber(pieza, 16)
        e.de[e[e.n]] = idx
    end
    esq_de[letra] = e
    return e
end

-- true si el .gani trae exactamente el esqueleto ref (las mismas piezas, en el mismo orden, con las
-- mismas pistas) y esta entero; si no, false y por que
local function examinar(path, ref)
    local f = io.open(path, "rb")
    if not f then return false, "falta el archivo" end
    local s = f:read(0x2000) or ""
    local tam = f:seek("end")
    f:close()
    if string.sub(s, 1, 4) ~= MAGIA then return false, "no es un .gani" end
    if #s < 0xA8 or string.sub(s, 0x51, 0x56) ~= "MOTION" or n32(s, 0x6C) ~= 0x30 then
        return false, "es un .gani de otra clase"
    end
    -- la cabecera dice cuanto pesa el archivo: uno mas corto esta cortado y sus pistas apuntan a datos que no estan
    local dice = n32(s, 8)
    if tam and dice and tam < dice then
        return false, string.format("esta cortado (pesa %d y su cabecera dice %d)", tam, dice)
    end
    local np, nt = n32(s, 0x90), n32(s, 0x94)
    if np ~= ref.n then return false, string.format("no es de jugador (trae %d piezas y %d pistas)", np, nt) end
    local pistas, t = 0, {}
    for i = 0, np - 1 do
        local q = 0x90 + (n32(s, 0xA4 + 4 * i) or 0x100000)
        local h, n = n32(s, q), string.byte(s, q + 5)
        if not h or not n or n < 1 or q + 8 + 8 * n > #s then return false, "no pude leer sus piezas" end
        for k = 1, n do
            local lo, hi, tipo = string.byte(s, q + 8 * k + 5, q + 8 * k + 7)
            t[k] = (lo + hi * 256) .. "." .. string.format("%X", tipo % 16)
        end
        for k = n + 1, #t do t[k] = nil end
        local idx = ref.de[h]
        if not idx then return false, "no es de jugador (mueve una pieza que el jugador no tiene)" end
        if ref[i + 1] ~= h then return false, "no es de jugador (trae las piezas en otro orden)" end
        if idx ~= table.concat(t, ",") then return false, "no es de jugador (una pieza trae otras pistas)" end
        pistas = pistas + n
    end
    if pistas ~= nt then return false, "su cabecera no cierra" end
    return true
end

-- se puede poner el archivo en el canal c? Para recorrer la lista se lee una sola vez por archivo;
-- al ponerlo y al entregarlo al juego (de_nuevo) se lee otra vez, por si cambio en el disco
local function apto(c, path, de_nuevo)
    local k = c.letra .. "|" .. path
    local v = aptos[k]
    if v ~= nil and not de_nuevo then return v, por_que_no[k] end
    local antes, por_que = v, nil
    v, por_que = examinar(path, esqueleto(c.letra))
    aptos[k], por_que_no[k] = v, por_que
    if not v and antes ~= false then
        malos = malos + 1
        if malos <= 40 then log(string.format("ProbadorAnimaciones: no se pone en %s: %s: %s", c.titulo, por_que, path)) end
    end
    return v, por_que
end

-------------------------------------------------------------------------------------------
-- el visor del Editar
-------------------------------------------------------------------------------------------
-- el exe tiene que ser el que conozco: miro el comienzo de las tres funciones que voy a llamar
local function exe_conocido()
    if exe_ok ~= nil then return exe_ok end
    if type(ffi) ~= "table" or type(memory) ~= "table" or type(memory.write) ~= "function" then
        exe_ok, exe_motivo = false, "a este Sider le falta ffi o memory: pone luajit.ext.enabled = 1 en sider.ini"
    elseif leer(FN_POSE, #FIRMA_POSE) ~= FIRMA_POSE or leer(FN_PROBAR, #FIRMA_PROBAR) ~= FIRMA_PROBAR
        or leer(FN_SALIR, #FIRMA_SALIR) ~= FIRMA_SALIR or leer(FN_PASO, #FIRMA_PASO) ~= FIRMA_PASO
        or leer(FN_FIJAR, #FIRMA_FIJAR) ~= FIRMA_FIJAR then
        exe_ok, exe_motivo = false, "este modulo solo anda con PES2021.exe 1.07.02 (o algo cambio esas funciones). No se toco nada"
    else
        -- convertir un numero en funcion crea tipos nuevos de ffi cada vez y la tabla de tipos tiene tope
        -- (65536 para TODOS los modulos de Sider): se hace una sola vez
        c_probar = ffi.cast("uint32_t (__fastcall *)(void *)", FN_PROBAR)
        c_salir = ffi.cast("uint32_t (__fastcall *)(void *)", FN_SALIR)
        c_pose = ffi.cast("uint8_t (__fastcall *)(void *, int32_t, float, uint8_t, uint8_t, uint8_t)", FN_POSE)
        t_void, t_char, t_uptr = ffi.typeof("void *"), ffi.typeof("char[?]"), ffi.typeof("uintptr_t")
        exe_ok = true
    end
    if not exe_ok then log("ProbadorAnimaciones: no puedo cambiar animaciones: " .. exe_motivo) end
    return exe_ok
end

-- ubica el puntero de nombre de un canal y comprueba que diga lo que tiene que decir
local function preparar(c)
    if c.celda then return true end
    local id = u32(T_POSES + POSE * 0x1c + c.o_rec)
    if id ~= 0x2b then return nil, "la tabla de poses no es la del 1.07.02" end
    local idx = u32(c.t_idx + 4 * (id - 0x29))
    if not idx or idx > 15 then return nil, "la tabla de nombres no es la del 1.07.02" end
    local celda = T_NOMBRES + 8 * idx
    local p = puntero(celda)
    if not p then return nil, "no pude leer el nombre de la pose" end
    local bien = c.nombre0 .. "\0"
    if leer(p, #bien) == bien then
        c.celda, c.orig, c.id = celda, p, id
        G.orig[c.letra] = p
    else
        -- no apunta al original. Solo sigo si es un nombre MIO de antes (Sider recargo el modulo con el
        -- juego abierto) y se donde esta el original; si no, lo cambio otro mod y no toco nada
        local o = G.orig[c.letra] or c.orig0
        if leer(p, 4) ~= "pa3_" or leer(o, #bien) ~= bien then
            return nil, "el nombre de la pose no es " .. c.nombre0 .. " (lo cambio otro mod?)"
        end
        c.celda, c.orig, c.id = celda, o, id
        c.puesto, c.en_juego = "una de antes", -1
        c.estado = "quedo puesta una animacion de antes (5 = original)"
    end
    log(string.format("ProbadorAnimaciones: %s: nombre de la pose en 0x%x (original 0x%x%s)", c.titulo, celda, c.orig,
        c.en_juego == -1 and "; habia una mia de antes" or ""))
    return true
end

-- un candado del juego esta armado si tiene un 1 en +0x28 (si no, sus funciones no hacen nada)
local function armado(cs)
    local b = leer(cs + 0x28, 1)
    return b ~= nil and string.byte(b) ~= 0
end

-- devuelve el visor, sus animaciones y su Impl, o nil y por que no
local function visor()
    local emv = puntero(EMV_PTR)
    if not emv then return nil, "entra a Editar y abri un jugador" end
    local pose = u32(emv + 0x100)            -- la ultima pose que el menu le pidio a este visor
    if not pose or pose < 0x29 or pose > 0x2b then
        return nil, "en esta pantalla el jugador esta en otra pose: anda a Editar > jugador (Apariencia)"
    end
    local task = puntero(emv + 0x78)
    local impl = task and puntero(task + 0x90)
    if not impl then return nil, "todavia no hay un jugador en pantalla" end
    local vt = leer(impl, 8)
    if not vt or numero(vt) ~= IMPL_VT then return nil, "no reconozco el visor del Editar" end
    local res = puntero(RES_PTR)
    if not res then return nil, "el visor todavia no cargo sus animaciones" end
    local res2 = puntero(impl + O_RES_EN_IMPL)
    if res2 and res2 ~= res then return nil, "el visor usa otras animaciones que las que conozco" end
    if not armado(CANDADO) or not armado(impl + O_CANDADO_IMPL) then return nil, "el visor todavia no esta listo" end
    return emv, res, impl
end

-- escribe el nombre, borra la marca de carga y le pide al visor la pose de reposo otra vez (como se
-- la pide el menu), todo con los candados del juego tomados en el orden del juego (primero el del
-- visor, despues el de las animaciones) pero SIN ESPERARLOS: si alguno esta ocupado no toca nada y
-- devuelve false (se reintenta en el cuadro siguiente). Asi este modulo nunca se queda esperando un
-- candado del juego mientras Sider espera a este modulo.
-- solo_pedir = no escribe nada, solo pide la pose (la animacion ya esta cargada: arranca de nuevo).
-- El juego toma todo tiempo menor o igual a 0 como 0; va distinto cada vez para que no lo descarte.
local function escribir(c, emv, impl, res, destino, solo_pedir)
    local dato = bytes64(destino)
    local cs_visor = impl and ffi.cast(t_void, impl + O_CANDADO_IMPL) or nil
    local cs_anim = (not solo_pedir) and armado(CANDADO) and ffi.cast(t_void, CANDADO) or nil
    if emv and not (cs_visor and (cs_anim or solo_pedir)) then return false end
    if cs_visor and c_probar(cs_visor) ~= 0 then return false end
    if cs_anim and c_probar(cs_anim) ~= 0 then
        if cs_visor then c_salir(cs_visor) end
        return false
    end
    -- ya con los candados: que el visor y sus animaciones sigan siendo los que mire antes de tomarlos
    local sigue = true
    if emv then
        local task = (puntero(EMV_PTR) == emv) and puntero(emv + 0x78) or nil
        sigue = (task ~= nil) and puntero(task + 0x90) == impl and (solo_pedir or puntero(RES_PTR) == res)
    elseif cs_anim then
        res = puntero(RES_PTR)
    end
    if sigue and not solo_pedir then
        memory.write(c.celda, dato)
        if res and cs_anim then memory.write(res + c.o_marca + 4 * c.id, CERO) end
    end
    if cs_anim then c_salir(cs_anim) end
    if sigue and emv then
        G.n_pedidos = (G.n_pedidos or (tonumber(SELLO, 16) % 997)) + 1
        c_pose(ffi.cast(t_void, emv), POSE, -((G.n_pedidos % 1000) + 1) * 0.000001, 1, 1, 0)
    end
    if cs_visor then c_salir(cs_visor) end
    return sigue
end

-- intenta escribir lo que quedo pedido en c.pend. Devuelve false si hay que seguir intentando
local function intentar(c)
    local p = c.pend
    local emv, res, impl = visor()
    if not emv then
        if p.e then
            c.pend = nil
            c.nota = "ESPERA: " .. res .. ". Despues apreta 4"
            return true
        end
        res, impl = puntero(RES_PTR), nil          -- el original: el puntero se repone aunque no haya visor
    end
    local hecho, solo = false, false
    if emv then
        local h = i32(res + c.o_marca + 4 * c.id)
        local igual = (puntero(c.celda) == p.destino)
        if igual and h == -1 then
            hecho = true                           -- eso mismo ya esta pedido y cargando: no se encola dos veces
        elseif igual and h and h > 0 then
            solo = true                            -- ya esta cargada: solo arranca de nuevo
        elseif h == -1 and p.intentos < 120 then
            p.intentos = p.intentos + 1            -- una carga por vez: espero a que termine la anterior
            c.nota = "esperando que termine la carga anterior..."
            return false
        end
    end
    if not hecho and not escribir(c, emv, impl, res, p.destino, solo) then
        p.intentos = p.intentos + 1
        if p.intentos < 300 then
            c.nota = "esperando al juego..."
            return false
        end
        c.pend = nil
        c.nota = "el juego esta ocupado y no pude cambiar nada. Apreta la tecla de nuevo"
        return true
    end
    c.pend = nil
    c.puesto, c.en_juego = p.nombre, (p.e and p.sel or 0)
    if not hecho then c.pidio = solo or (p.e == nil) end
    c.cuenta, c.vigilar = 0, (emv ~= nil)
    if p.e then
        if not solo and not hecho then
            log(string.format("ProbadorAnimaciones: %s -> %s (como %s.gani)", c.titulo, p.e.ruta, p.nombre))
        end
        c.nota = ""
    else
        log("ProbadorAnimaciones: " .. c.titulo .. " vuelve al original")
        c.nota = p.nota or ""
    end
    c.estado = emv and "cargando... (deja el overlay abierto hasta que cambie)" or "original repuesto"
    return true
end

-- pide poner la animacion e (o el original si e es nil) en el canal c. Si no se puede, deja una nota
-- y NO toca lo que ya estaba puesto ni deja de vigilarlo
local function poner(c, e, nota)
    c.nota = ""
    if sin_llamada then c.nota = "APAGADO: borra content\\probador-animaciones\\sin_llamada.txt y reinicia el juego" return false end
    if not exe_conocido() then c.nota = "NO SE PUEDE: " .. exe_motivo return false end
    local ok, por_que = preparar(c)
    if not ok then
        c.nota = "NO SE PUEDE: " .. por_que
        log("ProbadorAnimaciones: " .. c.titulo .. ": " .. por_que)
        return false
    end
    local p = { e = e, sel = c.sel, intentos = 0, destino = c.orig, nota = nota }
    if e then
        -- el archivo se mira ANTES que el visor: entre mirar el visor y usarlo no hay que tocar el disco
        local sirve, por_que2 = apto(c, e.ruta, true)
        if not sirve then
            c.nota = "no la puse: " .. por_que2 .. ". Segui con 0"
            return false
        end
        local emv, por_que3 = visor()
        if not emv then c.nota = "ESPERA: " .. por_que3 .. ". Despues apreta 4" return false end
        -- un nombre por archivo: si ya lo puse antes uso el mismo y el juego no lo vuelve a leer del disco
        local k = c.letra .. "|" .. e.ruta
        local d = G.nombre_de[k]
        if not d then
            G.serie = G.serie + 1
            local nombre = string.format("pa3_%s%s_%04d", c.letra, SELLO, G.serie)
            local buf = ffi.new(t_char, #nombre + 1)
            ffi.copy(buf, nombre)
            G.vivos[#G.vivos + 1] = buf
            d = { nombre = nombre, dir = tonumber(ffi.cast(t_uptr, buf)) }
            G.nombre_de[k] = d
            G.sirve[nombre .. ".gani"] = e.ruta
        end
        p.nombre, p.destino = d.nombre, d.dir
    end
    c.pend = p
    intentar(c)
    return true
end

-- se llama en cada cuadro con el overlay abierto: termina lo pendiente y compara lo que quedo
-- escrito con lo que el juego tiene cargado AHORA (no depende de que tecla se apreto antes)
local poner_elegida
local function mirar(c)
    if c.falta_poner then
        -- lo normal es ponerla al soltar la tecla (key_up); esto es por si este Sider no avisa el soltar
        c.n_tecla = (c.n_tecla or 0) + 1
        local t = reloj()
        if (t and c.t_tecla and t - c.t_tecla >= 1.2) or (not (t and c.t_tecla) and c.n_tecla >= 120) then poner_elegida(c) end
    end
    if c.pend and not intentar(c) then return end
    if not c.vigilar then return end
    local res = puntero(RES_PTR)
    if not res or not puntero(EMV_PTR) then        -- no hay jugador en pantalla: no cuento nada
        c.cuenta = 0
        if c.puesto then c.estado = "puesta: se va a ver cuando vuelvas a un jugador (5 = original)" end
        return
    end
    local h = i32(res + c.o_marca + 4 * c.id)
    if h and h > 0 then
        if c.cuenta >= 0 then
            log(string.format("ProbadorAnimaciones: %s cargada por el juego (%s, marca %d)", c.titulo, c.puesto or c.nombre0, h))
        end
        c.cuenta = -1
        if c.puesto then
            c.estado = "APLICADA"
        else
            c.estado = "original del juego"
            c.vigilar = false                      -- con el original puesto y cargado no queda nada mio
        end
        return
    end
    if c.cuenta < 0 then c.cuenta = 0 end          -- estaba cargada y ya no (jugador nuevo): vuelvo a contar
    c.estado = "cargando... (deja el overlay abierto hasta que cambie)"
    c.cuenta = c.cuenta + 1
    if c.cuenta < 300 then return end
    c.cuenta = 0
    if c.puesto then
        local que = c.pidio and "el juego pidio el archivo pero no lo cargo" or "el juego no llego a pedir el archivo"
        log(string.format("ProbadorAnimaciones: %s: %s (%s); vuelvo al original", c.titulo, que, c.puesto))
        c.pend = { e = nil, sel = 0, intentos = 120, destino = c.orig,     -- 120: no espera a la carga trabada
                   nota = "NO CARGO (" .. que .. "): deje el original. Segui con 0" }
        intentar(c)
    else
        c.estado = "el juego no esta cargando la pose: sali del jugador y volve a entrar"
    end
end

-------------------------------------------------------------------------------------------
-- archivos que pide el juego
-------------------------------------------------------------------------------------------
function m.make_key(ctx, filename)
    if string.lower(string.sub(filename, -5)) ~= ".gani" then return end
    local b = string.lower(basename(filename))
    local ruta = G.sirve[b]
    if ruta then
        local c = (string.sub(b, 5, 5) == "f") and CARA or CUERPO
        if c.puesto and c.puesto .. ".gani" == b then c.pidio = true end
        if not vistos[b] then
            vistos[b] = true
            log("ProbadorAnimaciones: el juego pide " .. filename .. " -> entrego " .. ruta)
        end
        return "pa3|" .. b
    end
    if not vistos[b] and n_vistos < 200 then
        vistos[b] = true
        n_vistos = n_vistos + 1
        log("ProbadorAnimaciones: el juego pide " .. filename)
    end
end

function m.get_filepath(ctx, filename, key)
    if key and string.sub(key, 1, 4) == "pa3|" then
        -- se vuelve a leer el comienzo del archivo: nunca se entrega algo que no sea de jugador
        local b = string.sub(key, 5)
        local ruta = G.sirve[b]
        if ruta and apto((string.sub(b, 5, 5) == "f") and CARA or CUERPO, ruta, true) then return ruta end
    end
end

-------------------------------------------------------------------------------------------
-- listas
-------------------------------------------------------------------------------------------
-- categorias: 8 y 7 saltan de una a la otra
local CATS = { "Editar y menus", "Gameplay (partido)", "Festejos de gol", "Ceremonias y final", "Entrada y previa",
               "Liga Master y myClub", "Arbitro, faltas, penales y cambios", "Gestos de cara", "Varias" }
-- comienzo del nombre de las escenas (carpeta FixDemo) -> categoria
local CAT_ESCENA = {
    dml_goal = 3, dml_hugset = 3, dml_offsidegoal = 3,
    dml_end = 4,
    dml_ent = 5, dml_tu = 5,
    dml_md = 6,
    dml_foul = 7, dml_pk = 7, dml_change = 7, dml_injury = 7, dml_assistant = 7,
    dml_menu = 1,
}

-- devuelve "cara" o "cuerpo", la categoria y un orden dentro de ella, o nil si no es de jugador
-- (por el nombre; lo que el nombre no dice lo decide despues el esqueleto del archivo)
local function clasificar(r, de_partido)
    local b = basename(r)
    if string.find(r, "\\hand\\", 1, true) or string.find(r, "audience", 1, true)
        or string.find(r, "standsflag", 1, true)
        or termina(b, "_lhand.gani") or termina(b, "_rhand.gani")   -- manos sueltas; "facehand", "onehand", "godhand" son de jugador
        or termina(b, "_eyes_target.gani") or termina(b, "_mobh.gani")
        or string.sub(b, 1, 8) == "dml_mobh" or string.sub(b, 1, 8) == "dml_prop"
        or string.find(b, "_enum_dummy", 1, true) then
        return nil
    end
    if de_partido then return "cuerpo", 2, 1 end            -- las del partido son todas de cuerpo (hay "..._face" que no son caras)
    local tipo = "cuerpo"
    if termina(b, "_face.gani") or string.find(r, "\\face\\", 1, true) then tipo = "cara" end
    if string.find(r, "\\face\\animations\\", 1, true) then
        return tipo, 8, string.find(r, "\\base\\", 1, true) and 1 or 2
    end
    if string.find(r, "\\interfaceload\\", 1, true) then return tipo, 1, 1 end
    if string.find(r, "\\interface\\", 1, true) then return tipo, 1, 2 end
    if string.find(r, "fixdemo", 1, true) then
        local a, c = string.match(b, "^(%a+)_(%a+)")
        if a ~= "dml" then return tipo, 1, 3 end            -- idle, match_home, uniselect_home: son de menu
        return tipo, CAT_ESCENA[a .. "_" .. c] or 9, 3
    end
    return tipo, 9, 4
end

-- grupo dentro de la categoria (3 y 2 saltan de uno al otro)
local function grupo_de(rel)
    local carpeta, nombre = string.match(rel, "^(.*)[\\/]([^\\/]+)$")
    if not carpeta then return "" end
    local corta = string.match(carpeta, "([^\\/]+)$") or carpeta
    local n = string.lower(nombre)
    if termina(string.lower(carpeta), "fixdemo\\animations") then
        -- carpeta gigante: la parto por el comienzo del nombre (dml_end_champione, dml_goal_celebrate_02**, ...)
        local a, c, d, resto = string.match(n, "^(%a+)_(%a+)_(%a+)(.*)$")
        if a then
            local cien = string.match(resto, "^_(%d%d)%d%d")
            return a .. "_" .. c .. "_" .. d .. (cien and ("_" .. cien .. "**") or "")
        end
        a, c = string.match(n, "^(%a+)_(%a+)")
        if a then return a .. "_" .. c end
        return string.match(n, "^(%a+)") or "otras"
    end
    if string.find(string.lower(carpeta), "\\face\\animations", 1, true) then
        return corta .. ": " .. (string.match(n, "^(%a+)") or "otras")
    end
    return corta
end

local function leer_listas()
    local todo, ya = { cara = {}, cuerpo = {} }, {}
    local total, afuera = 0, 0
    for _, nombre in ipairs({ "cara", "cuerpo", "partido" }) do
        local f = io.open(DIR .. nombre .. ".txt", "rb")
        if f then
            local txt = f:read("*a") or ""
            f:close()
            txt = string.gsub(txt, "^\239\187\191", "")         -- marca de UTF-8 del Bloc de notas
            local raiz, sin_raiz = "", false
            for line in string.gmatch(txt, "[^\r\n]+") do
                local l = string.match(line, "^%s*(.-)%s*$")
                local r = string.match(l, "^raiz%s*=%s*(.+)$")
                if r then
                    local c = string.sub(r, -1)
                    if c ~= "\\" and c ~= "/" then r = r .. "\\" end
                    raiz = r
                elseif l ~= "" and string.sub(l, 1, 1) ~= "#" and string.lower(string.sub(l, -5)) == ".gani" then
                    if raiz == "" and not sin_raiz then
                        sin_raiz = true
                        log("ProbadorAnimaciones: " .. nombre .. ".txt no empieza con la linea 'raiz = carpeta'")
                    end
                    l = string.gsub(l, "/", "\\")
                    local ruta = raiz .. l
                    local clave = string.lower(ruta)
                    if not ya[clave] then
                        ya[clave] = true
                        total = total + 1
                        local tipo, cat, rango = clasificar("\\" .. string.lower(l), nombre == "partido")
                        if tipo then
                            local t = todo[tipo]
                            local g = grupo_de(l)
                            t[#t + 1] = { ruta = ruta, nombre = basename(l), cat = cat, g = g, sg = cat .. "|" .. g,
                                          rango = rango, n = #t + 1 }
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
        if a.cat ~= b2.cat then return a.cat < b2.cat end
        if a.rango ~= b2.rango then return a.rango < b2.rango end
        return a.n < b2.n
    end
    table.sort(todo.cara, antes)
    table.sort(todo.cuerpo, antes)
    cara, cuerpo = todo.cara, todo.cuerpo
    CUERPO.lista, CARA.lista = cuerpo, cara
    for _, c in ipairs({ CUERPO, CARA }) do
        c.por_cat, c.orden_cat = {}, {}
        for _, e in ipairs(c.lista) do
            if not c.por_cat[e.cat] then
                c.por_cat[e.cat] = 0
                c.orden_cat[#c.orden_cat + 1] = e.cat
            end
            c.por_cat[e.cat] = c.por_cat[e.cat] + 1
        end
    end
    return total, afuera
end

-------------------------------------------------------------------------------------------
-- teclas y overlay
-------------------------------------------------------------------------------------------
-- primer lugar del grupo (campo "sg") o de la categoria (campo "cat") siguiente o anterior al de pos
local function salto(lista, pos, adelante, campo)      -- pos y resultado: indice desde 1
    local n = #lista
    local k = pos
    if k < 1 then return 1 end
    local g = lista[k][campo]
    if adelante then
        while k <= n and lista[k][campo] == g do k = k + 1 end
        if k > n then k = 1 end
    else
        while k >= 1 and lista[k][campo] == g do k = k - 1 end
        if k < 1 then k = n end
        local g2 = lista[k][campo]
        while k > 1 and lista[k - 1][campo] == g2 do k = k - 1 end
    end
    return k
end

-- lleva el cursor a la primera animacion de jugador del grupo o la categoria siguiente (o anterior);
-- si ese grupo no tiene ninguna sigue con el que le sigue. false = no encontro. Lee del disco 400
-- archivos como mucho por tecla; los ya leidos no cuentan, asi la tecla siguiente sigue desde ahi
local function saltar(c, adelante, campo)
    local lista, n, k, nuevos, vueltas = c.lista, #c.lista, c.sel, 0, 0
    while vueltas <= n do
        k = salto(lista, k, adelante, campo)
        local g, j = lista[k][campo], k
        while j <= n and lista[j][campo] == g do
            local ruta = lista[j].ruta
            if aptos[c.letra .. "|" .. ruta] == nil then
                nuevos = nuevos + 1
                if nuevos > 400 then return false end
            end
            if apto(c, ruta) then
                c.sel = j
                return true
            end
            j = j + 1
            vueltas = vueltas + 1
        end
    end
    return false
end

-- mueve el cursor de a uno (paso 1 o -1) hasta la proxima animacion que el jugador puede usar.
-- Devuelve cuantas salteo, o nil si no encontro (lee del disco 400 como mucho por tecla; los ya
-- leidos no cuentan, asi la tecla siguiente sigue desde ahi)
local function avanzar(c, paso)
    local n = #c.lista
    local nuevos = 0
    for i = 1, n do
        c.sel = c.sel + paso
        if c.sel > n then c.sel = 1 elseif c.sel < 1 then c.sel = n end
        local ruta = c.lista[c.sel].ruta
        if aptos[c.letra .. "|" .. ruta] == nil then
            nuevos = nuevos + 1
            if nuevos > 400 then return nil end
        end
        if apto(c, ruta) then return i - 1 end
    end
    return nil
end

poner_elegida = function(c)
    c.falta_poner = false
    if c.sel > 0 and c.lista[c.sel] then poner(c, c.lista[c.sel]) end
end

function m.key_down(ctx, vkey)
    aviso = ""
    local c = (pag == 1) and CUERPO or CARA
    local n = #c.lista
    if vkey == 0x36 then
        pag = (pag == 1) and 2 or 1
    elseif vkey == 0x35 then
        -- el cursor NO se mueve: despues de comparar con el original, 4 la vuelve a poner y 0 sigue
        c.falta_poner = false
        if not c.celda and not sin_llamada and exe_conocido() then preparar(c) end
        if c.celda and (c.puesto or c.pend or c.en_juego ~= 0) then
            poner(c, nil)
        else
            c.nota = "ya esta el original del juego"
        end
    elseif vkey == 0x34 then
        c.falta_poner = false
        if c.sel > 0 then poner(c, c.lista[c.sel]) else aviso = "primero elegi una animacion con 0 o 9" end
    elseif vkey == 0x30 or vkey == 0x39 or vkey == 0x38 or vkey == 0x37 or vkey == 0x33 or vkey == 0x32 then
        if n == 0 then
            aviso = "no hay animaciones de " .. string.lower(c.titulo) .. ": falta content\\probador-animaciones\\"
                .. string.lower(c.titulo) .. ".txt (o no trae archivos de " .. string.lower(c.titulo) .. ")"
            return
        end
        local salteo
        local estaba = c.sel
        if vkey == 0x30 or vkey == 0x39 then
            salteo = avanzar(c, (vkey == 0x30) and 1 or -1)
        elseif saltar(c, vkey == 0x38 or vkey == 0x33, (vkey == 0x38 or vkey == 0x37) and "cat" or "sg") then
            salteo = 0
        end
        if not salteo then
            -- el cursor vuelve a donde estaba; lo que estaba por ponerse al soltar la tecla sigue en pie
            c.sel = estaba
            c.nota = "no encontre animaciones de jugador cerca de ahi (mira sider.log): apreta de nuevo para seguir buscando"
            return
        end
        -- se pone al SOLTAR la tecla: dejarla apretada solo recorre la lista (no carga una por repeticion)
        c.falta_poner, c.t_tecla, c.n_tecla = true, reloj(), 0
        c.nota = "solta la tecla para ponerla"
        if salteo > 0 then c.nota = c.nota .. string.format(" (saltee %d que no son de jugador)", salteo) end
    end
end

function m.key_up(ctx, vkey)
    if vkey == 0x30 or vkey == 0x39 or vkey == 0x38 or vkey == 0x37 or vkey == 0x33 or vkey == 0x32 then
        if CUERPO.falta_poner then poner_elegida(CUERPO) end
        if CARA.falta_poner then poner_elegida(CARA) end
    end
end

local function renglones(t, c, marca)
    local e = c.sel > 0 and c.lista[c.sel]
    if e then
        t[#t + 1] = string.format("%s%s %d de %d   %s", marca, c.titulo, c.sel, #c.lista, e.nombre)
        local k = 0
        for i, cat in ipairs(c.orden_cat) do if cat == e.cat then k = i end end
        t[#t + 1] = string.format("     categoria %d de %d: %s (%d)   grupo: %s", k, #c.orden_cat, CATS[e.cat] or "?",
            c.por_cat[e.cat] or 0, e.g)
    else
        t[#t + 1] = string.format("%s%s: todavia no elegiste ninguna   (%d en la lista)", marca, c.titulo, #c.lista)
    end
    if c.nota ~= "" then t[#t + 1] = "     " .. c.nota end
    if c.estado ~= "" then
        local que = ""
        if c.en_juego ~= c.sel and c.en_juego >= 0 then   -- lo escrito en el juego no es lo que marca el cursor: lo digo
            local ej = c.en_juego > 0 and c.lista[c.en_juego]
            que = ej and ("en el juego sigue la " .. c.en_juego .. " (" .. ej.nombre .. "): ") or "en el juego esta el original: "
        end
        if c.en_juego == 0 and c.estado == "original del juego" then
            t[#t + 1] = "     en el juego esta el original" .. (c.sel > 0 and " (4 = poner la del cursor)" or "")
        else
            t[#t + 1] = "     " .. que .. c.estado
        end
    end
end

function m.overlay_on(ctx)
    mirar(CUERPO)
    mirar(CARA)
    local t = { string.format("ProbadorAnimaciones %s | con el jugador en pantalla en Editar (Apariencia), cambia en el momento", m.version) }
    renglones(t, CUERPO, (pag == 1) and "> " or "  ")
    renglones(t, CARA, (pag == 2) and "> " or "  ")
    t[#t + 1] = string.format("0 siguiente | 9 anterior | 8 / 7 categoria | 3 / 2 grupo | 4 de nuevo | 5 original | 6 pasar a %s | (tecla 1 = otro modulo)",
        (pag == 1) and "CARA" or "CUERPO")
    if G.serie >= 300 then
        t[#t + 1] = string.format("ya probaste %d animaciones distintas: si el juego se pone pesado, volve con Atras hasta el menu principal de Editar y entra de nuevo", G.serie)
    end
    if malos > 0 then t[#t + 1] = string.format("salteadas porque no son de jugador o faltan: %d (anotadas en sider.log)", malos) end
    if aviso ~= "" then t[#t + 1] = aviso end
    return table.concat(t, "\n")
end

function m.init(ctx)
    DIR = ctx.sider_dir .. "content\\probador-animaciones\\"
    local f = io.open(DIR .. "sin_llamada.txt", "rb")
    if f then
        f:close()
        sin_llamada = true
    end
    -- si Sider recarga el modulo con el juego abierto, sigo con los mismos nombres ya dados al juego
    local g = ctx.ProbadorAnimaciones3
    if type(g) == "table" and type(g.sirve) == "table" and type(g.nombre_de) == "table" and type(g.vivos) == "table"
        and type(g.orig) == "table" and type(g.serie) == "number" then
        G = g
    else
        ctx.ProbadorAnimaciones3 = G
    end
    local total, afuera = leer_listas()
    ctx.register("livecpk_make_key", m.make_key)
    ctx.register("livecpk_get_filepath", m.get_filepath)
    ctx.register("key_down", m.key_down)
    ctx.register("key_up", m.key_up)
    ctx.register("overlay_on", m.overlay_on)
    log(string.format("ProbadorAnimaciones %s listo (%d en las listas: %d de cuerpo, %d de cara, %d afuera; memoria %s; ffi %s%s)",
        m.version, total, #cuerpo, #cara, afuera, type(memory) == "table" and "si" or "NO",
        type(ffi) == "table" and "si" or "NO", sin_llamada and "; APAGADO por sin_llamada.txt" or ""))
end

return m
