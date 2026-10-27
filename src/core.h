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
constexpr int kParamCount = 336;           // float4 x 336 -> t120 en los shaders (IniParams.Load(int2(i,0))). (20/10: era 128 y los parametros 128+ se perdian; 25/10: 256 -> 320, la ropa contra el cuerpo usa 256 / 257; 27/10: 320 -> 336: la fila 320 (torso bajo la camiseta) quedaba fuera y el ini la descartaba)
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
// 320 = torso bajo la camiseta ajustada (27/10, pes_skintone.hlsli PesTorsoInset: x mm, w on).
// 321 = brazos, manos y muslos de TorsoBody para las caras reales (27/10, brazos.cpp: x prendido, y muslos, z tono forzado, w log).
// 297 / 298 = luz de prueba del editor (27/10 b, pes_ssss_inc.hlsli: sol de dia / tarde / noche donde no corre el sol del partido).
// 299 = pasto tanda 9a (hilo del pasto, rama kd63zh).
// 309 (ini) + 310, 316..319 (DLL) = sombra propia de la cabeza (27/10 c, headshadow.cpp + pes_headshadow.hlsli en los 7 shaders del sol).
// 305 musculos con textura (manos.cpp + pes_limbs.hlsli en el VS 1c02a92a + PS aa7d1833; x prendido, y definicion, z venas de los
// brazos, w venas de las manos con el puno apretado; v12.7).

struct Assign { int idx; int comp; std::vector<float> values; std::string var; };
struct KeyBind {
    std::string name, label;
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
    float def = 0;                         // valor de fabrica si la clave no esta en cloth.ini
};
struct MenuButton { std::string action, label, help; };   // button = accion | etiqueta | ayuda  (acciones: hair_export, hair_reload)
struct MenuGroup {
    std::string title, desc, section;
    int tIdx = -1, tComp = 0;            // parametro del interruptor (w46, etc.); -1 = sin interruptor
    std::vector<MenuSlider> sliders;
    std::vector<MenuButton> buttons;
    std::string special;                 // (24/10) tarjeta dibujada por el codigo: "musculatura_jugador"
};

// ---------------------------------------------------------------- reglas por shader (lo que eran [ShaderOverride])
struct RuleAction {
    enum Kind { CopyCB, RefSRV, BindCB, BindSRV, SetParam, EventTurf, FaceGen } kind;   // FaceGen: slot = mapa A, idx = mapa B
    int slot = 0;
    std::string name;          // recurso con nombre, o "file:ruta.dds"
    int idx = 0, comp = 0;     // SetParam
    float value = 0; std::string var;
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
struct HairCfg { std::vector<HairType> types; std::map<std::string, std::string> faces; std::string defType = "corto"; bool loaded = false; };

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
    float uiScale = 1.0f;                      // 19/10: tamano del menu ([PES21FX] ui_scale)
    bool compact = false;                      // 19/10: grupos contraidos por defecto ([PES21FX] compacto)
    bool autoCap = false; float autoCapSec = 20.0f; int autoCapMax = 150; bool autoCapAB = false; bool exeDump = true; bool motorPatch = true; int motorShadowRes = 0; float motorCascadeScale = 0.0f;   // [Motor]: parches en memoria (exepatch.cpp)   // exe_dump: volcado del exe desempaquetado (una vez por version)   // auto_captura_ab: pares A (todo) / B (un grupo del menu apagado 3 cuadros)
    int aniso = 16; float mipBias = 0.0f;   // 20/10: filtrado anisotropico forzado (1 = el del juego) y sesgo de mip global ([PES21FX] aniso, mip_bias)   // 19/10: captura automatica en partido ([PES21FX] auto_captura, auto_captura_seg, auto_captura_max)
    bool optReplace = true, optRules = true, optOverlay = true, optPin = false, optHooks = true, optDxgi = true, optPresent = true, optCtx = true, optExports = true, optOrphan = true, optDump = false;
    int memFix = 1; bool memWatch = true;     // (26/10) [PES21FX] memoria_arreglo (0/1 = retener, 2 = liberar entero) / memoria_vigilar (memwatch.cpp)
    bool grassScan = true, grassNoCull = true, grassDumpDebugP = true;   // (26/10) [Motor] pasto_buscar / pasto_sin_recorte / volcar_debugp: distancia del pasto 3D del juego (grassdist.cpp)
    bool lightTrace = true;                   // (27/10) [PES21FX] luz_rastrear: pila de quien llena el cbuffer de la luz del sol (hooks.cpp)
    bool sunWatch = true;                     // (26/10) [PES21FX] sol_vigilar: quien escribe el sol de las repeticiones (sunwatch.cpp)
};
extern Config g_cfg;
extern std::mutex g_cfgMutex;

