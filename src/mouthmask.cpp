// PES21 FX (04/11) - MASCARA DEL INTERIOR DE LA BOCA. Agustin: "no puedes hacer que distinga la lengua?". En el G-buffer la lengua
// (marcada como piel por el PS de la boca con w18 = 1) es igual a la piel de la cara: misma marca 250/255, mismo tipo de material,
// sin bits libres. Para que los arreglos de la luz de la piel no la toquen (REGLA FIJA: el interior de la boca no cambia), la DLL
// repite el dibujo de la boca (PS 63fab773, sin cambiarlo) con un PS que escribe 1 (pes_mouthmask_ps.hlsl) sobre una textura R8 del
// tamano del G-buffer, con la profundidad del juego (prueba sin escribir): queda 1 solo donde se VE la boca.
// La leen la luz de prueba del editor (ssss.cpp -> t7) y las pasadas del sol d0e9 / 7dbbc (hooks.cpp -> t43). Se limpia en cada
// Present (despues de que la usaron).
// (09/11, Agustin: "se nota que el modelo mouth se renderiza con transparencia por encima del modelo") la prueba de profundidad
// corre cuando se dibuja la boca, y la cara se dibuja DESPUES: la cavidad que la cara tapa (mejilla, labios, bigote) quedaba marcada
// como boca y ahi la piel usaba la formula de antes (borde y contraluz rojos) = un recuadro rojizo con la forma de la boca 3D. Ahora
// la mascara guarda la profundidad del dibujo de la boca (R32F, SV_Position.z) y los lectores solo la aceptan si coincide con la
// profundidad final del pixel (PesMouthVisible): donde la cara tapo la boca no cuenta. La boca no cambia. Fila 374: x = 1 prendida (0 = sin mascara: esos shaders usan la formula de antes en toda la piel).
#include "core.h"
#include <d3d11.h>
#include <mutex>
#include <atomic>
#include <vector>

