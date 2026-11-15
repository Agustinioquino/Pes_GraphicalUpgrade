--[[
  CuerpoPropio.lua 0.1  -  PES21 FX (15/11/2026)  -  Sider 7 / PES 2021 1.07.02 (Steam)
  Necesita en sider.ini:  luajit.ext.enabled = 1

  QUE HACE
    Le agrega a cada jugador (por id, o a todos) un MODELO SUELTO propio, que el juego carga junto con el uniforme sin apagar
    nada: la "pieza por ruta" de la unidad Parts del juego (la misma que usa para face/real/<id>/suit.fmdl).
    Sirve para el cuerpo propio (torso + brazos en un solo modelo, fuera del common package) y para la pechera en cualquier modo.

  COMO (leido en el codigo del juego, informe decompilado/hallazgos/20261115_cuerpo_piezas_segun_uniforme.cpp.md)
    HumanList::vf14 (0x141ecd100) arma el pedido de modelo de cada humano:
        141ecd51c  call 0x141ecc5c0     arma la configuracion del personaje (r15 = config)
        141ecd521  call 0x141ea44d0     <- ESTE call se desvia a codigo propio
    El codigo propio mira el tipo de humano ([rbp-0x5c], 0..3 = jugador) y su id ([rbp-0x60]); si la lista le da una ruta llama
    FUN_141a09fe0(config, ruta) (guarda la ruta de la pieza) y despues sigue al destino original. No toca el uniforme.
    El juego no recorta nada mientras el NOMBRE del archivo no empiece con suit, shirt ni jersey.

  LISTA: modules\cuerpo_propio.txt (se lee al abrir el juego y con la tecla 3)
        * = /Assets/pes16/model/character/cuerpo/cuerpo_a.fmdl      a todos los jugadores
        7511 = /Assets/pes16/model/character/cuerpo/cuerpo_b.fmdl   solo ese id
        40352 = no                                                  ese id, sin pieza aunque haya *
    Lineas que empiezan con # o ; no cuentan.

  TECLAS (con el overlay de Sider abierto y este modulo elegido): 2 prende / apaga, 3 vuelve a leer la lista.
  El cambio se ve cuando el juego vuelve a armar al jugador (salir y entrar al jugador, o el proximo partido).

  SEGURIDAD: antes de escribir comprueba los bytes del juego en los tres lugares que usa; si algo no coincide no toca nada.
  Al apagar (tecla 2) el desvio queda puesto pero el codigo propio no hace nada.
]]
local m = { version = "0.1" }

local SITIO      = 0x141ecd521          -- call 0x141ea44d0 (el que se desvia)
local SIGUE      = 0x141ea44d0          -- destino original de ese call
local PONER_RUTA = 0x141a09fe0          -- FUN_141a09fe0(config, const char *ruta)
local MAX_ID     = 0x100000
local N_RUTAS    = 254                  -- indices 1..254 (0 = nada, 255 = "no")
-- lo que tiene que haber en el juego (PES 2021 1.07.02), desde SITIO - 12:
--   mov rdx, r15 / lea rcx, [rbp-0x60] / call 0x141ecc5c0 / call 0x141ea44d0 / test rax, rax
local ANTES_DIR  = SITIO - 12
local ANTES      = "\x49\x8B\xD7\x48\x8D\x4D\xA0\xE8\x9F\xF0\xFF\xFF\xE8\xAA\x6F\xFD\xFF\x48\x85\xC0"
local PONER_INI  = "\x48\x89\x5C\x24\x08\x57\x48\x83\xEC\x20\x48\x8B\x79\x10\x48\x8B\xC2"   -- comienzo de FUN_141a09fe0
local SIGUE_INI  = "\x48\x83\xEC\x38\x48\xC7\x44\x24\x20\xFE\xFF\xFF\xFF"                   -- comienzo de FUN_141ea44d0

-- bloque propio: +0 codigo, +0x800 estado, +0x1000 punteros a rutas, +0x2000 rutas (256 B c/u), +0x12000 tabla por id
local O_ESTADO, O_PUNT, O_RUTAS, O_TABLA = 0x800, 0x1000, 0x2000, 0x12000
local TAM = O_TABLA + MAX_ID

local S = { estado = "sin iniciar", base = nil, p8 = nil, on = false, rutas = {}, n_ids = 0, defecto = 0, archivo = nil }
local C, PROC, HECHO, NR

local function L(t) log("[CuerpoPropio] " .. tostring(t)) end

local function u32le(v)
    v = v % 0x100000000
    return string.char(v % 256, math.floor(v / 256) % 256, math.floor(v / 65536) % 256, math.floor(v / 16777216) % 256)
end
local function u64le(v) return u32le(v % 0x100000000) .. u32le(math.floor(v / 0x100000000)) end

