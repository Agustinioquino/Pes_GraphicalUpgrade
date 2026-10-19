// PES21 FX - mod grafico para PES 2021 (DX11). Lo carga Sider (PES21FX.lua) y reemplaza a 3DMigoto.
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>
#include <memory>
#include <cstdint>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fx {

// ---------------------------------------------------------------- log
void Log(const char* fmt, ...);
std::string ModOff(void* p);   // "modulo.dll+0x1234" (diagnostico)
void ScanRefsInto(uintptr_t base, uintptr_t end, const char* dllName);   // quien apunta todavia a una DLL que se descarga
extern void* g_overlayOldProc;

// ---------------------------------------------------------------- parametros (lo que en 3DMigoto era IniParams)
constexpr int kParamCount = 128;           // float4 x 128 -> t120 en los shaders (IniParams.Load(int2(i,0)))

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
};

// ---------------------------------------------------------------- reglas por shader (lo que eran [ShaderOverride])
struct RuleAction {
    enum Kind { CopyCB, RefSRV, BindCB, BindSRV, SetParam, EventTurf, FaceGen } kind;   // FaceGen: slot = mapa A, idx = mapa B
    int slot = 0;
    std::string name;          // recurso con nombre, o "file:ruta.dds"
    int idx = 0, comp = 0;     // SetParam
    float value = 0; std::string var;
};
struct Rule { uint64_t hash = 0; std::vector<RuleAction> actions; bool hasParamOverride = false; int paramSet = -1; };

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
    std::map<std::string, float> vars;         // $pes_push, etc.
    HairCfg hair;                              // PES21FX_hair.ini
    int menuKey = VK_INSERT;
    bool bypass = false;                       // ver el juego original
    // [PES21FX] para aislar problemas: replace_shaders / rules / overlay = 0 apagan cada parte
    bool tutorials = true;                     // tarjetas de tutorial al abrir cada pestana ([PES21FX] tutorials)
    bool optReplace = true, optRules = true, optOverlay = true, optPin = false, optHooks = true, optDxgi = true, optPresent = true, optCtx = true, optExports = true, optOrphan = true, optDump = false;
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
    ID3D11Device* dev = nullptr;         // dispositivo que lo creo
    std::shared_ptr<std::vector<uint8_t>> code;   // bytecode original (para el catalogo: reflexion)
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
void GrassOnDraw(ID3D11DeviceContext* c, const ShaderEntry* vs, UINT count, UINT start, INT base);
void GrassPresent();
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
struct FaceGenExtra { ID3D11ShaderResourceView* seedMap = nullptr; ID3D11UnorderedAccessView* seedsUAV = nullptr; ID3D11ShaderResourceView* seedsSRV = nullptr; ID3D11Buffer* args = nullptr; ID3D11UnorderedAccessView* argsUAV = nullptr; ID3D11Buffer* hairCB = nullptr; int rtCount = 0; };
extern std::atomic<int> g_depthClearZero, g_depthClearOne;
bool FaceGenLookup(ID3D11DeviceContext* c, ID3D11ShaderResourceView** outA, ID3D11ShaderResourceView** outB, FaceGenExtra* ex);
void BrowDrawAfterFace(ID3D11DeviceContext* c, FaceGenExtra* ex);
void FaceGenPresent(); void FaceGenClear(); void FaceGenReloadShaders(); int FaceGenCount();
// inspector visual (inspect.cpp)
extern std::atomic<bool> g_inspectOn; extern std::atomic<int> g_inspectSel;
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
bool NamedCBRead(const std::string& name, std::vector<float>& out);   // hooks.cpp (Present)
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
// prueba automatica del menu (menutest.cpp)
void MenuTestPresent(IDXGISwapChain* sc);
void MenuTestStart(); void MenuTestStop(); bool MenuTestActive();
int MenuTestCount(int what); float MenuTestNoise(); const std::string& MenuTestStatus();
size_t MenuTestResults(); bool MenuTestResult_(size_t i, std::string& group, std::string& section, float& diff, int& state);
LRESULT OverlayWndProc(HWND, UINT, WPARAM, LPARAM, bool& handled);
extern std::atomic<bool> g_menuOpen;
extern std::atomic<int> g_replacedCount, g_failedCount;
extern std::vector<std::string> g_compileErrors;
extern std::mutex g_errMutex;

} // namespace fx
