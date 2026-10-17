// PES21 FX - mod grafico para PES 2021 (DX11). Lo carga Sider (PES21FX.lua) y reemplaza a 3DMigoto.
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>
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

// ---------------------------------------------------------------- reglas por shader (lo que eran [ShaderOverride])
struct RuleAction {
    enum Kind { CopyCB, RefSRV, BindCB, BindSRV, SetParam, EventTurf } kind;
    int slot = 0;
    std::string name;          // recurso con nombre, o "file:ruta.dds"
    int idx = 0, comp = 0;     // SetParam
    float value = 0; std::string var;
};
struct Rule { uint64_t hash = 0; std::vector<RuleAction> actions; bool hasParamOverride = false; int paramSet = -1; };

struct Config {
    std::wstring baseDir;                      // carpeta del mod (termina en '\')
    float base[kParamCount][4] = {};           // valores del ini
    float live[kParamCount][4] = {};           // valores actuales (teclas / menu)
    ParamInfo info[kParamCount][4];
    std::vector<KeyBind> keys;
    std::map<uint64_t, Rule> rules;
    std::map<std::string, float> vars;         // $pes_push, etc.
    int menuKey = VK_INSERT;
    bool bypass = false;                       // ver el juego original
    // [PES21FX] para aislar problemas: replace_shaders / rules / overlay = 0 apagan cada parte
    bool optReplace = true, optRules = true, optOverlay = true;
};
extern Config g_cfg;
extern std::mutex g_cfgMutex;

bool LoadConfig();                              // PES21FX.ini + PES21FX_rules.ini
bool SaveConfigValues();                        // escribe los valores live en PES21FX.ini
void PollKeys(bool focused);
void KeyActionUnlocked(KeyBind& k, int action);    // menu: alternar / ciclar (con g_cfgMutex tomado)
void UpdateTimeParams();                        // reloj del partido, viento, etc.
void OnTurfDrawn();
double Now();

// ---------------------------------------------------------------- d3d
bool InstallHooks();
void ReloadShaders();
std::wstring ShaderPath(uint64_t hash, const char* type);   // shaders\<hash>-ps_replace.txt

struct DeviceState {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* imm = nullptr;
    ID3D11Texture1D* paramTex = nullptr;
    ID3D11ShaderResourceView* paramSRV = nullptr;
    std::vector<ID3D11Texture1D*> altTex;      // conjuntos de parametros con cambios por shader (p.ej. w5 = $pes_push)
    std::vector<ID3D11ShaderResourceView*> altSRV;
};
extern DeviceState g_dev;

// ---------------------------------------------------------------- overlay
void OverlayPresent(IDXGISwapChain* sc);
LRESULT OverlayWndProc(HWND, UINT, WPARAM, LPARAM, bool& handled);
extern std::atomic<bool> g_menuOpen;
extern std::atomic<int> g_replacedCount, g_failedCount;
extern std::vector<std::string> g_compileErrors;
extern std::mutex g_errMutex;

} // namespace fx
