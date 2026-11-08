// PES21 FX (08/11) - cache unico de estados D3D11 del mod (profundidad, mezcla, rasterizado) por DESCRIPCION.
// Antes cada modulo tenia su anillo de 4 (grass.cpp) o un cache que se rehacia si cambiaba la descripcion (cejas en facegen.cpp,
// pasto del editor): con mas de 4 estados distintos del juego por cuadro se creaba y se soltaba un estado por dibujo. Soltar la
// ultima referencia de un estado que una lista diferida del juego todavia nombra es lo que vio el volcado del cierre
// (CCommandList::LUCDeferDeletion con un objeto de estado ya liberado). Aca:
//  - la clave es la descripcion (bytes), nunca un puntero del juego (los punteros se reutilizan);
//  - el cache tiene una referencia propia de cada estado y NUNCA la suelta mientras el dispositivo viva (D3D11 permite 4096
//    estados distintos por tipo; el mod usa unas decenas);
//  - devuelve el puntero SIN sumar referencia (prestado): el que llama no lo suelta.
// Si cambia el dispositivo, lo viejo se abandona (no se suelta: puede seguir nombrado en listas viejas).
#include "core.h"
#include <d3d11.h>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <cstring>

namespace fx {

namespace {
template<class Desc, class Obj> struct Cache {
    struct E { Desc d; Obj* o; };
    std::unordered_map<uint64_t, std::vector<E>> map; ID3D11Device* dev = nullptr; size_t n = 0;
};
std::mutex s_mx;
Cache<D3D11_DEPTH_STENCIL_DESC, ID3D11DepthStencilState> s_dss;
Cache<D3D11_BLEND_DESC, ID3D11BlendState> s_bl;
Cache<D3D11_RASTERIZER_DESC, ID3D11RasterizerState> s_rs;
std::atomic<int> s_created{0};

uint64_t Hash(const void* p, size_t n) {   // FNV-1a
    const unsigned char* b = (const unsigned char*)p; uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}
template<class Desc, class Obj, class Make> Obj* Get(Cache<Desc, Obj>& c, const Desc& d, Make make) {
    ID3D11Device* dev = g_dev.dev; if (!dev) return nullptr;
    std::lock_guard<std::mutex> lk(s_mx);
    if (c.dev != dev) { c.map.clear(); c.n = 0; c.dev = dev; }   // dispositivo nuevo: lo viejo se abandona sin soltar
    const uint64_t h = Hash(&d, sizeof d);
    auto& v = c.map[h];
    for (auto& e : v) if (!memcmp(&e.d, &d, sizeof d)) return e.o;
    Obj* o = nullptr;
    if (FAILED(make(dev, &d, &o)) || !o) return nullptr;
    v.push_back({ d, o }); c.n++; s_created++;
    return o;
}
}  // namespace

// las descripciones se copian enteras y los huecos de relleno se ponen en 0 para que la comparacion por bytes sea estable
ID3D11DepthStencilState* CachedDSS(const D3D11_DEPTH_STENCIL_DESC& in) {
    D3D11_DEPTH_STENCIL_DESC d; memset(&d, 0, sizeof d);   // (sin relleno en DEPTH_STENCILOP_DESC; el de despues de las mascaras queda en 0)
    d.DepthEnable = in.DepthEnable; d.DepthWriteMask = in.DepthWriteMask; d.DepthFunc = in.DepthFunc; d.StencilEnable = in.StencilEnable;
    d.StencilReadMask = in.StencilReadMask; d.StencilWriteMask = in.StencilWriteMask; d.FrontFace = in.FrontFace; d.BackFace = in.BackFace;
    return Get(s_dss, d, [](ID3D11Device* dv, const D3D11_DEPTH_STENCIL_DESC* p, ID3D11DepthStencilState** o) { return dv->CreateDepthStencilState(p, o); });
}
ID3D11BlendState* CachedBlend(const D3D11_BLEND_DESC& in) {
    D3D11_BLEND_DESC d; memset(&d, 0, sizeof d);
    d.AlphaToCoverageEnable = in.AlphaToCoverageEnable; d.IndependentBlendEnable = in.IndependentBlendEnable;
    for (int i = 0; i < 8; i++) { auto& o = d.RenderTarget[i]; const auto& r = in.RenderTarget[i];   // campo por campo: el relleno queda en 0
        o.BlendEnable = r.BlendEnable; o.SrcBlend = r.SrcBlend; o.DestBlend = r.DestBlend; o.BlendOp = r.BlendOp;
        o.SrcBlendAlpha = r.SrcBlendAlpha; o.DestBlendAlpha = r.DestBlendAlpha; o.BlendOpAlpha = r.BlendOpAlpha; o.RenderTargetWriteMask = r.RenderTargetWriteMask; }
    return Get(s_bl, d, [](ID3D11Device* dv, const D3D11_BLEND_DESC* p, ID3D11BlendState** o) { return dv->CreateBlendState(p, o); });
}
ID3D11RasterizerState* CachedRS(const D3D11_RASTERIZER_DESC& in) {
    D3D11_RASTERIZER_DESC d; memset(&d, 0, sizeof d); d = in;
    return Get(s_rs, d, [](ID3D11Device* dv, const D3D11_RASTERIZER_DESC* p, ID3D11RasterizerState** o) { return dv->CreateRasterizerState(p, o); });
}
// log cada 30 s (hooks Present): cuantos estados distintos tiene el cache (tiene que quedar quieto despues de unos segundos)
void StateCachePresent() {
    static double next = 0; double t = Now(); if (t < next) return; next = t + 30.0;
    int nw = s_created.exchange(0); if (!nw) return;
    size_t a, b, c; { std::lock_guard<std::mutex> lk(s_mx); a = s_dss.n; b = s_bl.n; c = s_rs.n; }
    Log("estados del mod (cache por descripcion): %d nuevos en 30 s; total profundidad %zu, mezcla %zu, rasterizado %zu (no se sueltan)", nw, a, b, c);
}

}  // namespace fx
