// PES21 FX (11/11/2026, hilo del Motor en tiempo real) - ENLACE CON EL "MOTOR EN TIEMPO REAL" + VISTA EN ALAMBRE
//
// Agustin: en el Motor en tiempo real (modulo de Sider MotorTiempoReal.lua, dentro del Editar) quiere los modos de vista de un
// motor de juego: alambre, sin shaders, normales, profundidad, solo luz, solo sombras. Lo que se leyo en el exe (11/11):
//  - el juego de venta NO trae los plugins de dibujo de depuracion del Fox Engine (GrPluginModelDebug, GrPluginDebugView...
//    son solo el texto del nombre) y GrTools.SwitchRenderMode a "wireframe" deja la pantalla negra (lista de plugins vacia);
//  - SI quedo en GrModelShaders_dx11.fsop el shader DeferredRenderingDebug (la composicion diferida con vista elegible:
//    luz difusa, especular, normal, normal de mundo, albedo, profundidad, oclusion), pero su unico llamador le pasa el
//    interruptor en 0 fijo (xor r8d, r8d en 0x1416a78d4) y PES pone ademas su propia composicion en MainViewport.
// Por eso las vistas van aca, del lado de la DLL, que ya reemplaza las dos composiciones de PES (619b partido, f807 editor):
//  - fila 429 de t120 (nunca se guarda, esta en kDebugParams): x429 = vista (pes_vista_motor.hlsli, mismas cuentas que el
//    shader del motor), z429 = corte de pantalla partida, w429 = escala de la profundidad;
//  - y429 = 1: ALAMBRE. No hay shader que lo haga: es el estado de raster (relleno en alambre). Se cambia solo en los dibujos
//    de geometria del G-buffer (los que tienen 4 o mas destinos de dibujo a la vez y vista de profundidad; el G-buffer de PES tiene 5), con el mismo descarte de caras que traia
//    el dibujo, y se devuelve apenas termina. Sombras, luces, composicion, menus y texto no se tocan.
//
// El modulo de Sider no puede escribir t120: se le deja un bloque de MEMORIA COMPARTIDA con nombre "PES21FX_Motor_<pid>"
// (64 bytes, estructura MotorLink). El modulo escribe vista / alambre / corte / escala y sube seq; la DLL lo aplica una vez
// por cuadro cuando seq cambia y le devuelve lo que rige (asi el panel del Motor muestra la verdad aunque se cambie desde el
// menu de la DLL). Si la DLL es vieja el bloque no existe y el modulo lo dice; si el modulo es viejo, nadie escribe y la fila
// queda en 0. No se guarda nada en el ini.
#include "core.h"
#include <cmath>

