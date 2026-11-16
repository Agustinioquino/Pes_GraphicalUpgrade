// ---------------------------------------------------------------- (15/11 q) vista extra del Motor, alternando cuadros
// Agustin 17:27: "permitirme crear una vista extra, con otra camara"; 17:39 eligio "las dos cosas": esta version para probar ya y,
// aparte, un visor real del motor (decompilado/hallazgos/20261115_render_target_segunda_vista.cpp.md; todavia sin armar).
// El juego dibuja UNA camara por cuadro. Con la vista extra prendida, freecam.cpp manda en los cuadros B la camara extra. Aca, en
// cada Present y ANTES de dibujar el editor:
//   cuadro A: se guarda la imagen en s_texA (es la vista principal);
//   cuadro B: se guarda la imagen en s_texB (la vista extra, la muestra el editor en su ventanita) y se vuelve a poner en pantalla la
//             ultima A: asi en pantalla siempre se ve la vista principal, sin parpadeo.
// Cada vista se renueva a la mitad de los cuadros por segundo. Si el juego dibuja la camara un cuadro despues de recibirla, las dos
// vistas salen cambiadas: el boton "Intercambiar" de la ventanita corre la cuenta un cuadro (FreeCamExtraSwap).
#include "core.h"
#include <atomic>

namespace fx {

static ID3D11Texture2D* s_texA = nullptr; static ID3D11Texture2D* s_texB = nullptr; static ID3D11ShaderResourceView* s_srvB = nullptr;
static UINT s_w = 0, s_h = 0; static DXGI_FORMAT s_fmt = DXGI_FORMAT_UNKNOWN; static bool s_haveA = false, s_haveB = false, s_failed = false;

static void XFree() {
    if (s_srvB) { s_srvB->Release(); s_srvB = nullptr; }
    if (s_texB) { s_texB->Release(); s_texB = nullptr; }
    if (s_texA) { s_texA->Release(); s_texA = nullptr; }
    s_w = s_h = 0; s_haveA = s_haveB = false;
}
static DXGI_FORMAT SrvFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

void* MotorExtraSRV(float* aspect) {
    if (!s_haveB || !s_srvB || !FreeCamExtra() || !MotorPageActive()) return nullptr;
    if (aspect) *aspect = s_h ? (float)s_w / (float)s_h : 1.7778f;
    return s_srvB;
}

void MotorExtraPresent(IDXGISwapChain* sc) {
    const bool on = sc && g_dev.dev && g_dev.imm && FreeCamExtra() && FreeCamActive() && MotorPageActive() && !s_failed;
    if (!on) { if (!FreeCamExtra()) { if (s_texA) XFree(); } else { s_haveA = false; s_haveB = false; } FreeCamExtraTick(); return; }
    const bool isB = FreeCamExtraBFrame();      // el cuadro que se esta por mostrar se dibujo con la camara extra
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) { FreeCamExtraTick(); return; }
    D3D11_TEXTURE2D_DESC d{}; bb->GetDesc(&d);
    if (d.SampleDesc.Count != 1) { bb->Release(); s_failed = true; Log("motor: vista extra: la pantalla usa MSAA (%u muestras): no se puede copiar, queda apagada", d.SampleDesc.Count); FreeCamExtraTick(); return; }
    if (d.Width != s_w || d.Height != s_h || d.Format != s_fmt || !s_texA || !s_texB) {
        XFree();
        D3D11_TEXTURE2D_DESC t = d; t.Usage = D3D11_USAGE_DEFAULT; t.CPUAccessFlags = 0; t.MiscFlags = 0; t.MipLevels = 1; t.ArraySize = 1;
        t.BindFlags = 0;
        HRESULT h1 = g_dev.dev->CreateTexture2D(&t, nullptr, &s_texA);
        t.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        HRESULT h2 = g_dev.dev->CreateTexture2D(&t, nullptr, &s_texB);
        HRESULT h3 = E_FAIL;
        if (SUCCEEDED(h2) && s_texB) { D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = SrvFormat(d.Format); sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
            h3 = g_dev.dev->CreateShaderResourceView(s_texB, &sv, &s_srvB); }
        if (FAILED(h1) || FAILED(h2) || FAILED(h3)) { Log("motor: vista extra: no se pudieron crear las copias (%08x %08x %08x, formato %d): queda apagada", (unsigned)h1, (unsigned)h2, (unsigned)h3, (int)d.Format); XFree(); s_failed = true; bb->Release(); FreeCamExtraTick(); return; }
        s_w = d.Width; s_h = d.Height; s_fmt = d.Format;
        Log("motor: vista extra lista (copias de %u x %u, formato %d)", s_w, s_h, (int)s_fmt);
    }
    if (isB) { g_dev.imm->CopyResource(s_texB, bb); s_haveB = true; if (s_haveA) g_dev.imm->CopyResource(bb, s_texA); }
    else { g_dev.imm->CopyResource(s_texA, bb); s_haveA = true; }
    bb->Release();
    FreeCamExtraTick();
}

} // namespace fx
