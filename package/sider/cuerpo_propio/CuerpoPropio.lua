--[[
  CuerpoPropio.lua 0.3  -  PES21 FX (16/11/2026)  -  Sider 7 / PES 2021 1.07.02 (Steam)
  Necesita en sider.ini:  luajit.ext.enabled = 1

  QUE HACE
    Le agrega a cada jugador (por id, o a todos) un MODELO SUELTO propio, que el juego carga junto con el uniforme sin apagar
    nada: la "pieza por ruta" de la unidad Parts del juego (la misma que usa para face/real/<id>/suit.fmdl).
    Sirve para el cuerpo propio (torso + brazos en un solo modelo, fuera del common package) y para la pechera en cualquier modo.

  COMO (leido en el codigo del juego, informe decompilado/hallazgos/20261115_cuerpo_piezas_segun_uniforme.cpp.md)
    HumanList::vf14 (0x141ecd100) arma el pedido de modelo de cada humano:
        141ecd51c  call 0x141ecc5c0     arma la configuracion del personaje (r15 = config)
        141ecd521  call 0x141ea44d0     <- ESTE call se desvia a codigo propio
    El codigo propio mira el tipo de humano ([rbp-0x5c], 0..3 = jugador), su id ([rbp-0x60]) y su tono de piel; si la lista le
    da una ruta llama FUN_141a09fe0(config, ruta) (guarda la ruta de la pieza) y despues sigue al destino original.
    El juego no recorta nada mientras el NOMBRE del archivo no empiece con suit, shirt ni jersey.

  LISTA: modules\cuerpo_propio.txt (se lee al abrir el juego y con la tecla 3)
        * = /Assets/pes16/model/character/cuerpo/cuerpo_a_#.fmdl      a todos los jugadores
        7511 = /Assets/pes16/model/character/cuerpo/cuerpo_b_#.fmdl   solo ese id
        40352 = no                                                    ese id, sin pieza aunque haya *
        7511 = /Assets/.../cuerpo_c_#.fmdl | sin_brazo                ademas el juego NO carga su brazo para ese jugador
    Un # en la ruta se cambia por el tono de piel del jugador (1..6; el juego lo deja en config+0x160; 0 y 7 valen 1).
    Una @ en la ruta se cambia por la FORMA del torso que el juego usaria para ese jugador (0.3): a = remera afuera,
    b = remera adentro del pantalon (config+0x64 == 0), c = remera afuera y uniforme ajustado (dato "tight" del kit:
    K = *(FUN_1419ab080(*0x1437d9af0, config+0x60) + 8), byte K+0x18; lo mismo que mira UniformUnit para pants_out_sub_tight).
    Si el kit todavia no esta en la lista del juego vale "a".
    "| sin_brazo" apaga la unidad Arm del juego (bit 8 de la mascara config+0x44): solo para modelos que traen los brazos.
    Lineas que empiezan con ; no cuentan.

  TECLAS (con el overlay de Sider abierto y este modulo elegido): 2 prende / apaga, 3 vuelve a leer la lista.
  El cambio se ve cuando el juego vuelve a armar al jugador (salir y entrar al jugador, o el proximo partido).

  SEGURIDAD: antes de escribir comprueba los bytes del juego en los tres lugares que usa; si algo no coincide no toca nada.
  Al apagar (tecla 2) el desvio queda puesto pero el codigo propio no hace nada.
]]
local m = { version = "0.3" }