bool LoadConfig();                              // PES21FX.ini + PES21FX_rules.ini
bool SaveConfigValues();
bool SaveOption(const std::string& key, const std::string& value);   // escribe/agrega clave en [PES21FX]
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
void GrassOnDraw(ID3D11DeviceContext* c, const ShaderEntry* vs, const ShaderEntry* ps, UINT count, UINT start, INT base);
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
struct GrassDbg { int calls = 0, emits = 0, patches = 0, sparse = 0, sparseLast = 0; int ring[3] = {0, 0, 0}; UINT lod = 0, rtW = 0, cb2off = 0; bool cam = false; float ex = 0, ez = 0; const char* why = nullptr; };
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
struct FaceGenExtra { ID3D11ShaderResourceView* seedMap = nullptr; ID3D11UnorderedAccessView* seedsUAV = nullptr; ID3D11ShaderResourceView* seedsSRV = nullptr; ID3D11Buffer* args = nullptr; ID3D11UnorderedAccessView* argsUAV = nullptr; ID3D11Buffer* hairCB = nullptr; int rtCount = 0;
    ID3D11Resource* key = nullptr; ID3D11Resource* t0 = nullptr; std::string id; };   // (20/10) identidad de la cara para el LOD: textura clave (t1), t0 y hash (punteros sin retener)