namespace fx {

constexpr int kMotorRow = 429;
constexpr uint32_t kMotorMagic = 0x4C52544Du;   // 'MTRL'

#pragma pack(push, 4)
struct MotorLink {
    uint32_t magic;                 // +0x00 kMotorMagic (lo pone la DLL)
    uint32_t version;               // +0x04 1
    uint32_t seq;                   // +0x08 lo sube el modulo en cada cambio
    float vista, alambre, corte;    // +0x0c +0x10 +0x14 pedido del modulo
    uint32_t dllSeq;                // +0x18 lo sube la DLL en cada cuadro (el modulo ve que esta viva)
    float vistaAct, alambreAct, corteAct;   // +0x1c +0x20 +0x24 lo que rige
    uint32_t alambreDibujos;        // +0x28 dibujos pasados a alambre en el ultimo cuadro
    float escala;                   // +0x2c pedido del modulo: escala de la vista de profundidad (0 = automatica)
    float escalaAct;                // +0x30 lo que rige
    uint32_t panel;                 // +0x34 (11/11) el modulo lo cambia en cada cuadro con su panel abierto: el mouse es del Motor (menumouse.cpp)
    uint32_t res[2];                // +0x38 res[0] = idioma del mod (0 es, 1 en, 2 pt; 15/11)
};
#pragma pack(pop)
static_assert(sizeof(MotorLink) == 64, "MotorLink debe medir 64 bytes (lo lee MotorTiempoReal.lua)");

std::atomic<bool> g_motorWire{ false };
static HANDLE s_map = nullptr;
static MotorLink* s_ml = nullptr;
static uint32_t s_lastSeq = 0;
static std::atomic<double> s_panelT{ -1.0e9 };   // (11/11) ultimo cuadro en que el modulo movio 'panel'
bool MotorPanelOpen() { return Now() - s_panelT.load(std::memory_order_relaxed) < 0.5; }
static std::atomic<unsigned> s_wireDraws{ 0 };

// ---------------------------------------------------------------- alambre
// un estado de raster en alambre por cada estado del juego (mismo descarte de caras, mismo sesgo de profundidad). La clave
// guarda una referencia al estado del juego: asi su puntero no se reutiliza para otro estado mientras este en la tabla.
static std::mutex s_wireMx;
static std::unordered_map<ID3D11RasterizerState*, ID3D11RasterizerState*> s_wire;
static bool s_wireFailLogged = false;
constexpr size_t kWireMax = 256;

static ID3D11RasterizerState* WireFor(ID3D11RasterizerState* cur) {
    std::lock_guard<std::mutex> lk(s_wireMx);
    auto it = s_wire.find(cur);
    if (it != s_wire.end()) return it->second;
    if (s_wire.size() >= kWireMax || !g_dev.dev) return nullptr;   // tabla llena: ese dibujo queda relleno
    D3D11_RASTERIZER_DESC rd{};
    if (cur) cur->GetDesc(&rd);
    else { rd.CullMode = D3D11_CULL_BACK; rd.DepthClipEnable = TRUE; }   // sin estado puesto = el estado por defecto de D3D11
    rd.FillMode = D3D11_FILL_WIREFRAME;
    ID3D11RasterizerState* w = nullptr;
    const HRESULT hr = g_dev.dev->CreateRasterizerState(&rd, &w);
    if (FAILED(hr) || !w) {
        if (!s_wireFailLogged) { s_wireFailLogged = true; Log("motor: no pude crear el estado de alambre (hr 0x%08lx): ese dibujo queda relleno", (unsigned long)hr); }
        w = nullptr;
    }
    if (cur) cur->AddRef();
    s_wire[cur] = w;
    if (s_wire.size() == 1 || s_wire.size() == kWireMax) Log("motor: alambre, %u estado(s) de raster del juego con su version en alambre%s", (unsigned)s_wire.size(), s_wire.size() == kWireMax ? " (tope: los nuevos quedan rellenos)" : "");
    return w;
}

// (17/11 06:59, Agustin: "que la vista de alambre muestre distintos colores en las mallas para entender de que se tratan") con
// g_motorWireCol cada malla del G-buffer sale TENIDA de un color propio: el destino 0 (albedo, SV_Target0 en los PS del G-buffer) se
// escribe como albedo x color (mezcla con el factor de mezcla), los demas destinos quedan como el juego los pide. El color sale de
// la malla (VS, cantidad de indices, primer indice y vertice base): la misma malla tiene siempre el mismo color. Los dibujos que ya
// mezclan en el destino 0 (pelo, calcos) no se tocan. La textura sigue debajo: los negros (pelo, barba) quedan oscuros.
std::atomic<bool> g_motorWireCol{ false };
static std::unordered_map<ID3D11BlendState*, ID3D11BlendState*> s_tint;   // bajo s_wireMx; nulo = ese estado no se tine
struct WireTintSave { bool on = false; ID3D11BlendState* bs = nullptr; FLOAT bf[4] = {}; UINT sm = 0xffffffffu; };
static thread_local WireTintSave t_tint;
static ID3D11BlendState* TintFor(ID3D11BlendState* cur) {
    std::lock_guard<std::mutex> lk(s_wireMx);
    auto it = s_tint.find(cur);
    if (it != s_tint.end()) return it->second;
    if (s_tint.size() >= kWireMax || !g_dev.dev) return nullptr;
    D3D11_BLEND_DESC bd{};
    if (cur) cur->GetDesc(&bd);
    else { for (auto& r : bd.RenderTarget) { r.SrcBlend = r.SrcBlendAlpha = D3D11_BLEND_ONE; r.DestBlend = r.DestBlendAlpha = D3D11_BLEND_ZERO; r.BlendOp = r.BlendOpAlpha = D3D11_BLEND_OP_ADD; r.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; } }
    ID3D11BlendState* t = nullptr;
    if (!bd.RenderTarget[0].BlendEnable && (bd.RenderTarget[0].RenderTargetWriteMask & 7)) {
        if (!bd.IndependentBlendEnable) { for (int i = 1; i < 8; i++) bd.RenderTarget[i] = bd.RenderTarget[0]; bd.IndependentBlendEnable = TRUE; }
        auto& r = bd.RenderTarget[0];
        r.BlendEnable = TRUE; r.SrcBlend = D3D11_BLEND_BLEND_FACTOR; r.DestBlend = D3D11_BLEND_ZERO; r.BlendOp = D3D11_BLEND_OP_ADD;
        r.SrcBlendAlpha = D3D11_BLEND_ONE; r.DestBlendAlpha = D3D11_BLEND_ZERO; r.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        if (FAILED(g_dev.dev->CreateBlendState(&bd, &t))) t = nullptr;
    }
    if (cur) cur->AddRef();
    s_tint[cur] = t;
    return t;
}
static void WireTintBegin(ID3D11DeviceContext* c, uint64_t seed) {
    static const float kPal[12][3] = { {1.0f, 0.35f, 0.3f}, {0.35f, 0.85f, 1.0f}, {0.45f, 1.0f, 0.4f}, {1.0f, 0.85f, 0.25f}, {0.85f, 0.45f, 1.0f}, {1.0f, 0.55f, 0.15f},
        {0.3f, 1.0f, 0.8f}, {1.0f, 0.4f, 0.75f}, {0.55f, 0.6f, 1.0f}, {0.8f, 1.0f, 0.3f}, {1.0f, 1.0f, 1.0f}, {0.3f, 0.6f, 1.0f} };
    ID3D11BlendState* bs = nullptr; FLOAT bf[4]; UINT sm = 0; c->OMGetBlendState(&bs, bf, &sm);
    ID3D11BlendState* t = TintFor(bs);
    if (!t) { if (bs) bs->Release(); return; }
    seed ^= seed >> 29; seed *= 0xbf58476d1ce4e5b9ull; seed ^= seed >> 32;
    const float* col = kPal[seed % 12];
    const FLOAT f[4] = { col[0], col[1], col[2], 1.0f };
    c->OMSetBlendState(t, f, sm);
    t_tint.on = true; t_tint.bs = bs; memcpy(t_tint.bf, bf, sizeof bf); t_tint.sm = sm;
}

// antes de un dibujo: true = se puso el estado de alambre y *prev guarda (con referencia) el que estaba (puede ser nulo)
bool MotorWireBegin(ID3D11DeviceContext* c, ID3D11RasterizerState** prev, uint64_t seed) {
    // G-buffer de PES = 5 destinos + profundidad. Con "3 o mas" entraban tambien las luces diferidas (difusa, especular y
    // sombra: 3 destinos) y el generador de mapas de cara de la DLL (facegen.cpp: 3 destinos, sin profundidad, RS nulo)
    ID3D11RenderTargetView* rt[4] = {}; ID3D11DepthStencilView* dsv = nullptr;
    c->OMGetRenderTargets(4, rt, &dsv);
    const bool gbuf = rt[0] && rt[1] && rt[2] && rt[3] && dsv;   // geometria del G-buffer: 4 o mas destinos a la vez + profundidad
    for (auto* r : rt) if (r) r->Release();
    if (dsv) dsv->Release();
    if (!gbuf) return false;
    ID3D11RasterizerState* cur = nullptr;
    c->RSGetState(&cur);
    ID3D11RasterizerState* w = WireFor(cur);
    if (!w) { if (cur) cur->Release(); return false; }
    c->RSSetState(w);
    *prev = cur;
    s_wireDraws.fetch_add(1, std::memory_order_relaxed);
    t_tint.on = false;
    if (g_motorWireCol.load(std::memory_order_relaxed)) WireTintBegin(c, seed);
    return true;
}

void MotorWireEnd(ID3D11DeviceContext* c, ID3D11RasterizerState* prev) {
    c->RSSetState(prev);
    if (prev) prev->Release();
    if (t_tint.on) { c->OMSetBlendState(t_tint.bs, t_tint.bf, t_tint.sm); if (t_tint.bs) t_tint.bs->Release(); t_tint = WireTintSave(); }
}

// ---------------------------------------------------------------- una vez por cuadro (antes de subir t120)
static float Limpio(float v, float lo, float hi) { return (v >= lo && v <= hi) ? v : 0.0f; }   // NaN o fuera de rango = 0

// ---------------------------------------------------------------- (15/11, Agustin 03:29) idioma de textos del juego
// CONFIRMADO en el C (menu::LanguageSelectText::vf75 0x140b22e80): al elegir idioma de textos el juego guarda su numero en
// *(*(0x143714240) + 0x50) + 0x13024 (u32). Que numero es cada idioma NO se saco del exe: se anota en el registro cada vez que
// cambia y la tabla numero -> es / en / pt va en el ini ([PES21FX] idioma_juego_ids = es,en,pt). Con idioma_auto = 1 y un numero
// conocido el menu pasa a ese idioma; elegir un idioma a mano apaga idioma_auto.
static bool RdOk(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (const uint8_t*)p + n <= (const uint8_t*)mi.BaseAddress + mi.RegionSize;
}
int GameTextLang() {
    static int ok = -1;
    if (ok < 0) { const HMODULE m = GetModuleHandleW(nullptr); ok = 0;
        if ((uintptr_t)m == 0x140000000ull) { const auto* dos = (const IMAGE_DOS_HEADER*)m; const auto* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)m + dos->e_lfanew);
            if (nt->FileHeader.TimeDateStamp == 0x62787C6Au) ok = 1; }
        if (!ok) Log("idioma del juego: exe distinto, no se lee"); }
    if (!ok) return -1;
    const void* const* g = (const void* const*)0x143714240ull;
    if (!RdOk(g, 8) || !*g) return -1;
    const uint8_t* a = (const uint8_t*)*g;
    if (!RdOk(a + 0x50, 8)) return -1;
    const uint8_t* b = *(const uint8_t* const*)(a + 0x50);
    if (!b || !RdOk(b + 0x13024, 4)) return -1;
    return (int)*(const uint32_t*)(b + 0x13024);
}
// (15/11, Agustin 03:59: el idioma lo elige Steam en Propiedades > Idioma; el juego no deja cambiarlo adentro) Steam deja el idioma
// del juego en steamapps\appmanifest_<appid>.acf, clave "language" (dentro de "UserConfig"). La carpeta del juego es
// steamapps\common\<nombre>\, asi que el archivo esta dos niveles arriba del exe. Se lee al arrancar y cada 30 s (solo lectura).
static int SteamLang() {
    static double t = -100.0; static int last = -1;
    if (Now() - t < 30.0) return last; t = Now();
    wchar_t exe[MAX_PATH] = {}; if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return last;
    std::wstring d(exe); for (int up = 0; up < 3; up++) { const size_t k = d.find_last_of(L"\\/"); if (k == std::wstring::npos) return last; d.resize(k); }
    WIN32_FIND_DATAW fd{}; HANDLE h = FindFirstFileW((d + L"\\appmanifest_*.acf").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return last;
    int found = -1; std::string lang;
    do { FILE* f = _wfopen((d + L"\\" + fd.cFileName).c_str(), L"rb"); if (!f) continue;
        std::string txt; char buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0 && txt.size() < 65536) txt.append(buf, n); fclose(f);
        if (txt.find("\"1259970\"") == std::string::npos && txt.find("PES 2021") == std::string::npos) continue;   // PES 2021 = app 1259970
        size_t k = txt.find("\"language\""); if (k == std::string::npos) continue;
        k = txt.find('"', k + 10); const size_t e = k == std::string::npos ? k : txt.find('"', k + 1); if (e == std::string::npos) continue;
        lang = txt.substr(k + 1, e - k - 1); for (char& c : lang) c = (char)tolower((unsigned char)c);
        found = (lang == "english") ? 1 : (lang == "portuguese" || lang == "brazilian") ? 2 : (lang == "spanish" || lang == "latam") ? 0 : 3;
        break; } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (found != last) Log("idioma de Steam del juego: %s (%s)", lang.empty() ? "no encontrado" : lang.c_str(), found == 0 ? "castellano" : found == 1 ? "ingles" : found == 2 ? "portugues" : found == 3 ? "otro: queda el ultimo elegido" : "-");
    last = found; return last;
}
static void GameLangPoll() {
    static double t = 0; if (Now() - t < 1.0) return; t = Now();
    static int last = -2; const int id = GameTextLang();
    if (id < 0) { if (g_cfg.langAuto) { const int sl = SteamLang(); if (sl >= 0 && sl <= 2 && g_cfg.lang != sl) { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.lang = sl; LoadMenuLang(); } } return; }
    if (id != last) { last = id; static int n = 0; if (n++ < 40) Log("idioma del juego: textos = numero %d (idioma_auto %d, tabla es %d en %d pt %d)", id, g_cfg.langAuto, g_cfg.langGame[0], g_cfg.langGame[1], g_cfg.langGame[2]); }
    if (!g_cfg.langAuto) return;
    { const int sl = SteamLang(); if (sl >= 0 && sl <= 2) { if (g_cfg.lang != sl) { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.lang = sl; LoadMenuLang(); Log("idioma del menu: sigue a Steam -> %d", sl); } return; } }
    for (int i = 0; i < 3; i++) if (g_cfg.langGame[i] == id && g_cfg.lang != i) { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.lang = i; LoadMenuLang(); Log("idioma del menu: sigue al juego -> %d", i); }
}

