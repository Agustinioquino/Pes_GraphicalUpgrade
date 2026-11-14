// PES21 FX (08/11) - VIGIA DE LIBERACIONES DE ESTADOS D3D11
// Cierre 08/11 11:19 UTC (volcado completo PES2021.exe.36188.dmp, menu previo al partido cargando a Messi): CCommandList::LUCDeferDeletion
// al soltar una lista del juego encuentra en su vector de referenciados un CUseCountedObject de 0x140 bytes (LFH, el bucket de los
// objetos de estado: blend / depth-stencil / rasterizer / sampler) ya liberado y reutilizado. Para saber QUIEN solto ese objeto:
// se engancha el Release (vtable[2]) de los cuatro tipos de estado y cada Release que deja la cuenta en 0 se anota en un anillo
// en memoria (g_relRing, con firma "PES21FX_RELRING!" para encontrarlo en el volcado completo): puntero publico, vtable,
// direccion de retorno (quien llamo), hilo y hora. Ademas cada 30 s el log resume cuantos llegaron a 0 desde el mod / el juego /
// otros modulos, y anota (hasta 40) los sitios del mod que sueltan la ultima referencia de un estado (direccion = d3d11 del mod
// + offset, para buscar la funcion en el .map). No cambia ningun comportamiento: solo mira. [PES21FX] vigia_estados = 1 (0 = no engancha).
#include "core.h"
#include <MinHook.h>
#include <windows.h>
#include <atomic>
#include <intrin.h>

