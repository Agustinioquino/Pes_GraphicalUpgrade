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
    uint32_t res[3];                // +0x34
};
#pragma pack(pop)
static_assert(sizeof(MotorLink) == 64, "MotorLink debe medir 64 bytes (lo lee MotorTiempoReal.lua)");

std::atomic<bool> g_motorWire{ false };
static HANDLE s_map = nullptr;
static MotorLink* s_ml = nullptr;
static uint32_t s_lastSeq = 0;
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

// antes de un dibujo: true = se puso el estado de alambre y *prev guarda (con referencia) el que estaba (puede ser nulo)
bool MotorWireBegin(ID3D11DeviceContext* c, ID3D11RasterizerState** prev) {
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
    return true;
}

void MotorWireEnd(ID3D11DeviceContext* c, ID3D11RasterizerState* prev) {
    c->RSSetState(prev);
    if (prev) prev->Release();
}

// ---------------------------------------------------------------- una vez por cuadro (antes de subir t120)
static float Limpio(float v, float lo, float hi) { return (v >= lo && v <= hi) ? v : 0.0f; }   // NaN o fuera de rango = 0

void MotorLinkPresent() {
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