local SITIO      = 0x141ecd521          -- call 0x141ea44d0 (el que se desvia)
local SIGUE      = 0x141ea44d0          -- destino original de ese call
local PONER_RUTA = 0x141a09fe0          -- FUN_141a09fe0(config, const char *ruta)
local MAX_ID     = 0x100000
local N_RUTAS    = 254                  -- entradas de la lista: indices 1..254 (0 = nada, 255 = "no")
-- lo que tiene que haber en el juego (PES 2021 1.07.02), desde SITIO - 12:
--   mov rdx, r15 / lea rcx, [rbp-0x60] / call 0x141ecc5c0 / call 0x141ea44d0 / test rax, rax
local ANTES_DIR  = SITIO - 12
local ANTES      = "\x49\x8B\xD7\x48\x8D\x4D\xA0\xE8\x9F\xF0\xFF\xFF\xE8\xAA\x6F\xFD\xFF\x48\x85\xC0"
local PONER_INI  = "\x48\x89\x5C\x24\x08\x57\x48\x83\xEC\x20\x48\x8B\x79\x10\x48\x8B\xC2"   -- comienzo de FUN_141a09fe0
local KITS_GLOBAL = 0x1437d9af0         -- lo que devuelve FUN_1419aa670 (mov rax, [rip+0x1e2f479] / ret)
local KITS_LEE    = 0x1419aa670
local KITS_LEE_INI = "\x48\x8B\x05\x79\xF4\xE2\x01\xC3"
local KIT_BUSCA   = 0x1419ab080         -- FUN_1419ab080(lista, id de kit): busqueda en un arbol, sin llamadas ni candados
local KIT_BUSCA_INI = "\x48\x8B\x41\x08\x48\x8B\x48\x08\x48\x85\xC9\x0F\x85\x8F\xE7\xFB\xFF\x33\xC0\xC3"
local SIGUE_INI  = "\x48\x83\xEC\x38\x48\xC7\x44\x24\x20\xFE\xFF\xFF\xFF"                   -- comienzo de FUN_141ea44d0

-- bloque propio: +0 codigo, +0x800 estado, +0xc00 banderas por entrada, +0x1000 punteros a rutas (entrada x 8 tonos x 4 formas),
-- +0x11000 textos de las rutas, +0x91000 tabla por id (un byte: entrada)
local O_ESTADO, O_BAND, O_PUNT, O_RUTAS, O_TABLA = 0x800, 0xC00, 0x1000, 0x11000, 0x91000
local TAM_RUTAS = O_TABLA - O_RUTAS
local FORMAS = { "a", "b", "c", "a" }
local TAM = O_TABLA + MAX_ID

local S = { estado = "sin iniciar", base = nil, p8 = nil, on = false, rutas = {}, n_ids = 0, defecto = 0, archivo = nil }
local C, PROC, HECHO, NR

local function L(t) log("[CuerpoPropio] " .. tostring(t)) end

local function u32le(v)
    v = v % 0x100000000
    return string.char(v % 256, math.floor(v / 256) % 256, math.floor(v / 65536) % 256, math.floor(v / 16777216) % 256)
end
local function u64le(v) return u32le(v % 0x100000000) .. u32le(math.floor(v / 0x100000000)) end

