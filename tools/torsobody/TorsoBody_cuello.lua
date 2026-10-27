-- TorsoBody_cuello.lua  (TorsoBody v11.3g; 1.10 de PES21 FX)
-- (1.10) Las caras cuya textura viene sin mips chicos (solo la grande) no se fundian ("sin mip chico" en sider.log: el cuello
-- quedaba de otro color y el corte se veia): el promedio ahora se arma de la textura grande. El color del torso del tono 1
-- es el de las texturas propias del usuario (torso_1_bsm retocado; el de TorsoBody era 155 / 115 / 95). Y PERFIL = true
-- anota en sider.log la forma del cuello de cada cara (ancho por angulo y altura cerca del borde del torso: solo medidas),
-- para darle al torso de TorsoBody la forma del cuello de Konami.
-- Las caras reales (escaneadas o hechas en Blender) traen un pedazo de cuello que baja en V sobre el pecho. Con el
-- torso de TorsoBody ese pedazo queda pegado a la piel del torso y las dos superficies se pisan (parpadeos, manchas).
-- Cuando el juego carga una cara real (face.fpk), este modulo le corta en memoria ese pedazo justo en el borde del
-- torso: la cara termina exactamente donde empieza el torso, igual que el cuello generico. No toca ningun archivo.
-- Solo se cortan la malla de piel (fox_skin_mat) y la capa collar_ao (detalle del cuello, por ejemplo al gritar) que
-- bajan del borde. Lo que queda arriba del borde no se mueve.
-- Desde la 1.2 ademas, cuando se carga la textura de esa cara (face_bsm_alp.ftex), se funden los ultimos 2,5 cm del
-- cuello con el color del torso y se borra la sombra/claviculas que los facemakers pintan abajo del cuello, para que
-- el cuello no se vea "pegado". Solo cambia esa franja de la textura, en memoria. En sider.log queda anotada cada cara.
-- Desde la 1.5 se reconocen tambien las caras exportadas desde Blender o convertidas de FIFA (material
-- "fox_skin_mat.001" y parecidos: se mira el shader). Desde la 1.6 el fundido busca el cuello en cualquier lugar de la
-- textura (las caras de FIFA lo tienen mas arriba y con los dientes abajo) y no toca nada que use otra parte de la cara.
-- Para no fundir la textura (solo cortar), poner FUNDIR = false aca abajo.

local FUNDIR = true      -- (1.9: prendido otra vez; los cierres del juego al salir no eran de aca. false = solo cortar)
-- (1.9) Normales del cuello en la union: abajo del todo el cuello de la cara toma las normales del borde del torso (las mismas
-- que tiene el cuello generico de TorsoBody, que empalma perfecto) y en los 2 cm de arriba pasa de a poco a las suyas. Asi la
-- luz no hace un escalon en la union cuando se ve el torso solo (sin camiseta). false = dejar las normales de la cara.
local NORMALES_TORSO = true
-- La capa collar_ao (la que agrega detalle al cuello, por ejemplo al gritar) se corta en el borde del torso y lo
-- de arriba queda igual. Con true se quita entera, como en las caras genericas de TorsoBody.
local QUITAR_COLLAR_AO = false
-- El tono de piel del torso se adivina por el color del cuello de la cara (el log dice cual eligio). Si en algun
-- jugador no coincide con el tono que tiene en el juego, se puede forzar aca: ["id de la cara"] = tono (1 a 6)
-- ejemplo: local TONO_FORZADO = { ["12345"] = 4, ["67890"] = 2 }
local TONO_FORZADO = {}
-- El compilador JIT de LuaJIT 2.1.0-beta3 (el que trae Sider) a veces compila mal los bucles largos de este modulo
-- y salen valores raros que fuera del juego no aparecen. Con true el modulo corre interpretado (solo este modulo:
-- los demas no cambian). Cuesta 0,3-0,8 s por textura de cara, una sola vez por cara.
local SIN_JIT = true
-- Cache en disco: la primera vez que se ve una cara se guardan el face.fpk cortado y la textura fundida en
-- Dream Patch Sider\content\TorsoBody_cuello_cache\ y las proximas veces Sider los sirve desde ahi sin procesar nada
-- (sin tirones en los menus). Si cambias el modelo o la textura de una cara, borra sus archivos de esa carpeta.
local USAR_CACHE = true
-- (1.10) anota en sider.log la forma del cuello de cada cara real (una vez por cara y por sesion)
local PERFIL = true
-- todo el modulo va adentro de una funcion para poder apagarle el JIT solo a el (jit.off(f, true) alcanza a todas
-- las funciones definidas adentro)
local NOJIT = false
local function modulo_cuello()
local m = { version = "1.13" }

local AXZ = 0.045          -- eje del cuello (z)
-- borde del cuello del torso TorsoBody v11.3c (x, y, z), ordenado por angulo alrededor del cuello
local LOOP = {
    {-0.02091796, 1.54977679, -0.02082366}, {-0.02596982, 1.55022728, -0.02014828}, {-0.03238868, 1.55158091, -0.01776943}, {-0.05597220, 1.55507994, -0.01210320},
    {-0.06619130, 1.55667996, -0.00152161}, {-0.07641040, 1.55827999, 0.00905999}, {-0.07733240, 1.55562174, 0.02799818}, {-0.07160983, 1.54583168, 0.04567898},
    {-0.06993975, 1.54327011, 0.05087034}, {-0.06722610, 1.52743995, 0.06561520}, {-0.06395049, 1.51579320, 0.07701001}, {-0.06309394, 1.51259041, 0.08065862},
    {-0.05803733, 1.50282192, 0.09062101}, {-0.05049378, 1.49520111, 0.10048424}, {-0.04963913, 1.49439657, 0.10127188}, {-0.04786197, 1.49330199, 0.10226797},
    {-0.03630891, 1.48727536, 0.10632483}, {-0.03150925, 1.48517525, 0.10845437}, {-0.02275776, 1.48195851, 0.10950296}, {-0.01660986, 1.48024583, 0.11014716},
    {-0.01220494, 1.47923958, 0.11016960}, {-0.00903625, 1.47869587, 0.11039033}, {-0.00428022, 1.47827744, 0.11066870}, {0.00000008, 1.47800004, 0.11053656},
    {0.00428052, 1.47827756, 0.11066917}, {0.00903657, 1.47869599, 0.11039079}, {0.01220420, 1.47923934, 0.11017062}, {0.01660888, 1.48024547, 0.11014830},
    {0.02275626, 1.48195779, 0.10950498}, {0.03150724, 1.48517430, 0.10845628}, {0.03630902, 1.48727548, 0.10632481}, {0.04785028, 1.49329388, 0.10227673},
    {0.04963555, 1.49439347, 0.10127607}, {0.05049255, 1.49519944, 0.10048728}, {0.05803050, 1.50281358, 0.09062916}, {0.06309199, 1.51258850, 0.08066031},
    {0.06395046, 1.51579320, 0.07700997}, {0.06722600, 1.52743995, 0.06561510}, {0.06994201, 1.54327142, 0.05086858}, {0.07160910, 1.54582822, 0.04568661},
    {0.07733440, 1.55562007, 0.02800415}, {0.07641040, 1.55827999, 0.00906000}, {0.06619130, 1.55667996, -0.00152160}, {0.05597220, 1.55507994, -0.01210320},
    {0.03238868, 1.55158091, -0.01776942}, {0.02596982, 1.55022728, -0.02014828}, {0.02091796, 1.54977679, -0.02082366}, {0.00000261, 1.54799998, -0.02344613},
}
-- (1.9) normal del borde del torso en cada punto de LOOP (= la del cuello generico de TorsoBody en el mismo punto)
local RING_N = {
    {-0.0794, 0.4877, -0.8694}, {-0.1166, 0.4939, -0.8617}, {-0.1772, 0.5227, -0.8339}, {-0.3052, 0.6295, -0.7145},
    {-0.4518, 0.7380, -0.5011}, {-0.5627, 0.7884, -0.2486}, {-0.6951, 0.6712, 0.2577}, {-0.7397, 0.4998, 0.4507},
    {-0.7405, 0.4550, 0.4946}, {-0.4822, 0.5432, 0.6873}, {-0.2911, 0.6378, 0.7130}, {-0.2826, 0.6237, 0.7288},
    {-0.2816, 0.5818, 0.7630}, {-0.2001, 0.5524, 0.8092}, {-0.1976, 0.5407, 0.8177}, {-0.1960, 0.5353, 0.8216},
    {-0.0816, 0.4849, 0.8707}, {-0.0528, 0.4730, 0.8795}, {-0.0199, 0.4458, 0.8949}, {0.0252, 0.4543, 0.8905},
    {0.0501, 0.4367, 0.8982}, {0.0484, 0.4436, 0.8949}, {0.0188, 0.4530, 0.8913}, {-0.0000, 0.4550, 0.8905},
    {-0.0188, 0.4530, 0.8913}, {-0.0484, 0.4436, 0.8949}, {-0.0501, 0.4367, 0.8982}, {-0.0252, 0.4545, 0.8904},
    {0.0199, 0.4458, 0.8949}, {0.0528, 0.4730, 0.8795}, {0.0816, 0.4849, 0.8707}, {0.1960, 0.5353, 0.8216},
    {0.1976, 0.5407, 0.8177}, {0.2001, 0.5524, 0.8092}, {0.2816, 0.5818, 0.7630}, {0.2826, 0.6237, 0.7288},
    {0.2911, 0.6378, 0.7130}, {0.4822, 0.5432, 0.6873}, {0.7404, 0.4552, 0.4945}, {0.7396, 0.5000, 0.4506},
    {0.6951, 0.6712, 0.2577}, {0.5627, 0.7884, -0.2486}, {0.4518, 0.7380, -0.5011}, {0.3052, 0.6295, -0.7145},
    {0.1772, 0.5227, -0.8339}, {0.1166, 0.4939, -0.8617}, {0.0794, 0.4877, -0.8694}, {0.0000, 0.4566, -0.8897},
}
-- intentos: {corte (m por debajo del borde), vertices hasta esta altura sobre el borde cuentan como borde,
--            puntos de corte casi iguales que se juntan (m), [vertices de abajo que se pueden dejar (m)]}
local TRIES = {
    { 0.00005, -0.00005, 0 },
    { 0.00005, -0.00005, 0.00002 },
    { 0.00005, -0.00015, 0.00002 },
    { 0.00005, -0.00015, 0.00002, 0.0015 },   -- 4o valor: se pueden dejar vertices de abajo (hasta esa profundidad extra)
    { 0.0003, -0.00015, 0.00002, 0.003 },
    { 0.0010, -0.00015, 0.00002, 0.006 },
    { 0.0010, -0.00015, 0.0001, 0.015 },
}

local floor, deg, rad, atan2, sqrt, abs, cos, sin = math.floor, math.deg, math.rad, math.atan2, math.sqrt, math.abs, math.cos, math.sin
local frexp = math.frexp
local byte, char = string.byte, string.char

-- ---------- borde del torso ----------
local NL = #LOOP
local LPH = {}
for i = 1, NL do LPH[i] = deg(atan2(LOOP[i][1], LOOP[i][3] - AXZ)) end
local function loop_y(phi)                 -- altura del borde del torso en el angulo phi (interseccion exacta)
    local i
    if phi < LPH[1] or phi >= LPH[NL] then i = NL
    else
        local lo, hi = 1, NL
        while hi - lo > 1 do
            local mid = floor((lo + hi) / 2)
            if LPH[mid] <= phi then lo = mid else hi = mid end
        end
        i = lo
    end
    local a, b = LOOP[i], LOOP[i % NL + 1]
    local r = rad(phi); local c, s = cos(r), sin(r)
    local sa = a[1] * c - (a[3] - AXZ) * s
    local sb = b[1] * c - (b[3] - AXZ) * s
    local t = 0
    if abs(sa - sb) > 1e-15 then t = sa / (sa - sb) end
    return a[2] + t * (b[2] - a[2])
end
local function depth(p)                    -- > 0: por debajo del borde del torso
    return loop_y(deg(atan2(p[1], p[3] - AXZ))) - p[2]
end
local function ring_n(phi)                 -- (1.9) normal del borde del torso en el angulo phi (interpolada como loop_y)
    local i
    if phi < LPH[1] or phi >= LPH[NL] then i = NL
    else
        local lo, hi = 1, NL
        while hi - lo > 1 do
            local mid = floor((lo + hi) / 2)
            if LPH[mid] <= phi then lo = mid else hi = mid end
        end
        i = lo
    end
    local j = i % NL + 1
    local a, b = LOOP[i], LOOP[j]
    local r = rad(phi); local c, s = cos(r), sin(r)
    local sa = a[1] * c - (a[3] - AXZ) * s
    local sb = b[1] * c - (b[3] - AXZ) * s
    local t = 0
    if abs(sa - sb) > 1e-15 then t = sa / (sa - sb) end
    if t < 0 then t = 0 elseif t > 1 then t = 1 end
    local na, nb = RING_N[i], RING_N[j]
    local x, y, z = na[1] + (nb[1] - na[1]) * t, na[2] + (nb[2] - na[2]) * t, na[3] + (nb[3] - na[3]) * t
    local l = sqrt(x * x + y * y + z * z); if l < 1e-9 then return na end
    return { x / l, y / l, z / l }
end

local function ring_r(phi)                 -- (1.12) distancia horizontal del eje al borde del torso en el angulo phi
    local y = loop_y(phi)
    local i
    if phi < LPH[1] or phi >= LPH[NL] then i = NL
    else
        local lo, hi = 1, NL
        while hi - lo > 1 do
            local mid = floor((lo + hi) / 2)
            if LPH[mid] <= phi then lo = mid else hi = mid end
        end
        i = lo
    end
    local a, b = LOOP[i], LOOP[i % NL + 1]
    local r = rad(phi); local c, s = cos(r), sin(r)
    local sa = a[1] * c - (a[3] - AXZ) * s
    local sb = b[1] * c - (b[3] - AXZ) * s
    local t = 0
    if abs(sa - sb) > 1e-15 then t = sa / (sa - sb) end
    local x, z = a[1] + (b[1] - a[1]) * t, a[3] + (b[3] - a[3]) * t
    return sqrt(x * x + (z - AXZ) ^ 2), y
end