extern std::atomic<int> g_depthClearZero, g_depthClearOne;
bool FaceGenLookup(ID3D11DeviceContext* c, ID3D11ShaderResourceView** outA, ID3D11ShaderResourceView** outB, FaceGenExtra* ex);
void BrowDrawAfterFace(ID3D11DeviceContext* c, FaceGenExtra* ex);
void FaceGenPresent(); void FaceGenClear(); void FaceGenReloadShaders(); int FaceGenCount();
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
int FaceGenExportHair(std::string& status);   // vuelca hair\\auto\\<id>.dds de cada cara generada (plantillas para editar)
bool LoadHairConfig();                         // PES21FX_hair.ini
const HairType* HairTypeFor(const std::string& id, std::string* outName);
ID3DBlob* CompileNamedShader(const wchar_t* file, const char* profile);   // shaders\<file> con #include
void NamedSetSRV(const std::string& name, ID3D11ShaderResourceView* v);
std::string DescribeInputLayout(ID3D11InputLayout* il);   // elementos de un input layout anotado al crearse   // publica un SRV como recurso con nombre (srv = N nombre en las reglas)
extern std::atomic<int> g_matchGen;                                        // sube en cada partido nuevo (reloj del cesped)
void PitchWearNotePlayer(ID3D11DeviceContext* c); void PitchWearPresent(); void PitchWearReloadShaders();   // desgaste de la cancha en vivo
void PitchClipsDraw(ID3D11DeviceContext* c, int frame);
void PitchBallNote(ID3D11DeviceContext* c, int frame);   // (20/10) capsula de la pelota del VS del pasto -> pasto aplastado   // recortes de pasto volando (desde el dibujo del pasto)
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
void LodBind(ID3D11DeviceContext* c, const LodIA& L, LodIA& sv); void LodRestore(ID3D11DeviceContext* c, LodIA& sv, const LodIA& L); void LodPresent();
extern float g_eyePos[4]; extern bool g_eyeValid;
extern thread_local LodIA t_grassSub;   // (20/10) pasto v4: malla propia para el dibujo de pasto en curso (la llena GrassOnDraw, la consume hk_DrawIdx)
void GrassDenseReset();
void ExeDumpPresent();
void ExePatchInstall();
void ExePatchPresent();   // (20/10) LOD de modelos (x142) cada segundo
void GrassDistPresent();  // (26/10) busca el recorte de distancia del pasto 3D del juego ya descifrado (grassdist.cpp)
// (23/10) camara del motor (camhook.cpp): cortes de camara del juego (CameraSystem 0x14198d3b0) y reinicios del TAA del juego
int PresentNo();                                  // numero de cuadro (sube en cada Present)
void CamHookPresent();                            // ganchos (primer Present), 160 = cuadros desde el corte / cortes / TAA / ok
bool CamCutSince(int& seen);                      // true una vez por cada corte nuevo (cada consumidor guarda su 'seen'); x161 = 1
int CamLastCutFrame();
bool EngineCamera(float pos[3], float tgt[3], int* frame);   // ultima camara aplicada por el motor (sin atraso)
ID3D11ShaderResourceView* MuscleClothSRV();     // (25/10) zonas de la musculatura para la fisica de la ropa (t121 del simulador)
// (25/10) la ropa choca con el cuerpo real (cuerpo.cpp): piel del cuerpo deformada por personaje para el simulador (t122..t124)
void BodyOnDraw(ID3D11DeviceContext* c, UINT count, UINT start, INT base);   // en cada dibujo de la piel del cuerpo (VS 1c02a92a)
void BodyPresent();                                // registro de mallas nuevas, lote del cuadro (personajes + piel), log
void BodyClothSRVs(ID3D11ShaderResourceView* out[3]);   // tablas, vertices, listas (null si no esta listo)
void BodyOnFinish(ID3D11DeviceContext* c, ID3D11CommandList* l);    // (25/10 e) FinishCommandList: los dibujos del contexto pasan a la lista
void BodyOnExecute(ID3D11DeviceContext* c, ID3D11CommandList* l);   // (25/10 e) ExecuteCommandList: cuentan para el lote del cuadro
extern std::atomic<bool> g_bodyDiagArmed;                            // (25/10 e) diagnostico: copiar las instancias del simulador de la ropa
void BodyNoteClothDispatch(ID3D11DeviceContext* c, UINT groups);    // en el Dispatch del simulador (contexto con su CS puesto)
void BodySimSeen();                                // el simulador del mod de ropa se uso en este cuadro
void BodyReloadShaders();
struct InputElemInfo { std::string sem; UINT idx, fmt, slot, off; };
bool InputLayoutInfo(ID3D11InputLayout* il, std::vector<InputElemInfo>& out);   // hooks.cpp: elementos anotados al crearse el layout
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
void HeadShadowPresent(); int HeadShadowSlots(); ID3D11ShaderResourceView* HeadShadowSRV(); ID3D11DepthStencilView* HeadShadowDSV();
ID3D11Buffer* HeadShadowCB(int i); void HeadShadowViewport(int i, D3D11_VIEWPORT& vp); bool HeadShadowIsGameShadow(ID3D11DepthStencilView* v);
bool HeadShadowIsNearCascade(ID3D11DeviceContext* c, ID3D11DepthStencilView* gameDSV); void HeadShadowOnClear(ID3D11DeviceContext* c, ID3D11DepthStencilView* v, float depth);
void HeadShadowRelease(); extern std::atomic<int> g_headShadowReplays;
ID3D11Resource* NamedSRVResource(const std::string& name);   // hooks.cpp: recurso detras de un SRV guardado por regla (sin referencia)
ID3D11ShaderResourceView* SSSSBeforeComposition(ID3D11DeviceContext* c); ID3D11ShaderResourceView* SSSSSpecSRV(); void SSSSReloadShaders(); extern std::atomic<int> g_ssssDraws;   // ssss.cpp
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
// (27/10) brazos.cpp: brazos, manos y muslos de TorsoBody para las caras reales (la cara trae arm_bsm / thigh_bsm de 2048):
// textura de TorsoBody del tono del jugador para t0 del PS en ese dibujo (nullptr = dejar la del juego); Present lee las huellas
ID3D11ShaderResourceView* ArmsTextureFor(ID3D11DeviceContext* c, uint64_t vsHash, UINT indexCount);
void ArmsPresent();
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
extern std::atomic<int> g_replacedCount, g_failedCount;
extern std::vector<std::string> g_compileErrors;
extern std::mutex g_errMutex;

} // namespace fx