-- ensamblador minimo: textos de bytes, { "etq", nombre } y { "salto", opcode de 2 bytes, nombre } (salto de 32 bits)
local function armar(partes)
    local pos, etq = 0, {}
    for i = 1, #partes do
        local p = partes[i]
        if type(p) == "string" then pos = pos + #p
        elseif p[1] == "etq" then etq[p[2]] = pos
        else pos = pos + 6 end
    end
    local out = {}
    pos = 0
    for i = 1, #partes do
        local p = partes[i]
        if type(p) == "string" then out[#out + 1] = p; pos = pos + #p
        elseif p[1] == "salto" then
            pos = pos + 6
            out[#out + 1] = p[2] .. u32le(etq[p[3]] - pos)
        end
    end
    return table.concat(out)
end
local JE, JA, JAE, JNE, JMP = "\x0F\x84", "\x0F\x87", "\x0F\x83", "\x0F\x85", "\x90\xE9"   -- JMP = nop + jmp de 32 bits

-- codigo propio (se puede armar sin el juego: lo comprueba y lo EJECUTA tools/sim/cuerpo_propio_asm.py)
function m.codigo(base)
    return armar({
        "\x48\x83\xEC\x28",                              -- sub rsp, 0x28
        "\x49\xBA" .. u64le(base + O_ESTADO),            -- mov r10, estado
        "\x49\xFF\x42\x08",                              -- inc qword [r10+8]            veces que paso
        "\x8B\x45\xA4" .. "\x41\x89\x42\x18",            -- mov eax, [rbp-0x5c] / mov [r10+0x18], eax   ultimo tipo
        "\x8B\x4D\xA0" .. "\x41\x89\x4A\x1C",            -- mov ecx, [rbp-0x60] / mov [r10+0x1c], ecx   ultimo id
        "\x41\x83\x3A\x00", { "salto", JE, "fin" },      -- cmp dword [r10], 0 / je fin   apagado
        "\x83\xF8\x03", { "salto", JA, "fin" },          -- cmp eax, 3 / ja fin           solo jugadores (tipos 0..3)
        "\x8B\xC1",                                      -- mov eax, ecx                  id
        "\x41\x8B\x4A\x04",                              -- mov ecx, [r10+4]              entrada para todos (*)
        "\x3D" .. u32le(MAX_ID), { "salto", JAE, "usar" }, -- cmp eax, MAX_ID / jae usar
        "\x49\xBB" .. u64le(base + O_TABLA),             -- mov r11, tabla
        "\x41\x0F\xB6\x04\x03",                          -- movzx eax, byte [r11+rax]
        "\x85\xC0",                                      -- test eax, eax
        "\x0F\x45\xC8",                                  -- cmovne ecx, eax               el id tiene entrada propia
        { "etq", "usar" },
        "\x85\xC9", { "salto", JE, "fin" },              -- test ecx, ecx / je fin        nada
        "\x81\xF9\xFF\x00\x00\x00", { "salto", JE, "fin" },   -- cmp ecx, 0xff / je fin    "no"
        "\x4D\x8B\x4F\x10",                              -- mov r9, [r15+0x10]            C = datos de la configuracion
        "\x4D\x85\xC9", { "salto", JE, "fin" },          -- test r9, r9 / je fin
        -- (0.3) forma del torso: 0 = remera afuera, 1 = adentro, 2 = afuera y ajustada. [rsp+0x20] = entrada, [rsp+0x24] = forma
        "\x89\x4C\x24\x20",                              -- mov [rsp+0x20], ecx
        "\xC7\x44\x24\x24\x00\x00\x00\x00",              -- mov dword [rsp+0x24], 0
        "\x41\x83\x79\x64\x00", { "salto", JNE, "afuera" }, -- cmp dword [r9+0x64], 0 / jne afuera
        "\xC7\x44\x24\x24\x01\x00\x00\x00",              -- mov dword [rsp+0x24], 1       remera adentro
        { "salto", JMP, "forma" },                       -- jmp forma
        { "etq", "afuera" },
        "\x48\xB8" .. u64le(KITS_GLOBAL),                -- mov rax, &lista de kits
        "\x48\x8B\x08",                                  -- mov rcx, [rax]
        "\x48\x85\xC9", { "salto", JE, "forma" },         -- test rcx, rcx / je forma
        "\x48\x8B\x41\x08",                              -- mov rax, [rcx+8]
        "\x48\x85\xC0", { "salto", JE, "forma" },         -- test rax, rax / je forma
        "\x41\x8B\x51\x60",                              -- mov edx, [r9+0x60]            id del kit
        "\x48\xB8" .. u64le(KIT_BUSCA),                  -- mov rax, FUN_1419ab080
        "\xFF\xD0",                                      -- call rax
        "\x48\x85\xC0", { "salto", JE, "forma" },         -- test rax, rax / je forma      kit sin cargar
        "\x48\x8B\x40\x08",                              -- mov rax, [rax+8]              K
        "\x48\x85\xC0", { "salto", JE, "forma" },         -- test rax, rax / je forma
        "\x80\x78\x18\x00", { "salto", JE, "forma" },     -- cmp byte [rax+0x18], 0 / je forma
        "\xC7\x44\x24\x24\x02\x00\x00\x00",              -- mov dword [rsp+0x24], 2       ajustada
        { "etq", "forma" },
        "\x4D\x8B\x4F\x10",                              -- mov r9, [r15+0x10]            (la llamada pudo pisar r9 / r10 / rcx)
        "\x49\xBA" .. u64le(base + O_ESTADO),            -- mov r10, estado
        "\x8B\x4C\x24\x20",                              -- mov ecx, [rsp+0x20]           entrada
        "\x41\x8B\x81\x60\x01\x00\x00",                  -- mov eax, [r9+0x160]           tono de piel
        "\x83\xE0\x07",                                  -- and eax, 7
        "\x41\x89\x42\x20",                              -- mov [r10+0x20], eax           ultimo tono
        "\x49\xBB" .. u64le(base + O_PUNT),              -- mov r11, punteros
        "\x8D\x04\xC8",                                  -- lea eax, [rax+rcx*8]          entrada * 8 + tono
        "\xC1\xE0\x02",                                  -- shl eax, 2
        "\x8B\x54\x24\x24",                              -- mov edx, [rsp+0x24]           forma
        "\x41\x89\x52\x24",                              -- mov [r10+0x24], edx           ultima forma
        "\x03\xC2",                                      -- add eax, edx                  (entrada * 8 + tono) * 4 + forma
        "\x49\x8B\x14\xC3",                              -- mov rdx, [r11+rax*8]          ruta
        "\x48\x85\xD2", { "salto", JE, "fin" },          -- test rdx, rdx / je fin
        "\x49\xBB" .. u64le(base + O_BAND),              -- mov r11, banderas
        "\x41\xF6\x04\x0B\x01", { "salto", JE, "poner" }, -- test byte [r11+rcx], 1 / je poner
        "\x41\x83\x61\x44\xF7",                          -- and dword [r9+0x44], ~8       sin la unidad Arm del juego
        { "etq", "poner" },
        "\x49\xFF\x42\x10",                              -- inc qword [r10+0x10]          veces que puso la pieza
        "\x4C\x89\xF9",                                  -- mov rcx, r15                  config
        "\x48\xB8" .. u64le(PONER_RUTA),                 -- mov rax, FUN_141a09fe0
        "\xFF\xD0",                                      -- call rax
        { "etq", "fin" },
        "\x48\x83\xC4\x28",                              -- add rsp, 0x28
        "\x48\xB8" .. u64le(SIGUE),                      -- mov rax, FUN_141ea44d0
        "\xFF\xE0",                                      -- jmp rax
    })
end

local function leer(dir, n)
    local buf = ffi.new("uint8_t[?]", n)
    if C.cpo_ReadProcessMemory(PROC, dir, buf, n, NR) == 0 or tonumber(NR[0]) ~= n then return nil end
    return ffi.string(buf, n)
end

local function escribir_protegido(dir, bytes)
    local n = #bytes
    local buf = ffi.new("uint8_t[?]", n)
    ffi.copy(buf, bytes, n)
    if C.cpo_VirtualProtect(dir, n, 0x40, HECHO) == 0 then return false end
    local antes = HECHO[0]
    local ok = C.cpo_WriteProcessMemory(PROC, dir, buf, n, NR) ~= 0
    C.cpo_VirtualProtect(dir, n, antes, HECHO)
    C.cpo_FlushInstructionCache(PROC, dir, n)
    return ok
end

local function reservar_cerca(destino, tam)
    local base = math.floor(destino / 0x10000) * 0x10000
    for i = 1, 1900 do
        for s = 1, 2 do
            local a = base + (s == 1 and i or -i) * 0x100000
            if a > 0x10000 and math.abs(a - destino) < 0x7F000000 then
                local p = C.cpo_VirtualAlloc(a, tam, 0x3000, 0x40)     -- MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE
                if p ~= 0 then return tonumber(p) end
            end
        end
    end
    return nil
end

-- lista -> tablas del bloque. Mientras se cambia queda apagado.
local function cargar_lista()
    local p8 = S.p8
    local rutas, por_ruta, ids, defecto = {}, {}, {}, nil
    local f = io.open(S.archivo, "r")
    if f then
        for linea in f:lines() do
            local k, v = string.match(linea, "^%s*([%*%d]+)%s*=%s*(.-)%s*$")
            local id = k and k ~= "*" and tonumber(k) or nil
            if k and v and v ~= "" and (k == "*" or (id and id >= 0 and id < MAX_ID)) then
                local idx
                local ruta, opc = string.match(v, "^(.-)%s*|%s*(.-)$")
                if not ruta then ruta, opc = v, "" end
                if ruta == "no" then idx = 255
                elseif string.sub(ruta, 1, 1) == "/" and #ruta < 240 then
                    local clave = ruta .. "|" .. opc
                    idx = por_ruta[clave]
                    if not idx and #rutas < N_RUTAS then
                        rutas[#rutas + 1] = { ruta = ruta, sin_brazo = (opc == "sin_brazo") }
                        idx = #rutas; por_ruta[clave] = idx
                    end
                end
                if idx then
                    if k == "*" then defecto = idx else ids[id] = idx end
                end
            end
        end
        f:close()
    end
    local estado = ffi.cast("uint32_t *", p8 + O_ESTADO)
    local prendido = estado[0]
    estado[0] = 0
    estado[1] = 0
    ffi.fill(p8 + O_TABLA, MAX_ID, 0)
    ffi.fill(p8 + O_BAND, 256, 0)
    ffi.fill(p8 + O_PUNT, 256 * 8 * 4 * 8, 0)
    local punt = ffi.cast("uint64_t *", p8 + O_PUNT)
    local libre, textos = 0, {}
    local function texto(t)                    -- un mismo texto se guarda una sola vez
        if textos[t] then return textos[t] end
        if libre + #t + 1 > TAM_RUTAS then return 0 end
        local d = S.base + O_RUTAS + libre
        ffi.copy(p8 + O_RUTAS + libre, t, #t)
        p8[O_RUTAS + libre + #t] = 0
        libre = libre + #t + 1
        textos[t] = d
        return d
    end
    for i = 1, #rutas do
        local r = rutas[i]
        for tono = 0, 7 do
            local n = (tono >= 1 and tono <= 6) and tono or 1
            local t = string.gsub(r.ruta, "#", tostring(n))
            for forma = 0, 3 do
                punt[(i * 8 + tono) * 4 + forma] = texto((string.gsub(t, "@", FORMAS[forma + 1])))
            end
        end
        p8[O_BAND + i] = r.sin_brazo and 1 or 0
    end
    local n = 0
    for id, idx in pairs(ids) do p8[O_TABLA + id] = idx; n = n + 1 end
    if defecto == 255 then defecto = nil end
    estado[1] = defecto or 0
    estado[0] = prendido
    S.rutas, S.n_ids, S.defecto = rutas, n, defecto or 0
    L("lista: " .. #rutas .. " entradas, " .. n .. " ids, para todos: " .. (defecto and rutas[defecto].ruta or "nada"))
end

local function iniciar(ctx)
    if type(ffi) ~= "table" or type(ffi.cdef) ~= "function" or type(ffi.new) ~= "function" or type(ffi.cast) ~= "function"
        or type(ffi.string) ~= "function" or type(ffi.copy) ~= "function" or type(ffi.fill) ~= "function" then
        S.estado = "falta ffi (luajit.ext.enabled = 1 en sider.ini)"; return
    end
    ffi.cdef[[
        void *cpo_GetCurrentProcess(void) __asm__("GetCurrentProcess");
        int cpo_ReadProcessMemory(void *h, uintptr_t dir, void *buf, size_t n, size_t *hecho) __asm__("ReadProcessMemory");
        int cpo_WriteProcessMemory(void *h, uintptr_t dir, const void *buf, size_t n, size_t *hecho) __asm__("WriteProcessMemory");
        int cpo_VirtualProtect(uintptr_t dir, size_t n, uint32_t prot, uint32_t *antes) __asm__("VirtualProtect");
        uintptr_t cpo_VirtualAlloc(uintptr_t dir, size_t n, uint32_t tipo, uint32_t prot) __asm__("VirtualAlloc");
        int cpo_FlushInstructionCache(void *h, uintptr_t dir, size_t n) __asm__("FlushInstructionCache");
    ]]
    C = ffi.C
    PROC = C.cpo_GetCurrentProcess()
    HECHO = ffi.new("uint32_t[1]")
    NR = ffi.new("size_t[1]")
    S.archivo = ctx.sider_dir .. "modules\\cuerpo_propio.txt"

    -- 1) los tres lugares del juego tienen que ser los esperados
    local a = leer(ANTES_DIR, #ANTES)
    if a ~= ANTES then
        -- ya desviado por este mismo modulo en otra carga de Sider? (no se vuelve a escribir)
        S.estado = "el juego no tiene los bytes esperados en 0x141ecd515 (otra version u otro mod): no toco nada"; return
    end
    if leer(PONER_RUTA, #PONER_INI) ~= PONER_INI then S.estado = "FUN_141a09fe0 no es la esperada: no toco nada"; return end
    if leer(KITS_LEE, #KITS_LEE_INI) ~= KITS_LEE_INI then S.estado = "FUN_1419aa670 no es la esperada: no toco nada"; return end
    if leer(KIT_BUSCA, #KIT_BUSCA_INI) ~= KIT_BUSCA_INI then S.estado = "FUN_1419ab080 no es la esperada: no toco nada"; return end
    if leer(SIGUE, #SIGUE_INI) ~= SIGUE_INI then S.estado = "FUN_141ea44d0 no es la esperada: no toco nada"; return end

    -- 2) bloque propio cerca del exe (para que el call de 5 bytes llegue)
    local base = reservar_cerca(SITIO, TAM)
    if not base then S.estado = "no pude reservar memoria cerca del exe"; return end
    S.base = base
    S.p8 = ffi.cast("uint8_t *", base)
    local cod = m.codigo(base)
    ffi.copy(S.p8, cod, #cod)
    C.cpo_FlushInstructionCache(PROC, base, #cod)
    cargar_lista()
    ffi.cast("uint32_t *", S.p8 + O_ESTADO)[0] = 1
    S.on = true

    -- 3) el desvio: solo los 4 bytes del destino del call
    local rel = base - (SITIO + 5)
    if rel < -0x7FFFFFF0 or rel > 0x7FFFFFF0 then S.estado = "el bloque quedo demasiado lejos"; return end
    if rel < 0 then rel = rel + 0x100000000 end
    if not escribir_protegido(SITIO + 1, u32le(rel)) then S.estado = "Windows no me dejo escribir el desvio"; return end
    local puesto = leer(SITIO, 5)
    if puesto ~= "\xE8" .. u32le(rel) then S.estado = "el desvio no quedo como se escribio"; return end
    S.estado = "activo"
    L(string.format("activo: desvio en 0x%X -> bloque 0x%X", SITIO, base))
end

function m.key_down(ctx, vkey)
    if not S.p8 then return end
    if vkey == 0x32 then            -- 2
        S.on = not S.on
        ffi.cast("uint32_t *", S.p8 + O_ESTADO)[0] = S.on and 1 or 0
        L(S.on and "prendido" or "apagado")
    elseif vkey == 0x33 then        -- 3
        cargar_lista()
    end
end

function m.overlay_on(ctx)
    if not S.p8 then return string.sub("CuerpoPropio " .. m.version .. ": " .. tostring(S.estado), 1, 112) end
    local e32 = ffi.cast("uint32_t *", S.p8 + O_ESTADO)
    local paso, puso = e32[2], e32[4]          -- parte baja de los contadores de 64 bits
    local r1 = string.format("CuerpoPropio %s: %s, %s [2] | lista [3]: %d entradas, %d ids",
        m.version, tostring(S.estado), S.on and "PRENDIDO" or "apagado", #S.rutas, S.n_ids)
    local r2 = string.format("paso %d, puso %d | ultimo: id %d tipo %d tono %d forma %s",
        tonumber(paso), tonumber(puso), tonumber(e32[7]), tonumber(e32[6]), tonumber(e32[8]), FORMAS[tonumber(e32[9]) % 4 + 1])
    local r3 = "para todos: " .. (S.defecto > 0 and tostring(S.rutas[S.defecto].ruta) or "nada")
    return string.sub(r1, 1, 112) .. "\n" .. string.sub(r2, 1, 112) .. "\n" .. string.sub(r3, 1, 112)
end

function m.init(ctx)
    iniciar(ctx)
    if S.estado ~= "activo" then L("NO activo: " .. tostring(S.estado)) end
    ctx.register("key_down", m.key_down)
    ctx.register("overlay_on", m.overlay_on)
end

return m