-- codigo propio (se puede armar sin el juego: lo comprueba tools/sim/cuerpo_propio_asm.py)
function m.codigo(base)
    local estado, punt, tabla = base + O_ESTADO, base + O_PUNT, base + O_TABLA
    local c = "\x48\x83\xEC\x28"                         -- 00 sub rsp, 0x28
        .. "\x49\xBA" .. u64le(estado)                   -- 04 mov r10, estado
        .. "\x49\xFF\x42\x08"                            -- 0e inc qword [r10+8]            veces que paso
        .. "\x8B\x45\xA4" .. "\x41\x89\x42\x18"          -- 12 mov eax, [rbp-0x5c] / mov [r10+0x18], eax   ultimo tipo
        .. "\x8B\x4D\xA0" .. "\x41\x89\x4A\x1C"          -- 19 mov ecx, [rbp-0x60] / mov [r10+0x1c], ecx   ultimo id
        .. "\x41\x83\x3A\x00"                            -- 20 cmp dword [r10], 0           prendido
        .. "\x74\x58"                                    -- 24 je fin
        .. "\x83\xF8\x03"                                -- 26 cmp eax, 3                   solo jugadores (tipos 0..3)
        .. "\x77\x53"                                    -- 29 ja fin
        .. "\x8B\xC1"                                    -- 2b mov eax, ecx                 id
        .. "\x41\x8B\x4A\x04"                            -- 2d mov ecx, [r10+4]             indice para todos (*)
        .. "\x3D" .. u32le(MAX_ID)                       -- 31 cmp eax, MAX_ID
        .. "\x73\x14"                                    -- 36 jae usar
        .. "\x49\xBB" .. u64le(tabla)                    -- 38 mov r11, tabla
        .. "\x41\x0F\xB6\x04\x03"                        -- 42 movzx eax, byte [r11+rax]
        .. "\x85\xC0"                                    -- 47 test eax, eax
        .. "\x0F\x45\xC8"                                -- 49 cmovne ecx, eax              el id tiene indice propio
        .. "\x85\xC9"                                    -- 4c usar: test ecx, ecx
        .. "\x74\x2E"                                    -- 4e je fin
        .. "\x81\xF9\xFF\x00\x00\x00"                    -- 50 cmp ecx, 0xff                "no"
        .. "\x74\x26"                                    -- 56 je fin
        .. "\x49\xBB" .. u64le(punt)                     -- 58 mov r11, punteros
        .. "\x49\x8B\x14\xCB"                            -- 62 mov rdx, [r11+rcx*8]
        .. "\x48\x85\xD2"                                -- 66 test rdx, rdx
        .. "\x74\x13"                                    -- 69 je fin
        .. "\x49\xFF\x42\x10"                            -- 6b inc qword [r10+0x10]         veces que puso la pieza
        .. "\x4C\x89\xF9"                                -- 6f mov rcx, r15                 config
        .. "\x48\xB8" .. u64le(PONER_RUTA)               -- 72 mov rax, FUN_141a09fe0
        .. "\xFF\xD0"                                    -- 7c call rax
        .. "\x48\x83\xC4\x28"                            -- 7e fin: add rsp, 0x28
        .. "\x48\xB8" .. u64le(SIGUE)                    -- 82 mov rax, FUN_141ea44d0
        .. "\xFF\xE0"                                    -- 8c jmp rax
    return c
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

-- lista -> tabla del bloque. Se escribe primero todo lo nuevo y al final el indice para todos.
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
                if v == "no" then idx = 255
                elseif string.sub(v, 1, 1) == "/" and #v < 250 then
                    idx = por_ruta[v]
                    if not idx and #rutas < N_RUTAS then rutas[#rutas + 1] = v; idx = #rutas; por_ruta[v] = idx end
                end
                if idx then
                    if k == "*" then defecto = idx
                    else ids[id] = idx end
                end
            end
        end
        f:close()
    end
    -- apagar mientras se cambia
    local estado = ffi.cast("uint32_t *", p8 + O_ESTADO)
    local prendido = estado[0]
    estado[0] = 0
    estado[1] = 0
    ffi.fill(p8 + O_TABLA, MAX_ID, 0)
    local punt = ffi.cast("uint64_t *", p8 + O_PUNT)
    for i = 0, 255 do punt[i] = 0 end
    for i = 1, #rutas do
        local d = p8 + O_RUTAS + i * 256
        ffi.fill(d, 256, 0)
        ffi.copy(d, rutas[i], #rutas[i])
        punt[i] = S.base + O_RUTAS + i * 256
    end
    local n = 0
    for id, idx in pairs(ids) do p8[O_TABLA + id] = idx; n = n + 1 end
    if defecto == 255 then defecto = nil end
    estado[1] = defecto or 0
    estado[0] = prendido
    S.rutas, S.n_ids, S.defecto = rutas, n, defecto or 0
    L("lista: " .. #rutas .. " rutas, " .. n .. " ids, para todos: " .. (defecto and rutas[defecto] or "nada"))
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
    local r1 = string.format("CuerpoPropio %s: %s, %s [2] | lista [3]: %d rutas, %d ids | paso %d, puso %d | ultimo: id %d tipo %d",
        m.version, tostring(S.estado), S.on and "PRENDIDO" or "apagado", #S.rutas, S.n_ids,
        tonumber(paso), tonumber(puso), tonumber(e32[7]), tonumber(e32[6]))
    local r2 = "para todos: " .. (S.defecto > 0 and tostring(S.rutas[S.defecto]) or "nada")
    return string.sub(r1, 1, 112) .. "\n" .. string.sub(r2, 1, 112)
end

function m.init(ctx)
    iniciar(ctx)
    if S.estado ~= "activo" then L("NO activo: " .. tostring(S.estado)) end
    ctx.register("key_down", m.key_down)
    ctx.register("overlay_on", m.overlay_on)
end

return m