-- ---------- lectura / escritura binaria ----------
-- Todo se hace sobre una copia del face.fpk en un string: Sider entrega la direccion del buffer como un puntero
-- con el que no se pueden hacer cuentas, asi que se lee el archivo entero de una vez, se cambian los bytes en la
-- copia y se escribe el archivo entero de vuelta (leer y escribir solo con la direccion que da Sider).
local BUF, PATCH = nil, nil
local function rd(off, n) return BUF:sub(off + 1, off + n) end           -- off: desde el principio del face.fpk
local function wr(off, s) PATCH[#PATCH + 1] = { off, s } end
local function apply_patches(buf, patches)
    table.sort(patches, function(x, y) return x[1] < y[1] end)
    local parts, pos = {}, 0
    for _, p in ipairs(patches) do
        local off, s = p[1], p[2]
        if off < pos or off + #s > #buf then return nil end               -- (no deberia pasar nunca)
        parts[#parts + 1] = buf:sub(pos + 1, off)
        parts[#parts + 1] = s
        pos = off + #s
    end
    parts[#parts + 1] = buf:sub(pos + 1)
    return table.concat(parts)
end
local PC = pcall or (type(_G) == "table" and _G.pcall) or nil           -- (Sider no siempre deja usar pcall)
local function u8(s, o) return byte(s, o + 1) end
local function u16(s, o) local a, b = byte(s, o + 1, o + 2); return a + b * 256 end
local function u32(s, o) local a, b, c, d = byte(s, o + 1, o + 4); return a + b * 256 + c * 65536 + d * 16777216 end
local function f32(s, o)
    local u = u32(s, o)
    local sign = 1
    if u >= 2147483648 then sign = -1; u = u - 2147483648 end
    local e = floor(u / 8388608); local mt = u % 8388608
    if e == 0 then return sign * mt * 2 ^ -149 end
    if e == 255 then return 0 end
    return sign * (1 + mt / 8388608) * 2 ^ (e - 127)
end
local function pf32(v)
    local sign = 0
    if v < 0 or (v == 0 and 1 / v < 0) then sign = 2147483648; v = -v end
    local u
    if v == 0 then u = 0
    else
        local mt, e = frexp(v)
        e = e - 1; mt = mt * 2
        if e < -126 then u = floor(v / 2 ^ -149 + 0.5)
        else
            local mm = floor((mt - 1) * 8388608 + 0.5)
            if mm >= 8388608 then mm = 0; e = e + 1 end
            u = (e + 127) * 8388608 + mm
        end
    end
    u = u + sign
    return char(u % 256, floor(u / 256) % 256, floor(u / 65536) % 256, floor(u / 16777216) % 256)
end
local function h2f(h)
    local sign = 1
    if h >= 32768 then sign = -1; h = h - 32768 end
    local e = floor(h / 1024); local mt = h % 1024
    if e == 0 then return sign * mt * 2 ^ -24 end
    if e == 31 then return 0 end
    return sign * (1 + mt / 1024) * 2 ^ (e - 15)
end
local function f2h(v)
    local sign = 0
    if v < 0 or (v == 0 and 1 / v < 0) then sign = 32768; v = -v end
    if v ~= v then return 32256 end
    if v < 2 ^ -25 then return sign end
    local mt, e = frexp(v); e = e - 1; mt = mt * 2
    if e < -14 then return sign + floor(v / 2 ^ -24 + 0.5) end
    if e > 15 then return sign + 31744 end
    local mm = floor((mt - 1) * 1024 + 0.5)
    if mm >= 1024 then mm = 0; e = e + 1; if e > 15 then return sign + 31744 end end
    return sign + (e + 15) * 1024 + mm
end
local function ph16(h) return char(h % 256, floor(h / 256)) end

-- ---------- FMDL ----------
local SIZES = { [0] = 48, [1] = 8, [2] = 32, [3] = 48, [4] = 16, [5] = 68, [6] = 4, [7] = 4, [8] = 4, [9] = 8, [10] = 8,
                [11] = 4, [12] = 8, [13] = 32, [14] = 16, [16] = 16, [17] = 8, [18] = 8, [20] = 128 }

local function parse_fmdl(base, size)
    local h = rd(base, 56)
    if h:sub(1, 4) ~= "FMDL" then return nil, "no es FMDL" end
    local desc = u32(h, 8)
    local n0, n1 = u32(h, 32), u32(h, 36)
    local s0off, s1off = u32(h, 40), u32(h, 48)
    if desc + n0 * 8 + n1 * 12 > size then return nil, "cabecera rara" end
    local d = rd(base + desc, n0 * 8 + n1 * 12)
    local b0, b1 = {}, {}
    for i = 0, n0 - 1 do
        local id, cnt, off = u16(d, i * 8), u16(d, i * 8 + 2), u32(d, i * 8 + 4)
        if SIZES[id] then b0[id] = { addr = base + s0off + off, count = cnt, esize = SIZES[id] } end
    end
    for i = 0, n1 - 1 do
        local o = n0 * 8 + i * 12
        local id, off, len = u32(d, o), u32(d, o + 4), u32(d, o + 8)
        b1[id] = { addr = base + s1off + off, len = len }
    end
    return { b0 = b0, b1 = b1 }
end
local function entry(F, id, i)
    local b = F.b0[id]
    if not b or i >= b.count then return nil end
    return rd(b.addr + i * b.esize, b.esize)
end
local function str(F, sid)
    local e = entry(F, 12, sid); if not e then return "" end
    local blk, len, off = u16(e, 0), u16(e, 2), u32(e, 4)
    local b = F.b1[blk]; if not b then return "" end
    return rd(b.addr + off, len)
end

local function mesh_layout(F, mfid)
    local a = entry(F, 9, mfid); if not a then return nil end
    local nmf, nvf, firstmf, firstvf = u8(a, 0), u8(a, 1), u16(a, 4), u16(a, 6)
    local buf = F.b1[2]; if not buf then return nil end
    local offs, strides = {}, {}
    for i = firstmf, firstmf + nmf - 1 do
        local e = entry(F, 10, i); if not e then return nil end
        local bid, cnt, stride, boff = u8(e, 0), u8(e, 1), u8(e, 2), u32(e, 4)
        local bo = entry(F, 14, bid); if not bo then return nil end
        local base = buf.addr + u32(bo, 8) + boff
        for k = 1, cnt do offs[#offs + 1] = base; strides[#strides + 1] = stride end
    end
    if #offs ~= nvf then return nil end
    local L = {}
    for k = 0, nvf - 1 do
        local e = entry(F, 11, firstvf + k)
        L[#L + 1] = { type = u8(e, 0), fmt = u8(e, 1), addr = offs[k + 1] + u16(e, 2), stride = strides[k + 1] }
    end
    return L
end

local FMT_SIZE = { [1] = 12, [2] = 8, [6] = 8, [7] = 4, [8] = 4, [9] = 4 }
local function read_attr(f, s, o)
    if f == 1 then return { f32(s, o), f32(s, o + 4), f32(s, o + 8) }
    elseif f == 2 then return { f32(s, o), f32(s, o + 4) }
    elseif f == 6 then return { h2f(u16(s, o)), h2f(u16(s, o + 2)), h2f(u16(s, o + 4)), h2f(u16(s, o + 6)) }
    elseif f == 7 then return { h2f(u16(s, o)), h2f(u16(s, o + 2)) }
    else return { u8(s, o), u8(s, o + 1), u8(s, o + 2), u8(s, o + 3) } end
end
local bad_vals = 0                      -- valores no validos (NaN, nil, fuera de rango) corregidos al escribir
local function fok(x)                   -- numero finito, si no 0
    if type(x) ~= "number" or x ~= x or x == 1 / 0 or x == -1 / 0 then bad_vals = bad_vals + 1; return 0 end
    return x
end
local function b8(x)                    -- byte 0..255
    if type(x) ~= "number" or x ~= x then bad_vals = bad_vals + 1; return 0 end
    if x < 0 then bad_vals = bad_vals + 1; return 0 elseif x > 255 then bad_vals = bad_vals + 1; return 255 end
    return floor(x + 0.5)
end
local function enc_attr(f, v)
    if f == 1 then return pf32(fok(v[1])) .. pf32(fok(v[2])) .. pf32(fok(v[3]))
    elseif f == 2 then return pf32(fok(v[1])) .. pf32(fok(v[2]))
    elseif f == 6 then return ph16(f2h(fok(v[1]))) .. ph16(f2h(fok(v[2]))) .. ph16(f2h(fok(v[3]))) .. ph16(f2h(fok(v[4])))
    elseif f == 7 then return ph16(f2h(fok(v[1]))) .. ph16(f2h(fok(v[2])))
    else return char(b8(v[1]), b8(v[2]), b8(v[3]), b8(v[4])) end
end

local function lerp(a, b, t)
    local r = {}
    for k = 1, #a do r[k] = a[k] + (b[k] - a[k]) * t end
    return r
end
local function nrm3(v)
    local l = sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3])
    if l > 1e-12 then v[1] = v[1] / l; v[2] = v[2] / l; v[3] = v[3] / l end
    return v
end

-- ---------- malla ----------
local function read_mesh(F, mi)
    local me = entry(F, 3, mi)
    local mfid, nv, firstface = u16(me, 8), u16(me, 10), u32(me, 16)
    local lodbase = u32(me, 24)
    local L = mesh_layout(F, mfid); if not L then return nil, "formato desconocido" end
    local P, attrs = nil, {}
    for _, a in ipairs(L) do
        if not FMT_SIZE[a.fmt] then return nil, "formato de vertice desconocido" end
        local s = rd(a.addr, (nv - 1) * a.stride + FMT_SIZE[a.fmt])
        local vals = {}
        for i = 0, nv - 1 do vals[i] = read_attr(a.fmt, s, i * a.stride) end
        a.vals = vals
        if a.type == 0 and a.fmt == 1 then P = vals end
        attrs[#attrs + 1] = a
    end
    if not P then return nil, "sin posiciones" end
    local below = false
    for i = 0, nv - 1 do if P[i][2] < 1.47 or depth(P[i]) > 0 then below = true; break end end
    if not below then return nil end                 -- no baja del borde del torso: nada que hacer
    local lodc = entry(F, 16, 0); local nlod = lodc and u32(lodc, 0) or 1
    if nlod < 1 or nlod > 16 then nlod = 1 end
    local ranges, seen = {}, {}
    for l = 0, nlod - 1 do
        local e = entry(F, 17, lodbase + l)
        if e then
            local first, cnt = firstface + u32(e, 0), u32(e, 4)
            local key = first .. ":" .. cnt
            if not seen[key] and cnt > 0 then seen[key] = true; ranges[#ranges + 1] = { first, cnt } end
        end
    end
    if #ranges == 0 then return nil, "sin caras" end
    local ib = F.b0[14] and entry(F, 14, 2); if not ib then return nil, "sin indices" end
    local ibase = F.b1[2].addr + u32(ib, 8)
    local tris = {}
    for r = 1, #ranges do
        local first, cnt = ranges[r][1], ranges[r][2]
        local s = rd(ibase + first * 2, cnt * 2)
        local T = {}
        for k = 0, floor(cnt / 3) - 1 do
            local a, b, c = u16(s, k * 6), u16(s, k * 6 + 2), u16(s, k * 6 + 4)
            if a >= nv or b >= nv or c >= nv then return nil, "indices fuera de rango" end
            if a ~= b and b ~= c and a ~= c then T[#T + 1] = { a, b, c } end      -- (los vacios no cuentan)
        end
        tris[r] = T
    end
    local M = { nv = nv, P = P, attrs = attrs, ranges = ranges, tris = tris, ibase = ibase }
    for _, a in ipairs(attrs) do
        if a.type == 1 then M.W = a end
        if a.type == 7 then M.I = a end
        if a.type == 8 and not M.UVA then M.UVA = a end
    end
    M.d = {}
    for i = 0, nv - 1 do M.d[i] = depth(P[i]) end
    return M
end

-- datos de un punto sobre la arista a-b (todos los atributos; huesos aparte)
local function point_vals(M, a, b, t)
    local vals = {}
    for _, at in ipairs(M.attrs) do
        if at.type ~= 1 and at.type ~= 7 then vals[at] = lerp(at.vals[a], at.vals[b], t) end
    end
    if M.W and M.I then
        local acc = {}
        for k = 1, 4 do
            local wa, ia = M.W.vals[a][k], M.I.vals[a][k]
            if wa > 0 then acc[ia] = (acc[ia] or 0) + wa / 255 * (1 - t) end
            local wb, ibb = M.W.vals[b][k], M.I.vals[b][k]
            if wb > 0 then acc[ibb] = (acc[ibb] or 0) + wb / 255 * t end
        end
        vals.bones = acc
    end
    return vals
end
-- vertice nuevo = promedio de varios puntos
local function merge_vals(M, list, pos)
    local nvals, n = {}, #list
    for _, at in ipairs(M.attrs) do
        if at.type ~= 1 and at.type ~= 7 then
            local r = {}
            for k = 1, #list[1][at] do
                local sum = 0
                for _, v in ipairs(list) do sum = sum + v[at][k] end
                r[k] = sum / n
            end
            if at.fmt == 8 or at.fmt == 9 then for k = 1, 4 do r[k] = floor(r[k] + 0.5) end end
            if at.type == 2 then nrm3(r) end
            if at.type == 14 then nrm3(r); r[4] = list[1][at][4] end
            if at.type == 0 and pos then r = { pos[1], pos[2], pos[3] } end
            nvals[at] = r
        end
    end
    if M.W and M.I then
        local acc = {}
        for _, v in ipairs(list) do for bi, w in pairs(v.bones) do acc[bi] = (acc[bi] or 0) + w / n end end
        local lst = {}
        for bi, w in pairs(acc) do lst[#lst + 1] = { bi, w } end
        table.sort(lst, function(x, y) return x[2] > y[2] end)
        local nn = math.min(4, #lst); local tot = 0
        for k = 1, nn do tot = tot + lst[k][2] end
        local ww, ii, ssum = { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, 0
        for k = 1, nn do ii[k] = lst[k][1]; ww[k] = floor(lst[k][2] / tot * 255 + 0.5); ssum = ssum + ww[k] end
        if nn > 0 then ww[1] = ww[1] + (255 - ssum) end
        nvals[M.W] = ww; nvals[M.I] = ii
    end
    return nvals
end

local function bisect(pa, pb, Tat)          -- punto de la arista a la profundidad del corte (el borde no es lineal)
    local lo, hi = 0, 1
    for _ = 1, 40 do
        local mid = (lo + hi) / 2
        local q = { pa[1] + (pb[1] - pa[1]) * mid, pa[2] + (pb[2] - pa[2]) * mid, pa[3] + (pb[3] - pa[3]) * mid }
        if depth(q) > Tat(q) then hi = mid else lo = mid end
    end
    return (lo + hi) / 2
end

-- ---------- donde el cuello de la cara queda adentro del torso ----------
-- Caras con el cuello mas finito (o corrido) que el normal: en el borde del torso queda un espacio entre el torso y el
-- cuello. Ahi la cara se deja seguir un poco mas abajo (adentro del torso, no se ve) para que no se vea ese espacio.
local function closest_on_tri(p, a, b, c)
    local abx, aby, abz = b[1] - a[1], b[2] - a[2], b[3] - a[3]
    local acx, acy, acz = c[1] - a[1], c[2] - a[2], c[3] - a[3]
    local apx, apy, apz = p[1] - a[1], p[2] - a[2], p[3] - a[3]
    local d1 = abx * apx + aby * apy + abz * apz; local d2 = acx * apx + acy * apy + acz * apz
    if d1 <= 0 and d2 <= 0 then return a[1], a[2], a[3] end
    local bpx, bpy, bpz = p[1] - b[1], p[2] - b[2], p[3] - b[3]
    local d3 = abx * bpx + aby * bpy + abz * bpz; local d4 = acx * bpx + acy * bpy + acz * bpz
    if d3 >= 0 and d4 <= d3 then return b[1], b[2], b[3] end
    local vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0 then local v = d1 / (d1 - d3); return a[1] + v * abx, a[2] + v * aby, a[3] + v * abz end
    local cpx, cpy, cpz = p[1] - c[1], p[2] - c[2], p[3] - c[3]
    local d5 = abx * cpx + aby * cpy + abz * cpz; local d6 = acx * cpx + acy * cpy + acz * cpz
    if d6 >= 0 and d5 <= d6 then return c[1], c[2], c[3] end
    local vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0 then local w = d2 / (d2 - d6); return a[1] + w * acx, a[2] + w * acy, a[3] + w * acz end
    local va = d3 * d6 - d5 * d4
    if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0 then
        local w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); return b[1] + w * (c[1] - b[1]), b[2] + w * (c[2] - b[2]), b[3] + w * (c[3] - b[3])
    end
    local den = 1 / (va + vb + vc); local v, w = vb * den, vc * den
    return a[1] + abx * v + acx * w, a[2] + aby * v + acy * w, a[3] + abz * v + acz * w
end
local GAP_OK = 0.0001       -- hasta 0.1 mm de diferencia se corta en el borde
local GAP_K = 3             -- si no: la cara sigue debajo del borde 3 veces lo que falta (hasta 2 cm)
local GAP_MAX = 0.020
local function extra_profile(M)
    local P, tris = M.P, M.tris[1]
    local cen = {}
    for ti, t in ipairs(tris) do
        local a, b, c = P[t[1]], P[t[2]], P[t[3]]
        local cx, cy, cz = (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3, (a[3] + b[3] + c[3]) / 3
        local r = 0
        for _, q in ipairs({ a, b, c }) do
            local dd = sqrt((q[1] - cx) ^ 2 + (q[2] - cy) ^ 2 + (q[3] - cz) ^ 2); if dd > r then r = dd end
        end
        cen[ti] = { cx, cy, cz, r }
    end
    local prof = {}
    for i = 1, NL do
        local a, b = LOOP[i], LOOP[i % NL + 1]
        for _, s in ipairs({ a, { (a[1] + b[1]) / 2, (a[2] + b[2]) / 2, (a[3] + b[3]) / 2 } }) do
            local best, bx, by, bz = 1e9, 0, 0, 0
            for ti, t in ipairs(tris) do
                local cc = cen[ti]
                local dc = sqrt((s[1] - cc[1]) ^ 2 + (s[2] - cc[2]) ^ 2 + (s[3] - cc[3]) ^ 2) - cc[4]
                if dc <= 0 or dc * dc < best then
                    local x, y, z = closest_on_tri(s, P[t[1]], P[t[2]], P[t[3]])
                    local d2 = (x - s[1]) ^ 2 + (y - s[2]) ^ 2 + (z - s[3]) ^ 2
                    if d2 < best then best, bx, by, bz = d2, x, y, z end
                end
            end
            local rs = sqrt(s[1] ^ 2 + (s[3] - AXZ) ^ 2); local rq = sqrt(bx ^ 2 + (bz - AXZ) ^ 2)
            local delta = sqrt(best); if rq > rs then delta = -delta end        -- > 0: la cara queda adentro del torso
            local ex = 0
            if delta > GAP_OK then ex = GAP_K * (delta - GAP_OK); if ex > GAP_MAX then ex = GAP_MAX end end
            prof[#prof + 1] = { phi = deg(atan2(s[1], s[3] - AXZ)), delta = delta, ex = ex }
        end
    end
    table.sort(prof, function(x, y) return x.phi < y.phi end)
    local n, ex2, maxd = #prof, {}, -1
    for i = 1, n do
        local e = 0
        for k = -2, 2 do local q = prof[(i - 1 + k) % n + 1]; if q.ex > e then e = q.ex end end
        ex2[i] = e
        if prof[i].delta > maxd then maxd = prof[i].delta end
    end
    for i = 1, n do prof[i].ex = ex2[i] end
    local function extra(phi)
        if phi < prof[1].phi or phi >= prof[n].phi then
            local a, b = prof[n], prof[1]
            local span = b.phi + 360 - a.phi; local x = phi < prof[1].phi and phi + 360 or phi
            local t = span > 0 and (x - a.phi) / span or 0
            return a.ex + (b.ex - a.ex) * t
        end
        local lo, hi = 1, n
        while hi - lo > 1 do local mid = floor((lo + hi) / 2); if prof[mid].phi <= phi then lo = mid else hi = mid end end
        local a, b = prof[lo], prof[hi]
        local t = (b.phi > a.phi) and (phi - a.phi) / (b.phi - a.phi) or 0
        return a.ex + (b.ex - a.ex) * t
    end
    return extra, maxd
end

-- plan de corte: devuelve una tabla con los vertices nuevos y los triangulos, o nil y el motivo
local function make_plan(M, T0, LOW, MERGE, partial, KEEP)
    local nv, P, d, tris = M.nv, M.P, M.d, M.tris
    local extra = M.extra
    local function Tat(p) return T0 + extra(deg(atan2(p[1], p[3] - AXZ))) end
    local Tv, isb, band, nbelow, nband = {}, {}, {}, 0, 0
    for i = 0, nv - 1 do
        local T = Tat(P[i]); Tv[i] = T
        if d[i] > T + 0.000001 then isb[i] = true; nbelow = nbelow + 1      -- (1 micra: un punto de corte ya hecho no se vuelve a cortar)
        elseif d[i] >= LOW and (d[i] <= T0 + 0.000001 or d[i] >= 0.75 * T) then band[i] = true; nband = nband + 1 end
        -- (donde se corta mas abajo, un vertice a media altura no sirve de borde: la cara tiene que llegar hasta abajo)
    end
    if nbelow == 0 then return "nada" end
    local edges, need, groups
    local tcache = {}
    local function collect()
        -- aristas que cruzan el corte
        edges, need = {}, {}
        for r = 1, #tris do
            for _, t in ipairs(tris[r]) do
                for k = 1, 3 do
                    local p, q = t[k], t[k % 3 + 1]
                    if (isb[p] == true) ~= (isb[q] == true) then
                        local a, b = p, q
                        if isb[a] then a, b = q, p end
                        local key = a * 65536 + b
                        if not edges[key] then
                            local e = { a = a, b = b }
                            edges[key] = e
                            if not band[a] and not partial then
                                local c = tcache[key]
                                if not c then
                                    local tt = bisect(P[a], P[b], Tat)
                                    c = { t = tt, p = lerp(P[a], P[b], tt) }
                                    if M.UVA then c.uv = lerp(M.UVA.vals[a], M.UVA.vals[b], tt) end
                                    tcache[key] = c
                                end
                                e.t, e.p, e.uv = c.t, c.p, c.uv
                                need[#need + 1] = e
                            end
                        end
                    end
                end
            end
        end
        -- puntos de corte casi iguales (misma posicion y UV) comparten vertice
        groups = {}
        for _, e in ipairs(need) do
            local g
            if MERGE > 0 then
                for _, gg in ipairs(groups) do
                    local q = gg[1]
                    local dx, dy, dz = q.p[1] - e.p[1], q.p[2] - e.p[2], q.p[3] - e.p[3]
                    if dx * dx + dy * dy + dz * dz <= MERGE * MERGE and
                       (not M.UVA or (abs(q.uv[1] - e.uv[1]) <= 0.002 and abs(q.uv[2] - e.uv[2]) <= 0.002)) then g = gg; break end
                end
            end
            if g then g[#g + 1] = e else g = { e }; groups[#groups + 1] = g end
            e.grp = g
        end
    end
    collect()
    -- si no alcanzan: se dejan en su lugar vertices de abajo a los que llegan varias aristas (cada uno ahorra varios puntos)
    local nkept = 0
    while KEEP and #groups > nbelow do
        local cnt = {}
        for _, e in ipairs(need) do cnt[e.b] = (cnt[e.b] or 0) + 1 end
        local best, bc, bd = nil, 1, 1e9
        for v, c in pairs(cnt) do
            if d[v] <= Tv[v] + KEEP and (c > bc or (c == bc and d[v] < bd)) then best, bc, bd = v, c, d[v] end
        end
        if not best then break end
        isb[best] = nil; band[best] = true; nbelow = nbelow - 1; nkept = nkept + 1
        collect()
    end
    if #groups > nbelow then return nil, string.format("faltan %d vertices", #groups - nbelow) end
    -- un vertice de los de abajo (que ya no se usan) para cada punto de corte
    local used, free = {}, {}
    for _, g in ipairs(groups) do
        for _, e in ipairs(g) do if not g.v and not used[e.b] then used[e.b] = true; g.v = e.b end end
    end
    for i = 0, nv - 1 do if isb[i] and not used[i] then free[#free + 1] = i end end
    local nfree = 1
    for _, g in ipairs(groups) do
        if not g.v then g.v = free[nfree]; used[g.v] = true; nfree = nfree + 1 end
    end
    -- triangulos
    local out, keepv, removed = {}, nil, 0
    for r = 1, #tris do
        local O = {}
        for _, t in ipairs(tris[r]) do
            local b1, b2, b3 = isb[t[1]], isb[t[2]], isb[t[3]]
            if not b1 and not b2 and not b3 then O[#O + 1] = { t[1], t[2], t[3] }; keepv = keepv or t[1]
            elseif b1 and b2 and b3 then removed = removed + 1
            elseif partial then O[#O + 1] = { t[1], t[2], t[3] }
            else
                local poly = {}
                for k = 1, 3 do
                    local p, q = t[k], t[k % 3 + 1]
                    if not isb[p] and poly[#poly] ~= p then poly[#poly + 1] = p end
                    if (isb[p] == true) ~= (isb[q] == true) then
                        local a, b = p, q
                        if isb[a] then a, b = q, p end
                        local e = edges[a * 65536 + b]
                        local c = band[a] and a or e.grp.v
                        if poly[#poly] ~= c then poly[#poly + 1] = c end
                    end
                end
                if #poly > 1 and poly[#poly] == poly[1] then poly[#poly] = nil end
                for k = 2, #poly - 1 do
                    if poly[1] ~= poly[k] and poly[k] ~= poly[k + 1] and poly[1] ~= poly[k + 1] then
                        O[#O + 1] = { poly[1], poly[k], poly[k + 1] }
                    end
                end
            end
        end
        if #O > floor(M.ranges[r][2] / 3) then return nil, "no entran los triangulos" end
        out[r] = O
    end
    if not keepv then return nil, "malla vacia" end
    -- uniones: un vertice del torso que cae en medio de un borde de la cara (entre dos vertices sobre el borde) se
    -- agrega tambien a la cara, para que las dos mallas compartan exactamente los mismos puntos
    local splits, nskip = {}, 0
    for r = 1, #out do
        local O = out[r]
        local cnt = {}
        for _, t in ipairs(O) do
            for k = 1, 3 do
                local a, b = t[k], t[k % 3 + 1]
                local key = (a < b) and (a * 65536 + b) or (b * 65536 + a)
                cnt[key] = (cnt[key] or 0) + 1
            end
        end
        local n0 = #O
        for ti = 1, n0 do
            local t = O[ti]
            for k = 1, 3 do
                local a, b = t[k], t[k % 3 + 1]
                local key = (a < b) and (a * 65536 + b) or (b * 65536 + a)
                if cnt[key] == 1 and band[a] and band[b] then
                    local pa, pb = P[a], P[b]
                    local ab = { pb[1] - pa[1], pb[2] - pa[2], pb[3] - pa[3] }
                    local l2 = ab[1] * ab[1] + ab[2] * ab[2] + ab[3] * ab[3]
                    for _, lp in ipairs(LOOP) do
                        local s = ((lp[1] - pa[1]) * ab[1] + (lp[2] - pa[2]) * ab[2] + (lp[3] - pa[3]) * ab[3]) / l2
                        if s > 0.01 and s < 0.99 then
                            local dx, dy, dz = pa[1] + s * ab[1] - lp[1], pa[2] + s * ab[2] - lp[2], pa[3] + s * ab[3] - lp[3]
                            if dx * dx + dy * dy + dz * dz < 4e-10 then
                                local skey = key .. ":" .. lp[1]
                                local sp = splits[skey]
                                if not sp then
                                    local v = free[nfree]
                                    if v then nfree = nfree + 1; used[v] = true; sp = { v = v, a = a, b = b, s = s, p = lp }; splits[skey] = sp
                                    else nskip = nskip + 1 end
                                end
                                if sp then
                                    local w = t[(k + 1) % 3 + 1]
                                    O[ti] = { a, sp.v, w }; O[#O + 1] = { sp.v, b, w }
                                    break
                                end
                            end
                        end
                    end
                end
                if O[ti] ~= t then break end
            end
        end
        if #O > floor(M.ranges[r][2] / 3) then return nil, "no entran los triangulos" end
    end
    -- vertices del borde que quedaron apenas arriba de la linea del torso (caras un poco distintas): se bajan por su
    -- arista hasta la linea (menos de 0.15 mm), asi no queda una rendija entre el torso y la cara
    local moves, nmoved = {}, 0
    if LOW < -0.00005 then
        local adj = {}
        for r = 1, #tris do
            for _, t in ipairs(tris[r]) do
                for k = 1, 3 do
                    local p, q = t[k], t[k % 3 + 1]
                    if band[p] and d[p] < 0 and isb[q] then adj[p] = adj[p] or {}; adj[p][#adj[p] + 1] = q end
                    if band[q] and d[q] < 0 and isb[p] then adj[q] = adj[q] or {}; adj[q][#adj[q] + 1] = p end
                end
            end
        end
        local bypos = {}
        local function pkey(p) return string.format("%.7f,%.7f,%.7f", p[1], p[2], p[3]) end
        for v, nb in pairs(adj) do
            local best, bs = nil, -1e9
            for _, q in ipairs(nb) do
                local dx, dy, dz = P[q][1] - P[v][1], P[q][2] - P[v][2], P[q][3] - P[v][3]
                local sc = (d[q] - d[v]) / math.max(sqrt(dx * dx + dy * dy + dz * dz), 1e-9)
                if sc > bs then best, bs = q, sc end
            end
            local tt = bisect(P[v], P[best], function() return 0.00001 end)
            local np = lerp(P[v], P[best], tt)
            local key = pkey(P[v])
            if not bypos[key] then bypos[key] = { np[1] - P[v][1], np[2] - P[v][2], np[3] - P[v][3] } end
        end
        for i = 0, nv - 1 do
            if band[i] and d[i] < 0 then
                local dv = bypos[pkey(P[i])]
                if dv then moves[i] = { P[i][1] + dv[1], P[i][2] + dv[2], P[i][3] + dv[3] }; nmoved = nmoved + 1 end
            end
        end
    end
    -- vertices nuevos
    local newv, nsplit = {}, 0
    for _, g in ipairs(groups) do
        local list = {}
        for _, e in ipairs(g) do list[#list + 1] = point_vals(M, e.a, e.b, e.t) end
        local pos = { 0, 0, 0 }
        for _, e in ipairs(g) do for k = 1, 3 do pos[k] = pos[k] + e.p[k] / #g end end
        newv[#newv + 1] = { v = g.v, vals = merge_vals(M, list, pos) }
    end
    for _, sp in pairs(splits) do
        newv[#newv + 1] = { v = sp.v, vals = merge_vals(M, { point_vals(M, sp.a, sp.b, sp.s) }, sp.p) }
        nsplit = nsplit + 1
    end
    local nmerged = #need - #groups
    return { out = out, newv = newv, keepv = keepv, moves = moves,
             msg = string.format("%d puntos de corte%s, %d vertices sobre el borde%s%s, %d uniones%s, %d triangulos quitados",
                 #groups, nmerged > 0 and string.format(" (%d juntados)", nmerged) or "", nband,
                 nkept > 0 and string.format(" (%d dejados de abajo)", nkept) or "",
                 nmoved > 0 and string.format(" (%d bajados a la linea)", nmoved) or "", nsplit,
                 nskip > 0 and string.format(" (%d sin lugar)", nskip) or "", removed) }
end

local function apply_plan(M, plan)
    for _, nvd in ipairs(plan.newv) do
        for a, val in pairs(nvd.vals) do wr(a.addr + nvd.v * a.stride, enc_attr(a.fmt, val)) end
    end
    if plan.moves then
        for _, a in ipairs(M.attrs) do
            if a.type == 0 and a.fmt == 1 then
                for v, p in pairs(plan.moves) do wr(a.addr + v * a.stride, enc_attr(1, p)) end
            end
        end
    end
    for r = 1, #M.ranges do
        local first, cnt = M.ranges[r][1], M.ranges[r][2]
        local O = plan.out[r]; local parts = {}
        for k = 1, floor(cnt / 3) do
            local t = O[k] or { plan.keepv, plan.keepv, plan.keepv }
            parts[#parts + 1] = ph16(t[1]) .. ph16(t[2]) .. ph16(t[3])
        end
        wr(M.ibase + first * 2, table.concat(parts))
    end
end

-- (1.9) normales del cuello de la cara en la union = las del borde del torso, y de a poco las suyas hasta NBLEND m mas arriba
-- (la tangente se endereza contra la normal nueva). Vertices nuevos del corte y los bajados a la linea: la del torso entera.
-- Se llama ANTES de apply_plan: a los vertices nuevos se les cambian los valores del plan (apply_plan los escribe) y a los
-- demas se les escribe la normal / tangente aparte (apply_plan no toca esos bytes: los parches no se pueden pisar)
local NBLEND = 0.020
local function blend_normals(M, plan)
    local An, At
    for _, a in ipairs(M.attrs) do
        if a.type == 2 and a.fmt == 6 then An = a end
        if a.type == 14 and a.fmt == 6 then At = a end
    end
    if not An then return 0 end
    local n = 0
    local function newnt(p, nv, tv, d)
        local w
        if d >= 0 then w = 1 else
            local x = -d / NBLEND; if x >= 1 then return nil end
            w = 1 - x * x * (3 - 2 * x)
        end
        local rn = ring_n(deg(atan2(p[1], p[3] - AXZ)))
        local nn = nrm3({ nv[1] + (rn[1] - nv[1]) * w, nv[2] + (rn[2] - nv[2]) * w, nv[3] + (rn[3] - nv[3]) * w })
        local no, to = { nn[1], nn[2], nn[3], nv[4] or 0 }, nil
        if tv then
            local dt = tv[1] * nn[1] + tv[2] * nn[2] + tv[3] * nn[3]
            local tt = nrm3({ tv[1] - nn[1] * dt, tv[2] - nn[2] * dt, tv[3] - nn[3] * dt })
            to = { tt[1], tt[2], tt[3], tv[4] }
        end
        n = n + 1
        return no, to
    end
    local newset = {}
    for _, nvd in ipairs(plan.newv) do
        newset[nvd.v] = true
        local nv = nvd.vals[An]; local tv = At and nvd.vals[At]
        local p
        for a, val in pairs(nvd.vals) do if a.type == 0 and a.fmt == 1 then p = val end end
        if nv and p then
            local no, to = newnt(p, nv, tv, 0)
            if no then nvd.vals[An] = no; if to then nvd.vals[At] = to end end
        end
    end
    for i = 0, M.nv - 1 do
        if not newset[i] then
            local mv = plan.moves and plan.moves[i]
            local d = mv and 0 or M.d[i]
            if d > -NBLEND then
                local no, to = newnt(mv or M.P[i], An.vals[i], At and At.vals[i], d)
                if no then
                    wr(An.addr + i * An.stride, enc_attr(6, no))
                    if to then wr(At.addr + i * At.stride, enc_attr(6, to)) end
                end
            end
        end
    end
    return n
end

-- ---------- textura de la cara: fundido del cuello con el torso ----------
-- Los facemakers pintan abajo del cuello una sombra oscura / las claviculas (pensado para quedar bajo el cuello de
-- la camiseta) y el color del cuello no siempre coincide con el del torso. Cuando el juego carga la textura de la
-- cara (face_bsm_alp.ftex) se cambia en memoria SOLO la franja de los ultimos TEX.RAMP metros del cuello sobre la
-- union: el color promedio de la cara pasa de a poco al del torso (se dejan los poros y el detalle fino), asi el
-- cuello no termina en un borde marcado. La profundidad de cada texel sale de la malla del cuello de la misma cara
-- (que se lee antes, en el face.fpk). El tono de piel se elige comparando el cuello de la cara con los 6 tonos.
local TEX = {
    RAMP = 0.025,                 -- m: alto del fundido sobre la union
    MAXD = 0.010,                 -- m: debajo de la union solo se toca hasta aca (lo de mas abajo se corta y no se ve;
                                  --    en las caras convertidas de FIFA ahi al lado estan los dientes y la lengua)
    SIGMA = 8,                    -- texels (en 1024): tamano del "color promedio" que se cambia
    C0 = 0.30, CW = 0.6, KW = 0.5,-- detalle que se deja: +-C0 lejos de la union, menos cerca (CW, KW)
    GU = 256, GV = 256,           -- grilla de profundidad en UV: u y v 0..1 en 256 x 256 celdas
    MINW = 32,                    -- mips mas chicos que esto no se tocan
    -- color del torso TorsoBody justo debajo de la union (albedo sRGB) por tono 1..6
    -- (1.10) tono 1 = torso_1_bsm propio del usuario (el de TorsoBody: 155.3, 115.2, 95.2)
    -- (1.13) tonos 2..6 = el torso_1 propio llevado a cada tono (make_tones.py): su color en el borde del cuello
    TORSO = { { 165.6, 125.5, 105.8 }, { 169.8, 129.5, 101.8 }, { 159.9, 116.4, 92.9 },
              { 141.6, 104.6, 85.1 }, { 116.5, 84.3, 71.4 }, { 99.2, 73.5, 65.6 } },
    -- color del cuello de las caras genericas de Konami por tono (head_color_w y o r d b), 25-50 mm sobre la union
    REF = { { 176.3, 136.9, 118.1 }, { 177.8, 134.2, 103.6 }, { 159.2, 118.1, 91.1 },
            { 126.9, 90.1, 67.4 }, { 102.7, 75.5, 62.0 }, { 89.0, 67.8, 58.6 } },
}
local POW2 = {}
for k = 0, 32 do POW2[k] = 2 ^ k end
local min, max = math.min, math.max

-- grilla de profundidad (m, + abajo de la union) del cuello en el espacio UV de la textura. Cubre toda la textura
-- (v 0..1: cada facemaker pone el cuello donde quiere), pero solo con los triangulos que llegan a menos de 65 mm de
-- la union. Si la cara tiene varias mallas de piel con la misma textura, se juntan en la misma grilla.
local function make_grid(M, g)
    g = g or { n = 0, j0 = TEX.GV, j1 = -1 }
    local UVA = M.UVA; if not UVA then return g end
    local GU, GV = TEX.GU, TEX.GV
    for _, t in ipairs(M.tris[1]) do
        local da, db, dc = M.d[t[1]], M.d[t[2]], M.d[t[3]]
        if max(da, db, dc) > -0.065 then
            local A, B, C = UVA.vals[t[1]], UVA.vals[t[2]], UVA.vals[t[3]]
            local ua, va, ub, vb, uc, vc = A[1], A[2], B[1], B[2], C[1], C[2]
            if max(ua, ub, uc) - min(ua, ub, uc) <= 0.5 and max(va, vb, vc) - min(va, vb, vc) <= 0.5 then
                local su, sv = floor((ua + ub + uc) / 3), floor((va + vb + vc) / 3)      -- uv fuera de 0..1: se repite
                local xa, ya = (ua - su) * GU - 0.5, (va - sv) * GU - 0.5
                local xb, yb = (ub - su) * GU - 0.5, (vb - sv) * GU - 0.5
                local xc, yc = (uc - su) * GU - 0.5, (vc - sv) * GU - 0.5
                local den = (xb - xa) * (yc - ya) - (xc - xa) * (yb - ya)
                if abs(den) > 1e-9 then
                    local i0, i1 = max(0, math.ceil(min(xa, xb, xc))), min(GU - 1, floor(max(xa, xb, xc)))
                    local j0, j1 = max(0, math.ceil(min(ya, yb, yc))), min(GV - 1, floor(max(ya, yb, yc)))
                    for j = j0, j1 do
                        for i = i0, i1 do
                            local px, py = i - xa, j - ya
                            local s = (px * (yc - ya) - (xc - xa) * py) / den
                            local q = ((xb - xa) * py - px * (yb - ya)) / den
                            if s >= -1e-6 and q >= -1e-6 and s + q <= 1 + 1e-6 then
                                local k = j * GU + i
                                local d = da + s * (db - da) + q * (dc - da)
                                local old = g[k]
                                if not old then
                                    g.n = g.n + 1
                                    if j < g.j0 then g.j0 = j end
                                    if j > g.j1 then g.j1 = j end
                                end
                                if not old or d > old then g[k] = d end         -- (uv compartidas: gana la mas baja)
                            end
                        end
                    end
                end
            end
        end
    end
    return g
end
-- celdas de la textura que usan otros triangulos de la cara, lejos del cuello (boca, dientes, cara). Ahi no se toca
-- nada aunque esten pegadas al cuello (en las caras convertidas de FIFA los dientes y la lengua estan al lado)
local function protect_mesh(F, mi, P)
    local me = entry(F, 3, mi); if not me then return end
    local nv, firstface, lodbase = u16(me, 10), u32(me, 16), u32(me, 24)
    local L = mesh_layout(F, u16(me, 8)); if not L or nv < 3 then return end
    local PA, UA
    for _, a in ipairs(L) do
        if a.type == 0 and a.fmt == 1 and not PA then PA = a end
        if a.type == 8 and FMT_SIZE[a.fmt] and not UA then UA = a end
    end
    if not PA or not UA then return end
    local sp = rd(PA.addr, (nv - 1) * PA.stride + 12)
    local su = rd(UA.addr, (nv - 1) * UA.stride + FMT_SIZE[UA.fmt])
    local D, U, V = {}, {}, {}
    for i = 0, nv - 1 do
        D[i] = depth(read_attr(1, sp, i * PA.stride))
        local uv = read_attr(UA.fmt, su, i * UA.stride)
        U[i], V[i] = uv[1], uv[2]
    end
    local e = entry(F, 17, lodbase); if not e then return end
    local first, cnt = firstface + u32(e, 0), u32(e, 4)
    local ib = entry(F, 14, 2); if not ib then return end
    local s = rd(F.b1[2].addr + u32(ib, 8) + first * 2, cnt * 2)
    local GU, GV = TEX.GU, TEX.GV
    for k = 0, floor(cnt / 3) - 1 do
        local a, b, c = u16(s, k * 6), u16(s, k * 6 + 2), u16(s, k * 6 + 4)
        if a < nv and b < nv and c < nv and a ~= b and b ~= c and a ~= c and max(D[a], D[b], D[c]) <= -0.065 then
            local ua, va, ub, vb, uc, vc = U[a], V[a], U[b], V[b], U[c], V[c]
            if max(ua, ub, uc) - min(ua, ub, uc) <= 0.5 and max(va, vb, vc) - min(va, vb, vc) <= 0.5 then
                local su_, sv_ = floor((ua + ub + uc) / 3), floor((va + vb + vc) / 3)
                local xa, ya = (ua - su_) * GU - 0.5, (va - sv_) * GU - 0.5
                local xb, yb = (ub - su_) * GU - 0.5, (vb - sv_) * GU - 0.5
                local xc, yc = (uc - su_) * GU - 0.5, (vc - sv_) * GU - 0.5
                -- las celdas de los vertices y las que tienen el centro adentro del triangulo
                for _, q in ipairs({ { xa, ya }, { xb, yb }, { xc, yc } }) do
                    local i, j = floor(q[1] + 0.5), floor(q[2] + 0.5)
                    if i >= 0 and i < GU and j >= 0 and j < GV then P[j * GU + i] = true end
                end
                local den = (xb - xa) * (yc - ya) - (xc - xa) * (yb - ya)
                if abs(den) > 1e-9 then
                    local i0, i1 = max(0, math.ceil(min(xa, xb, xc))), min(GU - 1, floor(max(xa, xb, xc)))
                    local j0, j1 = max(0, math.ceil(min(ya, yb, yc))), min(GV - 1, floor(max(ya, yb, yc)))
                    for j = j0, j1 do
                        for i = i0, i1 do
                            local px, py = i - xa, j - ya
                            local s_ = (px * (yc - ya) - (xc - xa) * py) / den
                            local q_ = ((xb - xa) * py - px * (yb - ya)) / den
                            if s_ >= -1e-6 and q_ >= -1e-6 and s_ + q_ <= 1 + 1e-6 then P[j * GU + i] = true end
                        end
                    end
                end
            end
        end
    end
end
-- profundidad en (u, v): bilineal con las celdas que existan (nil fuera del cuello)
local function grid_depth(g, u, v)
    local GU, GV = TEX.GU, TEX.GV
    local gx, gy = u * GU - 0.5, v * GU - 0.5
    if gy < -1 then return nil end
    local i0, j0 = floor(gx), floor(gy)
    local fx, fy = gx - i0, gy - j0
    local sw, sd = 0, 0
    for dj = 0, 1 do
        local j = j0 + dj
        if j >= 0 and j < GV then
            local wy = dj == 0 and 1 - fy or fy
            for di = 0, 1 do
                local i = (i0 + di) % GU
                local d = g[j * GU + i]
                if d then local w = wy * (di == 0 and 1 - fx or fx); sw = sw + w; sd = sd + w * d end
            end
        end
    end
    if sw < 0.25 then return nil end
    return sd / sw
end
local function ramp_w(d, dmax)
    if not d or d > dmax then return 0 end
    local a = -d
    if a <= 0 then return 1 end
    if a >= TEX.RAMP then return 0 end
    local s = 1 - a / TEX.RAMP
    return s * s * (3 - 2 * s)
end

-- ---------- BC1 / BC3 / BC7 ----------
-- (todo con tablas reusadas y sin funciones creadas adentro de los bucles: LuaJIT lo compila entero)
local BP2 = { [0] = 52428, 34952, 61166, 60616, 51328, 65260, 65224, 60544, 51200, 65516, 65152, 59392, 65512, 65280, 
    65520, 61440, 63248, 142, 28928, 2254, 140, 29456, 12544, 36046, 2188, 12560, 26214, 13932, 6120, 4080, 29070, 
    14748, 43690, 61680, 23130, 13260, 15420, 21930, 38550, 42330, 29646, 5064, 12876, 15324, 27030, 49980, 39270, 
    1632, 626, 1252, 20032, 10016, 51510, 37740, 14790, 25500, 37686, 40134, 33150, 59160, 52464, 4044, 30532, 60962 }
local BA2 = { [0] = 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 
    2, 2, 8, 8, 2, 2, 15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6, 6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 
    15, 2, 2, 15 }
local BP3 = { [0] = 2858963024, 1784303680, 1515864576, 1414570152, 2779054080, 2694860880, 1431675040, 1515868240, 
    2857697280, 2857719040, 2863289600, 2425393296, 2492765332, 2762253476, 2846200912, 705315408, 2777960512, 
    172118100, 2779096320, 1436590240, 2829603924, 1785348160, 2762231808, 437912832, 5285028, 2862977168, 342452500, 
    1768494080, 2693105056, 2860651540, 1352967248, 1784283648, 2846195712, 1351655592, 2829094992, 606348324, 
    11162880, 613566756, 608801316, 1352993360, 1342874960, 2863285316, 1717960704, 2778768800, 1352683680, 
    1764256040, 1152035396, 1717986816, 2856600644, 1420317864, 2508232064, 2526451200, 2824098984, 2157286784, 
    2853442580, 2526412800, 2863272980, 2689618080, 2695210400, 2516582400, 1082146944, 2846402984, 2863311428, 
    709513812 }
local BA3a = { [0] = 3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3, 3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 
    15, 15, 8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15, 3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 
    3, 3 }
local BA3b = { [0] = 15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8, 15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 
    10, 15, 15, 10, 8, 15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8, 15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 
    15, 15, 3, 15, 15, 8 }
local FAC = { [2] = { [0] = 0, 21, 43, 64 }, [3] = { [0] = 0, 9, 18, 27, 37, 46, 55, 64 },
              [4] = { [0] = 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 } }
-- modo: subconjuntos, bits de particion, rotacion, seleccion de indice, color, alfa, p-bit por extremo, p-bit compartido, indices
local BMODE = { [0] = { 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 }, { 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 }, { 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
                { 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 }, { 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 }, { 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
                { 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 }, { 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 } }
local function getbits(b, pos, n)              -- b[0..15] bytes; n <= 8
    if n == 0 then return 0 end
    local i = floor(pos / 8); local sh = pos - i * 8
    local v = b[i] + (i < 15 and b[i + 1] or 0) * 256
    return floor(v / POW2[sh]) % POW2[n]
end
local function expand(x, bits)
    local v = (x * POW2[8 - bits]) % 256
    return v + floor(v / POW2[bits])
end
local E1, E2, E3, E4 = {}, {}, {}, {}
-- out[k*4+1..k*4+4] = R G B A del texel k (fila por fila)
local function bc7_decode(b, out)
    local mode, pos = 0, 0
    while mode < 8 and getbits(b, pos, 1) == 0 do mode = mode + 1; pos = pos + 1 end
    if mode == 8 then for k = 1, 64 do out[k] = 0 end; return end
    pos = pos + 1
    local mi = BMODE[mode]
    local ns, pbits, rbits, isbits, cb, ab, epb, spb, ib0, ib1 = mi[1], mi[2], mi[3], mi[4], mi[5], mi[6], mi[7], mi[8], mi[9], mi[10]
    local mpb = epb ~= 0 and epb or spb
    local sc = POW2[mpb]
    local part = getbits(b, pos, pbits); pos = pos + pbits
    local rot = getbits(b, pos, rbits); pos = pos + rbits
    local isel = getbits(b, pos, isbits); pos = pos + isbits
    local ne = 2 * ns - 1
    for k = 0, ne do E1[k] = getbits(b, pos, cb) * sc; pos = pos + cb end
    for k = 0, ne do E2[k] = getbits(b, pos, cb) * sc; pos = pos + cb end
    for k = 0, ne do E3[k] = getbits(b, pos, cb) * sc; pos = pos + cb end
    for k = 0, ne do
        if ab > 0 then E4[k] = getbits(b, pos, ab) * sc; pos = pos + ab else E4[k] = 255 end
    end
    if mpb ~= 0 then
        for s = 0, ns - 1 do
            local pa = getbits(b, pos, 1); pos = pos + 1
            local pb = pa
            if spb == 0 then pb = getbits(b, pos, 1); pos = pos + 1 end
            local k0, k1 = 2 * s, 2 * s + 1
            E1[k0] = E1[k0] + pa; E1[k1] = E1[k1] + pb
            E2[k0] = E2[k0] + pa; E2[k1] = E2[k1] + pb
            E3[k0] = E3[k0] + pa; E3[k1] = E3[k1] + pb
            if ab > 0 then E4[k0] = E4[k0] + pa; E4[k1] = E4[k1] + pb end
        end
    end
    local bits = cb + mpb
    for k = 0, ne do E1[k] = expand(E1[k], bits); E2[k] = expand(E2[k], bits); E3[k] = expand(E3[k], bits) end
    if ab > 0 then for k = 0, ne do E4[k] = expand(E4[k], ab + mpb) end end
    local f0 = FAC[ib0]; local f1 = ib1 > 0 and FAC[ib1] or f0
    local o0, o1 = pos, pos + ns * (16 * ib0 - 1)
    for idx = 0, 15 do
        local sub, anchor = 0, 0
        if ns == 2 then
            sub = floor(BP2[part] / POW2[idx]) % 2
            if sub ~= 0 then anchor = BA2[part] end
        elseif ns == 3 then
            sub = floor(BP3[part] / POW2[2 * idx]) % 4
            if sub == 1 then anchor = BA3a[part] elseif sub == 2 then anchor = BA3b[part] end
        end
        local an = (idx == anchor) and 1 or 0
        local n0 = ib0 - an
        local i0 = getbits(b, o0, n0); o0 = o0 + n0
        local i1 = i0
        if ib1 > 0 then local n1 = ib1 - an; i1 = getbits(b, o1, n1); o1 = o1 + n1 end
        local fc, fa
        if isel == 0 then fc, fa = f0[i0], f1[i1] else fc, fa = f1[i1], f0[i0] end
        local s0, s1 = 2 * sub, 2 * sub + 1
        local r = floor((E1[s0] * (64 - fc) + E1[s1] * fc + 32) / 64)
        local gg = floor((E2[s0] * (64 - fc) + E2[s1] * fc + 32) / 64)
        local bb = floor((E3[s0] * (64 - fc) + E3[s1] * fc + 32) / 64)
        local a = floor((E4[s0] * (64 - fa) + E4[s1] * fa + 32) / 64)
        if rot == 1 then a, r = r, a elseif rot == 2 then a, gg = gg, a elseif rot == 3 then a, bb = bb, a end
        local o = idx * 4
        out[o + 1] = r; out[o + 2] = gg; out[o + 3] = bb; out[o + 4] = a
    end
end
local PR, PG, PB = {}, {}, {}
local function c565(q)
    local r5, g6, b5 = floor(q / 2048), floor(q / 32) % 64, q % 32
    return r5 * 8 + floor(r5 / 4), g6 * 4 + floor(g6 / 16), b5 * 8 + floor(b5 / 4)
end
local function pal565(q0, q1, four)              -- paleta en PR/PG/PB[0..3]; true si es de 3 colores + transparente
    local r0, g0, b0 = c565(q0); local r1, g1, b1 = c565(q1)
    PR[0], PG[0], PB[0], PR[1], PG[1], PB[1] = r0, g0, b0, r1, g1, b1
    if four or q0 > q1 then
        PR[2], PG[2], PB[2] = floor((2 * r0 + r1) / 3), floor((2 * g0 + g1) / 3), floor((2 * b0 + b1) / 3)
        PR[3], PG[3], PB[3] = floor((r0 + 2 * r1) / 3), floor((g0 + 2 * g1) / 3), floor((b0 + 2 * b1) / 3)
        return false
    end
    PR[2], PG[2], PB[2] = floor((r0 + r1) / 2), floor((g0 + g1) / 2), floor((b0 + b1) / 2)
    PR[3], PG[3], PB[3] = 0, 0, 0
    return true
end
-- bloque de color BC1 (8 bytes desde b[o]); four: siempre 4 colores (BC3). Devuelve false si usa transparencia.
local function bc1_decode(b, o, four, out)
    local q0, q1 = b[o] + b[o + 1] * 256, b[o + 2] + b[o + 3] * 256
    local trans = pal565(q0, q1, four)
    local d = b[o + 4] + b[o + 5] * 256 + b[o + 6] * 65536 + b[o + 7] * 16777216
    local used3 = false
    for i = 0, 15 do
        local k = d % 4; d = (d - k) / 4
        if k == 3 then used3 = true end
        local q = i * 4
        out[q + 1] = PR[k]; out[q + 2] = PG[k]; out[q + 3] = PB[k]; out[q + 4] = 255
    end
    return not (trans and used3)
end

-- media y eje principal (RGB) de 16 texels; px[i*4+1..3]
local function pca(px)
    local mr, mg, mb = 0, 0, 0
    for i = 0, 15 do local q = i * 4; mr = mr + px[q + 1]; mg = mg + px[q + 2]; mb = mb + px[q + 3] end
    mr, mg, mb = mr / 16, mg / 16, mb / 16
    local xx, xy, xz, yy, yz, zz = 0, 0, 0, 0, 0, 0
    for i = 0, 15 do
        local q = i * 4
        local r, g, b = px[q + 1] - mr, px[q + 2] - mg, px[q + 3] - mb
        xx = xx + r * r; xy = xy + r * g; xz = xz + r * b; yy = yy + g * g; yz = yz + g * b; zz = zz + b * b
    end
    local vx, vy, vz = 0.58, 0.58, 0.58
    for _ = 1, 8 do
        local nx = xx * vx + xy * vy + xz * vz
        local ny = xy * vx + yy * vy + yz * vz
        local nz = xz * vx + yz * vy + zz * vz
        local l = sqrt(nx * nx + ny * ny + nz * nz)
        if l < 1e-9 then break end
        vx, vy, vz = nx / l, ny / l, nz / l
    end
    local tmin, tmax = 1e9, -1e9
    for i = 0, 15 do
        local q = i * 4
        local t = (px[q + 1] - mr) * vx + (px[q + 2] - mg) * vy + (px[q + 3] - mb) * vz
        if t < tmin then tmin = t end
        if t > tmax then tmax = t end
    end
    return mr, mg, mb, vx, vy, vz, tmin, tmax
end
local function clamp255(x) if x < 0 then return 0 elseif x > 255 then return 255 end return x end

-- ---- BC1 (4 colores) ----
local function q565(r, g, b)
    local r5 = floor(clamp255(r) * 31 / 255 + 0.5); local g6 = floor(clamp255(g) * 63 / 255 + 0.5); local b5 = floor(clamp255(b) * 31 / 255 + 0.5)
    return r5 * 2048 + g6 * 32 + b5
end
local IDXA, IDXB = {}, {}
local function bc1_fit(px, q0, q1, idx)
    pal565(q0, q1, true)
    local err = 0
    for i = 0, 15 do
        local q = i * 4; local best, bk = 1e18, 0
        local r, g, b = px[q + 1], px[q + 2], px[q + 3]
        for k = 0, 3 do
            local dr, dg, db = r - PR[k], g - PG[k], b - PB[k]
            local e = dr * dr + dg * dg + db * db
            if e < best then best, bk = e, k end
        end
        idx[i] = bk; err = err + best
    end
    return err
end
local W3 = { [0] = 0, 1, 1 / 3, 2 / 3 }          -- peso del extremo 1 para indice 0..3
local function bc1_encode(px)
    local mr, mg, mb, vx, vy, vz, tmin, tmax = pca(px)
    local q0 = q565(mr + tmax * vx, mg + tmax * vy, mb + tmax * vz)
    local q1 = q565(mr + tmin * vx, mg + tmin * vy, mb + tmin * vz)
    if q0 == q1 then return char(q0 % 256, floor(q0 / 256), q1 % 256, floor(q1 / 256), 0, 0, 0, 0) end
    if q0 < q1 then q0, q1 = q1, q0 end
    local idx = IDXA
    local err = bc1_fit(px, q0, q1, idx)
    -- un refinamiento por minimos cuadrados con los indices elegidos
    local a2, ab, b2, sr0, sg0, sb0, sr1, sg1, sb1 = 0, 0, 0, 0, 0, 0, 0, 0, 0
    for i = 0, 15 do
        local t = W3[idx[i]]; local s = 1 - t; local q = i * 4
        a2 = a2 + s * s; ab = ab + s * t; b2 = b2 + t * t
        sr0 = sr0 + s * px[q + 1]; sg0 = sg0 + s * px[q + 2]; sb0 = sb0 + s * px[q + 3]
        sr1 = sr1 + t * px[q + 1]; sg1 = sg1 + t * px[q + 2]; sb1 = sb1 + t * px[q + 3]
    end
    local det = a2 * b2 - ab * ab
    if abs(det) > 1e-6 then
        local p0 = q565((b2 * sr0 - ab * sr1) / det, (b2 * sg0 - ab * sg1) / det, (b2 * sb0 - ab * sb1) / det)
        local p1 = q565((a2 * sr1 - ab * sr0) / det, (a2 * sg1 - ab * sg0) / det, (a2 * sb1 - ab * sb0) / det)
        if p0 ~= p1 then
            if p0 < p1 then p0, p1 = p1, p0 end
            local err2 = bc1_fit(px, p0, p1, IDXB)
            if err2 < err then q0, q1, idx = p0, p1, IDXB end
        end
    end
    local d = 0
    for i = 15, 0, -1 do d = d * 4 + idx[i] end
    return char(q0 % 256, floor(q0 / 256), q1 % 256, floor(q1 / 256), d % 256, floor(d / 256) % 256, floor(d / 65536) % 256, floor(d / 16777216) % 256)
end

-- ---- BC7: modo 6 (alfa constante) o modo 5 (alfa variable) ----
local OUTB = {}
local function putbits(pos, n, val)
    while n > 0 do
        local i = floor(pos / 8); local sh = pos - i * 8
        local take = n < 8 - sh and n or 8 - sh
        local part = val % POW2[take]
        OUTB[i] = OUTB[i] + part * POW2[sh]
        val = (val - part) / POW2[take]; pos = pos + take; n = n - take
    end
    return pos
end
local function outb_clear() for i = 0, 15 do OUTB[i] = 0 end end
local function outb_str()
    return char(OUTB[0], OUTB[1], OUTB[2], OUTB[3], OUTB[4], OUTB[5], OUTB[6], OUTB[7], OUTB[8], OUTB[9], OUTB[10], OUTB[11], OUTB[12], OUTB[13], OUTB[14], OUTB[15])
end
local IDX64 = {}                                 -- peso 0..64 -> indice BC7 de 4 bits mas cercano
for t = 0, 64 do
    local best, bk = 1e9, 0
    for k = 0, 15 do local e = abs(FAC[4][k] - t); if e < best then best, bk = e, k end end
    IDX64[t] = bk
end
-- indices de modo 6 para extremos (8 bits, ya cuantizados) a y b; devuelve el error
local function m6_fit(px, ar, ag, ab_, br, bg, bb, idx)
    local F = FAC[4]
    local dr, dg, db = br - ar, bg - ag, bb - ab_
    local dd = dr * dr + dg * dg + db * db
    local err = 0
    for i = 0, 15 do
        local q = i * 4
        local k = 0
        if dd > 1e-9 then
            local t = ((px[q + 1] - ar) * dr + (px[q + 2] - ag) * dg + (px[q + 3] - ab_) * db) / dd
            local ti = floor(t * 64 + 0.5); if ti < 0 then ti = 0 elseif ti > 64 then ti = 64 end
            k = IDX64[ti]
        end
        idx[i] = k
        local w = F[k]
        local er = px[q + 1] - floor((ar * (64 - w) + br * w + 32) / 64)
        local eg = px[q + 2] - floor((ag * (64 - w) + bg * w + 32) / 64)
        local eb = px[q + 3] - floor((ab_ * (64 - w) + bb * w + 32) / 64)
        err = err + er * er + eg * eg + eb * eb
    end
    return err
end
local function q7p(x, p)                          -- 7 bits + p-bit: devuelve el valor de 7 bits
    local v = floor((clamp255(x) - p) / 2 + 0.5)
    if v < 0 then return 0 elseif v > 127 then return 127 end
    return v
end
local function bc7_mode6(px, A)
    local p = A % 2
    local a7 = (A - p) / 2
    local F = FAC[4]
    local mr, mg, mb, vx, vy, vz, tmin, tmax = pca(px)
    local r0, g0, b0 = q7p(mr + tmin * vx, p), q7p(mg + tmin * vy, p), q7p(mb + tmin * vz, p)
    local r1, g1, b1 = q7p(mr + tmax * vx, p), q7p(mg + tmax * vy, p), q7p(mb + tmax * vz, p)
    local idx = IDXA
    local err = m6_fit(px, 2 * r0 + p, 2 * g0 + p, 2 * b0 + p, 2 * r1 + p, 2 * g1 + p, 2 * b1 + p, idx)
    -- refinamiento por minimos cuadrados
    local a2, ab, b2, s0r, s0g, s0b, s1r, s1g, s1b = 0, 0, 0, 0, 0, 0, 0, 0, 0
    for i = 0, 15 do
        local t = F[idx[i]] / 64; local s = 1 - t; local q = i * 4
        a2 = a2 + s * s; ab = ab + s * t; b2 = b2 + t * t
        s0r = s0r + s * px[q + 1]; s0g = s0g + s * px[q + 2]; s0b = s0b + s * px[q + 3]
        s1r = s1r + t * px[q + 1]; s1g = s1g + t * px[q + 2]; s1b = s1b + t * px[q + 3]
    end
    local det = a2 * b2 - ab * ab
    if abs(det) > 1e-6 then
        local nr0, ng0, nb0 = q7p((b2 * s0r - ab * s1r) / det, p), q7p((b2 * s0g - ab * s1g) / det, p), q7p((b2 * s0b - ab * s1b) / det, p)
        local nr1, ng1, nb1 = q7p((a2 * s1r - ab * s0r) / det, p), q7p((a2 * s1g - ab * s0g) / det, p), q7p((a2 * s1b - ab * s0b) / det, p)
        local err2 = m6_fit(px, 2 * nr0 + p, 2 * ng0 + p, 2 * nb0 + p, 2 * nr1 + p, 2 * ng1 + p, 2 * nb1 + p, IDXB)
        if err2 < err then r0, g0, b0, r1, g1, b1, idx = nr0, ng0, nb0, nr1, ng1, nb1, IDXB end
    end
    local flip = idx[0] >= 8                   -- el texel 0 guarda su indice con 3 bits: el bit alto tiene que ser 0
    if flip then r0, g0, b0, r1, g1, b1 = r1, g1, b1, r0, g0, b0 end
    outb_clear()
    local pos = putbits(0, 7, 64)
    pos = putbits(pos, 7, r0); pos = putbits(pos, 7, r1)
    pos = putbits(pos, 7, g0); pos = putbits(pos, 7, g1)
    pos = putbits(pos, 7, b0); pos = putbits(pos, 7, b1)
    pos = putbits(pos, 7, a7); pos = putbits(pos, 7, a7)
    pos = putbits(pos, 1, p); pos = putbits(pos, 1, p)
    for i = 0, 15 do
        local k = idx[i]; if flip then k = 15 - k end
        pos = putbits(pos, i == 0 and 3 or 4, k)
    end
    return outb_str()
end
local function x7(v) return v * 2 + floor(v / 64) end
local function q7(x) local v = floor(clamp255(x) / 2 + 0.5); if v > 127 then v = 127 end; return v end
local function bc7_mode5(px)
    local F = FAC[2]
    local mr, mg, mb, vx, vy, vz, tmin, tmax = pca(px)
    local r0, g0, b0 = q7(mr + tmin * vx), q7(mg + tmin * vy), q7(mb + tmin * vz)
    local r1, g1, b1 = q7(mr + tmax * vx), q7(mg + tmax * vy), q7(mb + tmax * vz)
    local er0, eg0, eb0, er1, eg1, eb1 = x7(r0), x7(g0), x7(b0), x7(r1), x7(g1), x7(b1)
    for k = 0, 3 do
        local w = F[k]
        PR[k] = floor((er0 * (64 - w) + er1 * w + 32) / 64); PG[k] = floor((eg0 * (64 - w) + eg1 * w + 32) / 64); PB[k] = floor((eb0 * (64 - w) + eb1 * w + 32) / 64)
    end
    local ci = IDXA
    for i = 0, 15 do
        local q = i * 4; local best, bk = 1e18, 0
        for k = 0, 3 do
            local dr, dg, db = px[q + 1] - PR[k], px[q + 2] - PG[k], px[q + 3] - PB[k]
            local e = dr * dr + dg * dg + db * db
            if e < best then best, bk = e, k end
        end
        ci[i] = bk
    end
    local cflip = ci[0] >= 2
    if cflip then r0, g0, b0, r1, g1, b1 = r1, g1, b1, r0, g0, b0 end
    local amin, amax = 255, 0
    for i = 0, 15 do local a = px[i * 4 + 4]; if a < amin then amin = a end; if a > amax then amax = a end end
    local a0, a1 = floor(amin + 0.5), floor(amax + 0.5)
    local ai = IDXB
    for i = 0, 15 do
        local a = px[i * 4 + 4]; local best, bk = 1e18, 0
        for k = 0, 3 do
            local w = F[k]; local v = floor((a0 * (64 - w) + a1 * w + 32) / 64)
            local e = (a - v) * (a - v)
            if e < best then best, bk = e, k end
        end
        ai[i] = bk
    end
    local aflip = ai[0] >= 2
    if aflip then a0, a1 = a1, a0 end
    outb_clear()
    local pos = putbits(0, 6, 32)
    pos = putbits(pos, 2, 0)
    pos = putbits(pos, 7, r0); pos = putbits(pos, 7, r1)
    pos = putbits(pos, 7, g0); pos = putbits(pos, 7, g1)
    pos = putbits(pos, 7, b0); pos = putbits(pos, 7, b1)
    pos = putbits(pos, 8, a0); pos = putbits(pos, 8, a1)
    for i = 0, 15 do local k = ci[i]; if cflip then k = 3 - k end; pos = putbits(pos, i == 0 and 1 or 2, k) end
    for i = 0, 15 do local k = ai[i]; if aflip then k = 3 - k end; pos = putbits(pos, i == 0 and 1 or 2, k) end
    return outb_str()
end
local function bc7_encode(px)
    local A = floor(px[4] + 0.5); local const = true
    for i = 1, 15 do if floor(px[i * 4 + 4] + 0.5) ~= A then const = false; break end end
    if const then return bc7_mode6(px, A) end
    return bc7_mode5(px)
end

-- ---------- FTEX ----------
local function ftex_parse(buf)
    if #buf < 128 or buf:sub(1, 4) ~= "FTEX" then return nil, "no es FTEX" end
    local fmt = u16(buf, 8); local W, H = u16(buf, 10), u16(buf, 12); local nm = u8(buf, 16)
    local BS = (fmt == 2) and 8 or ((fmt == 4 or fmt == 11) and 16) or nil
    if not BS then return nil, "formato " .. fmt .. " (no es BC1/BC3/BC7)" end
    if W < 64 or H < 64 or nm < 1 or nm > 16 or 64 + 16 * nm > #buf then return nil, "cabecera rara" end
    local T = { fmt = fmt, W = W, H = H, BS = BS, mips = {}, pad = true }
    for j = 0, nm - 1 do
        local e = 64 + 16 * j
        local off, usize, csize = u32(buf, e), u32(buf, e + 4), u32(buf, e + 8)
        local lvl, fnum, nch = u8(buf, e + 12), u8(buf, e + 13), u16(buf, e + 14)
        if fnum ~= 0 then return nil, "mips en archivos .ftexs" end
        if nch < 1 or off + 8 * nch > #buf then return nil, "tabla de mips rota" end
        local ch, tot = {}, 0
        for i = 0, nch - 1 do
            local c, u, o = u16(buf, off + 8 * i), u16(buf, off + 8 * i + 2), u32(buf, off + 8 * i + 4)
            local fl = o >= 2147483648
            if fl then o = o - 2147483648 end
            if u == 0 then u = 65536 end
            if off + o + c > #buf then return nil, "pedazo fuera del archivo" end
            ch[i + 1] = { c = c, u = u, o = o, raw = fl or c == u, fl = fl, start = tot }   -- (algunas herramientas guardan sin comprimir sin la marca)
            tot = tot + u
        end
        local w, h = max(1, floor(W / POW2[lvl])), max(1, floor(H / POW2[lvl]))
        if tot ~= usize or usize ~= floor((w + 3) / 4) * floor((h + 3) / 4) * BS then return nil, "tamano de mip raro" end
        if csize % 16 ~= 0 then T.pad = false end
        T.mips[j + 1] = { e = e, off = off, usize = usize, csize = csize, lvl = lvl, nch = nch, ch = ch, w = w, h = h, bw = floor((w + 3) / 4) }
    end
    return T
end
local function chunk_data(buf, mp, k)
    local c = mp.ch[k]
    if not c.data then
        local piece = buf:sub(mp.off + c.o + 1, mp.off + c.o + c.c)
        if c.raw then c.data = piece
        else
            local d, err = zlib.uncompress(piece, c.u)
            if type(d) ~= "string" or #d ~= c.u then error("zlib: " .. tostring(err)) end
            c.data = d
        end
    end
    return c.data
end
-- bytes del bloque (bx, by) del mip en BB[0..BS-1]; devuelve BB, el pedazo y el offset dentro del pedazo
local BB = {}
local function block_bytes(buf, T, mp, bx, by)
    local off = (by * mp.bw + bx) * T.BS
    local k = mp.lastk or 1
    if mp.ch[k].start > off then k = 1 end
    while mp.ch[k + 1] and mp.ch[k + 1].start <= off do k = k + 1 end
    mp.lastk = k
    local c = mp.ch[k]
    local d = chunk_data(buf, mp, k)
    local o = off - c.start
    if o + T.BS <= #d then
        for i = 0, T.BS - 1 do BB[i] = byte(d, o + i + 1) end
        return BB, k, o
    end
    -- bloque partido entre dos pedazos (hay herramientas que cortan los pedazos en cualquier lado): se lee igual,
    -- pero despues no se cambia
    local kk, oo = k, o
    for i = 0, T.BS - 1 do
        while oo >= #d and mp.ch[kk + 1] do kk = kk + 1; d = chunk_data(buf, mp, kk); oo = 0 end
        BB[i] = byte(d, oo + 1) or 0; oo = oo + 1
    end
    return BB, k, o, true
end
local function decode_block(T, b, out)            -- out RGBA 16 texels; false = no tocar (transparencia BC1)
    if T.fmt == 11 then bc7_decode(b, out); return true end
    if T.fmt == 4 then
        bc1_decode(b, 8, true, out)
        -- alfa BC3 (se conserva tal cual, solo para el promedio no hace falta)
        return true
    end
    return bc1_decode(b, 0, false, out)
end
local function encode_block(T, b, px)
    if T.fmt == 11 then return bc7_encode(px) end
    local col = bc1_encode(px)
    if T.fmt == 4 then return char(b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]) .. col end
    return col
end

-- color promedio de la franja: el mip de ~128 (cada texel ya es el promedio de 8x8 texels de 1024) suavizado dos
-- veces con 3x3 (solo con texels del cuello) ~ desenfoque de 8-10 texels en 1024. Tambien elige el tono de piel.
local function low_pass(buf, T, g)
    local mp, src, f = nil, nil, 1
    for _, m_ in ipairs(T.mips) do if m_.w <= 128 and m_.w >= 32 and (not mp or m_.w > mp.w) then mp = m_ end end
    if not mp then
        -- (1.10) sin mips chicos (solo la textura grande): el "mip de ~128" se arma promediando f x f texels de la grande
        for _, m_ in ipairs(T.mips) do if m_.w > 128 and (not src or m_.w < src.w) then src = m_ end end
        if not src then return nil, "sin mip chico" end
        f = floor(src.w / 128); if f < 2 then f = 2 end
        mp = { w = floor(src.w / f), h = floor(src.h / f) }
    end
    local w, h = mp.w, mp.h
    local vmin = 1                                   -- filas: desde 60 mm arriba de la union
    for j = g.j0, g.j1 do
        for i = 0, TEX.GU - 1 do
            local d = g[j * TEX.GU + i]
            if d and d > -0.060 then local v = j / TEX.GU; if v < vmin then vmin = v end end
        end
    end
    local by0 = max(0, floor(vmin * h / 4) - 1)
    local y0 = by0 * 4
    local nh = h - y0
    local R, G, B, Vd, Dp = {}, {}, {}, {}, {}
    local px = {}
    if src then
        local SN = {}
        for k = 0, nh * w - 1 do R[k], G[k], B[k], SN[k] = 0, 0, 0, 0 end
        local sw, sh = src.w, src.h
        for tby = floor(y0 * f / 4), floor((sh + 3) / 4) - 1 do
            for tbx = 0, src.bw - 1 do
                local b = block_bytes(buf, T, src, tbx, tby)
                decode_block(T, b, px)
                for ty = 0, 3 do
                    local Y = tby * 4 + ty
                    local y = floor(Y / f)
                    if Y < sh and y >= y0 and y < h then
                        for tx = 0, 3 do
                            local X = tbx * 4 + tx
                            local x = floor(X / f)
                            if X < sw and x < w then
                                local k = (y - y0) * w + x; local q = (ty * 4 + tx) * 4
                                R[k] = R[k] + px[q + 1]; G[k] = G[k] + px[q + 2]; B[k] = B[k] + px[q + 3]; SN[k] = SN[k] + 1
                            end
                        end
                    end
                end
            end
        end
        for y = y0, h - 1 do
            for x = 0, w - 1 do
                local k = (y - y0) * w + x
                local n = SN[k]
                if n > 0 then R[k], G[k], B[k] = R[k] / n, G[k] / n, B[k] / n else R[k], G[k], B[k] = nil, nil, nil end
                local d = grid_depth(g, (x + 0.5) / w, (y + 0.5) / h)
                Vd[k] = d and 1 or 0; Dp[k] = d or false
            end
        end
    end
    for by = by0, (src and -1 or floor((h + 3) / 4) - 1) do
        for bx = 0, mp.bw - 1 do
            local b = block_bytes(buf, T, mp, bx, by)
            decode_block(T, b, px)
            for ty = 0, 3 do
                for tx = 0, 3 do
                    local x, y = bx * 4 + tx, by * 4 + ty
                    if x < w and y < h then
                        local k = (y - y0) * w + x; local q = (ty * 4 + tx) * 4
                        R[k], G[k], B[k] = px[q + 1], px[q + 2], px[q + 3]
                        local d = grid_depth(g, (x + 0.5) / w, (y + 0.5) / h)
                        Vd[k] = d and 1 or 0; Dp[k] = d or false
                    end
                end
            end
        end
    end
    -- tono: promedio del cuello entre 25 y 50 mm sobre la union
    local sr, sg, sb, n = 0, 0, 0, 0
    for k = 0, nh * w - 1 do
        local d = Dp[k]
        if d and d < -0.025 and d > -0.050 then sr = sr + R[k]; sg = sg + G[k]; sb = sb + B[k]; n = n + 1 end
    end
    if n < 8 then return nil, "no se encontro el cuello en la textura" end
    sr, sg, sb = sr / n, sg / n, sb / n
    local tone, best = 1, 1e18
    for t = 1, 6 do
        local c = TEX.REF[t]; local e = (sr - c[1]) ^ 2 + (sg - c[2]) ^ 2 + (sb - c[3]) ^ 2
        if e < best then best, tone = e, t end
    end
    -- dos pasadas de promedio 3x3 con la mascara del cuello (u da la vuelta)
    local MR, MG, MB, MW = {}, {}, {}, {}
    local missing = 0
    for k = 0, nh * w - 1 do
        local v, r = Vd[k], R[k]
        if v == nil or r == nil or G[k] == nil or B[k] == nil then
            missing = missing + 1; R[k], G[k], B[k], v = 128, 128, 128, 0; Vd[k] = 0
        end
        MR[k] = R[k] * v; MG[k] = G[k] * v; MB[k] = B[k] * v; MW[k] = v
    end
    if missing > 0 then
        return nil, string.format("textura rara (%dx%d, fmt %d, mip %dx%d desde la fila %d: faltan %d texels)", T.W, T.H, T.fmt, w, h, y0, missing)
    end
    for _ = 1, 2 do
        local NR, NG, NB, NW = {}, {}, {}, {}
        for y = 0, nh - 1 do
            for x = 0, w - 1 do
                local sr2, sg2, sb2, sw = 0, 0, 0, 0
                for dy = -1, 1 do
                    local yy = y + dy
                    if yy >= 0 and yy < nh then
                        for dx = -1, 1 do
                            local k = yy * w + (x + dx) % w
                            sr2 = sr2 + MR[k]; sg2 = sg2 + MG[k]; sb2 = sb2 + MB[k]; sw = sw + MW[k]
                        end
                    end
                end
                local k = y * w + x
                NR[k], NG[k], NB[k], NW[k] = sr2, sg2, sb2, sw
            end
        end
        MR, MG, MB, MW = NR, NG, NB, NW
    end
    for k = 0, nh * w - 1 do
        local dn = MW[k]
        if dn > 1e-3 then MR[k], MG[k], MB[k] = MR[k] / dn, MG[k] / dn, MB[k] / dn
        else MR[k], MG[k], MB[k] = R[k], G[k], B[k] end
    end
    return { w = w, h = h, y0 = y0, nh = nh, R = MR, G = MG, B = MB, tone = tone, neck = { sr, sg, sb } }
end
local function low_at(L, u, v)
    local x, y = u * L.w - 0.5, v * L.h - 0.5 - L.y0
    local x0, y0 = floor(x), floor(y)
    local fx, fy = x - x0, y - y0
    local r, g, b = 0, 0, 0
    for dy = 0, 1 do
        local yy = min(max(y0 + dy, 0), L.nh - 1)
        local wy = dy == 0 and 1 - fy or fy
        for dx = 0, 1 do
            local xx = (x0 + dx) % L.w
            local w = wy * (dx == 0 and 1 - fx or fx)
            local k = yy * L.w + xx
            r = r + w * L.R[k]; g = g + w * L.G[k]; b = b + w * L.B[k]
        end
    end
    return r, g, b
end

-- arma el .ftex de nuevo con los pedazos cambiados (los demas se copian tal cual, en el mismo orden)
local function p32(v) return char(v % 256, floor(v / 256) % 256, floor(v / 65536) % 256, floor(v / 16777216) % 256) end
local function ftex_build(buf, T)
    local order = {}
    for _, mp in ipairs(T.mips) do order[#order + 1] = mp end
    table.sort(order, function(a, b) return a.off < b.off end)
    local nm = #T.mips
    local pos = 64 + 16 * nm
    local body, entries = {}, {}
    for _, mp in ipairs(order) do
        local tab, dat, rel = {}, {}, 8 * mp.nch
        for _, c in ipairs(mp.ch) do
            local piece, flag
            if c.mod then
                local offs = {}
                for o in pairs(c.mod) do offs[#offs + 1] = o end
                table.sort(offs)
                local segs, p = {}, 0
                for _, o in ipairs(offs) do
                    segs[#segs + 1] = c.data:sub(p + 1, o); segs[#segs + 1] = c.mod[o]; p = o + #c.mod[o]
                end
                segs[#segs + 1] = c.data:sub(p + 1)
                local rawd = table.concat(segs)
                if #rawd ~= c.u then error("pedazo mal armado") end
                local z = zlib.compress(rawd)
                if type(z) == "string" and #z < c.u and #z <= 65535 and byte(z, 1) == 120 then
                    if zlib.uncompress(z, c.u) ~= rawd then error("zlib no devuelve lo mismo") end
                    piece, flag = z, 0
                elseif c.u <= 65535 then piece, flag = rawd, 2147483648
                else error("pedazo de 64 KB que no se comprime") end
            else
                piece = buf:sub(mp.off + c.o + 1, mp.off + c.o + c.c); flag = c.fl and 2147483648 or 0
            end
            tab[#tab + 1] = ph16(#piece) .. ph16(c.u == 65536 and 0 or c.u) .. p32(rel + flag)
            dat[#dat + 1] = piece; rel = rel + #piece
        end
        local blk = table.concat(tab) .. table.concat(dat)
        if T.pad and #blk % 16 ~= 0 then blk = blk .. string.rep("\0", 16 - #blk % 16) end
        entries[mp] = { pos, #blk }
        body[#body + 1] = blk; pos = pos + #blk
    end
    local tabl = {}
    for _, mp in ipairs(T.mips) do
        local en = entries[mp]
        tabl[#tabl + 1] = p32(en[1]) .. p32(mp.usize) .. p32(en[2]) .. char(mp.lvl, 0) .. ph16(mp.nch)
    end
    return buf:sub(1, 64) .. table.concat(tabl) .. table.concat(body)
end

-- algun texel del bloque (o de al lado) cae en una celda que usa otra parte de la cara
local function prot_block(g, bx, by, w, h)
    local P = g.P; if not P then return false end
    local GU, GV = TEX.GU, TEX.GV
    local i0, i1 = floor(bx * 4 / w * GU) - 1, floor((bx * 4 + 4) / w * GU)
    local j0, j1 = max(0, floor(by * 4 / h * GU) - 1), min(GV - 1, floor((by * 4 + 4) / h * GU))
    for j = j0, j1 do
        for i = i0, i1 do
            if P[j * GU + i % GU] then return true end
        end
    end
    return false
end
-- deep = true: tambien se alisa lo que queda debajo de la union (se corta y no se ve; liso comprime mejor), pero solo
-- en bloques que caen enteros adentro del cuello; en los bordes del cuello se llega hasta TEX.MAXD
local function blend_pass(buf, T, g, L, C0, KW, deep)
    local tr, tg, tb = TEX.TORSO[L.tone][1], TEX.TORSO[L.tone][2], TEX.TORSO[L.tone][3]
    local GU = TEX.GU
    local lim, lim2 = -(TEX.RAMP + 0.003), deep and 1 or TEX.MAXD + 0.004
    local nblk = 0
    local px, nx = {}, {}
    for _, mp in ipairs(T.mips) do
        if mp.w >= TEX.MINW and mp.h >= TEX.MINW then
            local w, h, bw = mp.w, mp.h, mp.bw
            local nby = floor((h + 3) / 4)
            -- bloques que tocan la franja (celdas de la grilla cerca de la union, con un bloque de margen)
            local mark, list = {}, {}
            for j = g.j0, g.j1 do
                for i = 0, GU - 1 do
                    local d = g[j * GU + i]
                    if d and d > lim and d < lim2 then
                        local bx0, bx1 = floor(i / GU * w / 4) - 1, floor((i + 1) / GU * w / 4) + 1
                        local by0, by1 = floor(j / GU * h / 4) - 1, floor((j + 1) / GU * h / 4) + 1
                        for by = max(by0, 0), min(by1, nby - 1) do
                            for bx = bx0, bx1 do
                                local key = by * bw + (bx % bw)
                                if not mark[key] then mark[key] = true; list[#list + 1] = key end
                            end
                        end
                    end
                end
            end
            table.sort(list)
            for _, key in ipairs(list) do
                local by = floor(key / bw); local bx = key - by * bw
                local b, k, o, split = block_bytes(buf, T, mp, bx, by)
                -- peso del fundido y color promedio en los 4 texels de las esquinas; adentro del bloque, bilineal
                local ua, ub, va, vb = (bx * 4 + 0.5) / w, (bx * 4 + 3.5) / w, (by * 4 + 0.5) / h, (by * 4 + 3.5) / h
                local d00, d10, d01, d11 = grid_depth(g, ua, va), grid_depth(g, ub, va), grid_depth(g, ua, vb), grid_depth(g, ub, vb)
                local dm = (deep and d00 and d10 and d01 and d11) and 1 or TEX.MAXD
                local w00, w10, w01, w11 = ramp_w(d00, dm), ramp_w(d10, dm), ramp_w(d01, dm), ramp_w(d11, dm)
                if not split and (w00 > 0 or w10 > 0 or w01 > 0 or w11 > 0) and not prot_block(g, bx, by, w, h) and decode_block(T, b, px) then
                    local r00, g00, b00 = low_at(L, ua, va); local r10, g10, b10 = low_at(L, ub, va)
                    local r01, g01, b01 = low_at(L, ua, vb); local r11, g11, b11 = low_at(L, ub, vb)
                    local changed = false
                    for t = 0, 15 do
                        local tx, ty = t % 4, floor(t / 4)
                        local fx, fy = tx / 3, ty / 3
                        local c00, c10, c01, c11 = (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy
                        local wgt = w00 * c00 + w10 * c10 + w01 * c01 + w11 * c11
                        local q = t * 4
                        nx[q + 4] = px[q + 4]
                        if wgt > 0.002 then
                            changed = true
                            local lr = r00 * c00 + r10 * c10 + r01 * c01 + r11 * c11
                            local lg = g00 * c00 + g10 * c10 + g01 * c01 + g11 * c11
                            local lb = b00 * c00 + b10 * c10 + b01 * c01 + b11 * c11
                            local c = C0 * (1 - TEX.CW * wgt); local kk = 1 - KW * wgt; local iw = 1 - wgt
                            local d = px[q + 1] / max(lr, 1) - 1
                            if d > c then d = c elseif d < -c then d = -c end
                            nx[q + 1] = clamp255((1 + d * kk) * (lr * iw + tr * wgt))
                            d = px[q + 2] / max(lg, 1) - 1
                            if d > c then d = c elseif d < -c then d = -c end
                            nx[q + 2] = clamp255((1 + d * kk) * (lg * iw + tg * wgt))
                            d = px[q + 3] / max(lb, 1) - 1
                            if d > c then d = c elseif d < -c then d = -c end
                            nx[q + 3] = clamp255((1 + d * kk) * (lb * iw + tb * wgt))
                        else
                            nx[q + 1], nx[q + 2], nx[q + 3] = px[q + 1], px[q + 2], px[q + 3]
                        end
                    end
                    if changed then
                        local c = mp.ch[k]
                        c.mod = c.mod or {}
                        c.mod[o] = encode_block(T, b, nx)
                        nblk = nblk + 1
                    end
                end
            end
        end
    end
    return nblk
end
-- si la textura armada no entra en el lugar del archivo original (zlib comprime distinto que la herramienta con
-- la que se hizo la cara), se prueba de nuevo dejando menos detalle fino en la franja (comprime mejor)
local TRIES_TEX = { { 1.0, 0, true }, { 0.5, 0.8, true }, { 0, 1, true } }       -- {factor de C0, KW, alisar abajo}
local function tex_process(buf, g, forced)
    local T, err = ftex_parse(buf)
    if not T then return nil, err end
    local L, err2 = low_pass(buf, T, g)
    if not L then return nil, err2 end
    local guessed = L.tone
    if forced and TEX.TORSO[forced] then L.tone = forced end
    local last
    for ti, tr in ipairs(TRIES_TEX) do
        if ti > 1 then for _, mp in ipairs(T.mips) do for _, c in ipairs(mp.ch) do c.mod = nil end end end
        local nblk = blend_pass(buf, T, g, L, TEX.C0 * tr[1], tr[2] > 0 and tr[2] or TEX.KW, tr[3])
        if nblk == 0 then return nil, "nada que fundir" end
        local out = ftex_build(buf, T)
        if #out <= #buf then
            if #out < #buf then out = out .. string.rep("\0", #buf - #out) end
            return out, string.format("tono %d%s, %d bloques%s", L.tone, L.tone ~= guessed and " (forzado)" or "", nblk, ti > 1 and string.format(", detalle reducido %d", ti - 1) or ""), L
        end
        last = string.format("no entra (%d > %d bytes)", #out, #buf)
    end
    return nil, last
end

local CUR_GRIDS = nil            -- grillas del cuello de la cara que se esta cortando (para su textura), por textura
local CUR_FACE_KEY = nil         -- textura de la carpeta de la cara (si el material no dice cual usa)
-- "model/character/face/real/7511/sourceimages/face_bsm_alp" tanto para la ruta del fmdl como para el archivo leido
local function tex_key(path)
    local p = string.lower(path):gsub("\\", "/")
    p = p:gsub("#windx11/", ""):gsub("#win/", "")
    p = p:gsub("%.[^%./]*$", "")
    local k = p:find("model/", 1, true)
    if k then p = p:sub(k) end
    return p
end
-- textura base (Base_Tex_SRGB, o la que se llame Base_Tex...) del material de la malla mi
local function base_tex_key(F, mi, strict)
    local fallback = not strict and CUR_FACE_KEY or nil
    local me = entry(F, 3, mi); if not me then return fallback end
    local mat = entry(F, 4, u16(me, 4)); if not mat then return fallback end
    local cnt, first = u8(mat, 6), u16(mat, 8)
    local alt
    for k = 0, cnt - 1 do
        local a = entry(F, 7, first + k)
        local nm = a and str(F, u16(a, 0)) or ""
        if nm == "Base_Tex_SRGB" or (not alt and nm:find("Base_Tex", 1, true)) then
            local t = entry(F, 6, u16(a, 2))
            local key = t and tex_key(str(F, u16(t, 2)) .. str(F, u16(t, 0)))
            if key and key ~= "" then
                if nm == "Base_Tex_SRGB" then return key end
                alt = key
            end
        end
    end
    return alt or fallback
end

-- (1.10) forma del cuello de la cara cerca del borde del torso: por cada uno de 24 angulos alrededor del eje del cuello, el
-- radio de la piel (rayo horizontal desde el eje) a 3, 2, 1 y 0 cm arriba del borde y 1 cm abajo, y cuanto baja la malla en ese
-- angulo. Solo medidas (mm), en sider.log; con eso se le da al torso la forma del cuello de Konami
local PERFIL_HECHO = {}
local function neck_profile(M)
    local P, tris = M.P, M.tris[1]
    local sel = {}
    for _, t in ipairs(tris) do                          -- solo los triangulos de la franja del cuello
        local a, b, c = P[t[1]], P[t[2]], P[t[3]]
        local ok = false
        for _, q in ipairs({ a, b, c }) do
            local dq = depth(q)
            if dq > -0.045 and dq < 0.06 and sqrt(q[1] ^ 2 + (q[3] - AXZ) ^ 2) < 0.14 then ok = true end
        end
        if ok then sel[#sel + 1] = t end
    end
    local H = { 0.03, 0.02, 0.01, 0.0, -0.01 }
    local out = {}
    for ia = 0, 23 do
        local phi = -180 + ia * 15
        local r = rad(phi); local dx, dz = sin(r), cos(r)
        local yl = loop_y(phi)
        local vals = {}
        for _, hh in ipairs(H) do
            local oy = yl + hh
            local best = nil
            for _, t in ipairs(sel) do                     -- rayo (0, oy, AXZ) + s (dx, 0, dz): corte con el triangulo
                local a, b, c = P[t[1]], P[t[2]], P[t[3]]
                local e1x, e1y, e1z = b[1] - a[1], b[2] - a[2], b[3] - a[3]
                local e2x, e2y, e2z = c[1] - a[1], c[2] - a[2], c[3] - a[3]
                local px_, py_, pz_ = -e2y * dz, dz * e2x - dx * e2z, dx * e2y   -- d x e2, d = (dx, 0, dz)
                local det = e1x * px_ + e1y * py_ + e1z * pz_
                if abs(det) > 1e-14 then
                    local inv = 1 / det
                    local tx, ty, tz = -a[1], oy - a[2], AXZ - a[3]
                    local u = (tx * px_ + ty * py_ + tz * pz_) * inv
                    if u >= 0 and u <= 1 then
                        local qx, qy, qz = ty * e1z - tz * e1y, tz * e1x - tx * e1z, tx * e1y - ty * e1x
                        local v = (dx * qx + dz * qz) * inv
                        if v >= 0 and u + v <= 1 then
                            local s_ = (e2x * qx + e2y * qy + e2z * qz) * inv
                            if s_ > 0.005 and (not best or s_ < best) then best = s_ end
                        end
                    end
                end
            end
            vals[#vals + 1] = best and string.format("%.1f", best * 1000) or "-"
        end
        local low = nil                                    -- lo mas bajo de la malla en ese angulo (+-8 grados), mm bajo el borde
        for _, t in ipairs(sel) do
            for k = 1, 3 do
                local q = P[t[k]]
                local a = deg(atan2(q[1], q[3] - AXZ))
                local da = (a - phi + 540) % 360 - 180
                if abs(da) < 8 then local dq = depth(q); if not low or dq > low then low = dq end end
            end
        end
        out[#out + 1] = string.format("%d:%s/%s", phi, table.concat(vals, ","), low and string.format("%.1f", low * 1000) or "-")
    end
    return table.concat(out, " ")
end

-- (1.12) SOLDADURA POR CARA: el cuello de 11 de 19 caras reales medidas es el de Konami de siempre (en el borde del torso el
-- radio es el del borde al 0.5 mm: el torso de TorsoBody ya tiene esa forma), pero otras tienen el cuello hasta 8 mm mas
-- fino o mas grueso en algun angulo: quedaba un escalon (se veia el labio del torso o el cuello salido). Aca, por cara: el
-- radio del cuello justo arriba del borde (rayos desde el eje a 3 y 8 mm, extrapolado al borde) contra el del borde del
-- torso, en 24 angulos; los vertices de los SOLDAR m de abajo se mueven en horizontal (desde el eje) esa diferencia, entera
-- en el borde y nada a SOLDAR m (smoothstep): arriba queda la forma de Konami. Las caras que ya coinciden (< 1 mm) no se tocan.
local SOLDAR = 0.025
local SOLDAR_MAX = 0.012
local CUR_DELTA = nil              -- diferencias de la malla de piel de la cara actual (las otras mallas usan las mismas)
local function band_tris(M)
    local P, sel = M.P, {}
    for _, T in ipairs(M.tris) do
        for _, t in ipairs(T) do
            local ok = false
            for k = 1, 3 do
                local q = P[t[k]]; local dq = depth(q)
                if dq > -0.045 and dq < 0.06 and sqrt(q[1] ^ 2 + (q[3] - AXZ) ^ 2) < 0.14 then ok = true; break end
            end
            if ok then sel[#sel + 1] = t end
        end
    end
    return sel
end
local function ray_r(P, sel, phi, oy)
    local r = rad(phi); local dx, dz = sin(r), cos(r)
    local best = nil
    for _, t in ipairs(sel) do
        local a, b, c = P[t[1]], P[t[2]], P[t[3]]
        local e1x, e1y, e1z = b[1] - a[1], b[2] - a[2], b[3] - a[3]
        local e2x, e2y, e2z = c[1] - a[1], c[2] - a[2], c[3] - a[3]
        local px_, py_, pz_ = -e2y * dz, dz * e2x - dx * e2z, dx * e2y
        local det = e1x * px_ + e1y * py_ + e1z * pz_
        if abs(det) > 1e-14 then
            local inv = 1 / det
            local tx, ty, tz = -a[1], oy - a[2], AXZ - a[3]
            local u = (tx * px_ + ty * py_ + tz * pz_) * inv
            if u >= 0 and u <= 1 then
                local qx, qy, qz = ty * e1z - tz * e1y, tz * e1x - tx * e1z, tx * e1y - ty * e1x
                local v = (dx * qx + dz * qz) * inv
                if v >= 0 and u + v <= 1 then
                    local s_ = (e2x * qx + e2y * qy + e2z * qz) * inv
                    if s_ > 0.005 and (not best or s_ < best) then best = s_ end
                end
            end
        end
    end
    return best
end
local function weld_deltas(M)
    local sel = band_tris(M)
    local D, n, mx = {}, 0, 0
    for ia = 0, 23 do
        local phi = -180 + ia * 15
        local rr, y0 = ring_r(phi)
        local r3, r8 = ray_r(M.P, sel, phi, y0 + 0.003), ray_r(M.P, sel, phi, y0 + 0.008)
        if r3 and r8 then
            local d = rr - (r3 + (r3 - r8) * 0.6)
            if d > SOLDAR_MAX then d = SOLDAR_MAX elseif d < -SOLDAR_MAX then d = -SOLDAR_MAX end
            D[ia] = d; n = n + 1; if abs(d) > mx then mx = abs(d) end
        end
    end
    if n < 12 then return nil, "sin medida" end
    for ia = 0, 23 do                                   -- los angulos sin medida: de los vecinos
        if not D[ia] then
            local l, r_ = nil, nil
            for k = 1, 12 do
                if not l and D[(ia - k) % 24] then l = { D[(ia - k) % 24], k } end
                if not r_ and D[(ia + k) % 24] then r_ = { D[(ia + k) % 24], k } end
            end
            D[ia] = (l[1] * r_[2] + r_[1] * l[2]) / (l[2] + r_[2])
        end
    end
    if mx < 0.001 then return nil, string.format("cuello de Konami (%.1f mm)", mx * 1000) end
    return D, string.format("soldado al borde (hasta %.1f mm)", mx * 1000)
end
local function weld_move(D, p, d)
    local w
    if d >= 0 then w = 1 else
        local x = -d / SOLDAR; if x >= 1 then return nil end
        x = 1 - x; w = x * x * (3 - 2 * x)
    end
    local ox, oz = p[1], p[3] - AXZ
    local l = sqrt(ox * ox + oz * oz); if l < 0.01 then return nil end
    local f = (deg(atan2(ox, oz)) + 180) / 15
    local i0 = floor(f); local t = f - i0
    local dd = (D[i0 % 24] * (1 - t) + D[(i0 + 1) % 24] * t) * w
    return { p[1] + ox / l * dd, p[2], p[3] + oz / l * dd }
end
local function apply_weld(M, plan, D)
    local Pa
    for _, a in ipairs(M.attrs) do if a.type == 0 and a.fmt == 1 then Pa = a end end
    if not Pa then return 0 end
    local n, newset = 0, {}
    for _, nvd in ipairs(plan.newv) do
        newset[nvd.v] = true
        local p = nvd.vals[Pa]
        if p then local q = weld_move(D, p, 0); if q then nvd.vals[Pa] = { q[1], q[2], q[3], p[4] }; n = n + 1 end end
    end
    plan.moves = plan.moves or {}
    for i = 0, M.nv - 1 do
        if not newset[i] then
            local mv = plan.moves[i]
            local q = weld_move(D, mv or M.P[i], mv and 0 or M.d[i])
            if q then plan.moves[i] = q; n = n + 1 end
        end
    end
    return n
end

local function cut_mesh(F, mi, name, shader)
    local M, err = read_mesh(F, mi)
    if not M then return err end
    if PERFIL and name == "fox_skin_mat" and CUR_FACE_KEY and not PERFIL_HECHO[CUR_FACE_KEY] then
        PERFIL_HECHO[CUR_FACE_KEY] = true
        local okp, prof
        if PC then okp, prof = PC(neck_profile, M) else okp, prof = true, neck_profile(M) end   -- (1.11) Sider no tiene pcall
        log("TorsoBody cuello: perfil " .. tostring(CUR_FACE_KEY) .. " (radio mm a +3/+2/+1/0/-1 cm del borde / baja mm): " .. (okp and prof or ("error " .. tostring(prof))))
    end
    if FUNDIR and name == "fox_skin_mat" and (shader == "" or shader:find("skin", 1, true)) then
        local key = base_tex_key(F, mi)
        if key then CUR_GRIDS[key] = make_grid(M, CUR_GRIDS[key]) end
    end
    local maxd
    M.extra, maxd = extra_profile(M)
    local note = ""
    if maxd > GAP_OK then note = string.format(" [cuello hasta %.1f mm adentro del torso: ahi se corta mas abajo]", maxd * 1000) end
    local last
    for ti, tr in ipairs(TRIES) do
        local plan, why = make_plan(M, tr[1], tr[2], tr[3], false, tr[4])
        if DEBUG_CUELLO then log(string.format("  %s intento %d: %s", name, ti, plan and (plan == "nada" and "nada" or plan.msg) or tostring(why))) end
        if plan == "nada" then return nil end
        if plan then
            bad_vals = 0
            if NORMALES_TORSO and name:find("skin", 1, true) and (shader == "" or shader:find("skin", 1, true)) then
                local nn = blend_normals(M, plan)
                if nn > 0 then note = note .. string.format(" [%d normales de la union]", nn) end
            end
            if SOLDAR > 0 then                              -- (1.12) soldadura del cuello de esta cara al borde del torso
                local D, wmsg = CUR_DELTA, nil
                if name == "fox_skin_mat" then             -- las otras mallas (barba, etc.) solo con lo de la piel
                    if PC then local okw, a1, a2 = PC(weld_deltas, M); if okw then D, wmsg = a1, a2 else D, wmsg = nil, "error " .. tostring(a1) end
                    else D, wmsg = weld_deltas(M) end
                    CUR_DELTA = D or false
                end
                if D then
                    local nw = apply_weld(M, plan, D)
                    note = note .. string.format(" [%s, %d vertices]", wmsg or "soldado como la piel", nw)
                elseif wmsg then note = note .. " [" .. wmsg .. "]" end
            end
            apply_plan(M, plan)
            if bad_vals > 0 then note = note .. string.format(" [%d valores no validos corregidos]", bad_vals) end
            if ti == 1 then return plan.msg .. note end
            return string.format("%s [corte %.2f mm]%s", plan.msg, tr[1] * 1000, note)
        end
        last = why
    end
    -- ultimo recurso: se sacan solo los triangulos que quedan enteros debajo del borde (la V del pecho)
    local plan = make_plan(M, 0.00005, -0.00005, 0, true)
    if plan and plan ~= "nada" then
        apply_plan(M, plan)
        return "corte parcial (" .. tostring(last) .. "): " .. plan.msg .. note
    end
    return "no se pudo cortar: " .. tostring(last)
end

-- opcion QUITAR_COLLAR_AO: la capa collar_ao se quita entera, como en las caras genericas de TorsoBody.
-- Sus triangulos quedan vacios (los tres vertices iguales), asi el archivo mantiene el mismo tamano.
local function hide_mesh(F, mi)
    local me = entry(F, 3, mi); if not me then return "sin malla" end
    local firstface, lodbase = u32(me, 16), u32(me, 24)
    local lodc = entry(F, 16, 0); local nlod = lodc and u32(lodc, 0) or 1
    if nlod < 1 or nlod > 16 then nlod = 1 end
    local ib = F.b0[14] and entry(F, 14, 2); if not ib then return "sin indices" end
    local ibase = F.b1[2].addr + u32(ib, 8)
    local seen, n = {}, 0
    for l = 0, nlod - 1 do
        local e = entry(F, 17, lodbase + l)
        if e then
            local first, cnt = firstface + u32(e, 0), u32(e, 4)
            local key = first .. ":" .. cnt
            if not seen[key] and cnt > 0 then
                seen[key] = true
                local s = rd(ibase + first * 2, cnt * 2)
                local parts = {}
                for k = 0, floor(cnt / 3) - 1 do
                    local a = u16(s, k * 6)
                    if not (a == u16(s, k * 6 + 2) and a == u16(s, k * 6 + 4)) then n = n + 1 end
                    parts[#parts + 1] = ph16(a) .. ph16(a) .. ph16(a)
                end
                wr(ibase + first * 2, table.concat(parts))
            end
        end
    end
    if n == 0 then return nil end
    return string.format("quitada entera (%d triangulos)", n)
end

-- para el log cuando no se corta nada: punto mas bajo de la malla respecto del borde del torso (mm, + = abajo)
local function mesh_lowest(F, mi)
    local me = entry(F, 3, mi); if not me then return nil end
    local L = mesh_layout(F, u16(me, 8)); if not L then return nil end
    local nv = u16(me, 10)
    for _, a in ipairs(L) do
        if a.type == 0 and a.fmt == 1 then
            local s = rd(a.addr, (nv - 1) * a.stride + 12)
            local best = -1e9
            for i = 0, nv - 1 do
                local d = depth(read_attr(1, s, i * a.stride))
                if d > best then best = d end
            end
            return best * 1000, nv
        end
    end
    return nil
end

local function process_fmdl(base, size)
    local F, err = parse_fmdl(base, size)
    if not F then return false, err end
    if not (F.b0[3] and F.b0[4] and F.b0[9] and F.b0[10] and F.b0[11] and F.b0[14] and F.b0[17] and F.b1[2]) then
        return false, "faltan bloques"
    end
    local done = {}
    for mi = 0, F.b0[3].count - 1 do
        local me = entry(F, 3, mi)
        local mat = entry(F, 4, u16(me, 4))
        local name = mat and str(F, u16(mat, 0)) or ""
        local shader = ""
        if mat then local mt = entry(F, 8, u16(mat, 4)); if mt then shader = string.lower(str(F, u16(mt, 0))) end end
        if name ~= "collar_ao" and name ~= "fox_skin_mat" then        -- caras con otros nombres de material
            if shader == "pes_3ddf_skin_face" then name = "fox_skin_mat" elseif shader == "pes_3ddc_adjust" then name = "collar_ao" end
        end
        if name == "fox_skin_mat" or name == "collar_ao" then
            local mark = #PATCH
            local ok, msg
            local fn_ = name == "collar_ao" and QUITAR_COLLAR_AO and hide_mesh or cut_mesh
            if PC then ok, msg = PC(fn_, F, mi, name, shader) else ok, msg = true, fn_(F, mi, name, shader) end
            if not ok then
                for k = #PATCH, mark + 1, -1 do PATCH[k] = nil end           -- lo de esta malla no se escribe
                done[#done + 1] = name .. ": error " .. tostring(msg)
            elseif msg then done[#done + 1] = name .. ": " .. msg end
        end
    end
    local any_grid = false
    for _ in pairs(CUR_GRIDS) do any_grid = true; break end
    if FUNDIR and any_grid then                                -- lo que usan las demas partes de la cara no se toca
        for mi = 0, F.b0[3].count - 1 do
            local key = base_tex_key(F, mi, true)
            local g = key and CUR_GRIDS[key]
            if g then
                g.P = g.P or {}
                if PC then PC(protect_mesh, F, mi, g.P) else protect_mesh(F, mi, g.P) end
            end
        end
    end
    if #done == 0 then                                         -- nada para cortar: se anota que mallas tiene la cara
        local info = {}
        for mi = 0, min(F.b0[3].count - 1, 15) do
            local me = entry(F, 3, mi)
            local mat = entry(F, 4, u16(me, 4))
            local nm = mat and str(F, u16(mat, 0)) or "?"
            local sh = "?"
            if mat then local mt = entry(F, 8, u16(mat, 4)); if mt then sh = str(F, u16(mt, 0)) end end
            local low, nv = mesh_lowest(F, mi)
            info[#info + 1] = string.format("%d %s/%s %sv %s", mi, nm, sh, tostring(nv or "?"), low and string.format("%+.0fmm", low) or "?")
        end
        return true, "nada que cortar; mallas: " .. table.concat(info, ", ")
    end
    return true, table.concat(done, "; ")
end

local count_skip = 0
local GRIDS, GRID_ORDER = {}, {}
-- la grilla se guarda compacta (2 bytes por celda, 36 KB por cara) hasta que llegue la textura
-- la grilla se guarda compacta (2 bytes por celda, solo las filas que tienen cuello) hasta que llegue la textura
local function grid_pack(g)
    local GU = TEX.GU
    local parts = { char(g.j0 % 256, floor(g.j0 / 256), g.j1 % 256, floor(g.j1 / 256)) }
    for k = g.j0 * GU, (g.j1 + 1) * GU - 1 do
        local d = g[k]; local q = 0
        if d then
            q = floor((d + 0.08) / 0.2 * 65533 + 0.5) + 2
            if q < 1 then q = 1 elseif q > 65535 then q = 65535 end
        end
        parts[#parts + 1] = char(q % 256, floor(q / 256))
    end
    local P, pp = g.P or {}, {}
    for k = g.j0 * GU, (g.j1 + 1) * GU - 1 do pp[#pp + 1] = P[k] and "\1" or "\0" end
    parts[#parts + 1] = table.concat(pp)
    return table.concat(parts)
end
local function grid_unpack(s)
    local GU = TEX.GU
    local a, b, c, e = byte(s, 1, 4)
    local g = { j0 = a + b * 256, j1 = c + e * 256 }
    local base = g.j0 * GU
    for k = base, (g.j1 + 1) * GU - 1 do
        local p = 4 + 2 * (k - base)
        local x, y = byte(s, p + 1, p + 2)
        local q = x + y * 256
        if q > 0 then g[k] = (q - 2) / 65533 * 0.2 - 0.08 end
    end
    local P, pb = {}, 4 + 2 * ((g.j1 + 1) * GU - base)
    for k = base, (g.j1 + 1) * GU - 1 do
        if byte(s, pb + 1 + k - base) == 1 then P[k] = true end
    end
    g.P = P
    return g
end
local function keep_grid(id, g)
    if not GRIDS[id] then GRID_ORDER[#GRID_ORDER + 1] = id end
    GRIDS[id] = grid_pack(g)
    while #GRID_ORDER > 64 do GRIDS[table.remove(GRID_ORDER, 1)] = nil end
end
local SEEN_TEX, n_seen = {}, 0      -- texturas de caras que llegaron sin grilla (para avisar si la cara viene despues)

-- ---------- cache en disco ----------
local CACHE_DIR = nil                -- se fija en init (ctx.sider_dir .. content\TorsoBody_cuello_cache\)
local CACHE_OK = true                -- false si no se pudo escribir (carpeta inexistente): se sigue sin cache
local FROM_CACHE = {}                -- archivos (en minusculas) que Sider esta sirviendo desde la cache
local CACHE_TAG = "_v21"   -- (1.10) color del tono 1 nuevo
local function cache_paths(fn)
    if not CACHE_DIR then return nil end
    local id = fn:match("face[\\/]+real[\\/]+([^\\/]+)[\\/]")
    if not id or id:find("[^%w_%-]") then return nil end
    if fn:find("face%.fpk$") then return CACHE_DIR .. id .. CACHE_TAG .. ".face.fpk", CACHE_DIR .. id .. CACHE_TAG .. ".grid", id end
    if fn:find("face_bsm_alp%.ftex$") then return CACHE_DIR .. id .. CACHE_TAG .. ".face_bsm_alp.ftex", nil, id end
    return nil
end
local function file_read(p)
    local f = io.open(p, "rb"); if not f then return nil end
    local d = f:read("*a"); f:close(); return d
end
local function file_write(p, d)
    local f = io.open(p, "wb")
    if not f then
        if CACHE_OK then CACHE_OK = false; log("TorsoBody cuello: no se puede escribir la cache en " .. tostring(CACHE_DIR) .. " (crea esa carpeta); se sigue sin cache") end
        return false
    end
    f:write(d); f:close(); return true
end
local function file_exists(p) local f = io.open(p, "rb"); if f then f:close(); return true end; return false end
-- grillas de una cara: "clave\n" .. datos empaquetados, varias seguidas (largo fijo por grilla)
local function grids_save(path, grids)
    local parts = {}
    for key, g in pairs(grids) do
        if g.n >= 100 then local gp = grid_pack(g); parts[#parts + 1] = key .. "\n" .. string.format("%010d", #gp) .. gp end
    end
    if #parts > 0 then file_write(path, table.concat(parts)) end
end
local function grids_load(path)
    local d = file_read(path); if not d then return 0 end
    local pos, n = 1, 0
    while pos <= #d do
        local nl = d:find("\n", pos, true); if not nl then break end
        local key = d:sub(pos, nl - 1); local len = tonumber(d:sub(nl + 1, nl + 10)); if not len then break end
        local gp = d:sub(nl + 11, nl + 10 + len)
        if #gp ~= len then break end
        if not GRIDS[key] then GRID_ORDER[#GRID_ORDER + 1] = key end
        GRIDS[key] = gp; n = n + 1
        pos = nl + 11 + len
    end
    while #GRID_ORDER > 64 do GRIDS[table.remove(GRID_ORDER, 1)] = nil end
    return n
end
local function make_key(ctx, filename)
    if not (USAR_CACHE and CACHE_OK) then return end
    local fn = string.lower(filename)
    if not fn:find("face[\\/]+real[\\/]") then return end
    local cp, gp = cache_paths(fn); if not cp then return end
    if file_exists(cp) then
        FROM_CACHE[fn] = cp
        if gp then grids_load(gp) end                           -- la textura de esa cara va a necesitar su grilla
        return "tbcuello|" .. cp
    end
    FROM_CACHE[fn] = nil
end
local function get_filepath(ctx, filename, key)
    if type(key) == "string" and key:sub(1, 9) == "tbcuello|" then return key:sub(10) end
end
local function gc() if collectgarbage then collectgarbage("collect") end end

local function texture_ready(filename, fn, data, size, total_size, offset)
    if FROM_CACHE[fn] then return end                            -- ya viene cortada/fundida desde la cache
    local key = tex_key(fn)
    local gs = GRIDS[key]
    if not gs then                                               -- textura de una cara que no se corto: queda igual
        if not SEEN_TEX[key] and n_seen < 4000 and fn:find("face_bsm_alp%.ftex$") then SEEN_TEX[key] = true; n_seen = n_seen + 1 end
        return
    end
    if offset ~= 0 or size < total_size then
        count_skip = count_skip + 1
        if count_skip <= 20 then log("textura leida en partes, sin fundir: " .. filename) end
        return
    end
    local buf = memory.read(data, size)
    if type(buf) ~= "string" or #buf ~= size then return end
    local g = grid_unpack(gs)
    local id = fn:match("face[\\/]+real[\\/]+([^\\/]+)[\\/]")
    log("fundiendo " .. tostring(id) .. " (" .. size .. " bytes)...")
    local forced = id and TONO_FORZADO[id]
    local t0 = os and os.clock and os.clock() or 0
    local ok, out, msg
    if PC then ok, out, msg = PC(tex_process, buf, g, forced) else ok = true; out, msg = tex_process(buf, g, forced) end
    if not ok then log("cuello fundido: error en " .. filename .. ": " .. tostring(out)); return end
    if not out then log("cuello fundido: sin cambios en " .. filename .. " (" .. tostring(msg) .. ")"); return end
    memory.write(data, out)
    local cached = ""
    if USAR_CACHE and CACHE_OK then local cp = cache_paths(fn); if cp and file_write(cp, out) then cached = ", guardada en cache" end end
    out = nil; buf = nil; gc()
    local dt = (os and os.clock and os.clock() or 0) - t0
    log(string.format("cuello fundido: %s (%s, %.2f s%s)", filename, msg, dt, cached))
end
local function data_ready(ctx, filename, data, size, total_size, offset)
    size, total_size, offset = tonumber(size), tonumber(total_size), tonumber(offset or 0)
    if not (filename and data and size and total_size) then return end
    local fn = string.lower(filename)
    if not fn:find("face[\\/]+real[\\/]") then return end
    if FUNDIR and fn:find("%.ftex$") then return texture_ready(filename, fn, data, size, total_size, offset) end
    if not fn:find("face%.fpk$") then return end
    if FROM_CACHE[fn] then return end                            -- ya viene cortada desde la cache
    if offset ~= 0 or size < total_size then
        count_skip = count_skip + 1
        if count_skip <= 20 then log("saltado (leido en partes): " .. filename) end
        return
    end
    local t0 = os and os.clock and os.clock() or 0
    local buf = memory.read(data, size)
    if type(buf) ~= "string" or #buf ~= size or buf:sub(1, 6) ~= "foxfpk" then
        count_skip = count_skip + 1
        if count_skip <= 20 then log("saltado (no es fpk sin comprimir): " .. filename) end
        return
    end
    local n = u32(buf, 36)
    if n > 64 then return end
    log("cortando " .. tostring(fn:match("face[\\/]+real[\\/]+([^\\/]+)[\\/]")) .. " (" .. size .. " bytes)...")
    BUF, PATCH, CUR_GRIDS = buf, {}, {}
    CUR_FACE_KEY = tex_key((fn:gsub("#win[\\/]+face%.fpk$", "sourceimages/face_bsm_alp")))
    local msgs = {}
    for i = 0, n - 1 do
        local e = 48 + i * 48
        local off, dsize, soff, slen = u32(buf, e), u32(buf, e + 8), u32(buf, e + 16), u32(buf, e + 24)
        if soff + slen <= size and off + dsize <= size then
            local name = string.lower(buf:sub(soff + 1, soff + slen))
            if name:find("face_high%.fmdl$") then
                local ok, msg = process_fmdl(off, dsize)
                msgs[#msgs + 1] = ok and (msg ~= "" and msg or "nada que cortar") or ("no se pudo: " .. tostring(msg))
            end
        end
    end
    local patches, grids = PATCH, CUR_GRIDS
    BUF, PATCH, CUR_GRIDS, CUR_FACE_KEY, CUR_DELTA = nil, nil, nil, nil, nil
    if #patches > 0 then
        local newbuf = apply_patches(buf, patches)
        if not newbuf or #newbuf ~= size then
            log("no se pudo cortar " .. filename .. ": error al armar el archivo")
            return
        end
        memory.write(data, newbuf)
        local note = ""
        if USAR_CACHE and CACHE_OK then
            local cp, gp = cache_paths(fn)
            if cp and file_write(cp, newbuf) then note = " [guardada en cache]"; if FUNDIR and gp then grids_save(gp, grids) end end
        end
        if FUNDIR then
            local kept = 0
            for key, g in pairs(grids) do
                if g.n >= 100 then
                    keep_grid(key, g); kept = kept + 1
                    if SEEN_TEX[key] then note = note .. " [su textura ya se habia cargado antes: se funde la proxima vez que se cargue]" end
                end
            end
            if kept == 0 then note = " [no se encontro el cuello en el mapa de la textura: la textura queda igual]" end
        end
        newbuf = nil; buf = nil; gc()
        log(string.format("cuello cortado: %s (%s)%s [%.2f s]", filename, table.concat(msgs, " | "), note, (os and os.clock and os.clock() or 0) - t0))
    elseif #msgs > 0 then
        log(string.format("sin cambios: %s (%s)", filename, table.concat(msgs, " | ")))
    end
end

function m.init(ctx)
    if ctx.torsobody_cuello then          -- la linea esta dos veces en sider.ini: con una copia alcanza
        log("TorsoBody cuello: ya estaba cargado (lua.module repetido en sider.ini), esta copia no hace nada")
        return
    end
    ctx.torsobody_cuello = m.version
    if FUNDIR then                       -- la textura se arma con el zlib de Sider: se prueba una vez
        local ok = false
        if zlib and zlib.compress and zlib.uncompress then
            local probe = string.rep("TorsoBody cuello 1234567890 ", 64)
            local z = zlib.compress(probe)
            ok = type(z) == "string" and byte(z, 1) == 120 and zlib.uncompress(z, #probe) == probe
        end
        if not ok then FUNDIR = false; log("TorsoBody cuello: el zlib de Sider no responde como se espera, solo se corta (sin fundir texturas)") end
    end
    local nojit = NOJIT and " (sin JIT)" or ""
    if USAR_CACHE and type(ctx.sider_dir) == "string" then
        -- se prueban varias carpetas: el juego tiene que poder escribir ahi (la crea el con fs.make_dirs)
        local cands = { "content\\TorsoBody_cuello_cache\\datos\\", "content\\TorsoBody_cuello_cache\\", "content\\TorsoBody_cuello_cache2\\", "modules\\TorsoBody_cuello_cache\\" }
        local found = nil
        for _, rel in ipairs(cands) do
            local dir = ctx.sider_dir .. rel
            if type(fs) == "table" and type(fs.make_dirs) == "function" then fs.make_dirs(dir) end
            local f = io.open(dir .. "prueba_escritura.tmp", "wb")
            if f then f:write("ok"); f:close(); found = dir; break end
        end
        if found then CACHE_DIR = found; nojit = nojit .. " (cache en " .. found:sub(#ctx.sider_dir + 1) .. ")"
        else CACHE_OK = false; log("TorsoBody cuello: no se pudo escribir en ninguna carpeta de cache (content\\TorsoBody_cuello_cache); se sigue sin cache") end
        ctx.register("livecpk_make_key", make_key)
        ctx.register("livecpk_get_filepath", get_filepath)
    end
    ctx.register("livecpk_data_ready", data_ready)
    log("TorsoBody cuello " .. m.version .. ": corta el cuello de las caras reales en el borde del torso" .. (FUNDIR and " y funde su textura con el torso" or "") .. nojit)
end

m._test = { process_fmdl = process_fmdl, data_ready = data_ready, depth = depth, tex_process = tex_process, make_grid = make_grid, GRIDS = GRIDS,
            bc7_decode = bc7_decode, bc7_encode = bc7_encode, bc1_decode = bc1_decode, bc1_encode = bc1_encode }
return m
end
if SIN_JIT and type(jit) == "table" and jit.off then jit.off(modulo_cuello, true); NOJIT = true end
return modulo_cuello()