namespace fx {

struct RelRec { void* obj; void* vtbl; void* ret; unsigned tid; unsigned ms; };
struct RelRing { char magic[16]; volatile long next; unsigned cap; RelRec r[16384]; };
RelRing g_relRing = { { 'P','E','S','2','1','F','X','_','R','E','L','R','I','N','G','!' }, 0, 16384, {} };

typedef ULONG(STDMETHODCALLTYPE* RelFn)(IUnknown*);
static RelFn o_rel[4] = {};
static void* s_relTarget[4] = {};
static std::atomic<long long> s_zeroMod{0}, s_zeroExe{0}, s_zeroOther{0};
static uintptr_t s_modLo = 0, s_modHi = 0;
static const uintptr_t kExeLo = 0x140000000ull, kExeHi = 0x146000000ull;
static void* s_modSites[40] = {}; static std::atomic<int> s_modSiteN{0};
// (08/11 h) vtables publicas de los 4 tipos de estado: el Release enganchado es el mismo para todos los objetos D3D11 (ICF de
// CContainedObject::Release), asi que se separan los estados del resto (texturas, vistas, buffers, listas) por su vtable
static void* s_stVt[4] = {};
static std::atomic<long long> s_stMod[4], s_stExe{0}, s_stOther{0};

static inline void Note(IUnknown* p, void* vt, void* ret) {
    long i = InterlockedIncrement(&g_relRing.next) - 1;
    RelRec& e = g_relRing.r[(unsigned)i % 16384u];
    e.obj = p; e.vtbl = vt; e.ret = ret; e.tid = GetCurrentThreadId(); e.ms = GetTickCount();
    uintptr_t r = (uintptr_t)ret;
    int kind = -1; for (int k = 0; k < 4; k++) if (vt && vt == s_stVt[k]) { kind = k; break; }
    const bool fromMod = r >= s_modLo && r < s_modHi;
    if (kind >= 0) { if (fromMod) s_stMod[kind]++; else if (r >= kExeLo && r < kExeHi) s_stExe++; else s_stOther++; }
    if (fromMod) {
        s_zeroMod++;
        if (kind < 0) return;   // sitios del log: solo estados
        int n = s_modSiteN.load(std::memory_order_relaxed);
        for (int k = 0; k < n && k < 40; k++) if (s_modSites[k] == ret) return;
        if (n < 40 && s_modSiteN.compare_exchange_strong(n, n + 1)) {
            s_modSites[n] = ret;
            static const char* kN[4] = { "blend", "profundidad", "rasterizador", "sampler" };
            Log("vigia estados: el mod solto la ultima referencia de un estado (%s) en d3d11.dll(mod)+0x%llx (objeto %p) [sitio %d]",
                kN[kind], (unsigned long long)(r - s_modLo), (void*)p, n + 1);
        }
    } else if (r >= kExeLo && r < kExeHi) s_zeroExe++;
    else s_zeroOther++;
}
template<int K> static ULONG STDMETHODCALLTYPE hk_rel(IUnknown* p) {
    void* vt = p ? *(void**)p : nullptr;
    void* ret = _ReturnAddress();
    ULONG n = o_rel[K](p);
    if (n == 0) Note(p, vt, ret);
    return n;
}
static void* const kHk[4] = { (void*)&hk_rel<0>, (void*)&hk_rel<1>, (void*)&hk_rel<2>, (void*)&hk_rel<3> };

static void** VT(void* obj) { return *(void***)obj; }
static bool s_tried = false;
void RelWatchPresent() {
    static double next = 0;
    if (s_tried) {
        double t = Now();
        if (s_relTarget[0] && t > next) { next = t + 30.0;
            long long b = s_stMod[0].exchange(0), d = s_stMod[1].exchange(0), rs = s_stMod[2].exchange(0), sm = s_stMod[3].exchange(0);
            Log("vigia estados (30 s): ESTADOS a 0 referencias: desde el mod %lld (blend %lld, profundidad %lld, rasterizador %lld, sampler %lld), desde el juego %lld, otros %lld | todos los objetos D3D11: mod %lld, juego %lld, otros %lld (anillo %ld)",
                b + d + rs + sm, b, d, rs, sm, s_stExe.exchange(0), s_stOther.exchange(0), s_zeroMod.exchange(0), s_zeroExe.exchange(0), s_zeroOther.exchange(0), g_relRing.next); }
        return;
    }
    s_tried = true;
    if (!g_cfg.relWatch) { Log("vigia estados: apagado (vigia_estados = 0)"); return; }
    if (!g_dev.dev) { s_tried = false; return; }
    HMODULE me = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&RelWatchPresent, &me);
    if (me) { auto dos = (const IMAGE_DOS_HEADER*)me; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)me + dos->e_lfanew);
        s_modLo = (uintptr_t)me; s_modHi = s_modLo + nt->OptionalHeader.SizeOfImage; }
    // un objeto de cada tipo para leer su vtable (descripciones raras para no coincidir con estados del juego)
    IUnknown* o[4] = {};
    { D3D11_BLEND_DESC d = {}; d.RenderTarget[0].SrcBlend = d.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; d.RenderTarget[0].DestBlend = d.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
      d.RenderTarget[0].BlendOp = d.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; d.RenderTarget[0].RenderTargetWriteMask = 0x5;
      ID3D11BlendState* b = nullptr; g_dev.dev->CreateBlendState(&d, &b); o[0] = b; }
    { D3D11_DEPTH_STENCIL_DESC d = {}; d.DepthEnable = TRUE; d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; d.DepthFunc = D3D11_COMPARISON_NOT_EQUAL;
      d.FrontFace.StencilFailOp = d.FrontFace.StencilDepthFailOp = d.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP; d.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS; d.BackFace = d.FrontFace;
      ID3D11DepthStencilState* s = nullptr; g_dev.dev->CreateDepthStencilState(&d, &s); o[1] = s; }
    { D3D11_RASTERIZER_DESC d = {}; d.FillMode = D3D11_FILL_WIREFRAME; d.CullMode = D3D11_CULL_FRONT; d.DepthBias = 7; d.DepthClipEnable = TRUE;
      ID3D11RasterizerState* r = nullptr; g_dev.dev->CreateRasterizerState(&d, &r); o[2] = r; }
    { D3D11_SAMPLER_DESC d = {}; d.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; d.AddressU = d.AddressV = d.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR_ONCE; d.MipLODBias = 3.25f; d.MaxLOD = 7.0f; d.ComparisonFunc = D3D11_COMPARISON_NEVER;
      ID3D11SamplerState* s = nullptr; g_dev.dev->CreateSamplerState(&d, &s); o[3] = s; }
    static const char* kName[4] = { "blend", "profundidad", "rasterizador", "sampler" };
    int hooked = 0; char tgt[200] = ""; size_t tl = 0;
    for (int k = 0; k < 4; k++) {
        if (!o[k]) continue;
        s_stVt[k] = VT(o[k]);
        void* f = VT(o[k])[2];
        bool dup = false; for (int j = 0; j < k; j++) if (s_relTarget[j] == f) dup = true;
        if (!dup && MH_CreateHook(f, kHk[k], (void**)&o_rel[k]) == MH_OK && MH_EnableHook(f) == MH_OK) { s_relTarget[k] = f; hooked++; }
        tl += snprintf(tgt + tl, sizeof(tgt) - tl, "%s%s %p%s", k ? ", " : "", kName[k], f, dup ? " (=)" : "");
        if (tl >= sizeof(tgt)) tl = sizeof(tgt) - 1;
    }
    for (auto* x : o) if (x) x->Release();
    if (!s_relTarget[0]) for (int k = 1; k < 4; k++) if (s_relTarget[k]) { s_relTarget[0] = s_relTarget[k]; break; }
    Log("vigia estados: Release de estados enganchado en %d funcion(es) [%s]; anillo g_relRing %p (16384 registros, firma PES21FX_RELRING!)", hooked, tgt, (void*)&g_relRing);
}

}  // namespace fx