namespace fx {

static std::mutex s_mx;
static ID3D11Texture2D* s_tex = nullptr; static ID3D11RenderTargetView* s_rtv = nullptr; static ID3D11ShaderResourceView* s_srv = nullptr;
static UINT s_w = 0, s_h = 0;
static ID3D11PixelShader* s_ps = nullptr; static bool s_psTried = false;
static ID3D11DepthStencilState* s_dss[2] = {};   // 0 = profundidad normal (menor o igual), 1 = invertida (mayor o igual)
static std::atomic<bool> s_dirty{false};
static std::atomic<int> s_draws{0}, s_fail{0};
static double s_logT = 0;

// (05/11) cierre 17:41 (CFG al liberar un objeto D3D11 que apuntaba a otro ya destruido): por las dudas, la mascara NUNCA libera nada
// mientras el juego corre. Lo viejo (textura de otro tamano, PS antes de recargar) queda guardado aca y no se suelta: otro hilo o una
// command list pueden tenerlo enlazado. Son pocos objetos (cambio de resolucion / F10), no pesa.
static std::vector<IUnknown*> s_retired;
static void Retire(IUnknown* o) { if (o) s_retired.push_back(o); }
static void ReleaseTex() {
    Retire(s_srv); Retire(s_rtv); Retire(s_tex);
    s_srv = nullptr; s_rtv = nullptr; s_tex = nullptr; s_w = s_h = 0;
}

bool MouthMaskOn() { return g_cfg.live[374][0] > 0.5f; }

// RTV de la mascara del tamano de rt0 (el G-buffer del dibujo de la boca), el PS y el estado de profundidad. nullptr = no se puede
ID3D11RenderTargetView* MouthMaskBegin(ID3D11RenderTargetView* rt0, ID3D11DepthStencilState* gameDSS, ID3D11PixelShader** ps, ID3D11DepthStencilState** dss) {
    if (!MouthMaskOn() || !rt0 || !g_dev.dev) return nullptr;
    ID3D11Resource* r = nullptr; rt0->GetResource(&r); if (!r) return nullptr;
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN; r->GetType(&dim);
    D3D11_TEXTURE2D_DESC td{}; if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) ((ID3D11Texture2D*)r)->GetDesc(&td);
    r->Release();
    if (!td.Width || !td.Height || td.SampleDesc.Count != 1) { s_fail++; return nullptr; }
    std::lock_guard<std::mutex> lk(s_mx);
    if (!s_psTried) {
        s_psTried = true;
        if (ID3DBlob* b = CompileNamedShader(L"pes_mouthmask_ps.hlsl", "ps_5_0")) { g_dev.dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_ps); b->Release(); }
        Log("mascara de la boca: shader %s", s_ps ? "listo" : "NO compilo (ver errores): la luz de la piel usa la formula de antes en toda la piel");
    }
    if (!s_ps) return nullptr;
    if (s_w != td.Width || s_h != td.Height) {
        ReleaseTex();
        D3D11_TEXTURE2D_DESC md{}; md.Width = td.Width; md.Height = td.Height; md.MipLevels = 1; md.ArraySize = 1; md.Format = DXGI_FORMAT_R32_FLOAT;   // (09/11) profundidad de la boca, no 1
        md.SampleDesc.Count = 1; md.Usage = D3D11_USAGE_DEFAULT; md.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_dev.dev->CreateTexture2D(&md, nullptr, &s_tex)) || FAILED(g_dev.dev->CreateRenderTargetView(s_tex, nullptr, &s_rtv)) ||
            FAILED(g_dev.dev->CreateShaderResourceView(s_tex, nullptr, &s_srv))) { ReleaseTex(); s_fail++; Log("mascara de la boca: no se pudo crear la textura %ux%u", td.Width, td.Height); return nullptr; }
        s_w = td.Width; s_h = td.Height;
        Log("mascara de la boca: textura %ux%u R32F (profundidad)", s_w, s_h);
        // nace limpia (D3D11 no garantiza ceros): se limpia en el contexto inmediato en el proximo Present; mientras, nada la lee
        s_dirty = true;
        return nullptr;
    }
    // profundidad: la del juego, sin escribir, "menor o igual" (o "mayor o igual" con profundidad invertida)
    D3D11_DEPTH_STENCIL_DESC gd{}; if (gameDSS) gameDSS->GetDesc(&gd); else { gd.DepthEnable = TRUE; gd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; }
    bool rev;
    if (gd.DepthFunc == D3D11_COMPARISON_GREATER || gd.DepthFunc == D3D11_COMPARISON_GREATER_EQUAL) rev = true;
    else if (gd.DepthFunc == D3D11_COMPARISON_LESS || gd.DepthFunc == D3D11_COMPARISON_LESS_EQUAL) rev = false;
    else rev = g_depthClearZero.load() > g_depthClearOne.load();
    if (!s_dss[rev ? 1 : 0]) {
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc = rev ? D3D11_COMPARISON_GREATER_EQUAL : D3D11_COMPARISON_LESS_EQUAL; dd.StencilEnable = FALSE;
        g_dev.dev->CreateDepthStencilState(&dd, &s_dss[rev ? 1 : 0]);
    }
    if (!s_dss[rev ? 1 : 0]) return nullptr;
    *ps = s_ps; *dss = s_dss[rev ? 1 : 0];
    s_dirty = true; s_draws++;
    return s_rtv;
}

// SRV para los shaders que la leen (nullptr si esta apagada o todavia no hay textura limpia)
ID3D11ShaderResourceView* MouthMaskSRV() {
    if (!MouthMaskOn()) return nullptr;
    std::lock_guard<std::mutex> lk(s_mx);
    return s_srv;
}

// Present (contexto inmediato): limpiar para el cuadro siguiente y registro cada 30 s
void MouthMaskPresent() {
    if (s_dirty.exchange(false) && g_dev.imm) {
        std::lock_guard<std::mutex> lk(s_mx);
        if (s_rtv) { const float z[4] = { 0, 0, 0, 0 }; g_dev.imm->ClearRenderTargetView(s_rtv, z); }
    }
    double t = Now();
    if (t - s_logT > 30.0) {
        s_logT = t;
        int d = s_draws.exchange(0), f = s_fail.exchange(0);
        if (d || f) Log("mascara de la boca (30 s): %d dibujos de la boca marcados, %d sin poder marcar (textura %ux%u)", d, f, s_w, s_h);
    }
}

void MouthMaskReloadShaders() {
    std::lock_guard<std::mutex> lk(s_mx);
    Retire(s_ps); s_ps = nullptr; s_psTried = false;
}

}   // namespace fx