void MotorLinkPresent() {
    GameLangPoll();
    static bool tried = false;
    if (!tried) {
        tried = true;
        char nm[64];
        snprintf(nm, sizeof(nm), "PES21FX_Motor_%lu", (unsigned long)GetCurrentProcessId());
        s_map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)sizeof(MotorLink), nm);
        const bool ya = s_map && GetLastError() == ERROR_ALREADY_EXISTS;
        if (s_map) s_ml = (MotorLink*)MapViewOfFile(s_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(MotorLink));
        if (s_ml) {
            if (!ya || s_ml->magic != kMotorMagic) memset(s_ml, 0, sizeof(MotorLink));
            s_ml->version = 1;
            s_lastSeq = s_ml->seq;   // un pedido viejo (DLL recargada) no se vuelve a aplicar solo
            s_ml->magic = kMotorMagic;
        }
        Log("motor: enlace con el Motor en tiempo real %s (memoria compartida %s, fila %d: x vista, y alambre, z corte, w escala de profundidad)",
            s_ml ? "listo" : "NO disponible (las vistas se manejan solo desde el menu)", nm, kMotorRow);
    }
    float v[4];
    {
        std::lock_guard<std::mutex> lk(g_cfgMutex);
        if (s_ml) {
            { static uint32_t pl = 0; const uint32_t pn = s_ml->panel; if (pn != pl) { pl = pn; s_panelT.store(Now(), std::memory_order_relaxed); } }
            const uint32_t q = s_ml->seq;
            if (q != s_lastSeq) {
                s_lastSeq = q;
                g_cfg.live[kMotorRow][0] = floorf(Limpio(s_ml->vista, 0.0f, 10.5f) + 0.5f);
                g_cfg.live[kMotorRow][1] = s_ml->alambre > 0.5f ? 1.0f : 0.0f;
                g_cfg.live[kMotorRow][2] = Limpio(s_ml->corte, 0.0f, 1.0f);
                g_cfg.live[kMotorRow][3] = Limpio(s_ml->escala, 0.0f, 100.0f);
                static int n = 0;
                if (n++ < 60) Log("motor: pedido %u del modulo de Sider -> vista %g, alambre %g, corte %.2f, escala %g", (unsigned)q,
                                  g_cfg.live[kMotorRow][0], g_cfg.live[kMotorRow][1], g_cfg.live[kMotorRow][2], g_cfg.live[kMotorRow][3]);
            }
        }
        for (int i = 0; i < 4; i++) v[i] = g_cfg.live[kMotorRow][i];
    }
    const bool wire = v[1] > 0.5f;
    if (wire != g_motorWire.load(std::memory_order_relaxed)) {
        g_motorWire.store(wire, std::memory_order_relaxed);
        static int n = 0;
        if (n++ < 40) Log("motor: vista en alambre %s", wire ? "PRENDIDA (geometria del G-buffer con relleno en alambre)" : "apagada");
    }
    const unsigned nd = s_wireDraws.exchange(0, std::memory_order_relaxed);
    if (s_ml) {
        s_ml->vistaAct = v[0]; s_ml->alambreAct = v[1]; s_ml->corteAct = v[2]; s_ml->escalaAct = v[3];
        s_ml->alambreDibujos = nd;
        s_ml->res[0] = (g_cfg.lang == 1 || g_cfg.lang == 2) ? (uint32_t)g_cfg.lang : 0u;   // (15/11) idioma del mod: el modulo de Sider nombra la pestana del Editar con esto
        s_ml->dllSeq = s_ml->dllSeq + 1;
    }
    if (wire) {   // cada 10 s con el alambre prendido: cuantos dibujos se cambiaron (0 = ningun dibujo de G-buffer: avisar)
        static double t = 0; static unsigned acc = 0, frames = 0;
        acc += nd; frames++;
        const double now = Now();
        if (now - t > 10.0) { static int n = 0; if (n++ < 30) Log("motor: alambre, %u dibujos cambiados en %u cuadros%s", acc, frames, acc ? "" : " (ningun dibujo de G-buffer, 4 o mas destinos + profundidad: nada que pasar a alambre)"); t = now; acc = 0; frames = 0; }
    }
}

}   // namespace fx
