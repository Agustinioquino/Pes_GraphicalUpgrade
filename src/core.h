// PES21 FX - mod grafico para PES 2021 (DX11). Lo carga Sider (PES21FX.lua) y reemplaza a 3DMigoto.
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct ImFont;   // (26/10 c) imgui.h

namespace fx {

// ---------------------------------------------------------------- log
void Log(const char* fmt, ...);
void SunLog(const char* fmt, ...);   // (27/10) solo al registro del sol (PES21FX\registro_sol)
std::string ModOff(void* p);   // "modulo.dll+0x1234" (diagnostico)
void ScanRefsInto(uintptr_t base, uintptr_t end, const char* dllName);   // quien apunta todavia a una DLL que se descarga
extern void* g_overlayOldProc;

// ---------------------------------------------------------------- parametros (lo que en 3DMigoto era IniParams)
constexpr int kParamCount = 376;   // (07/11) 375 = medidor de VRAM y de ms de GPU (gpumeter.cpp) -> 376   // (04/11 boca) 374 = mascara del interior de la boca (mouthmask.cpp) -> 375   // (04/11 o) 373 = color de las hebras de ceja de TODOS (x mezcla, yzw rgb)           // float4 x 348 -> t120 en los shaders (IniParams.Load(int2(i,0))). (20/10: era 128 y los parametros 128+ se perdian; 25/10: 256 -> 320, la ropa contra el cuerpo usa 256 / 257; 27/10: 320 -> 336: la fila 320 (torso bajo la camiseta) quedaba fuera y el ini la descartaba; 31/10: 340 -> 344 (expresiones 340 / 341); 01/11: 344 -> 346: 343 camara lenta, 344 / 345 camara libre; 01/11 b: 346 -> 347: 346 seguir la cara; 347 -> 348: 347 abrir la boca / nariz; 348 -> 349: 348 musculos virtuales; 01/11 c: 349 -> 351: 349 torso, 350 boca por huesos; 351 torso bajo la camiseta ajustada; 352 mangas mas pegadas al brazo; 353 viento por partido; 03/11: 354 -> 355: 354 ojos en circulos (expresiones); 355 -> 356: 355 gestos enojo / sonrisa / asombro / hablar; 04/11: 356 -> 357: 356 grito, puchero, perfiles por cara; 04/11 b: 357 -> 359: 357 / 358 teselacion de la cara; 04/11 c: 359 -> 360: 359 relieve de la teselacion; 04/11 d: 360 -> 361: 360 prueba de la teselacion; 04/11 e: 361 / 362 sliders por hueso (expresiones); 363 -> 364: 363 caja de las piernas del atlas de sombras (grosor de la piel))   // (04/11 g) 364 = teselacion del cuerpo -> 365; (04/11 h) 365 = angulo de las cejas (expresiones), 366 = borde del ojo -> 367; (04/11 i) 367 = color de las cejas del jugador -> 368; 368 = pestanas con hebras -> 369; (04/11 l) 369 = color de ceja a contraluz, 370 = hebras extra por jugador -> 371   // (04/11 m) 371 / 372 = teselacion por jugador -> 373
// (26/10) filas de t120 ocupadas por tablas que escribe la DLL en cada cuadro (no usarlas para el ini): 194..254 tonos de piel por
// jugador (skintone.cpp), 257 cuadro de la ropa contra el cuerpo (cuerpo.cpp). 300..304 son de las manos (manos.cpp + pes_hands.hlsli; 303 = manos relajadas, 304 = unas de los jugadores con textura propia, pes_nailfix.hlsli en el PS aa7d1833; v12.6).
// 260..263 son de la musculatura (musculo.cpp: grupos de la v7 y musculos vivos).
// 290..292 y 294 pasto tanda 6 (26/10 f: sombra del sol y color del suelo en las hojas del mod, hundimiento y desgaste), 295 tanda 7 (27/10 h: lineas pintadas en las hojas, contraste por hoja, puntas al sol), 296 sombra del cesped fria / honda en la composicion, 299 tanda 9a (27/10 l: hojas solo sobre la base dibujada, oclusion promediada y lineas nitidas a media distancia); config.cpp pone sus valores por defecto.
// 264..284 variantes robusto / atletico de TorsoBody (skintone.cpp las escribe en cada cuadro: 264 = (t general, tabla, prendido),
// 265 + i = t de la entrada i de la tabla de jugadores); 285 = (prendido, texturas, definicion) del ini (pes_variants.hlsli);
// 286.x = relieve de TorsoBody al derecho (el PS de la piel invierte el verde de sus normal maps de 4096); 287 = hombros del menu
// Fisico (x inclinacion, y tipo, z tamano; por jugador, musculo.cpp); 288.x = columna marcada (t19 del PS de la piel); 289.x = base
// esculpida (el relieve del normal map de TorsoBody en la malla: filas 7..9 de los datos de t127 + correccion de la textura en t20);
// 293 = piel real (x prendido, y brillo, z translucidez; zonas del torso en t21 / t24; w = union suave del brazo con el torso).
// 320 = torso bajo la camiseta ajustada (27/10, pes_skintone.hlsli PesTorsoInset: x mm, w on; y 0 = toda la piel bajo la camiseta con fundido, 1 = solo el torso, 2 = preciso: lo que mide 327.w en t124, x = si no hay mapa).
// 321 = brazos, manos y muslos de TorsoBody para las caras reales (27/10, brazos.cpp: x prendido, y muslos, z tono forzado, w log).
// 322 = cuello de las caras reales fundido con el torso (27/10, pes_collar.hlsli en 5ce3 / ce188: x prendido, y alto mm, z cuanto, w diagnostico).
// 323 = ocultar la camiseta interior (inner*, highneck de common_package.fpk) al mirar el cuerpo sin camiseta (27/10, hooks.cpp: x prendido, w log); (27/10 d) y hundir la base del cuello de la cara bajo el torso en mm (VS faab87ee / 455eb97b, pes_collar.hlsli), z alto del fundido mm.
// 312 = sol estable (la DLL lo escribe cada cuadro: config.cpp, PESSunStable). NO usarla para otra cosa.
// 326 = costura del hombro de la musculatura (27/10, aa7d1833: x prendido, y ancho m, z fuerza; antes 312, chocaba con el sol).
// 325 = nitidez de las hojas del mod (27/10, grass.cpp: x ancho minimo de la hoja en px, 0 = el de la calidad x61).
// 329 = cesped al sol, tanda 11 (27/10 n: x transpiracion = translucidez de la punta + rocio en 1 de 8 hojas (18af), y desgaste mas visible (PESWear), z duracion del rastro de la pelota en s (pitchwear.cpp, pes_flat.G; 0 = 30), w rastro visible en el color (VS 498b, 5e92, c956); w112 = verde natural, cromatica r/g 0.78 b/g 0.42).
// 330 = nitidez extra con la camara quieta (27/10 n, 0f5c CAS: x fuerza que se suma a x39 segun 160.z cuadros desde el reinicio del TAA).
// 331 = piel (x giro del disco SSSS, hilo de piel e376066).
// 332 = antialiasing temporal del juego (27/10 n, PS 1a7534929602c747 reemplazado: x alfa minimo hacia el cuadro actual, y ancho de la caja, z alfa extra por contraste, w on/off; con w = 0 identico al juego).
// 333 = densidad adaptativa del pasto por FPS (27/10 o, grass.cpp GrassPresent: x FPS objetivo (0 = apagado; Calidad x61 = 4 Ultra la ignora), y densidad minima 0.2..1; multiplica Densidad x58 en la emision).
// 334 = pasto de la camara mas cercana, tanda 12 (27/10 p, >= 250-600 px/m, hojas del mod y parcelas del juego pegadas a la camara: x rocio fino (18af), y cal mas densa / borde irregular (498b), z sombra por hoja (18af)).
// 335 = luz volumetrica (31/10, composicion 619b: x densidad del aire 0 = apagado, y distancia maxima m, z fase HG hacia el sol, w altura de la bruma m; w12 = 4 la muestra sola).
// 336 = SSGI calidad (31/10: x muestras cercanas 8..16, y limpieza 0..2 = filtro espacial que respeta bordes al subir de resolucion, z nitidez por normales 0..4, w saturacion del rebote 0..2).
// 337 = SSGI (31/10: x cuanto recibe la piel 0..2, y distancia maxima m (0 = 90), z resolucion completa (ssgi.cpp), w sombra del sol alineada por shader (PESShadowAlign en pes_common.hlsli: p' = R^T R^T p antes de m_shadowProjection; prueba sin DLL de sombra_parche 1)).
// 338 = traspaso de luz en los kits con el grosor real del shadow map (31/10, 5 PS del sol: x densidad 1/m, y grosor maximo cm, z resto del modo viejo, w on).
// 340 = expresiones faciales (31/10, expr.cpp + pes_expr.hlsli en los VS faab87ee / 455eb97b: x prendido, y ganancia general (1 = juego), z tope del agregado en cm, w cuanto se suma de los gestos grandes (mandibula abierta) 0..1).
// 342 = lengua de la boca 3D (01/11, pes_expr.hlsli PesTongue en faab87ee; expr.cpp marca la malla de la boca: x fuerza, y movimiento vivo, z velocidad; w parpadeo del mod 0..1, PesBlink con los parpados que expr.cpp encuentra por malla).
// 347 = abrir la boca (01/11, pes_expr.hlsli PesJawApply en faab87ee / 455eb97b): x apertura extra -0.25..1 (1 = boca abierta del todo, negativo = cierra un poco); y nariz con la boca 0..1; z labio de arriba (gesto extra 0..1.5); w mirada viva 0..2 (VS del ojo 69758ab9 gira el globo, PesGaze; el parpado acompana)
// 348 = musculos virtuales (01/11, pes_expr.hlsli PesMuscleApply): x mejilla que se infla al sonreir, y surco nasolabial, z ceno (entrecejo) 0..2; expr.cpp pasa comisuras y cejas internas en b12; w arrugas por compresion real (PS 5ce3ed87 / ce188ae1: jacobiano del gesto, TEXCOORD11 del VS) 0..2.
//       expr.cpp marca por malla los huesos de abajo (mandibula, labio de abajo, papada) y las comisuras (la mitad).
// 341 = expresiones por zona (31/10: x frente y cejas, y ojos y parpados, z mejillas y nariz, w boca y labios; 1 = la ganancia general sola).
// 339 = pelo a contraluz (31/10, 5 PS del sol; los PS del pelo 02f1d510 / c75f6047 marcan g_SpecularTexture.z = 252/255: x fuerza, y densidad 1/m, z grosor maximo cm, w on).
// 328 = matas y variedad de las hojas del mod (27/10 m, tanda 10: VS 498b / PS 18af: x matas, y variedad por hoja, z oclusion entre matas; grass.cpp: w capas por pixel cerca, 0 = 6).
// 324 = tono de piel del cuerpo por jugador (27/10, personajes.cpp: PES21FX\tonos_piel.txt; x prendido, y desplazamiento en HumanData, z bit, w volcado al log).
// 327 = cuerpo del tono de la cara (27/10 b, brazos.cpp + pes_tono_brazo_cs.hlsl: con x321, la textura de brazo de la cara real se mide contra arm_1..6 y torso / brazos / muslos van con torso_M / arm_M / thigh_M de ese tono; x prendido, y umbral de piel 0..1 (0 = 0.12); w (27/10 d) medicion camiseta contra torso al log cada 20 s, cuerpo.cpp + pes_body_measure_cs.hlsl).
// 297 / 298 = luz de prueba del editor (27/10 b, pes_ssss_inc.hlsli: sol de dia / tarde / noche donde no corre el sol del partido).
// 299 = pasto tanda 9a (hilo del pasto, rama kd63zh).
// 315 = rebote de luz acumulado v2 (27/10, pes_ssgi_ps.hlsl: x estimador fisico, y alcance lejano x R, z fuerza lejana).
// 331.x (ini) = giro del disco de la SSSS por cuadro (27/10 g, pes_ssss_inc.hlsli).
// 309 (ini) + 310, 316..319 (DLL) = sombra propia de la cabeza (27/10 c, headshadow.cpp + pes_headshadow.hlsli en los 7 shaders del sol).
// 305 musculos con textura (manos.cpp + pes_limbs.hlsli en el VS 1c02a92a + PS aa7d1833; x prendido, y definicion, z venas de los
// brazos, w venas de las manos con el puno apretado; v12.7).

struct Assign { int idx; int comp; std::vector<float> values; std::string var; };
struct KeyBind {
    std::string name, label;
    std::string labelEn;                   // (27/10) rotulo en ingles (PES21FX_menu_en.ini)
    int vk = 0;
    int ctrl = -1, alt = -1, shift = -1;   // -1 = no importa, 0 = no apretada, 1 = apretada
    enum Type { Activate, Toggle, Cycle, Hold } type = Activate;
    std::vector<Assign> assigns;
    std::string run;                       // comando especial (p.ej. reiniciar el reloj del sudor)
    bool active = false, wasDown = false;
    int cycleIdx = -1;
    std::vector<float> saved;
};

struct ParamInfo { std::string comment; bool defined = false; };

// grupo del menu ([Menu...] en PES21FX.ini): titulo, descripcion, interruptor y sliders de una mejora
struct MenuSlider {
    int idx, comp; std::string label; float lo, hi;
    std::string help, fmt = "%.2f";
    std::vector<std::string> options;      // si no esta vacio: selector (valor = indice de la opcion)
    std::vector<float> optVals;            // si no esta vacio: valor de cada opcion ("Etiqueta=valor")
    std::string ext;                       // "seccion.clave" de cloth-mod\cloth.ini (si no esta vacio no usa idx/comp)
    std::string labelEn, helpEn;           // (27/10) textos en ingles (PES21FX_menu_en.ini); vacios = se usa el espanol
    std::vector<std::string> optionsEn;
    float def = 0;                         // valor de fabrica si la clave no esta en cloth.ini
};
struct MenuButton { std::string action, label, help, labelEn, helpEn; };   // button = accion | etiqueta | ayuda  (acciones: hair_export, hair_reload)
struct MenuGroup {
    std::string title, desc, section;
    std::string id;                      // (27/10) nombre de la seccion del ini sin "Menu" (clave de PES21FX_menu_en.ini)
    std::string titleEn, descEn, sectionEn;   // (27/10) textos en ingles; vacios = se usa el espanol
    int tIdx = -1, tComp = 0;            // parametro del interruptor (w46, etc.); -1 = sin interruptor
    std::vector<MenuSlider> sliders;
    std::vector<MenuButton> buttons;
    std::string special;                 // (24/10) tarjeta dibujada por el codigo: "musculatura_jugador"
};

// ---------------------------------------------------------------- reglas por shader (lo que eran [ShaderOverride])
struct RuleAction {
    enum Kind { CopyCB, RefSRV, BindCB, BindSRV, SetParam, EventTurf, FaceGen, Expr, Beard } kind;   // FaceGen: slot = mapa A, idx = mapa B
    int slot = 0;
    std::string name;          // recurso con nombre, o "file:ruta.dds"
    int idx = 0, comp = 0;     // SetParam
    float value = 0; std::string var;
    UINT onlyIdx = 0, skipIdx = 0;   // (04/11) srv: solo / salvo el dibujo con esa cantidad de indices (boca propia de un jugador)
};
struct Rule { uint64_t hash = 0; uint64_t vsCond = 0; std::vector<RuleAction> actions; bool hasParamOverride = false; int paramSet = -1; };   // vsCond: [rule <ps> vs <vs>] (20/10)

// presets por estadio (presets\stadium_<id>.ini; el estadio lo informa Sider via match.ini)
struct PresetValue { int idx, comp; float value; };
struct PresetFile { std::string file, name, author; int stadium = -1, timeofday = -1; std::vector<PresetValue> values; };
struct MatchInfo { bool known = false; int stadium = -1, timeofday = -1, weather = -1, season = -1, tournament = -1; };

// --- pelo en hebras (18/10): tipos de pelo (PES21FX_hair.ini) y asignacion por cara ---
struct HairType {
    std::string name = "corto";
    float largo = 3.5f, rizo = 0.1f, rigidez = 0.8f, peso = 0.5f, grosor = 0.08f, densidad = 1.0f, brillo = 0.6f, flequillo = 0.0f;
    float raiz[3] = { -1, -1, -1 }, punta[3] = { -1, -1, -1 };   // -1 = del propio bsm
    float cejaGrosor = 1.0f, cejaDens = 1.0f, cejaElev = 1.0f, cejaLargo = 1.0f;
};
struct HairCfg { std::vector<HairType> types; std::map<std::string, std::string> faces; std::string defType = "corto"; bool loaded = false;
    std::map<std::string, std::vector<float>> browFace; };   // (04/11 i) id = cantidad, largo, mezcla de color, r, g, b   // (04/11) [cejas_cara] id = cantidad, largo (multiplicadores por jugador)

struct Config {
    std::wstring baseDir;                      // carpeta del mod (termina en '\')
    float base[kParamCount][4] = {};           // valores del ini
    float live[kParamCount][4] = {};           // valores actuales (teclas / menu)
    ParamInfo info[kParamCount][4];
    std::vector<KeyBind> keys;
    std::vector<MenuGroup> groups;
    // cloth-mod\cloth.ini editado desde el menu (el mod de ropa lo relee solo ~1 s despues de guardarlo)
    std::wstring clothPath; bool clothFound = false;
    std::map<std::string, float> clothVals, clothLoaded;
    bool clothDirty = false; double clothChangeT = 0; std::string clothStatus;
    MatchInfo match;                           // partido actual (match.ini)
    PresetFile presetActive;                   // preset aplicado (file vacio = ninguno)
    std::vector<PresetValue> presetKeys;       // claves que toco el preset (se restauran a base al salir)
    bool presetManual = false;                 // el usuario aplico/quito un preset a mano: no pisar hasta el proximo partido
    std::map<uint64_t, Rule> rules;
    std::map<std::pair<uint64_t, uint64_t>, Rule> pairRules;   // (20/10) [rule <ps> vs <vs>]: acciones solo cuando ese PS se dibuja con ese VS
    std::map<std::string, float> vars;         // $pes_push, etc.
    HairCfg hair;                              // PES21FX_hair.ini
    int menuKey = VK_INSERT;
    bool bypass = false;                       // ver el juego original
    // [PES21FX] para aislar problemas: replace_shaders / rules / overlay = 0 apagan cada parte
    bool tutorials = true;                     // tarjetas de tutorial al abrir cada pestana ([PES21FX] tutorials)
    std::string favorites;                     // 19/10: titulos de grupos favoritos separados por '|' ([PES21FX] favoritos)
    std::string menuSection;                   // (04/11 n) ultima pagina del menu ([PES21FX] menu_seccion: sec:<seccion>, fav, chg, rec, presets, tools, settings)
    float uiScale = 1.0f;                      // 19/10: tamano del menu ([PES21FX] ui_scale)
    bool compact = false;                      // 19/10: grupos contraidos por defecto ([PES21FX] compacto)
    int blockKeys = 1;                         // (31/10) teclas del juego con el menu: 0 nunca se bloquean, 1 al escribir en un campo, 2 con el menu abierto ([PES21FX] bloquear_teclado)
    int lang = 0;                              // (27/10) idioma del menu: 0 espanol, 1 ingles ([PES21FX] language = es / en)
    bool autoCap = false; float autoCapSec = 20.0f; int autoCapMax = 150; bool autoCapAB = false; bool exeDump = true; bool motorPatch = true; bool motorSunFix = true; int motorShadowFix = 1; int motorShadowRes = 0; float motorCascadeScale = 0.0f;   // [Motor]: parches en memoria (exepatch.cpp)   // exe_dump: volcado del exe desempaquetado (una vez por version)   // auto_captura_ab: pares A (todo) / B (un grupo del menu apagado 3 cuadros)
    int aniso = 16; float mipBias = 0.0f;   // 20/10: filtrado anisotropico forzado (1 = el del juego) y sesgo de mip global ([PES21FX] aniso, mip_bias)   // 19/10: captura automatica en partido ([PES21FX] auto_captura, auto_captura_seg, auto_captura_max)
    bool optReplace = true, optRules = true, optOverlay = true, optPin = false, optHooks = true, optDxgi = true, optPresent = true, optCtx = true, optExports = true, optOrphan = true, optDump = false;
    int memFix = 1; bool memWatch = true;     // (26/10) [PES21FX] memoria_arreglo (0/1 = retener, 2 = liberar entero) / memoria_vigilar (memwatch.cpp)
    bool grassScan = true, grassNoCull = true, grassDumpDebugP = true;   // (26/10) [Motor] pasto_buscar / pasto_sin_recorte / volcar_debugp: distancia del pasto 3D del juego (grassdist.cpp)
    bool lightTrace = true;                   // (27/10) [PES21FX] luz_rastrear: pila de quien llena el cbuffer de la luz del sol (hooks.cpp)
    bool meterOn = true;                      // (07/11) [PES21FX] medidor: VRAM por dueno y ms de GPU por efecto (gpumeter.cpp)
    bool sunWatch = true;                     // (26/10) [PES21FX] sol_vigilar: quien escribe el sol de las repeticiones (sunwatch.cpp)
};
extern Config g_cfg;
extern std::mutex g_cfgMutex;

bool LoadConfig();                              // PES21FX.ini + PES21FX_rules.ini
bool SaveConfigValues();
bool SaveOption(const std::string& key, const std::string& value);   // escribe/agrega clave en [PES21FX]
// (27/10) idioma del menu (lang.cpp): TRS devuelve la traduccion de un texto fijo del codigo (UTF-8) si el menu esta en ingles
// y existe; si no, el mismo puntero. LS elige entre el texto del ini y su version en ingles.
const char* TRS(const char* es);
inline const std::string& LS(const std::string& es, const std::string& en) { return (g_cfg.lang != 0 && !en.empty()) ? en : es; }
void LoadMenuLang();   // lee PES21FX_menu_en.ini sobre g_cfg.groups / g_cfg.keys (con g_cfgMutex tomado)
bool LoadCloth();
bool SaveCloth();                        // escribe los valores live en PES21FX.ini
void PollKeys(bool focused);
void KeyActionUnlocked(KeyBind& k, int action);    // menu: alternar / ciclar (con g_cfgMutex tomado)
void UpdateTimeParams();                        // reloj del partido, viento, etc.
void OnTurfDrawn();
void UpdateClothWet(double t);
void UpdateClothFit(double t);
double Now();
// presets por estadio (presets.cpp)
bool ReadPreset(const std::wstring& path, PresetFile& out);
bool ApplyPresetFile(const std::string& file);
void ClearPreset();
void ScanPresets(std::vector<PresetFile>& out);
void MatchWind(const MatchInfo& m, unsigned long long seed);   // (02/11) viento por partido (fila 353) -> cloth.ini, con g_cfgMutex tomado
void PollMatch();                               // cada ~1 s: match.ini -> preset del estadio
bool SavePresetForMatch(const std::string& name, const std::string& author, bool onlyThisTime, std::string& outFile);
bool DeletePresetFile(const std::string& file);

// ---------------------------------------------------------------- d3d
bool InstallHooks();
void OnDeviceCreated(ID3D11Device* d);
void ReloadShaders();
void WatchShaderFolder();                      // recarga sola los shaders que cambian en disco
std::wstring ShaderPath(uint64_t hash, const char* type);   // shaders\<hash>-ps_replace.txt

// un shader creado por el juego (tabla en hooks.cpp)
struct Rule;
struct ShaderEntry {
    uint64_t hash = 0;
    char type = 0;                       // 'p' 'v' 'c'
    ID3D11DeviceChild* repl = nullptr;   // nuestro shader (o null)
    const Rule* rule = nullptr;
    const std::vector<const Rule*>* pairRules = nullptr;  // (20/10) reglas de este PS que solo valen con cierto VS (25/10 d: lista inmutable;
                                                          // al recargar se cambia el puntero y la vieja no se libera, porque un dibujo puede estar recorriendola)
    ID3D11Device* dev = nullptr;         // dispositivo que lo creo
    std::shared_ptr<std::vector<uint8_t>> code;   // bytecode original (para el catalogo: reflexion)
    bool clothSim = false;               // (25/10) simulador del mod de ropa (cloth_sim.hlsl): se le enlazan t120 / t121
};
std::vector<const ShaderEntry*> AllShaders();

// catalogo de shaders (catalog.cpp)
extern std::atomic<bool> g_catalogActive, g_catalogHook;
extern std::string g_catalogStatus;
void CatalogStart();
bool CatalogStop(UINT bbW, UINT bbH, std::vector<const ShaderEntry*> all);
void CatalogDraw(ID3D11DeviceContext* c, const ShaderEntry* vs, const ShaderEntry* ps, UINT verts, int kind, UINT inst);
void CatalogCS(const ShaderEntry* cs);
void CatalogPresent();
void CatalogDumpCBs();
extern std::atomic<bool> g_grassProbeOn;
void CatalogProbeGrass();
extern std::atomic<bool> g_recordOn;
void CatalogRecord(bool on);                     // "Grabar partido": volcados periodicos numerados en catalog\rec                        // "Volcar pasto": mide las parcelas de pasto de un cuadro
// pasto extendido (grass.cpp)
void GrassOnDraw(ID3D11DeviceContext* c, const ShaderEntry* vs, const ShaderEntry* ps, UINT count, UINT start, INT base); bool GrassSkipGameDraw(); void GrassClearSkip();   // (2/11) x178: omitir el dibujo del pasto del juego
void GrassOnBaseDraw(ID3D11DeviceContext* c);    // (25/10) v3: al terminar la base de la cancha (PS 5e92 / c956)
void GrassDiagState(ID3D11DeviceContext* c, uint64_t psHash);   // (26/10 f) log del stencil de las pasadas del sol y de la base de la cancha
// (26/10 g) pasto del juego en el G-buffer con los estados de las hojas del mod (w290): se ponen antes del dibujo y se devuelven despues
struct GrassStScope { bool on = false, b12 = false; ID3D11DepthStencilState* dss = nullptr; UINT ref = 0; ID3D11BlendState* bs = nullptr; float bf[4] = {}; UINT bm = 0xffffffff; ID3D11Buffer* vsB12 = nullptr; };
void GrassGameStates(ID3D11DeviceContext* c, GrassStScope& s);
void GrassGameStatesRestore(ID3D11DeviceContext* c, GrassStScope& s);
// (26/10 g) emision del pasto del mod despues del ULTIMO dibujo de la base de la cancha (w294): pendiente por contexto hasta el siguiente
// dibujo que no sea de la base, un cambio de targets, limpieza de la profundidad, ClearState o el fin / la ejecucion de la lista
extern std::atomic<int> g_grassPendN;
void GrassBaseFlush(ID3D11DeviceContext* c, int why, uint64_t psHash, UINT nRtv = 0, ID3D11RenderTargetView* const* rtv = nullptr, ID3D11DepthStencilView* dsv = nullptr);
void GrassPresent();                             // log (despues de UploadParams)
void GrassFrameBoundary(UINT presentFlags);      // (26/10 d) en hk_Present ANTES de UploadParams: reclamo del cuadro en la GPU, x63
void GrassOnFinishCL(ID3D11DeviceContext* c, ID3D11CommandList* l);    // (26/10 d) una emision por grabacion: el contexto arranca de cero
void GrassOnExecuteCL(ID3D11DeviceContext* c, ID3D11CommandList* l);   // (26/10 d) demora grabacion -> ejecucion (log); listas anidadas
// (25/10) shaders de un dibujo tal como los ve la DLL: entradas (para BeforeDraw) + objetos puestos (hooks.cpp)
struct ShaderBind { const ShaderEntry* vs = nullptr; ID3D11VertexShader* vsObj = nullptr; const ShaderEntry* ps = nullptr; ID3D11PixelShader* psObj = nullptr; };
ShaderBind BindShadersRaw(ID3D11DeviceContext* c, const ShaderBind& b);
extern thread_local bool t_sparseGrass;
extern int g_grassPatchesLastFrame;
struct GrassDbg { int calls = 0, emits = 0, patches = 0, sparse = 0, sparseLast = 0, lost = 0; int ring[3] = {0, 0, 0}; UINT lod = 0, rtW = 0, cb2off = 0; bool cam = false; float ex = 0, ez = 0; const char* why = nullptr; };
extern GrassDbg g_grassDbg;
extern UINT g_bbW, g_bbH;                       // tamano del back buffer (lo fija Present)
void CatalogGrassDraw(ID3D11DeviceContext* c, const ShaderEntry* vs, UINT count, UINT start, INT base);                           // proximo cuadro: volcar los cbuffers de composicion y luz del sol

struct DeviceState {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* imm = nullptr;
    ID3D11Texture1D* paramTex = nullptr;
    ID3D11ShaderResourceView* paramSRV = nullptr;
    ID3D11Texture1D* grassTex = nullptr;       // parametros con la marca de parcela lejana (w60 = 1)
    ID3D11ShaderResourceView* grassSRV = nullptr;
    std::vector<ID3D11Texture1D*> altTex;      // conjuntos de parametros con cambios por shader (p.ej. w5 = $pes_push)
    std::vector<ID3D11ShaderResourceView*> altSRV;
};
extern DeviceState g_dev;

// ---------------------------------------------------------------- overlay
void OverlayPresent(IDXGISwapChain* sc);
// quinta textura de la cara generada en vivo (facegen.cpp)
struct FaceGenExtra { ID3D11ShaderResourceView* seedMap = nullptr; ID3D11ShaderResourceView* browMap = nullptr; ID3D11UnorderedAccessView* seedsUAV = nullptr; ID3D11ShaderResourceView* seedsSRV = nullptr; ID3D11Buffer* args = nullptr; ID3D11UnorderedAccessView* argsUAV = nullptr; ID3D11Buffer* hairCB = nullptr; int rtCount = 0;
    uint64_t browVS = 0; UINT browCount = 0, browStart = 0; INT browBase = 0;   // (04/11) hebras v2: VS del dibujo de la cara y sus argumentos (0 = sin hebras)
    ID3D11Resource* key = nullptr; ID3D11Resource* t0 = nullptr; std::string id; };   // (20/10) identidad de la cara para el LOD: textura clave (t1), t0 y hash (punteros sin retener)
extern std::atomic<int> g_depthClearZero, g_depthClearOne;
bool FaceGenLookup(ID3D11DeviceContext* c, ID3D11ShaderResourceView** outA, ID3D11ShaderResourceView** outB, FaceGenExtra* ex);
// (04/11) cejas con hebras v2: repetir el dibujo de la cara con el GS de hebras (facegen.cpp; el dibujo lo hace hooks.cpp)
bool BrowWanted(const FaceGenExtra& ex, uint64_t vsHash); bool LashWanted(uint64_t vsHash, uint64_t psHash); bool BrowShadowOn(ID3D11DeviceContext* c); void BrowShadowOff(ID3D11DeviceContext* c); bool BrowStrandsBegin(ID3D11DeviceContext* c, const FaceGenExtra* ex, uint64_t vsHash); void BrowStrandsEnd(ID3D11DeviceContext* c);
void FaceGenPresent(); void FaceGenClear(); void FaceGenReloadShaders(); int FaceGenCount();
// (04/11) caras en alta desde PES21FX\caras_hd (caras_hd.cpp): volcado de originales, texturas grandes para t0..t3 de la piel
void CarasHdNewFace(ID3D11DeviceContext* c, const std::string& id, ID3D11ShaderResourceView* const in[4]);
bool CarasHdGet(const std::string& id, ID3D11ShaderResourceView* out[4]); void CarasHdPresent();
void CarasHdHair(ID3D11DeviceContext* c, uint64_t psHash, ID3D11ShaderResourceView* out[4]); void CarasHdClearKeys();   // (04/11) pelo y barba: texturas grandes por slot
std::string HashFaceTexture(ID3D11DeviceContext* c, ID3D11Resource* res);   // facegen.cpp: huella de una textura (mip <= 64 px)
// inspector visual (inspect.cpp)
extern std::atomic<bool> g_inspectOn; extern std::atomic<int> g_inspectSel;
extern std::atomic<bool> g_autoDumpAny; void InspectAutoDump(ID3D11DeviceContext* c, const ShaderEntry* ps);   // volcado automatico de texturas (clases con volcar = 1)
bool InspectBegin(ID3D11DeviceContext* c, const ShaderEntry* ps, const ShaderEntry* vs, int kind, UINT verts, UINT inst);
void InspectEnd(ID3D11DeviceContext* c); void InspectPresent(); void InspectDrawOverlay(); void InspectReload();
// capas editables (ventana "Capas" del inspector; PES21FX_capas.ini): por hash de pixel shader, textura por slot y "oculta"
struct LayerOv { std::map<int, std::string> slot; bool hidden = false; bool highlight = false; };   // highlight: dibujar con PS magenta plano   // slot -> "file:ruta" | "none"
extern std::atomic<bool> g_layersAny;   // hay alguna capa definida (evita el costo cuando no se usa)
bool InspectLayerFor(uint64_t ps, LayerOv& out);
bool InspectShouldSkip(uint64_t ps, UINT verts);
uint64_t InspectFingerprint(ID3D11DeviceContext* c, const ShaderEntry* vs);
uint64_t InspectAliasFor(uint64_t fingerprint, uint64_t vsHash, UINT verts);
const ShaderEntry* FindShaderByHash(uint64_t h);
ID3D11PixelShader* InspectHighlightPS(uint64_t ps);   // PS magenta si la capa esta en "resaltar", si no null   // capa oculta o "solo esta" activo
extern std::atomic<int> g_layerSkips;   // true si hay capas definidas para ese shader (copia)
ID3D11ShaderResourceView* FileTextureAny(const std::string& rel);   // hooks.cpp: "file:..." dds o png (cache)
void EnsureComInit();
bool NamedCBRead(const std::string& name, std::vector<float>& out, int* frameOut = nullptr);   // frameOut: cuadro de la copia leida   // hooks.cpp (Present)
int NamedAge(const std::string& name);                                // hooks.cpp: cuadros desde la ultima copia (-1 = nunca)
const Rule* RuleForHash(uint64_t h);
extern std::atomic<int> g_drawOrder; extern std::atomic<int> g_aliasDraws;
std::string NamedOrigin(ID3D11ShaderResourceView* v);     // hooks.cpp: "archivo: textures\x.dds" o recurso con nombre
std::string FaceGenOrigin(ID3D11ShaderResourceView* v);   // facegen.cpp: "generada: quinta textura A" etc.
extern std::atomic<int> g_stFaceDraws, g_stFaceRule, g_stFaceT23, g_stFaceT26, g_stFaceCtxDef;
void FaceGenHairChanged();                     // PES21FX_hair.ini recargado: refrescar los cbuffers por cara
int FaceGenExportHair(std::string& status);
std::string BrowSaveCurrent();                 // (04/11) guarda cantidad / largo de cejas (z77 / w77) del jugador solo en pantalla en [cejas_cara]   // vuelca hair\\auto\\<id>.dds de cada cara generada (plantillas para editar)
bool LoadHairConfig();                         // PES21FX_hair.ini
const HairType* HairTypeFor(const std::string& id, std::string* outName);
ID3DBlob* CompileNamedShader(const wchar_t* file, const char* profile);   // shaders\<file> con #include
void NamedSetSRV(const std::string& name, ID3D11ShaderResourceView* v);
std::string DescribeInputLayout(ID3D11InputLayout* il);   // elementos de un input layout anotado al crearse   // publica un SRV como recurso con nombre (srv = N nombre en las reglas)
extern std::atomic<int> g_matchGen;                                        // sube en cada partido nuevo (reloj del cesped)
void PitchWearNotePlayer(ID3D11DeviceContext* c); void PitchWearPresent(); void PitchWearReloadShaders();   // desgaste de la cancha en vivo
void PitchClipsDraw(ID3D11DeviceContext* c, int frame);
void PitchBallNote(ID3D11DeviceContext* c, int frame); ID3D11Buffer* PitchBallDrawNote(ID3D11DeviceContext* c, UINT verts); bool PitchClipDrawActive();   // (2/11) dibujo de los recortes en curso (atlas pes_debris en t8/t9)   // (2/11) dibujo de la pelota (aadffecf): b12 con las manchas o nullptr   // (20/10) capsula de la pelota del VS del pasto -> pasto aplastado   // recortes de pasto volando (desde el dibujo del pasto)
// (20/10) LOD de caras (lod.cpp): diagnostico (x118) y LOD0 forzado por sustitucion de geometria (y118)
struct LodIA {
    ID3D11Buffer* vb[4] = {}; UINT stride[4] = {}, offset[4] = {};
    ID3D11Buffer* ib = nullptr; DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN; UINT ibOff = 0;
    ID3D11InputLayout* il = nullptr; D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11VertexShader* vs = nullptr; uint64_t vsHash = 0, psHash = 0;
    UINT count = 0, start = 0; INT base = 0; UINT cb[8] = {};
    void Release(); void AddRefAll() const;
};
extern std::atomic<bool> g_lodAny; extern std::atomic<int> g_lodSubs, g_lodFaces;
bool LodNoteFace(ID3D11DeviceContext* c, ID3D11Resource* key, ID3D11Resource* t0, const std::string& id, uint64_t vsHash, uint64_t psHash, UINT count, UINT start, INT base, LodIA* out);
bool LodNoteAnyDraw(ID3D11DeviceContext* c, uint64_t vsHash, uint64_t psHash, UINT count, UINT start, INT base, LodIA* out);
// (04/11) bocajug.cpp: boca propia por jugador (vertices del perfil de caras.ini en el dibujo de la boca de su cara)
bool BocaJugDraw(ID3D11DeviceContext* c, uint64_t face, UINT count, UINT start, LodIA& L, ID3D11ShaderResourceView** bsm); void BocaJugLog(); void BocaJugReset();
void LodBind(ID3D11DeviceContext* c, const LodIA& L, LodIA& sv); void LodRestore(ID3D11DeviceContext* c, LodIA& sv, const LodIA& L); void LodPresent();
extern float g_eyePos[4]; extern bool g_eyeValid;
extern thread_local LodIA t_grassSub;   // (20/10) pasto v4: malla propia para el dibujo de pasto en curso (la llena GrassOnDraw, la consume hk_DrawIdx)
void GrassDenseReset();
void ExeDumpPresent();
void ExePatchInstall();
void ShadowCasterLogPresent();   // (02/11) hooks.cpp: b2 de la pasada de sombras (cascada cercana) al log
void ExePatchPresent();   // (20/10) LOD de modelos (x142) cada segundo
void SolRelojPresent();   // (02/11) registro de hora del dia / sol / sunLux del motor, solo lectura (solreloj.cpp)
void GrassDistPresent();  // (26/10) busca el recorte de distancia del pasto 3D del juego ya descifrado (grassdist.cpp)
// (23/10) camara del motor (camhook.cpp): cortes de camara del juego (CameraSystem 0x14198d3b0) y reinicios del TAA del juego
int PresentNo();                                  // numero de cuadro (sube en cada Present)
void CamHookPresent();                            // ganchos (primer Present), 160 = cuadros desde el corte / cortes / TAA / ok
// (31/10) camara libre (freecam.cpp): pisa el parametro de la camara del motor desde el gancho de camhook.cpp; 344 (ini) / 345 (DLL)
void FreeCamApply(unsigned char* param);
void FreeCamPresent();
void FreeCamWheel(int delta);
bool FreeCamWantsHead();   // (1/11) camara libre prendida con "seguir la cara": hace falta la cabeza (expr.cpp)   // (1/11) rueda del mouse (WM_MOUSEWHEEL / raw input) -> zoom   // (1/11) gancho a la camara del visor del editor (primer Present) + log
bool FreeCamActive();
bool FreeCamBlocksKeys();                         // x344 = 1: el juego no ve el teclado mientras la camara libre esta prendida
// (31/10) camara lenta (tiempo.cpp): gancho al reloj de juego del motor (0x14035dcb0), 343 (ini: x velocidad, y prendida; DLL: z, w)
void TiempoPresent();
bool TiempoHooked();
float TiempoFactor();   // (1/11) velocidad pedida por el menu (1 = apagada) para el aviso en pantalla
bool CamCutSince(int& seen);                      // true una vez por cada corte nuevo (cada consumidor guarda su 'seen'); x161 = 1
int CamLastCutFrame();
bool EngineCamera(float pos[3], float tgt[3], int* frame);   // ultima camara aplicada por el motor (sin atraso)
ID3D11ShaderResourceView* MuscleClothSRV();     // (25/10) zonas de la musculatura para la fisica de la ropa (t121 del simulador)
ID3D11ShaderResourceView* MuscleEffortSRV();    // (02/11) esfuerzo por jugador para las venas (t123 del VS de la piel)
// (25/10) la ropa choca con el cuerpo real (cuerpo.cpp): piel del cuerpo deformada por personaje para el simulador (t122..t124)
void BodyOnDraw(ID3D11DeviceContext* c, UINT count, UINT start, INT base, int kind = 0);   // en cada dibujo de la piel del cuerpo (VS 1c02a92a); kind 1 = camiseta (medicion, 327.w)
ID3D11ShaderResourceView* TorsoNeedSRV();   // (31/10) mapa "torso bajo la camiseta" preciso (t124 del VS de la piel y de su sombra)
void TorsoShirtNote(ID3D11DeviceContext* c, int kind);   // (31/10 b) dibujo de la camiseta: origen del personaje para "camiseta por jugador" (fila 320)
void BodyPresent();                                // registro de mallas nuevas, lote del cuadro (personajes + piel), log
void BodyClothSRVs(ID3D11ShaderResourceView* out[3]);   // tablas, vertices, listas (null si no esta listo)
void BodyOnFinish(ID3D11DeviceContext* c, ID3D11CommandList* l);    // (25/10 e) FinishCommandList: los dibujos del contexto pasan a la lista
void BodyOnExecute(ID3D11DeviceContext* c, ID3D11CommandList* l);   // (25/10 e) ExecuteCommandList: cuentan para el lote del cuadro
extern std::atomic<bool> g_bodyDiagArmed;                            // (25/10 e) diagnostico: copiar las instancias del simulador de la ropa
void BodyNoteClothDispatch(ID3D11DeviceContext* c, UINT groups);    // en el Dispatch del simulador (contexto con su CS puesto)
void BodySimSeen();                                // el simulador del mod de ropa se uso en este cuadro
void BodyReloadShaders();
struct InputElemInfo { std::string sem; UINT idx, fmt, slot, off; };
bool InputLayoutInfo(ID3D11InputLayout* il, std::vector<InputElemInfo>& out);
// (04/11 c) barba que sigue los huesos de la cara (regla "beard = <slot>" en el VS del pelo 2f1af485): cbuffer de la malla + paleta de las pieles (t116) + enlace por vertice (t117)
bool BeardForDraw(ID3D11DeviceContext* c, UINT count, UINT start, INT base, ID3D11Buffer** cb, ID3D11ShaderResourceView** pool, ID3D11ShaderResourceView** bind);
ID3D11Buffer* ExprBufferForDraw(ID3D11DeviceContext* c, UINT count, UINT start, INT base, const float** headCentroid = nullptr, int* headIdx = nullptr, ID3D11ShaderResourceView** shapeSrv = nullptr, ID3D11ShaderResourceView** shapeWr = nullptr, ID3D11ShaderResourceView** tessSeam = nullptr, bool body = false);   // (04/11 g) body: piel del cuerpo (VS 1c02a92a), solo pesos de borde   // (04/11 b) tessSeam: pesos de borde de la malla (t119 del VS) solo si es piel de la cara teselable (facetess.cpp)
// (04/11 b) teselacion de la cara en primeros planos (facetess.cpp): HS + DS detras del VS de la cabeza / cara, por dibujo
struct FaceTessScope { bool on = false, relief = false; D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; int v = -1; UINT cnt = 0, start = 0; INT base = 0; };   // (04/11 piel) v / cnt / start / base: para repetir la cara teselada en el atlas de las cabezas
ID3D11DomainShader* FaceTessAtlasDS(int v);   // (04/11 piel) DS de la cara teselada para el atlas de las cabezas (v 0 cabeza, 1 cara animada)
bool HeadShadowClearedThisFrame(); ID3D11DepthStencilState* HeadShadowAtlasDSS(); ID3D11RasterizerState* HeadShadowAtlasRS(ID3D11RasterizerState* cur); void HeadShadowTessNote(int what);   // (04/11 piel) cara teselada en el atlas
bool FaceTessBegin(ID3D11DeviceContext* c, uint64_t vsHash, ID3D11ShaderResourceView* seam, ID3D11ShaderResourceView* params, FaceTessScope& s, const std::string& faceId = std::string());   // (04/11 m) faceId = cara del dibujo (teselacion por jugador)
void FaceTessSetSaved(const std::map<std::string, std::vector<float>>& m);   // (04/11 m) [teselacion_cara] de PES21FX_hair.ini
std::string FaceTessSaveCurrent();   // (04/11 m) boton tess_save
void FaceTessEnd(ID3D11DeviceContext* c, FaceTessScope& s);
void FaceTessReloadShaders(); void FaceTessPresent(); void FaceTessNote(int why);   // why: 0 sin pesos de borde, 1 sin t120, 2 apagada   // shapeWr: arrugas de la sonrisa esculpidas (PS t31)   // (04/11) shapeSrv: formas esculpidas de la malla (VS t118)
void ExprCaptureHead(ID3D11DeviceContext* c, const float* hc, int head);   // (1/11) copia la matriz de sk_head del dibujo en curso (b6)
bool ExprHeadWorld(float out[3], double* age);                            // (1/11) ultima posicion de la cabeza en el mundo   // (31/10) expresiones: cbuffer de la malla de cara (cabeza en la paleta) para el VS
void ExprPresent();                                // (31/10) expresiones: lectura de las mallas de cara nuevas, log
void ExprBonesFrame(const void* skel);             // (01/11) exprbones.cpp: en el Eval del cuello (musculo.cpp), gira la mandibula del esqueleto (350)
void ExprBonesPresent();                           // (01/11) exprbones.cpp: estado para el shader (350.y) y resumen   // hooks.cpp: elementos anotados al crearse el layout
void MuscleHookPresent();                         // (23/10) musculatura (musculo.cpp): huesos de musculo del juego x 164..171 (v5: 32 clases)
void MuscleDrawPlayerPanel();                     // (24/10) tarjeta "Musculatura por jugador" (ImGui; la llama DrawGroup con g_cfgMutex tomado)
bool MusclePhysiqueActive();                      // (24/10) el menu Fisico del juego (Editar > Jugadores) esta en pantalla
double MusclePhysiqueAge();                       // (24/10) segundos desde el ultimo cuadro con el menu Fisico activo (1e9 = nunca)
double MusclePhysiqueSince();                     // segundos desde que se entro al menu Fisico
std::string MuscleLatestPlayerName();             // nombre del ultimo jugador armado ("" si no hay)
float MuscleProfileGet(uint32_t id, int idx, int comp);                          // (24/10) valor propio del jugador o el general
void MuscleProfileSet(uint32_t id, const char* name, int idx, int comp, float v); // crea el perfil si hace falta; se guarda solo
int MusclePresetCount(); const char* MusclePresetName(int k); const char* MusclePresetDesc(int k);   // (26/10) tipos de cuerpo
void MusclePresetApply(uint32_t id, const char* name, int k);   // id 0 = generales (con g_cfgMutex tomado)
int MusclePresetMatch(uint32_t id);                             // -1 = valores a mano
float MuscleDefinitionFor(uint32_t id);                         // (26/10 b) definicion propia del jugador (0 = la general)
void MuscleCompare(bool on); bool MuscleCompareOn();              // (26/10 c) "Ver: como el juego" (sin la musculatura mientras se edita)
void FisicoCapturePresent(ID3D11Texture2D* frame);              // (26/10 c) captura de las paginas del menu Fisico (fisico.cpp)
void FisicoHighlightRequest(int idx, int comp);                 // (26/10 c) la ventana del mod pide que lata un musculo en el modelo
void FisicoCfgDirty();                                          // (26/10 c) guardar PES21FX.ini en un rato (opciones cambiadas desde el Fisico)
const char* FisicoEditName();                                   // (26/10 c) nombre del jugador que se edita en el Fisico
void MuscleDrawFisicoWindow(::ImFont* bold, ::ImFont* big);     // (26/10 c) ventana del mod en el menu Fisico (musculo_ui.cpp; con g_cfgMutex tomado)
void FisicoInstall();                             // (24/10) filas de musculatura dentro del menu Fisico del juego (fisico.cpp)
bool FisicoEditing(uint32_t* id);                 // el menu Fisico del juego esta armado para un jugador (su id)
void FisicoHighlightPresent();                    // (24/10) resaltado del musculo de la fila elegida (x174..182 para los shaders)
extern std::atomic<bool> g_skinDiagArmed;         // (24/10) skintone.cpp: el proximo cuadro se anotan los dibujos de personajes
void SkinDiagDraw(ID3D11DeviceContext* c, uint64_t ps, uint64_t vs, UINT count);
void SkinDiagPresent();
extern std::atomic<bool> g_toneChkArmed;          // (24/10 c) skintone.cpp: comprobacion del punto de los dibujos de piel (cada 30 s)
void SkinToneCheckDraw(ID3D11DeviceContext* c, uint64_t ps, uint64_t vs);
constexpr int kSkinTones = 30;                    // (24/10) tonos de piel propios (skintone.cpp); los 6 del juego son 3, 8, ..., 28
bool SkinToneGain(int gameIdx, int tono, int matiz, float out[3]);   // ganancia lineal (tono elegido / color del juego)
void SkinToneSwatch(int tono, unsigned char rgb[3]);
struct SkinToneProf { int tono = 0, matiz = 0, luz = 0, sat = 0, juego = 0, igualar = 1, zona = 0; uint32_t rgb = 0, cuerpo = 0, cara = 0; };   // (24/10 e) perfil de piel de un jugador; (24/10 g) zona: 0 cara y cuerpo, 1 solo el cuerpo, 2 solo el torso
bool SkinToneGetProf(uint32_t id, SkinToneProf& out);   // true si tiene algo propio
void SkinToneSetProf(uint32_t id, const char* name, const SkinToneProf& q);
bool SkinToneGet(uint32_t id, int* tono, int* matiz, uint32_t* rgb = nullptr);   // rgb: color propio 0xRRGGBB (0 = ninguno)
void SkinToneSet(uint32_t id, const char* name, int tono, int matiz, uint32_t rgb = 0);
extern std::atomic<bool> g_skinMeasure;             // (24/10 e) medir el color del cuello del cuerpo y de la cara (menu Fisico)
void SkinMeasureReport(const uint32_t* body, const uint32_t* face, uint32_t yMinInv, uint32_t yMax, uint32_t faceN);
bool CharNodeWorldPos(void* node, float out[3]);   // (26/10) posicion en el mundo de un SceneCharacterNode (skintone.cpp)
bool CharNodeToWorld(void* node, const float p[3], float out[3], bool point);   // (02/11) modelo -> mundo con la matriz del nodo (skintone.cpp)
bool ExprFaceFrame(const float* camPos, const float* camFwd, const void** lock, float c[3], float fw[3], float up[3], double* age);   // (02/11) cara en el mundo desde el esqueleto (exprbones.cpp)
void SkinTonePresent();                           // guardado diferido + tabla de jugadores con tono propio (194..254)
bool SkinToneParams(int gameIdx, const SkinToneProf& q, float k, float out[3], float outBody[3]);   // (24/10 c) (kL, m0, m1) en Oklab para pes_skintone.hlsli
int SkinToneGame(uint32_t id);                    // color de piel del juego guardado para ese jugador (0 = no se sabe)
int FisicoSkinIndex();                            // color de piel del juego (1..6) del jugador del menu Fisico (0 = no se sabe)
void SunWatchFrame(const float* w, float devSky, int frame);
void SunWatchRequestArm(uintptr_t a);   // (27/10) vigilar escrituras en el origen del cbuffer de la luz (sunwatch.cpp)   // (26/10) sol de la luz en mundo por cuadro (sunwatch.cpp)
void CodeWatchStart();                            // (24/10) vigila la seccion .text del exe: anota en el log lo que cambie (codewatch.cpp)
bool CodeWatchOrig(uintptr_t a, unsigned char* out, size_t n);   // bytes de la copia (registro de cierres)
void MemWatchStart();                             // (26/10) vigila el liberador de memoria del juego y VirtualProtect/VirtualFree (memwatch.cpp)
void MemCrashInfo(FILE* f, EXCEPTION_POINTERS* ep);   // (26/10) informe de memoria en un cierre (con el log tomado)
FILE* LogLock();                                  // (26/10) el archivo del log con el mutex tomado (soltar con LogUnlock)
void LogUnlock();
// (24/10) ganchos que se pueden apagar para buscar un cierre (x173 personajes, y173 menu Fisico, z173 vigilancia del codigo,
// w173 huesos de musculatura); se leen al arrancar
inline bool DiagOn(int comp) { return g_cfg.live[173][comp] > 0.5f; }
// (24/10) personajes del motor (personajes.cpp): que jugador es cada esqueleto (HumanList -> handle -> SceneCharacterNode)
constexpr int kCharSlots = 64;
struct CharInfo {
    bool known = false;                           // se sabe que jugador es (paso por HumanList)
    uint32_t id = 0, rawId = 0;                   // id del jugador (& 0x800bffff, el de face\real\<id>) y como viene en la clave
    uint64_t key = 0; int type = -1;              // clave de la base de datos del juego y tipo de personaje
    char name[64] = {};                           // nombre (UTF-8) o vacio
    void* node = nullptr; void* skel = nullptr;   // SceneCharacterNode y su esqueleto principal (+0x88 = restriccion + 0x10)
    uint64_t handle = 0; unsigned seq = 0; double t = 0;   // handle del nodo, orden de armado, hora
};
void CharTrackInstall(); void CharTrackPresent(); bool CharTrackOk();
int CharSlotForSkel(const void* skel);            // lugar del personaje de ese esqueleto (-1 = no se sabe); sin candado (Eval)
bool CharPlayerName(uint64_t key, char* out, size_t n);   // nombre del jugador de esa clave (base de datos del juego)
bool CharGet(int slot, CharInfo& out);
int CharSnapshot(CharInfo out[kCharSlots], bool alive[kCharSlots]);   // copia de la tabla (devuelve cuantos vivos)
unsigned CharTableGen();                          // sube cada vez que cambia la tabla
bool CamCutPeek(int seen);                        // hay un corte nuevo para este consumidor (sin consumirlo)
void ZeroBuffer(ID3D11DeviceContext* c, ID3D11Buffer* b);   // pone en cero un buffer DEFAULT (cbuffer "anterior" -> sin historia)
extern std::atomic<int> g_cutUse[3];              // cortes usados: 0 SSGI, 1 GTAO, 2 medidor (exposicion)
bool AsyncShot(ID3D11Texture2D* frame, const std::wstring& path); void AsyncShotPresent();   // (20/10) capturas JPEG sin frenar el juego (asyncshot.cpp)   // (20/10) volcado de las secciones del exe desempaquetado en memoria (exedump.cpp)
// (20/10) medidor de luz y luz ambiente calibrada (lightmeter.cpp): 125 = salida (x ganancia ambiente), 126 = controles
extern std::atomic<bool> g_meterHook;
ID3D11ShaderResourceView* GTAOBeforeComposition(ID3D11DeviceContext* c, int frame); ID3D11ShaderResourceView* GTAONeutralSRV(); void GTAOReloadShaders(); extern std::atomic<int> g_gtaoDraws;   // (20/10) GTAO (gtao.cpp) -> t27
ID3D11ShaderResourceView* SSGIBeforeComposition(ID3D11DeviceContext* c, int frame); void SSGIReloadShaders(); void SSGIPresent(int presentNo);   // (21/10) SSGI temporal (ssgi.cpp) -> t26
void LightMeterAfterComposition(ID3D11DeviceContext* c); void LightMeterPresent(); void LightMeterReloadShaders(); std::string LightMeterSummary();   // posicion de la camara (m_eyepos del SceneCB, config.cpp, 2 cuadros de atraso)
void AutoCapturePresent(ID3D11Texture2D* frame);   // hooks.cpp
// (27/10 c) sombra propia de la cabeza (headshadow.cpp): atlas de sombras de alta resolucion de las cabezas mas cercanas
// (04/11 boca) mascara del interior de la boca (mouthmask.cpp): la luz de la piel no cambia la boca
bool MouthMaskOn(); ID3D11RenderTargetView* MouthMaskBegin(ID3D11RenderTargetView* rt0, ID3D11DepthStencilState* gameDSS, ID3D11PixelShader** ps, ID3D11DepthStencilState** dss);
ID3D11ShaderResourceView* MouthMaskSRV(); void MouthMaskPresent(); void MouthMaskReloadShaders();
// (07/11) medidor de VRAM y de costo en la GPU (gpumeter.cpp)
enum { kGzSSSS, kGzGTAO, kGzSSGI, kGzLightMeter, kGzBody, kGzPitchWear, kGzGrass, kGzFaceGen, kGzCarasHd, kGzRing, kGzFrameCopy, kGzOverlay, kGzGlass, kGpuZoneN };
void GpuMeterHookDevice(ID3D11Device* d); void GpuMeterPresent(); std::string GpuMeterSummary();
int GpuZoneBegin(ID3D11DeviceContext* c, int zone); void GpuZoneEnd(ID3D11DeviceContext* c, int idx);
struct GpuZoneScope { ID3D11DeviceContext* c; int i; GpuZoneScope(ID3D11DeviceContext* c_, int z) : c(c_), i(GpuZoneBegin(c_, z)) {} ~GpuZoneScope() { GpuZoneEnd(c, i); } };
void HeadShadowPresent(); int HeadShadowSlots(); bool HeadShadowBodyOn(); ID3D11ShaderResourceView* HeadShadowSRV(); ID3D11DepthStencilView* HeadShadowDSV();
ID3D11Buffer* HeadShadowCB(int i); void HeadShadowViewport(int i, D3D11_VIEWPORT& vp); bool HeadShadowIsGameShadow(ID3D11DepthStencilView* v); bool HeadShadowIsGameRes(ID3D11DepthStencilView* v);
bool HeadShadowIsNearCascade(ID3D11DeviceContext* c, ID3D11DepthStencilView* gameDSV, int kind); ID3D11DepthStencilView* HeadShadowBind(UINT n, ID3D11RenderTargetView* const* rt, ID3D11DepthStencilView* dsv); void HeadShadowOnClear(ID3D11DeviceContext* c, ID3D11DepthStencilView* v, float depth);
void HeadShadowRelease(); extern std::atomic<int> g_headShadowReplays; ID3D11RasterizerState* HeadShadowNoCullRS(ID3D11RasterizerState* orig);
ID3D11Resource* NamedSRVResource(const std::string& name);   // hooks.cpp: recurso detras de un SRV guardado por regla (sin referencia)
ID3D11ShaderResourceView* SSSSBeforeComposition(ID3D11DeviceContext* c); ID3D11ShaderResourceView* SSSSSpecSRV(); void SSSSReloadShaders(); double SSSSEditorLightAge(); void SSSSRenderSize(unsigned& w, unsigned& h); extern std::atomic<int> g_ssssDraws;   // ssss.cpp
// prueba automatica del menu (menutest.cpp)
void MenuTestPresent(IDXGISwapChain* sc);
void MenuTestStart(); void MenuTestStop(); bool MenuTestActive();
int MenuTestCount(int what); float MenuTestNoise(); const std::string& MenuTestStatus();
size_t MenuTestResults(); bool MenuTestResult_(size_t i, std::string& group, std::string& section, float& diff, int& state);
LRESULT OverlayWndProc(HWND, UINT, WPARAM, LPARAM, bool& handled);
// (24/10) vidrio de los menus del juego, estilo Liquid Glass (glass.cpp): GS + PS propios para los dibujos del menu (55a003da),
// copia desenfocada del fondo antes de los paneles; 189..192 opciones. v2 (24/10 c): el GS decide por triangulo, sin buffer
struct GlassScope { bool on = false; ID3D11PixelShader* ps = nullptr; ID3D11ShaderResourceView* srv[2] = {}; ID3D11ShaderResourceView* t120 = nullptr; ID3D11SamplerState* samp = nullptr; ID3D11Buffer* cb = nullptr;
                    ID3D11ShaderResourceView* gsT8 = nullptr; ID3D11ShaderResourceView* gsT21 = nullptr; ID3D11ShaderResourceView* gsT120 = nullptr; ID3D11SamplerState* gsS0 = nullptr; };
bool GlassBeforeDraw(ID3D11DeviceContext* c, uint64_t ps, uint64_t vs, UINT verts, GlassScope& g);
void GlassAfterDraw(ID3D11DeviceContext* c, GlassScope& g);
void GlassPresent(); void GlassReloadShaders();
extern std::atomic<int> g_glassDraws, g_glassSnaps;
// (24/10 c) aro y punto blanco del musculo elegido como capa de interfaz (ring.cpp): variante de la piel que junta las marcas
// en u5 y un dibujo extra encima de la imagen del jugador del menu Fisico
struct RingScope { bool on = false, overlay = false, face = false; ID3D11UnorderedAccessView* uav = nullptr; ID3D11PixelShader* prevPS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11BlendState* oBS = nullptr; float oBF[4] = {}; UINT oSM = 0;
                   ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; ID3D11ShaderResourceView* oT20 = nullptr; ID3D11ShaderResourceView* oT120 = nullptr; };
bool RingBeforeDraw(ID3D11DeviceContext* c, uint64_t ps, uint64_t vs, RingScope& r);
void RingAfterDraw(ID3D11DeviceContext* c, RingScope& r);
void RingFaceBind(ID3D11DeviceContext* c, RingScope& r);   // (24/10 e) u7 para la medicion de la cara, despues de las cejas
void RingOverlayBegin(ID3D11DeviceContext* c, RingScope& r);
void RingOverlayEnd(ID3D11DeviceContext* c, RingScope& r);
void RingReloadShaders();
void RingPresent(IDXGISwapChain* sc);               // (24/10 e) el aro sobre la imagen final, con las marcas de este cuadro
ID3D11ShaderResourceView* HandDataFor(uint64_t vsHash, UINT indexCount, UINT start, INT base);   // (25/10) manos.cpp: datos por vertice de la mano (t126 del VS)
ID3D11ShaderResourceView* LimbDataFor(uint64_t vsHash, UINT indexCount, UINT start, INT base, bool* isArm);   // (26/10, v12.7) manos.cpp: brazos y piernas (t126 del VS)
ID3D11ShaderResourceView* VeinTexture();   // (26/10, v12.7) manos.cpp: venas de brazos y manos (t29 del PS)
ID3D11ShaderResourceView* ArmAnatTexture(int which);   // (02/11) grupos musculares del brazo: 0 = pendientes (t26), 1 = cavidad (t30), 2 = grupo por texel (t36)
// (27/10) brazos.cpp: brazos, manos y muslos de TorsoBody para las caras reales (la cara trae arm_bsm / thigh_bsm de 2048):
// textura de TorsoBody del tono del jugador para t0 del PS en ese dibujo (nullptr = dejar la del juego); Present lee las huellas
ID3D11ShaderResourceView* ArmsTextureFor(ID3D11DeviceContext* c, uint64_t vsHash, UINT indexCount);
int ArmsModTextureKind(ID3D11ShaderResourceView* v);   // (03/11) 0 = no es del mod; 1 arm, 2 thigh, 3 torso, 4 referencia
void ArmsPresent();
int CharSkinToneFor(uint32_t id);   // (27/10) tono 1..6 de PES21FX\tonos_piel.txt (0 = no esta), fila 324
ID3D11ShaderResourceView* ArmsTorsoTextureForFace();   // (27/10) torso_N para t29 de los PS de la cara (cuello, pes_collar.hlsli, fila 322)
void ArmsStats(unsigned* swaps, unsigned* unknown);
ID3D11ShaderResourceView* VariantDataFor(uint64_t vsHash, UINT indexCount, bool* torso);   // (26/10) manos.cpp: variantes del torso / brazos (t127 del VS)
bool VariantTextures(ID3D11ShaderResourceView* out[3]);   // (26/10) manos.cpp: mapas del torso (t16 robusto, t17 atletico, t18 estrias del PS)
ID3D11ShaderResourceView* SpineTextureFor(uint64_t vsHash, UINT indexCount);   // (26/10) manos.cpp: columna marcada (t19 del PS de la piel, x288)
ID3D11ShaderResourceView* BaseTextureFor(uint64_t vsHash, UINT indexCount);    // (26/10 b) manos.cpp: base esculpida (t20 del PS de la piel, x289)
bool SkinTexturesFor(uint64_t vsHash, UINT indexCount, ID3D11ShaderResourceView* out[2]);   // (26/10 b) manos.cpp: piel real (t21 / t24, x293)
ID3D11ShaderResourceView* PelvisAlbedoFor(uint64_t vsHash, UINT indexCount);   // (26/10 c) manos.cpp: pelvis sin short (t23 del PS de la piel)
bool PelvisDetailFor(uint64_t vsHash, UINT indexCount, ID3D11ShaderResourceView* out[2]);   // (26/10 d) manos.cpp: detalle de piel de la pelvis (t34 / t35)
bool TiltTexturesFor(uint64_t vsHash, UINT indexCount, ID3D11ShaderResourceView* out[3]);   // (26/10 d) manos.cpp: tope del relieve (t31..t33, x307)
float MuscleVariantT(uint32_t id, bool* own);   // (26/10) musculo.cpp: variante del jugador (-1 robusto .. 1 atletico; propia o la general)
extern std::atomic<bool> g_menuOpen;
extern std::atomic<bool> g_blockGameKeys;   // (31/10) keyblock.cpp: el juego no ve el teclado mientras esta prendido
extern std::atomic<bool> g_menuTyping;      // (31/10) overlay.cpp: hay un campo de texto del menu activo (la camara libre no lee W A S D)
void KeyBlockInstall();
extern std::atomic<int> g_replacedCount, g_failedCount;
extern std::vector<std::string> g_compileErrors;
extern std::mutex g_errMutex;

} // namespace fx
