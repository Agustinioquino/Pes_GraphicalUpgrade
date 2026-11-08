// PES21 FX - "quinta textura" de la cara generada en vivo (18/10).
// Cuando un dibujo con regla `facegen` aparece con una textura de normal (t1) que no vimos, se anota un pedido;
// en el siguiente Present se dibuja (contexto inmediato) un triangulo de pantalla completa con
// shaders\pes_facegen_ps.hlsl leyendo las 4 texturas de esa cara, a dos texturas propias con mips. De ahi en mas,
// cada dibujo de esa cara las recibe en los slots pedidos. Cache por recurso; se vacia al empezar un partido nuevo
// (las texturas de cara del partido anterior se liberan y sus direcciones pueden reutilizarse).
#include "core.h"
#include <d3d11_1.h>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <string>
#include <DDSTextureLoader.h>
#include <ScreenGrab.h>
#include <WICTextureLoader.h>
#include <wincodec.h>
#include <objbase.h>
#include <cstdio>
#include <set>
#include <map>
#include <fstream>

namespace fx {

struct FaceEntry {
    ID3D11ShaderResourceView* a = nullptr; ID3D11ShaderResourceView* b = nullptr; ID3D11ShaderResourceView* c = nullptr;   // c = semillas de cejas
    ID3D11ShaderResourceView* d = nullptr;   // (04/11 d) mapa de la CEJA a resolucion completa: rgb color de sus pelos, a contorno
    ID3D11Buffer* seeds = nullptr; ID3D11UnorderedAccessView* seedsUAV = nullptr; ID3D11ShaderResourceView* seedsSRV = nullptr;
    ID3D11Buffer* args = nullptr; ID3D11UnorderedAccessView* argsUAV = nullptr;   // 18/10: argumentos de DrawInstancedIndirect (cuenta de semillas que escribe la cara)
    bool pending = false; bool failed = false;
    // 18/10 pelo: identidad de la cara (hash del mapa de normales), tipo asignado y sus parametros (b11); mapa propio del usuario
    std::string id, type; ID3D11Buffer* hairCB = nullptr; int hairGen = -1; bool userMap = false; UINT w = 0, h = 0;
};
// cbuffer b11 que leen el shader de cara y los de hebras (6 float4 = 96 bytes)
struct HairCB { float a[4]; float b[4]; float root[4]; float tip[4]; float brow[4]; float misc[4]; float browCol[4]; float browBack[4]; float browX[4]; };   // (04/11 l) browBack = color a contraluz (rgb, w mezcla), browX.x = hebras extra   // (04/11 i) browCol = color de ceja del jugador (rgb, w mezcla)
static std::atomic<int> s_hairGen{0};
void FaceGenHairChanged() { s_hairGen++; }
static float FaceSeed(const std::string& id) {   // 19/10: semilla 0..1 estable por cara (para variar dientes/encias por jugador)
    uint32_t h = 2166136261u; for (unsigned char ch : id) { h ^= ch; h *= 16777619u; } return (h & 0xffffff) / 16777216.0f; }
// (04/11) cejas por jugador (Agustin: "modificar el largo y la cantidad de hebras por jugador"). El "jugador actual" es la UNICA
// cara con hebras en los ultimos ~30 cuadros (editor, presentacion de un jugador). Sus valores (z77 cantidad, w77 largo) se
// aplican en vivo a esa cara; "Guardar cejas de este jugador" los escribe en PES21FX_hair.ini [cejas_cara] id = cantidad, largo.
static std::string BLower(std::string x) { for (auto& ch : x) ch = (char)tolower((unsigned char)ch); return x; }
static std::string BTrim(const std::string& x) { size_t a = x.find_first_not_of(" \t\r\n"), b = x.find_last_not_of(" \t\r\n"); return a == std::string::npos ? std::string() : x.substr(a, b - a + 1); }
static std::mutex s_browMx;
using BrowVals = std::vector<float>;   // (04/11 i) cantidad, largo, mezcla de color, r, g, b; (04/11 l) + mezcla contraluz, r, g, b, hebras extra
static BrowVals BrowDefault() { return { 1.f, 1.f, 0.f, 0.35f, 0.2f, 0.12f, 0.f, 0.55f, 0.3f, 0.15f, 0.f, 1.f, 1.f, 0.f }; }   // (04/11 m) + elevacion, grosor (multiplicadores); (06/11) + distancia a la piel (mm)
static void BrowPad(BrowVals& v) { BrowVals d = BrowDefault(); for (size_t i = v.size(); i < d.size(); i++) v.push_back(d[i]); }
static std::map<std::string, BrowVals> s_browLive;   // lo tocado en el menu en esta sesion (aun sin guardar)
static std::string s_browCur;                                        // jugador actual ("" = ninguno o varios)
static std::set<std::string> s_browSeen;                             // caras con hebras desde el ultimo corte
static bool BrowFaceGet(const std::string& id, BrowVals& v) {
    if (id.empty()) return false;
    { std::lock_guard<std::mutex> lk(s_browMx); auto it = s_browLive.find(id); if (it != s_browLive.end()) { v = it->second; return true; } }
    auto it = g_cfg.hair.browFace.find(BLower(id)); if (it == g_cfg.hair.browFace.end() || it->second.size() < 6) return false;
    v = it->second; BrowPad(v); return true;
}
static HairCB MakeHairCB(const HairType& t, bool userMap, const std::string& id = "") {
    HairCB cb{};
    cb.a[0] = t.largo; cb.a[1] = t.rizo; cb.a[2] = t.rigidez; cb.a[3] = t.peso;
    cb.b[0] = t.grosor; cb.b[1] = t.densidad; cb.b[2] = t.brillo; cb.b[3] = t.flequillo;
    for (int i = 0; i < 3; i++) { cb.root[i] = t.raiz[i] < 0 ? 0 : t.raiz[i]; cb.tip[i] = t.punta[i] < 0 ? 0 : t.punta[i]; }
    cb.root[3] = t.raiz[0] >= 0 ? 1.f : 0.f; cb.tip[3] = t.punta[0] >= 0 ? 1.f : 0.f;
    cb.brow[0] = t.cejaGrosor; cb.brow[1] = t.cejaDens; cb.brow[2] = t.cejaElev; cb.brow[3] = t.cejaLargo;
    { BrowVals v;   // (04/11) cejas por jugador: lo guardado en [cejas_cara] o lo que se esta tocando en el menu
      if (BrowFaceGet(id, v)) { BrowPad(v); cb.brow[1] *= v[0]; cb.brow[3] *= v[1]; cb.browCol[0] = v[3]; cb.browCol[1] = v[4]; cb.browCol[2] = v[5]; cb.browCol[3] = v[2];
        cb.browBack[0] = v[7]; cb.browBack[1] = v[8]; cb.browBack[2] = v[9]; cb.browBack[3] = v[6]; cb.browX[0] = v[10]; cb.brow[2] *= v[11] > 0.f ? v[11] : 1.f; cb.brow[0] *= v[12] > 0.f ? v[12] : 1.f; cb.browX[1] = v[13]; } }   // 0 = fila ausente en el ini: como todos
    cb.misc[0] = 1.f; cb.misc[1] = userMap ? 1.f : 0.f; cb.misc[2] = FaceSeed(id); cb.misc[3] = 0;
    return cb;
}
// identidad estable de una cara: hash FNV-1a de un mip chico (<= 64 px) de su mapa de normales (copia a staging, contexto inmediato)
std::string HashFaceTexture(ID3D11DeviceContext* c, ID3D11Resource* res) {   // (04/11) tambien caras_hd.cpp (pelo)
    D3D11_RESOURCE_DIMENSION dim; res->GetType(&dim); if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return "";
    ID3D11Texture2D* tex = (ID3D11Texture2D*)res; D3D11_TEXTURE2D_DESC d; tex->GetDesc(&d);
    UINT mip = 0; UINT w = d.Width, h = d.Height;
    while ((w > 64 || h > 64) && mip + 1 < d.MipLevels) { mip++; w = (w + 1) / 2; h = (h + 1) / 2; }
    D3D11_TEXTURE2D_DESC sd = d; sd.Width = w; sd.Height = h; sd.MipLevels = 1; sd.ArraySize = 1; sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
    ID3D11Texture2D* st = nullptr; if (FAILED(g_dev.dev->CreateTexture2D(&sd, nullptr, &st))) return "";
    c->CopySubresourceRegion(st, 0, 0, 0, 0, res, D3D11CalcSubresource(mip, 0, d.MipLevels), nullptr);
    D3D11_MAPPED_SUBRESOURCE m{}; std::string out;
    if (SUCCEEDED(c->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
        bool bc = (d.Format >= DXGI_FORMAT_BC1_TYPELESS && d.Format <= DXGI_FORMAT_BC5_SNORM) || (d.Format >= DXGI_FORMAT_BC6H_TYPELESS && d.Format <= DXGI_FORMAT_BC7_UNORM_SRGB);
        bool bc8 = d.Format <= DXGI_FORMAT_BC1_UNORM_SRGB || (d.Format >= DXGI_FORMAT_BC4_TYPELESS && d.Format <= DXGI_FORMAT_BC4_SNORM);
        UINT rows = bc ? (h + 3) / 4 : h;
        UINT rowBytes = bc ? ((w + 3) / 4) * (bc8 ? 8 : 16) : w * 4;
        if (rowBytes > m.RowPitch) rowBytes = m.RowPitch;
        uint64_t hsh = 1469598103934665603ull;
        for (UINT y = 0; y < rows; y++) { const uint8_t* row = (const uint8_t*)m.pData + (size_t)y * m.RowPitch; for (UINT x = 0; x < rowBytes; x++) { hsh ^= row[x]; hsh *= 1099511628211ull; } }
        c->Unmap(st, 0);
        char buf[32]; snprintf(buf, sizeof(buf), "%012llx", (unsigned long long)(hsh & 0xffffffffffffull)); out = buf;
    }
    st->Release();
    return out;
}
static std::wstring Widen(const std::string& a) { return std::wstring(a.begin(), a.end()); }
static void EnsureCom() { static thread_local bool done = false; if (!done) { done = true; CoInitializeEx(nullptr, COINIT_MULTITHREADED); } }   // WIC (png): COM por hilo
void EnsureComInit() { EnsureCom(); }
static bool FileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
// mapa de pelo propio del usuario: PES21FX\hair\<id>.png (o .dds)
static ID3D11ShaderResourceView* LoadUserHairMap(const std::string& id) {
    ID3D11ShaderResourceView* srv = nullptr;
    std::wstring base = g_cfg.baseDir + L"hair\\" + Widen(id);
    if (FileExists(base + L".png")) { EnsureCom(); if (FAILED(DirectX::CreateWICTextureFromFileEx(g_dev.dev, g_dev.imm, (base + L".png").c_str(), 0, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, DirectX::WIC_LOADER_IGNORE_SRGB, nullptr, &srv))) srv = nullptr; }
    if (!srv && FileExists(base + L".dds")) { if (FAILED(DirectX::CreateDDSTextureFromFile(g_dev.dev, (base + L".dds").c_str(), nullptr, &srv))) srv = nullptr; }
    return srv;
}
static const UINT kSeedCount = 65536;   // grilla 256x256 de la quinta textura
static const UINT kSeedBytes = 64;
// 18/10: estadisticas para el log (cada 10 s): consultas, aciertos, sin entradas, pendientes, dibujos de hebras
static std::atomic<int> s_stLook{0}, s_stHit{0}, s_stNoIn{0}, s_stPend{0}, s_stFull{0}, s_stDraw{0};
static double s_stNext = 0;
std::atomic<int> g_stFaceDraws{0}, g_stFaceRule{0}, g_stFaceT23{0}, g_stFaceT26{0}, g_stFaceCtxDef{0};   // 18/10 c: dibujos con el PS de cara, con regla, con t23/t26 enlazados al final, en contexto diferido
struct FaceReq { ID3D11Resource* key; ID3D11ShaderResourceView* in[4]; UINT w, h; };

static std::unordered_map<ID3D11Resource*, FaceEntry> s_cache;
static std::vector<FaceReq> s_reqs;
static std::mutex s_mx;
static ID3D11VertexShader* s_vs = nullptr;
static ID3D11PixelShader* s_ps = nullptr;
static ID3D11SamplerState* s_samp = nullptr;
static ID3D11PixelShader* s_bmPS = nullptr;   // (04/11 d) mapa de la ceja (pes_browmap_ps.hlsl)
static bool s_shadersTried = false, s_shadersOk = false;
static int s_generated = 0;
int FaceGenCount() { return s_generated; }

static void ReleaseEntry(FaceEntry& e) {
    if (e.a) e.a->Release(); if (e.b) e.b->Release(); if (e.c) e.c->Release(); if (e.d) e.d->Release(); e.a = e.b = e.c = e.d = nullptr;
    if (e.seedsUAV) e.seedsUAV->Release(); if (e.seedsSRV) e.seedsSRV->Release(); if (e.seeds) e.seeds->Release();
    e.seedsUAV = nullptr; e.seedsSRV = nullptr; e.seeds = nullptr;
    if (e.argsUAV) e.argsUAV->Release(); if (e.args) e.args->Release(); e.argsUAV = nullptr; e.args = nullptr;
    if (e.hairCB) e.hairCB->Release(); e.hairCB = nullptr;
}
// hebras de ceja: shaders propios (se compilan en Present, no en los hilos del juego)
// (04/11) v2: GS sobre la malla de la cara (pes_brow_a_gs = VS faab87ee, pes_brow_b_gs = VS 455eb97b) + el mismo PS de la v1
static ID3D11VertexShader* s_browVS = nullptr; static ID3D11PixelShader* s_browPS = nullptr; static bool s_browTried = false; static ID3D11RasterizerState* s_browRS = nullptr;
static ID3D11GeometryShader* s_browGS[2] = {};
static ID3D11GeometryShader* s_atGS[2] = {};   // (07/11 c) hebras en el atlas de sombra de las cabezas (pes_browat_*_gs.hlsl, PES_ATLAS)
static ID3D11GeometryShader* s_lashGS = nullptr;
// (08/11 v, Agustin: "como se plasman los pelos de hebras en juegos modernos" / "todos los puntos") PASADA PROPIA DEL PELO: el GS de las
// hebras, al dibujarse en el G-buffer, deja tambien sus vertices en una salida de flujo (stream output); despues de la composicion
// diferida se vuelven a dibujar desde ahi (DrawAuto) con su propio sombreado de pelo (pes_hairfwd_ps: Marschner con la tangente,
// cobertura real con mezcla aditiva, sombra del estadio + atlas + grosor real). w382 = fuerza (0 = apagada)
static ID3D11GeometryShader* s_browGSso[2] = {};
static ID3D11Buffer* s_hfSO = nullptr; static ID3D11VertexShader* s_hfVS = nullptr; static ID3D11PixelShader* s_hfPS = nullptr;
static ID3D11InputLayout* s_hfIL = nullptr; static ID3D11Buffer* s_hfCB = nullptr; static ID3D11BlendState* s_hfBlend = nullptr;
static ID3D11DepthStencilState* s_hfDSS = nullptr; static ID3D11SamplerState* s_hfSamp = nullptr;
// (08/11 v, Agustin: "mejorar la sombra que plasma el pelo") la SOMBRA DE CONTACTO de las cejas tambien pasa a despues de la luz: antes
// multiplicaba el albedo en el G-buffer (oscurecia tambien la luz del cielo y en un estadio a la sombra igual marcaba la ceja sobre la
// piel). Ahora el GS de sombra deja la hebra proyectada en otra salida de flujo y tras la composicion se multiplica la imagen solo donde
// el sol llega de verdad (mascara del juego t14) y segun cuanta luz directa hay (pes_hairsh_ps)
static ID3D11GeometryShader* s_shGSso[2] = {}; static ID3D11Buffer* s_hfSO2 = nullptr; static ID3D11PixelShader* s_hsPS = nullptr; static ID3D11BlendState* s_hsBlend = nullptr;
static std::atomic<int> s_hfSoFrame2{-1}, s_hfStreamed2{0};
static std::atomic<int> s_hfFrame{0}, s_hfSoFrame{-1}, s_hfStreamed{0}, s_hfDraws{0};
static std::atomic<unsigned> s_compW{0}, s_compH{0};
static constexpr UINT kHfStride = 108;          // 27 floats: igual a GSOut de pes_brow_gs.hlsli
static constexpr UINT kHfBytes = 160u << 20;    // (08/11 y) 64 -> 160 MB: con las hebras v3 (16 segmentos de cerca) cada hebra ocupa ~3x; si se llena, el resto no entra en la pasada (sigue en el G-buffer)
static const D3D11_SO_DECLARATION_ENTRY kHfSO[] = {
    { 0, "SV_POSITION", 0, 0, 4, 0 }, { 0, "TEXCOORD", 0, 0, 3, 0 }, { 0, "TEXCOORD", 1, 0, 3, 0 }, { 0, "TEXCOORD", 2, 0, 3, 0 },
    { 0, "TEXCOORD", 3, 0, 2, 0 }, { 0, "TEXCOORD", 4, 0, 3, 0 }, { 0, "TEXCOORD", 5, 0, 3, 0 }, { 0, "TEXCOORD", 6, 0, 3, 0 },
    { 0, "TEXCOORD", 7, 0, 3, 0 } };
static std::atomic<double> s_hfLastDraw{-10.0};
// (08/11 y, Agustin: "creo que estamos planteando mal la geometria de las hebras" -> "si, rehacer por pasos") HEBRAS v3 (x416): el GS
// de la cara deja solo la raiz de cada hebra (pes_brow_roots_*_gs, 8 float4) en s_v3Roots; despues un GS por hebra (pes_strand_gs) la
// arma con hasta 16 segmentos (DrawAuto sobre las raices). Mismo PS, misma pasada propia (s_v3GSso escribe en s_hfSO)
static ID3D11GeometryShader* s_v3RootGS[2] = {}; static ID3D11GeometryShader* s_v3GS = nullptr; static ID3D11GeometryShader* s_v3GSso = nullptr;
static ID3D11VertexShader* s_v3VS = nullptr; static ID3D11InputLayout* s_v3IL = nullptr; static ID3D11Buffer* s_v3Roots = nullptr;
static constexpr UINT kV3Stride = 128;            // 8 float4 (RootOut de pes_brow_gs.hlsli)
static constexpr UINT kV3Bytes = 8u << 20;        // 65536 raices por dibujo de cara
static const D3D11_SO_DECLARATION_ENTRY kV3SO[] = {
    { 0, "TEXCOORD", 0, 0, 4, 0 }, { 0, "TEXCOORD", 1, 0, 4, 0 }, { 0, "TEXCOORD", 2, 0, 4, 0 }, { 0, "TEXCOORD", 3, 0, 4, 0 },
    { 0, "TEXCOORD", 4, 0, 4, 0 }, { 0, "TEXCOORD", 5, 0, 4, 0 }, { 0, "TEXCOORD", 6, 0, 4, 0 }, { 0, "TEXCOORD", 7, 0, 4, 0 } };
// (08/11 y) la pasada propia dibujo hace menos de medio segundo: recien ahi las cejas le dejan el brillo y la sombra (si no, en el
// editor sin composicion 619b salian negras y planas: sin brillo propio ni sombra de contacto)
bool HairFwdActive() { return Now() - s_hfLastDraw.load() < 0.5; }
static bool HairFwdWanted() { return g_cfg.live[382][3] > 0.001f && g_cfg.live[74][0] > 0.5f; }
static void EnsureHairFwd() {
    if (!g_dev.dev) return;
    if (!s_hfSO) { D3D11_BUFFER_DESC bd{}; bd.ByteWidth = kHfBytes; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER;
        if (FAILED(g_dev.dev->CreateBuffer(&bd, nullptr, &s_hfSO))) s_hfSO = nullptr; }
    if (!s_hfCB) { D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 32; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(g_dev.dev->CreateBuffer(&bd, nullptr, &s_hfCB))) s_hfCB = nullptr; }
    if (!s_hfBlend) { D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        g_dev.dev->CreateBlendState(&bd, &s_hfBlend); }
    if (!s_hfDSS) { D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dd.DepthFunc = D3D11_COMPARISON_ALWAYS; dd.StencilEnable = FALSE;
        g_dev.dev->CreateDepthStencilState(&dd, &s_hfDSS); }
    if (!s_hfSamp) { D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        g_dev.dev->CreateSamplerState(&sd, &s_hfSamp); }
    if (!s_hfVS) if (ID3DBlob* b = CompileNamedShader(L"pes_hairfwd_vs.hlsl", "vs_5_0")) {
        g_dev.dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_hfVS);
        const D3D11_INPUT_ELEMENT_DESC il[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32G32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 4, DXGI_FORMAT_R32G32B32_FLOAT, 0, 60, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 5, DXGI_FORMAT_R32G32B32_FLOAT, 0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 6, DXGI_FORMAT_R32G32B32_FLOAT, 0, 84, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 7, DXGI_FORMAT_R32G32B32_FLOAT, 0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        if (FAILED(g_dev.dev->CreateInputLayout(il, 9, b->GetBufferPointer(), b->GetBufferSize(), &s_hfIL))) s_hfIL = nullptr;
        b->Release(); }
    if (!s_hfPS) if (ID3DBlob* b = CompileNamedShader(L"pes_hairfwd_ps.hlsl", "ps_5_0")) { g_dev.dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_hfPS); b->Release(); }
    if (!s_hsPS) if (ID3DBlob* b = CompileNamedShader(L"pes_hairsh_ps.hlsl", "ps_5_0")) { g_dev.dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s_hsPS); b->Release(); }
    if (!s_hfSO2) { D3D11_BUFFER_DESC bd{}; bd.ByteWidth = kHfBytes / 2; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER;
        if (FAILED(g_dev.dev->CreateBuffer(&bd, nullptr, &s_hfSO2))) s_hfSO2 = nullptr; }
    if (!s_hsBlend) { D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].BlendEnable = TRUE;   // multiplicativa: destino x color del PS
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO; bd.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        g_dev.dev->CreateBlendState(&bd, &s_hsBlend); }
}
// (04/11 k, Agustin: "no solo brillo, luz real") SOMBRA DE CONTACTO de cada pelo sobre la piel de abajo, con la direccion real
// del sol / luz principal: antes de las hebras se repite el GS en modo sombra (la hebra proyectada sobre el plano de la piel
// siguiendo la luz) con un PS que multiplica el albedo del G-buffer (solo RT0, mezcla multiplicativa, sin escribir profundidad)
static ID3D11GeometryShader* s_shGS[3] = {};   // cabeza, cara animada, pestanas
static ID3D11PixelShader* s_shPS = nullptr; static ID3D11BlendState* s_shBlend = nullptr;   // (04/11 i) pestanas con hebras (pes_lash_gs.hlsl, VS 66917213)
static void EnsureBrowShaders() {
    if (s_browTried || !g_dev.dev) return;
    s_browTried = true;
    ID3DBlob* pb = CompileNamedShader(L"pes_brow_ps.hlsl", "ps_5_0");
    if (pb) { g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_browPS); pb->Release(); }
    const wchar_t* gsf[2] = { L"pes_brow_a_gs.hlsl", L"pes_brow_b_gs.hlsl" };
    for (int i = 0; i < 2; i++) if (ID3DBlob* gb = CompileNamedShader(gsf[i], "gs_5_0")) { g_dev.dev->CreateGeometryShader(gb->GetBufferPointer(), gb->GetBufferSize(), nullptr, &s_browGS[i]);
        UINT st = kHfStride;   // (08/11 v) la misma hebra con salida de flujo para la pasada propia del pelo (se rasteriza igual, flujo 0)
        HRESULT hso = g_dev.dev->CreateGeometryShaderWithStreamOutput(gb->GetBufferPointer(), gb->GetBufferSize(), kHfSO, 9, &st, 1, 0, nullptr, &s_browGSso[i]);
        if (FAILED(hso)) { s_browGSso[i] = nullptr; Log("cejas: pasada propia del pelo: salida de flujo GS %d fallo (0x%08X)", i, (unsigned)hso); }
        gb->Release(); }
    EnsureHairFwd();
    {   // (08/11 y) hebras v3
        const wchar_t* rf[2] = { L"pes_brow_roots_a_gs.hlsl", L"pes_brow_roots_b_gs.hlsl" };
        for (int i = 0; i < 2; i++) if (ID3DBlob* gb = CompileNamedShader(rf[i], "gs_5_0")) { UINT st = kV3Stride;
            if (FAILED(g_dev.dev->CreateGeometryShaderWithStreamOutput(gb->GetBufferPointer(), gb->GetBufferSize(), kV3SO, 8, &st, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &s_v3RootGS[i]))) s_v3RootGS[i] = nullptr;
            gb->Release(); }
        if (ID3DBlob* gb = CompileNamedShader(L"pes_strand_gs.hlsl", "gs_5_0")) {
            g_dev.dev->CreateGeometryShader(gb->GetBufferPointer(), gb->GetBufferSize(), nullptr, &s_v3GS);
            UINT st = kHfStride; if (FAILED(g_dev.dev->CreateGeometryShaderWithStreamOutput(gb->GetBufferPointer(), gb->GetBufferSize(), kHfSO, 9, &st, 1, 0, nullptr, &s_v3GSso))) s_v3GSso = nullptr;
            gb->Release(); }
        if (ID3DBlob* vb = CompileNamedShader(L"pes_strand_vs.hlsl", "vs_5_0")) {
            g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_v3VS);
            D3D11_INPUT_ELEMENT_DESC ie[8];
            for (UINT k = 0; k < 8; k++) ie[k] = { "TEXCOORD", k, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16 * k, D3D11_INPUT_PER_VERTEX_DATA, 0 };
            g_dev.dev->CreateInputLayout(ie, 8, vb->GetBufferPointer(), vb->GetBufferSize(), &s_v3IL);
            vb->Release(); }
        if (!s_v3Roots) { D3D11_BUFFER_DESC bd{}; bd.ByteWidth = kV3Bytes; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER;
            if (FAILED(g_dev.dev->CreateBuffer(&bd, nullptr, &s_v3Roots))) s_v3Roots = nullptr; }
        Log("cejas: hebras v3 (x416): raices GS %s %s, GS por hebra %s (flujo %s), VS %s, IL %s, buffer %s", s_v3RootGS[0] ? "si" : "NO", s_v3RootGS[1] ? "si" : "NO",
            s_v3GS ? "si" : "NO", s_v3GSso ? "si" : "NO", s_v3VS ? "si" : "NO", s_v3IL ? "si" : "NO", s_v3Roots ? "si" : "NO");
    }
    Log("cejas: pasada propia del pelo (w382): flujo %s %s, buffer %s, VS %s, PS %s", s_browGSso[0] ? "si" : "NO", s_browGSso[1] ? "si" : "NO", s_hfSO ? "si" : "NO", (s_hfVS && s_hfIL) ? "si" : "NO", s_hfPS ? "si" : "NO");
    { const wchar_t* shf[3] = { L"pes_browsh_a_gs.hlsl", L"pes_browsh_b_gs.hlsl", L"pes_lashsh_gs.hlsl" };
      for (int i = 0; i < 3; i++) if (ID3DBlob* gb = CompileNamedShader(shf[i], "gs_5_0")) { g_dev.dev->CreateGeometryShader(gb->GetBufferPointer(), gb->GetBufferSize(), nullptr, &s_shGS[i]);
          if (i < 2) { UINT st = kHfStride; if (FAILED(g_dev.dev->CreateGeometryShaderWithStreamOutput(gb->GetBufferPointer(), gb->GetBufferSize(), kHfSO, 9, &st, 1, 0, nullptr, &s_shGSso[i]))) s_shGSso[i] = nullptr; }   // (08/11 v)
          gb->Release(); }
      if (ID3DBlob* sb = CompileNamedShader(L"pes_browsh_ps.hlsl", "ps_5_0")) { g_dev.dev->CreatePixelShader(sb->GetBufferPointer(), sb->GetBufferSize(), nullptr, &s_shPS); sb->Release(); }
      if (!s_shBlend) { D3D11_BLEND_DESC bd{}; bd.IndependentBlendEnable = TRUE;
          bd.RenderTarget[0].BlendEnable = TRUE; bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO; bd.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
          bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
          bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
          for (int i = 1; i < 8; i++) bd.RenderTarget[i].RenderTargetWriteMask = 0;
          g_dev.dev->CreateBlendState(&bd, &s_shBlend); }
      Log("hebras: sombra de contacto sobre la piel: GS %s %s %s, PS %s", s_shGS[0] ? "si" : "NO", s_shGS[1] ? "si" : "NO", s_shGS[2] ? "si" : "NO", s_shPS ? "si" : "NO"); }
    { const wchar_t* atf[2] = { L"pes_browat_a_gs.hlsl", L"pes_browat_b_gs.hlsl" };
      for (int i = 0; i < 2; i++) if (ID3DBlob* gb = CompileNamedShader(atf[i], "gs_5_0")) { g_dev.dev->CreateGeometryShader(gb->GetBufferPointer(), gb->GetBufferSize(), nullptr, &s_atGS[i]); gb->Release(); }
      Log("cejas: hebras en el atlas de sombra de las cabezas: GS %s %s", s_atGS[0] ? "si" : "NO", s_atGS[1] ? "si" : "NO"); }
    if (ID3DBlob* lb = CompileNamedShader(L"pes_lash_gs.hlsl", "gs_5_0")) { g_dev.dev->CreateGeometryShader(lb->GetBufferPointer(), lb->GetBufferSize(), nullptr, &s_lashGS); lb->Release(); }
    Log("pestanas: hebras (sobre la tira de pestanas): GS %s", s_lashGS ? "si" : "NO (ver errores)");
    if (!s_browRS) { D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; g_dev.dev->CreateRasterizerState(&rd, &s_browRS); }
    Log("cejas: hebras v2 (pegadas a la malla de la cara): PS %s, GS cabeza %s, GS cara animada %s", s_browPS ? "si" : "NO", s_browGS[0] ? "si" : "NO", s_browGS[1] ? "si" : "NO (ver errores)");
}
bool BrowReady() { return s_browPS && (s_browGS[0] || s_browGS[1]); }

void FaceGenClear() {
    CarasHdClearKeys();   // (04/11 b) pelo en alta: las direcciones de las texturas se reutilizan en el partido nuevo
    std::lock_guard<std::mutex> lk(s_mx);
    for (auto& kv : s_cache) ReleaseEntry(kv.second);
    s_cache.clear();
    for (auto& r : s_reqs) for (int i = 0; i < 4; i++) if (r.in[i]) r.in[i]->Release();
    s_reqs.clear();
}
void FaceGenReloadShaders() {
    std::lock_guard<std::mutex> lk(s_mx);
    if (s_vs) { s_vs->Release(); s_vs = nullptr; }
    if (s_ps) { s_ps->Release(); s_ps = nullptr; }
    if (s_bmPS) { s_bmPS->Release(); s_bmPS = nullptr; }
    s_shadersTried = false; s_shadersOk = false;
    if (s_browVS) { s_browVS->Release(); s_browVS = nullptr; }
    if (s_browPS) { s_browPS->Release(); s_browPS = nullptr; }
    for (auto*& g : s_browGS) if (g) { g->Release(); g = nullptr; }
    for (auto*& g : s_browGSso) if (g) { g->Release(); g = nullptr; }
    for (auto*& g : s_shGSso) if (g) { g->Release(); g = nullptr; }
    if (s_hsPS) { s_hsPS->Release(); s_hsPS = nullptr; } s_hfStreamed2 = 0;
    if (s_hfVS) { s_hfVS->Release(); s_hfVS = nullptr; } if (s_hfPS) { s_hfPS->Release(); s_hfPS = nullptr; } if (s_hfIL) { s_hfIL->Release(); s_hfIL = nullptr; }
    s_hfStreamed = 0;
    for (auto*& g : s_atGS) if (g) { g->Release(); g = nullptr; }
    if (s_lashGS) { s_lashGS->Release(); s_lashGS = nullptr; }
    for (auto*& g : s_shGS) if (g) { g->Release(); g = nullptr; }
    if (s_shPS) { s_shPS->Release(); s_shPS = nullptr; }
    s_browTried = false;
    // lo ya generado se rehace con el shader nuevo
    for (auto& kv : s_cache) ReleaseEntry(kv.second);
    s_cache.clear();
}

// llamado antes de un dibujo con regla facegen (cualquier contexto): devuelve las dos texturas si ya existen
bool FaceGenLookup(ID3D11DeviceContext* c, ID3D11ShaderResourceView** outA, ID3D11ShaderResourceView** outB, FaceGenExtra* ex) {
    *outA = *outB = nullptr; if (ex) { ex->seedMap = nullptr; ex->browMap = nullptr; ex->seedsUAV = nullptr; ex->seedsSRV = nullptr; ex->hairCB = nullptr; ex->key = nullptr; ex->t0 = nullptr; ex->id.clear(); }
    ID3D11ShaderResourceView* in[4] = {};
    c->PSGetShaderResources(0, 4, in);
    s_stLook++;
    if (!in[1]) { s_stNoIn++; for (int i = 0; i < 4; i++) if (in[i]) in[i]->Release(); return false; }
    ID3D11Resource* key = nullptr; in[1]->GetResource(&key);
    if (ex) { ex->key = key; if (in[0]) { ID3D11Resource* r0 = nullptr; in[0]->GetResource(&r0); if (r0) { ex->t0 = r0; r0->Release(); } } }   // (20/10) identidad para el LOD (punteros, sin retener)
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        auto it = s_cache.find(key);
        if (it != s_cache.end()) {
            if (ex) ex->id = it->second.id;   // (04/11) tambien para la boca por jugador (bocajug.cpp)
            if (it->second.a) { s_stHit++; *outA = it->second.a; *outB = it->second.b; found = true;
                FaceEntry& fe = it->second;
                if (ex) { ex->seedMap = fe.c; ex->browMap = fe.d; ex->seedsUAV = fe.seedsUAV; ex->seedsSRV = fe.seedsSRV; ex->args = fe.args; ex->argsUAV = fe.argsUAV; ex->hairCB = fe.hairCB; } }
            else s_stPend++;
        } else if (in[0] && in[2] && in[3] && s_cache.size() < 96) {
            D3D11_RESOURCE_DIMENSION dim; key->GetType(&dim);
            if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
                D3D11_TEXTURE2D_DESC td; ((ID3D11Texture2D*)key)->GetDesc(&td);
                FaceReq r; r.key = key; r.w = td.Width > 2048 ? 2048 : td.Width; r.h = td.Height > 2048 ? 2048 : td.Height;
                for (int i = 0; i < 4; i++) { r.in[i] = in[i]; in[i]->AddRef(); }
                s_reqs.push_back(r);
                s_cache[key].pending = true;
            }
        }
    }
    key->Release();
    for (int i = 0; i < 4; i++) if (in[i]) in[i]->Release();
    return found;
}

static bool EnsureShaders() {
    if (s_shadersTried) return s_shadersOk;
    s_shadersTried = true;
    ID3DBlob* vb = CompileNamedShader(L"pes_facegen_vs.hlsl", "vs_5_0");
    ID3DBlob* pb = CompileNamedShader(L"pes_facegen_ps.hlsl", "ps_5_0");
    if (ID3DBlob* bb = CompileNamedShader(L"pes_browmap_ps.hlsl", "ps_5_0")) { if (g_dev.dev) g_dev.dev->CreatePixelShader(bb->GetBufferPointer(), bb->GetBufferSize(), nullptr, &s_bmPS); bb->Release(); }
    if (vb && pb && g_dev.dev) {
        if (SUCCEEDED(g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_vs)) &&
            SUCCEEDED(g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_ps))) s_shadersOk = true;
    }
    if (vb) vb->Release(); if (pb) pb->Release();
    if (!s_samp && g_dev.dev) {
        D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        g_dev.dev->CreateSamplerState(&sd, &s_samp);
    }
    if (!s_shadersOk) Log("facegen: no se pudieron compilar los shaders del generador (ver errores arriba)");
    else Log("facegen: generador compilado (shaders\\pes_facegen_ps.hlsl)");
    return s_shadersOk;
}

static bool MakeTarget(UINT w, UINT h, ID3D11Texture2D** tex, ID3D11RenderTargetView** rtv, ID3D11ShaderResourceView** srv) {
    D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 0; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE; d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(g_dev.dev->CreateTexture2D(&d, nullptr, tex))) return false;
    if (FAILED(g_dev.dev->CreateRenderTargetView(*tex, nullptr, rtv))) return false;
    if (FAILED(g_dev.dev->CreateShaderResourceView(*tex, nullptr, srv))) return false;
    return true;
}

// en Present, contexto inmediato: genera hasta 2 caras por cuadro
static std::atomic<int> s_browFaces{0};   // caras con hebras dibujadas en este cuadro (x77 = maximo por pantalla)
void FaceGenPresent() {
    s_browFaces.store(0);
    s_hfFrame.fetch_add(1);
    { static double hfNext = 0; double t = Now(); if (t > hfNext) { hfNext = t + 10.0; int d = s_hfDraws.exchange(0); if (d > 0) Log("cejas: pasada propia del pelo: %d dibujos en 10 s", d); } }
    { double t = Now(); if (t > s_stNext) { s_stNext = t + 10.0;
        if (s_stLook.load() > 0 || g_stFaceDraws.load() > 0) Log("facegen: 10 s: %d consultas, %d con texturas, %d pendientes, %d sin t1, %d en cache, %d dibujos de hebras | PS cara: %d dibujos, %d con regla, %d con t23, %d con t26, %d diferidos", s_stLook.load(), s_stHit.load(), s_stPend.load(), s_stNoIn.load(), (int)s_cache.size(), s_stDraw.load(), g_stFaceDraws.load(), g_stFaceRule.load(), g_stFaceT23.load(), g_stFaceT26.load(), g_stFaceCtxDef.load());
        s_stLook = 0; s_stHit = 0; s_stPend = 0; s_stNoIn = 0; s_stDraw = 0; g_stFaceDraws = 0; g_stFaceRule = 0; g_stFaceT23 = 0; g_stFaceT26 = 0; g_stFaceCtxDef = 0; } }
    if (!g_dev.dev || !g_dev.imm) return;
    // PES21FX_hair.ini recargado: tipo y parametros nuevos para cada cara ya generada (aca, en el hilo de Present)
    { static int seenGen = 0; int g = s_hairGen.load();
      if (g != seenGen) { seenGen = g; std::lock_guard<std::mutex> lk(s_mx);
        for (auto& kv : s_cache) { FaceEntry& fe = kv.second; if (!fe.hairCB) continue;
            const HairType* ht = HairTypeFor(fe.id, &fe.type);
            if (ht) { HairCB cb = MakeHairCB(*ht, fe.userMap, fe.id); g_dev.imm->UpdateSubresource(fe.hairCB, 0, nullptr, &cb, 0, 0); fe.hairGen = g; } } } }
    // (04/11) cejas por jugador: cada 30 cuadros, si una sola cara tuvo hebras es el "jugador actual"; al cambiar, los sliders
    // z77 / w77 toman sus valores; si el usuario los mueve, se aplican en vivo a su cbuffer
    // (04/11 i, Agustin: "un slider para el color", las cejas de Messi son del color de la barba) + fila 367 = color de ceja del
    // jugador: x mezcla (0 = color pintado de la cara, 1 = el elegido), y / z / w = rojo / verde / azul
    { static int frames = 0; static BrowVals last;
      auto cur6 = []() { return BrowVals{ g_cfg.live[77][2], g_cfg.live[77][3], g_cfg.live[367][0], g_cfg.live[367][1], g_cfg.live[367][2], g_cfg.live[367][3],
          g_cfg.live[369][0], g_cfg.live[369][1], g_cfg.live[369][2], g_cfg.live[369][3], g_cfg.live[370][0], g_cfg.live[370][1], g_cfg.live[370][2], g_cfg.live[370][3] }; };   // (06/11) w370 distancia a la piel   // (04/11 m) y370 elevacion, z370 grosor   // (04/11 l) 369 contraluz, x370 hebras extra
      std::string cur;
      { std::lock_guard<std::mutex> lk(s_browMx);
        if (++frames >= 30) { frames = 0; std::string nc = s_browSeen.size() == 1 ? *s_browSeen.begin() : std::string(); s_browSeen.clear();
            if (nc != s_browCur) { s_browCur = nc;
                if (!nc.empty()) { BrowVals v = BrowDefault(); v[3] = g_cfg.live[367][1]; v[4] = g_cfg.live[367][2]; v[5] = g_cfg.live[367][3]; v[7] = g_cfg.live[369][1]; v[8] = g_cfg.live[369][2]; v[9] = g_cfg.live[369][3];   // sin guardar: sin mezcla, el tono queda el ultimo elegido
                    auto it = s_browLive.find(nc);
                    if (it != s_browLive.end()) v = it->second;
                    else { auto jt = g_cfg.hair.browFace.find(BLower(nc)); if (jt != g_cfg.hair.browFace.end() && jt->second.size() >= 6) v = jt->second; }
                    BrowPad(v);
                    g_cfg.live[77][2] = v[0]; g_cfg.live[77][3] = v[1]; g_cfg.live[367][0] = v[2]; g_cfg.live[367][1] = v[3]; g_cfg.live[367][2] = v[4]; g_cfg.live[367][3] = v[5];
                    g_cfg.live[369][0] = v[6]; g_cfg.live[369][1] = v[7]; g_cfg.live[369][2] = v[8]; g_cfg.live[369][3] = v[9]; g_cfg.live[370][0] = v[10]; g_cfg.live[370][1] = v[11]; g_cfg.live[370][2] = v[12]; g_cfg.live[370][3] = v[13];
                    Log("cejas: jugador actual = cara %s (cantidad %.2f, largo %.2f, color %.2f: %.2f %.2f %.2f, contraluz %.2f: %.2f %.2f %.2f, extra %.0f, elevacion %.2f, grosor %.2f, distancia %.2f mm)", nc.c_str(), v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13]); }
                last = cur6(); } }
        cur = s_browCur; }
      BrowVals now = cur6();
      if (!cur.empty() && now != last && now[0] > 0.f && now[1] > 0.f) {
          last = now;
          { std::lock_guard<std::mutex> lk(s_browMx); s_browLive[cur] = now; }
          std::lock_guard<std::mutex> lk(s_mx);
          for (auto& kv : s_cache) { FaceEntry& fe = kv.second; if (fe.id != cur || !fe.hairCB) continue;
              if (const HairType* ht = HairTypeFor(fe.id, &fe.type)) { HairCB cb = MakeHairCB(*ht, fe.userMap, fe.id); g_dev.imm->UpdateSubresource(fe.hairCB, 0, nullptr, &cb, 0, 0); } }
      } }
    std::vector<FaceReq> todo;
    {
        std::lock_guard<std::mutex> lk(s_mx);
        if (s_reqs.empty()) return;
        int n = (int)s_reqs.size() < 2 ? (int)s_reqs.size() : 2;
        todo.assign(s_reqs.begin(), s_reqs.begin() + n);
        s_reqs.erase(s_reqs.begin(), s_reqs.begin() + n);
    }
    ID3D11DeviceContext* c = g_dev.imm;
    bool ok = EnsureShaders();
    EnsureBrowShaders();
    // --- guardar estado ---
    ID3D11RenderTargetView* oldRT[8] = {}; ID3D11DepthStencilView* oldDS = nullptr;
    c->OMGetRenderTargets(8, oldRT, &oldDS);
    UINT nvp = 1; D3D11_VIEWPORT oldVP[1] = {}; c->RSGetViewports(&nvp, oldVP); if (nvp == 0) nvp = 1;
    ID3D11VertexShader* oldVS = nullptr; ID3D11PixelShader* oldPS = nullptr; ID3D11GeometryShader* oldGS = nullptr; ID3D11HullShader* oldHS = nullptr; ID3D11DomainShader* oldDSh = nullptr;
    c->VSGetShader(&oldVS, nullptr, nullptr); c->PSGetShader(&oldPS, nullptr, nullptr); c->GSGetShader(&oldGS, nullptr, nullptr); c->HSGetShader(&oldHS, nullptr, nullptr); c->DSGetShader(&oldDSh, nullptr, nullptr);
    ID3D11ShaderResourceView* oldSRV[4] = {}; c->PSGetShaderResources(0, 4, oldSRV);
    ID3D11SamplerState* oldSamp = nullptr; c->PSGetSamplers(0, 1, &oldSamp);
    ID3D11BlendState* oldBS = nullptr; FLOAT bf[4]; UINT sm; c->OMGetBlendState(&oldBS, bf, &sm);
    ID3D11DepthStencilState* oldDSS = nullptr; UINT sref; c->OMGetDepthStencilState(&oldDSS, &sref);
    ID3D11RasterizerState* oldRS = nullptr; c->RSGetState(&oldRS);
    ID3D11InputLayout* oldIL = nullptr; c->IAGetInputLayout(&oldIL);
    D3D11_PRIMITIVE_TOPOLOGY oldTopo; c->IAGetPrimitiveTopology(&oldTopo);

    for (auto& r : todo) {
        FaceEntry e;
        if (ok) {
            ID3D11Texture2D *ta = nullptr, *tb = nullptr, *tc = nullptr, *td = nullptr; ID3D11RenderTargetView *ra = nullptr, *rb = nullptr, *rc = nullptr, *rd = nullptr;
            if (MakeTarget(r.w, r.h, &ta, &ra, &e.a) && MakeTarget(r.w, r.h, &tb, &rb, &e.b) && MakeTarget(r.w, r.h, &tc, &rc, &e.c)) {
                // buffer de semillas de cejas (lo escribe el shader de cara en cada dibujo, lo lee el dibujo de hebras)
                D3D11_BUFFER_DESC bd{}; bd.ByteWidth = kSeedCount * kSeedBytes; bd.Usage = D3D11_USAGE_DEFAULT;
                bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = kSeedBytes;
                if (SUCCEEDED(g_dev.dev->CreateBuffer(&bd, nullptr, &e.seeds))) {
                    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_UNKNOWN; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = kSeedCount;
                    g_dev.dev->CreateUnorderedAccessView(e.seeds, &ud, &e.seedsUAV);
                    D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = DXGI_FORMAT_UNKNOWN; sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Buffer.NumElements = kSeedCount;
                    g_dev.dev->CreateShaderResourceView(e.seeds, &sd, &e.seedsSRV);
                }
                // argumentos del dibujo indirecto: {vertices por instancia, instancias, 0, 0}; la cara los escribe (contador de semillas)
                { D3D11_BUFFER_DESC ad{}; ad.ByteWidth = 16; ad.Usage = D3D11_USAGE_DEFAULT; ad.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
                  ad.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
                  if (SUCCEEDED(g_dev.dev->CreateBuffer(&ad, nullptr, &e.args))) {
                      D3D11_UNORDERED_ACCESS_VIEW_DESC au{}; au.Format = DXGI_FORMAT_R32_TYPELESS; au.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; au.Buffer.NumElements = 4; au.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
                      g_dev.dev->CreateUnorderedAccessView(e.args, &au, &e.argsUAV);
                  } }
                ID3D11RenderTargetView* rts[3] = { ra, rb, rc };
                c->OMSetRenderTargets(3, rts, nullptr);
                D3D11_VIEWPORT vp{ 0, 0, (float)r.w, (float)r.h, 0, 1 }; c->RSSetViewports(1, &vp);
                c->VSSetShader(s_vs, nullptr, 0); c->PSSetShader(s_ps, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
                c->PSSetShaderResources(0, 4, r.in); c->PSSetSamplers(0, 1, &s_samp);
                c->OMSetBlendState(nullptr, nullptr, 0xffffffff); c->OMSetDepthStencilState(nullptr, 0); c->RSSetState(nullptr);
                c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                c->Draw(3, 0);
                ID3D11RenderTargetView* none[3] = {}; c->OMSetRenderTargets(3, none, nullptr);
                c->GenerateMips(e.a); c->GenerateMips(e.b); c->GenerateMips(e.c);
                // (04/11 d) mapa de la ceja (color + contorno) a la mitad de la resolucion: otra pasada con pes_browmap_ps
                if (s_bmPS && MakeTarget(std::max(r.w / 2, 64u), std::max(r.h / 2, 64u), &td, &rd, &e.d)) {
                    c->OMSetRenderTargets(1, &rd, nullptr);
                    D3D11_VIEWPORT vp2{ 0, 0, (float)std::max(r.w / 2, 64u), (float)std::max(r.h / 2, 64u), 0, 1 }; c->RSSetViewports(1, &vp2);
                    c->PSSetShader(s_bmPS, nullptr, 0);
                    c->Draw(3, 0);
                    ID3D11RenderTargetView* none1 = nullptr; c->OMSetRenderTargets(1, &none1, nullptr);
                    c->GenerateMips(e.d);
                }
                s_generated++;
                e.w = r.w; e.h = r.h;
                // 18/10 pelo: identidad de la cara, tipo de pelo (PES21FX_hair.ini) y mapa de pelo propio (PES21FX\hair\<id>.dds)
                { ID3D11Resource* res = nullptr; r.in[1]->GetResource(&res);
                  if (res) { e.id = HashFaceTexture(c, res); res->Release(); } }
                if (!e.id.empty()) CarasHdNewFace(c, e.id, r.in);   // (04/11) caras en alta: volcar originales / buscar las reescaladas
                if (!e.id.empty()) {
                    ID3D11ShaderResourceView* usrv = LoadUserHairMap(e.id);
                    if (usrv) { if (e.c) e.c->Release(); e.c = usrv; e.userMap = true; }
                }
                const HairType* ht = HairTypeFor(e.id, &e.type);
                if (ht) {
                    HairCB cb = MakeHairCB(*ht, e.userMap, e.id);
                    D3D11_BUFFER_DESC cd{}; cd.ByteWidth = sizeof(HairCB); cd.Usage = D3D11_USAGE_DEFAULT; cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                    D3D11_SUBRESOURCE_DATA init{ &cb, 0, 0 };
                    g_dev.dev->CreateBuffer(&cd, &init, &e.hairCB); e.hairGen = s_hairGen.load();
                }
                Log("pelo: cara %s (%ux%u) -> tipo '%s'%s", e.id.empty() ? "?" : e.id.c_str(), r.w, r.h, e.type.c_str(), e.userMap ? ", mapa propio hair\\<id>.png" : "");
            } else { ReleaseEntry(e); e.failed = true; }
            if (ra) ra->Release(); if (rb) rb->Release(); if (rc) rc->Release(); if (rd) rd->Release(); if (ta) ta->Release(); if (tb) tb->Release(); if (tc) tc->Release(); if (td) td->Release();
        } else e.failed = true;
        e.pending = false;
        { std::lock_guard<std::mutex> lk(s_mx); auto it = s_cache.find(r.key); if (it != s_cache.end()) { ReleaseEntry(it->second); it->second = e; } else ReleaseEntry(e); }
        for (int i = 0; i < 4; i++) if (r.in[i]) r.in[i]->Release();
    }
    // --- restaurar estado ---
    c->OMSetRenderTargets(8, oldRT, oldDS); for (auto* v : oldRT) if (v) v->Release(); if (oldDS) oldDS->Release();
    c->RSSetViewports(nvp, oldVP);
    c->VSSetShader(oldVS, nullptr, 0); c->PSSetShader(oldPS, nullptr, 0); c->GSSetShader(oldGS, nullptr, 0); c->HSSetShader(oldHS, nullptr, 0); c->DSSetShader(oldDSh, nullptr, 0);
    if (oldVS) oldVS->Release(); if (oldPS) oldPS->Release(); if (oldGS) oldGS->Release(); if (oldHS) oldHS->Release(); if (oldDSh) oldDSh->Release();
    c->PSSetShaderResources(0, 4, oldSRV); for (auto* v : oldSRV) if (v) v->Release();
    c->PSSetSamplers(0, 1, &oldSamp); if (oldSamp) oldSamp->Release();
    c->OMSetBlendState(oldBS, bf, sm); if (oldBS) oldBS->Release();
    c->OMSetDepthStencilState(oldDSS, sref); if (oldDSS) oldDSS->Release();
    c->RSSetState(oldRS); if (oldRS) oldRS->Release();
    c->IASetInputLayout(oldIL); if (oldIL) oldIL->Release();
    c->IASetPrimitiveTopology(oldTopo);
}


std::string FaceGenOrigin(ID3D11ShaderResourceView* v) {
    if (!v) return "";
    std::lock_guard<std::mutex> lk(s_mx);
    for (auto& kv : s_cache) { FaceEntry& e = kv.second;
        if (v == e.a) return "generada: quinta textura A (cavidad/curvatura/grosor/vello) cara " + e.id;
        if (v == e.b) return "generada: quinta textura B (poros/brillo/convexidad) cara " + e.id;
        if (v == e.d) return "mapa de la ceja (color y contorno), cara " + e.id;
        if (v == e.c) return std::string(e.userMap ? "mapa de pelo PROPIO hair\\" : "mapa de pelo automatico, cara ") + e.id + (e.userMap ? ".png" : "");
        if (v == e.seedsSRV) return "semillas de cejas (buffer) cara " + e.id; }
    return "";
}

// 18/10 pelo: "Volcar mapas de pelo": escribe hair\auto\<id>.png (el mapa de pelo automatico de cada cara generada, listo para
// editar y copiar a hair\<id>.png) y hair\auto\lista.txt (id, tamano, tipo). Se llama desde el menu (Present, contexto inmediato).
int FaceGenExportHair(std::string& status) {
    if (!g_dev.imm) { status = "Sin dispositivo"; return 0; }
    std::wstring dir = g_cfg.baseDir + L"hair"; CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\auto"; CreateDirectoryW(dir.c_str(), nullptr);
    int n = 0, fail = 0; std::string lista = "; id de cara ; tamano ; tipo asignado ; mapa propio\n";
    std::lock_guard<std::mutex> lk(s_mx);
    for (auto& kv : s_cache) {
        FaceEntry& e = kv.second; if (!e.c || e.id.empty()) continue;
        ID3D11Resource* res = nullptr; e.c->GetResource(&res); if (!res) continue;
        std::wstring path = dir + L"\\" + Widen(e.id) + L".png";
        EnsureCom();
        HRESULT hr = DirectX::SaveWICTextureToFile(g_dev.imm, res, GUID_ContainerFormatPng, path.c_str(), nullptr, nullptr, false);
        res->Release();
        if (SUCCEEDED(hr)) n++; else fail++;
        char line[256]; snprintf(line, sizeof(line), "%s ; %ux%u ; %s ; %s\n", e.id.c_str(), e.w, e.h, e.type.c_str(), e.userMap ? "si" : "no"); lista += line;
    }
    { FILE* f = _wfopen((dir + L"\\lista.txt").c_str(), L"wb"); if (f) { fwrite(lista.data(), 1, lista.size(), f); fclose(f); } }
    char buf[160]; snprintf(buf, sizeof(buf), "%d mapas de pelo en PES21FX\\hair\\auto (%d fallaron)", n, fail); status = buf;
    Log("pelo: %s", buf);
    return n;
}

// --- (04/11) cejas con hebras v2: se dibujan con el MISMO dibujo de la cara, apenas termina y antes de que la DLL devuelva
// su estado (asi siguen enlazados los huesos, los gestos esculpidos, el cbuffer de expresiones y los render targets).
// La DLL (hooks.cpp BrowStrands) llama Begin, repite el DrawIndexed original (sin pasar por los hooks) y llama End.
// v1 (18/10) usaba semillas que el PS de la cara escribia cada cuadro en un UAV: solo nacian donde la cara tenia pixeles
// (aparecian y desaparecian con la distancia) y en el editor salian cintas gigantes; por eso estaba solo en partido.
static constexpr uint64_t kVSHead = 0xfaab87ee4ef8b783ULL, kVSFaceAnim = 0x455eb97bc2c3a958ULL, kVSLash = 0x66917213728b4dc9ULL;
ID3D11GeometryShader* BrowAtlasGS(uint64_t vsHash) { return vsHash == kVSLash ? nullptr : s_atGS[vsHash == kVSFaceAnim ? 1 : 0]; }   // (07/11 c) hebras en el atlas de sombra
struct BrowSaved {
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr; ID3D11GeometryShader* gs = nullptr; ID3D11HullShader* hs = nullptr; ID3D11DomainShader* ds = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED; ID3D11RasterizerState* rs = nullptr;
    ID3D11DepthStencilState* dss = nullptr; UINT ref = 0;
    ID3D11Buffer* gsCB[12] = {}; ID3D11ShaderResourceView* gsT0 = nullptr; ID3D11ShaderResourceView* gsT26 = nullptr; ID3D11ShaderResourceView* gsT27 = nullptr; ID3D11ShaderResourceView* gsT120 = nullptr;
    ID3D11SamplerState* gsS0 = nullptr; ID3D11Buffer* psCB11 = nullptr; ID3D11ShaderResourceView* psT120 = nullptr;
    ID3D11ShaderResourceView* gsT23[2] = {}; ID3D11ShaderResourceView* gsT121 = nullptr;   // (04/11 e) relieve + aviso de teselacion
    ID3D11BlendState* blend = nullptr; float blendF[4] = {}; UINT blendM = 0xffffffff;   // (04/11 k) pasada de sombra
    bool shadowFwd = false, shadowSO = false;   // (08/11 v) sombra de contacto a la salida de flujo (se aplica tras la composicion)
    ID3D11DepthStencilState* strandDSS = nullptr; ID3D11DepthStencilState* shadowDSS = nullptr; ID3D11GeometryShader* strandGS = nullptr; ID3D11GeometryShader* shadowGS = nullptr;   // no se liberan (cache / globales)
};
static thread_local BrowSaved t_bs;
bool BrowWanted(const FaceGenExtra& ex, uint64_t vsHash) {
    if (!ex.seedMap || g_cfg.live[74][0] < 0.5f) return false;
    if (vsHash != kVSHead && vsHash != kVSFaceAnim) return false;
    // y77 = 0: como la v1, solo en partido (y24 lo pone PollMatch al ver el pasto)
    if (g_cfg.live[77][1] < 0.5f && g_cfg.live[93][3] < 0.5f) return false;
    return true;
}
// (04/11 i, Agustin: "puedes darle hebras a las pestanas?") la tira de pestanas: VS 66917213 (reemplazo de expresiones) + PS del
// juego f0d74b9c; fila 368.x prende. Se repite con pes_lash_gs y el PS de las hebras (BrowStrandsBegin con vsHash = kVSLash)
bool LashWanted(uint64_t vsHash, uint64_t psHash) {
    if (vsHash != kVSLash || psHash != 0xf0d74b9c069f3ee3ULL || g_cfg.live[368][0] < 0.5f) return false;
    return s_lashGS && s_browPS;   // los shaders se compilan en Present (EnsureBrowShaders)
}
bool BrowStrandsBegin(ID3D11DeviceContext* c, const FaceGenExtra* ex, uint64_t vsHash) {
    const bool lash = vsHash == kVSLash;
    if (!s_browPS || !s_browRS) return false;
    if (!lash && (!ex || !ex->seedMap || !ex->browMap)) return false;
    ID3D11GeometryShader* gs = lash ? s_lashGS : s_browGS[vsHash == kVSFaceAnim ? 1 : 0];
    if (!lash && HairFwdWanted() && s_browGSso[vsHash == kVSFaceAnim ? 1 : 0] && s_hfSO) gs = s_browGSso[vsHash == kVSFaceAnim ? 1 : 0];   // (08/11 v)
    if (!gs) return false;
    int maxFaces = (int)g_cfg.live[77][0]; if (maxFaces < 1) maxFaces = 22;
    if (!lash && s_browFaces.fetch_add(1) >= maxFaces) return false;   // tope de caras por cuadro (rendimiento)
    BrowSaved& s = t_bs; s = BrowSaved();
    c->VSGetShader(&s.vs, nullptr, nullptr); c->PSGetShader(&s.ps, nullptr, nullptr); c->GSGetShader(&s.gs, nullptr, nullptr); c->HSGetShader(&s.hs, nullptr, nullptr); c->DSGetShader(&s.ds, nullptr, nullptr);
    c->IAGetPrimitiveTopology(&s.topo); c->RSGetState(&s.rs); c->OMGetDepthStencilState(&s.dss, &s.ref);
    c->GSGetConstantBuffers(0, 12, s.gsCB); c->GSGetShaderResources(0, 1, &s.gsT0); c->GSGetShaderResources(26, 1, &s.gsT26); c->GSGetShaderResources(27, 1, &s.gsT27); c->GSGetShaderResources(120, 1, &s.gsT120);
    c->GSGetSamplers(0, 1, &s.gsS0); c->PSGetConstantBuffers(11, 1, &s.psCB11); c->PSGetShaderResources(120, 1, &s.psT120);
    c->GSGetShaderResources(23, 2, s.gsT23); c->GSGetShaderResources(121, 1, &s.gsT121);
    // profundidad: la cara se dibuja con "igual" (pre-pasada) y eso descartaria todas las hebras -> "menor o igual"
    // (o "mayor o igual" si el juego usa profundidad invertida), escribiendo profundidad
    D3D11_DEPTH_STENCIL_DESC dd{};
    if (s.dss) s.dss->GetDesc(&dd); else { dd.DepthEnable = TRUE; dd.StencilEnable = FALSE; }
    D3D11_COMPARISON_FUNC origFunc = dd.DepthFunc;
    bool reversed;
    if (origFunc == D3D11_COMPARISON_GREATER || origFunc == D3D11_COMPARISON_GREATER_EQUAL) reversed = true;
    else if (origFunc == D3D11_COMPARISON_LESS || origFunc == D3D11_COMPARISON_LESS_EQUAL) reversed = false;
    else reversed = g_depthClearZero.load() > g_depthClearOne.load();
    dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = reversed ? D3D11_COMPARISON_GREATER_EQUAL : D3D11_COMPARISON_LESS_EQUAL;
    ID3D11DepthStencilState* useDSS = nullptr;
    {   // (08/11) cache por descripcion (statecache.cpp): antes se soltaba y se recreaba cada vez que el stencil del juego cambiaba entre caras
        useDSS = CachedDSS(dd);
        D3D11_DEPTH_STENCIL_DESC dn = dd; dn.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;   // (04/11 k) sombra: prueba sin escribir
        t_bs.shadowDSS = CachedDSS(dn);
    }
    static std::atomic<int> s_logged{0};
    static std::atomic<int> s_lashLogged{0};
    if (lash) { if (s_lashLogged.fetch_add(1) < 2) Log("pestanas: dibujo de hebras sobre la tira (%u indices)", ex ? ex->browCount : 0u); }
    else if (s_logged.fetch_add(1) < 2) Log("cejas v2: primer dibujo de hebras (VS %s, profundidad del juego func %d, %s, cara %s)", vsHash == kVSFaceAnim ? "455eb97b" : "faab87ee", (int)origFunc, reversed ? "invertida" : "normal", ex->id.empty() ? "?" : ex->id.c_str());
    // el VS (reemplazo del mod) queda como esta: sus salidas son la entrada del GS. Teselacion fuera (el GS va despues del VS).
    c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->GSSetShader(gs, nullptr, 0); c->PSSetShader(s_browPS, nullptr, 0);
    ID3D11Buffer* b2 = nullptr; c->VSGetConstantBuffers(2, 1, &b2); c->GSSetConstantBuffers(2, 1, &b2); if (b2) b2->Release();
    if (!lash && ex->hairCB) { c->GSSetConstantBuffers(11, 1, &ex->hairCB); c->PSSetConstantBuffers(11, 1, &ex->hairCB); }
    else if (lash) { ID3D11Buffer* nb = nullptr; c->PSSetConstantBuffers(11, 1, &nb); }   // sin tipo de pelo: valores por defecto del PS
    ID3D11ShaderResourceView* t0 = nullptr; c->PSGetShaderResources(0, 1, &t0); c->GSSetShaderResources(0, 1, &t0); if (t0) t0->Release();
    ID3D11ShaderResourceView* hm = lash ? nullptr : ex->seedMap; c->GSSetShaderResources(26, 1, &hm);
    ID3D11ShaderResourceView* bm = lash ? nullptr : ex->browMap; c->GSSetShaderResources(27, 1, &bm);
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->GSSetShaderResources(120, 1, &prm); c->PSSetShaderResources(120, 1, &prm);
    c->GSSetSamplers(0, 1, &s_samp);
    // (04/11 e, Agustin: "las cejas no salen desde el modelo del rostro o se ven separadas") si este dibujo de la cara esta teselado
    // (hay DS puesto), el GS repite el corrimiento del DS en la raiz: t121 = parametros (solo como aviso "teselado"), t23 / t24 =
    // las texturas del relieve que la teselacion le dio al DS. Sin teselacion t121 queda vacio y la raiz va sobre el triangulo.
    {
        ID3D11ShaderResourceView* rel[2] = {}; ID3D11ShaderResourceView* on = nullptr;
        if (s.ds) { c->DSGetShaderResources(23, 2, rel); on = g_dev.paramSRV; }
        // (04/11 m) teselacion por jugador: los multiplicadores de esta cara (b10 del DS) tambien al GS (gsCB 0..11 se restaura en End)
        { ID3D11Buffer* pb = nullptr; if (s.ds) c->DSGetConstantBuffers(10, 1, &pb); c->GSSetConstantBuffers(10, 1, &pb); if (pb) pb->Release(); }
        c->GSSetShaderResources(23, 2, rel); c->GSSetShaderResources(121, 1, &on);
        for (auto* r : rel) if (r) r->Release();
    }
    c->RSSetState(s_browRS);   // sin culling (cintas vistas de ambos lados)
    if (useDSS) c->OMSetDepthStencilState(useDSS, s.ref);
    s.strandDSS = useDSS; s.strandGS = gs;
    s.shadowGS = s_shGS[lash ? 2 : (vsHash == kVSFaceAnim ? 1 : 0)];
    if (!lash && HairFwdWanted() && HairFwdActive() && s_hfSO2 && s_hsPS && s_shGSso[vsHash == kVSFaceAnim ? 1 : 0]) { s.shadowGS = s_shGSso[vsHash == kVSFaceAnim ? 1 : 0]; s.shadowFwd = true; }   // (08/11 v)
    c->OMGetBlendState(&s.blend, s.blendF, &s.blendM);
    s_stDraw++;
    if (!lash && !ex->id.empty()) { std::lock_guard<std::mutex> lk(s_browMx); s_browSeen.insert(ex->id); }
    return true;
}
// (04/11 k) pasada de sombra de contacto (entre Begin y el dibujo de las hebras): GS en modo sombra, PS multiplicativo, sin
// escribir profundidad; la luz principal (b0 del PS de la cara / pestanas: m_dominantLightDir) tambien va al GS
bool BrowShadowOn(ID3D11DeviceContext* c) {
    BrowSaved& s = t_bs;
    if (!s.shadowGS || !s_shPS || !s_shBlend || !s.shadowDSS) return false;
    ID3D11Buffer* b0 = nullptr; c->PSGetConstantBuffers(0, 1, &b0); if (!b0) return false;
    c->GSSetConstantBuffers(0, 1, &b0); b0->Release();
    c->GSSetShader(s.shadowGS, nullptr, 0); c->PSSetShader(s_shPS, nullptr, 0);
    float bf[4] = { 1, 1, 1, 1 }; c->OMSetBlendState(s_shBlend, bf, 0xffffffff);
    c->OMSetDepthStencilState(s.shadowDSS, s.ref);
    s.shadowSO = false;
    if (s.shadowFwd) {   // (08/11 v) vista principal: solo a la salida de flujo (sin PS: el G-buffer no se toca); si no, como antes
        D3D11_VIEWPORT vp{}; UINT n = 1; c->RSGetViewports(&n, &vp);
        unsigned cw = s_compW.load(), ch = s_compH.load();
        if (n && cw && fabsf(vp.Width - (float)cw) <= 0.03f * cw && fabsf(vp.Height - (float)ch) <= 0.03f * ch) {
            int f = s_hfFrame.load();
            UINT off = (s_hfSoFrame2.exchange(f) != f) ? 0u : (UINT)-1;
            c->SOSetTargets(1, &s_hfSO2, &off);
            c->PSSetShader(nullptr, nullptr, 0);
            s.shadowSO = true; s_hfStreamed2.store(1);
        }
    }
    return true;
}
void BrowShadowOff(ID3D11DeviceContext* c) {
    BrowSaved& s = t_bs;
    if (s.shadowSO) { ID3D11Buffer* nb = nullptr; UINT o = 0; c->SOSetTargets(1, &nb, &o); s.shadowSO = false; }
    c->GSSetShader(s.strandGS, nullptr, 0); c->PSSetShader(s_browPS, nullptr, 0);
    c->OMSetBlendState(s.blend, s.blendF, s.blendM);
    if (s.strandDSS) c->OMSetDepthStencilState(s.strandDSS, s.ref);
}
void BrowStrandsEnd(ID3D11DeviceContext* c) {
    BrowSaved& s = t_bs;
    c->OMSetBlendState(s.blend, s.blendF, s.blendM); if (s.blend) s.blend->Release();
    c->VSSetShader(s.vs, nullptr, 0); c->PSSetShader(s.ps, nullptr, 0); c->GSSetShader(s.gs, nullptr, 0); c->HSSetShader(s.hs, nullptr, 0); c->DSSetShader(s.ds, nullptr, 0);
    c->IASetPrimitiveTopology(s.topo); c->RSSetState(s.rs); c->OMSetDepthStencilState(s.dss, s.ref);
    c->GSSetConstantBuffers(0, 12, s.gsCB); c->GSSetShaderResources(0, 1, &s.gsT0); c->GSSetShaderResources(26, 1, &s.gsT26); c->GSSetShaderResources(27, 1, &s.gsT27); c->GSSetShaderResources(120, 1, &s.gsT120);
    c->GSSetSamplers(0, 1, &s.gsS0); c->PSSetConstantBuffers(11, 1, &s.psCB11); c->PSSetShaderResources(120, 1, &s.psT120);
    c->GSSetShaderResources(23, 2, s.gsT23); c->GSSetShaderResources(121, 1, &s.gsT121);
    for (auto* r : s.gsT23) if (r) r->Release(); if (s.gsT121) s.gsT121->Release();
    if (s.vs) s.vs->Release(); if (s.ps) s.ps->Release(); if (s.gs) s.gs->Release(); if (s.hs) s.hs->Release(); if (s.ds) s.ds->Release();
    if (s.rs) s.rs->Release(); if (s.dss) s.dss->Release();
    for (auto* b : s.gsCB) if (b) b->Release();
    if (s.gsT0) s.gsT0->Release(); if (s.gsT26) s.gsT26->Release(); if (s.gsT27) s.gsT27->Release(); if (s.gsT120) s.gsT120->Release(); if (s.gsS0) s.gsS0->Release();
    if (s.psCB11) s.psCB11->Release(); if (s.psT120) s.psT120->Release();
    s = BrowSaved();
}

// (08/11 y) hebras v3: 1) el dibujo de la cara solo deja las raices (sin rasterizar); 2) DrawAuto de las raices con el GS por hebra
bool BrowV3Roots(ID3D11DeviceContext* c, uint64_t vsHash) {
    BrowSaved& s = t_bs;
    if (g_cfg.live[416][0] < 0.5f || vsHash == kVSLash || !s.strandGS) return false;
    ID3D11GeometryShader* rg = s_v3RootGS[vsHash == kVSFaceAnim ? 1 : 0];
    if (!rg || !s_v3GS || !s_v3VS || !s_v3IL || !s_v3Roots) return false;
    c->GSSetShader(rg, nullptr, 0);
    UINT off = 0; c->SOSetTargets(1, &s_v3Roots, &off);
    { static std::atomic<int> lg{0}; if (lg.fetch_add(1) < 2) Log("cejas: hebras v3: primer dibujo de raices (VS %s)", vsHash == kVSFaceAnim ? "455eb97b" : "faab87ee"); }
    return true;
}
void BrowV3Strands(ID3D11DeviceContext* c) {
    BrowSaved& s = t_bs;
    { ID3D11Buffer* nb = nullptr; UINT o = 0; c->SOSetTargets(1, &nb, &o); }
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL);
    ID3D11Buffer* oVB = nullptr; UINT oStr = 0, oOff = 0; c->IAGetVertexBuffers(0, 1, &oVB, &oStr, &oOff);
    D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11VertexShader* oVS = nullptr; c->VSGetShader(&oVS, nullptr, nullptr);
    ID3D11GeometryShader* keep = s.strandGS;
    c->IASetInputLayout(s_v3IL); UINT st = kV3Stride, of = 0; c->IASetVertexBuffers(0, 1, &s_v3Roots, &st, &of);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    c->VSSetShader(s_v3VS, nullptr, 0);
    c->PSSetShader(s_browPS, nullptr, 0);
    bool so = false;
    if (HairFwdWanted() && s_v3GSso) { s.strandGS = s_v3GSso; so = BrowSOOn(c); }
    c->GSSetShader(so ? s_v3GSso : s_v3GS, nullptr, 0);
    c->DrawAuto();
    if (so) BrowSOOff(c);
    s.strandGS = keep;
    c->IASetInputLayout(oIL); c->IASetVertexBuffers(0, 1, &oVB, &oStr, &oOff); c->IASetPrimitiveTopology(oTopo);
    c->VSSetShader(oVS, nullptr, 0); c->GSSetShader(keep, nullptr, 0);
    if (oIL) oIL->Release(); if (oVB) oVB->Release(); if (oVS) oVS->Release();
    { static std::atomic<int> lg{0}; if (lg.fetch_add(1) < 2) Log("cejas: hebras v3: primer dibujo de hebras (pasada propia %s)", so ? "si" : "no"); }
}
// (08/11 v) salida de flujo de las hebras: solo alrededor del dibujo de las hebras (no de la sombra de contacto ni del atlas) y solo en
// la vista principal (mismo tamano que la composicion del cuadro anterior: no las miniaturas ni los reflejos)
bool BrowSOOn(ID3D11DeviceContext* c) {
    BrowSaved& s = t_bs;
    if (!HairFwdWanted() || !s_hfSO || !s.strandGS || (s.strandGS != s_browGSso[0] && s.strandGS != s_browGSso[1] && s.strandGS != s_v3GSso)) return false;
    D3D11_VIEWPORT vp{}; UINT n = 1; c->RSGetViewports(&n, &vp);
    unsigned cw = s_compW.load(), ch = s_compH.load();
    // (08/11 w) tolerancia del 3 % (el editor puede dibujar la cara con un viewport apenas distinto al de la composicion)
    bool okVp = n && cw && fabsf(vp.Width - (float)cw) <= 0.03f * cw && fabsf(vp.Height - (float)ch) <= 0.03f * ch;
    { static std::atomic<int> lg{0}; if (lg.fetch_add(1) < 3) Log("cejas: pasada propia: dibujo de hebras con viewport %.0fx%.0f, composicion %ux%u -> %s", n ? vp.Width : 0.f, n ? vp.Height : 0.f, cw, ch, okVp ? "a la salida de flujo" : "NO (otra vista)"); }
    if (!okVp) return false;
    int f = s_hfFrame.load();
    UINT off = (s_hfSoFrame.exchange(f) != f) ? 0u : (UINT)-1;   // la primera cara del cuadro vacia el buffer, las demas agregan
    c->SOSetTargets(1, &s_hfSO, &off);
    s_hfStreamed.store(1);
    return true;
}
void BrowSOOff(ID3D11DeviceContext* c) { ID3D11Buffer* nb = nullptr; UINT o = 0; c->SOSetTargets(1, &nb, &o); }
// (08/11 v) PASADA PROPIA DEL PELO: apenas termina la composicion diferida (PS 619b86cb, RT0 = imagen HDR, t11 profundidad, t14 mascara de
// sombra del sol, b2 escena), antes del TAA y del postprocesado
void HairFwdAfterComposition(ID3D11DeviceContext* c) {
    ID3D11RenderTargetView* rt[8] = {}; ID3D11DepthStencilView* dsv = nullptr; c->OMGetRenderTargets(8, rt, &dsv);
    UINT W = 0, H = 0;
    if (rt[0]) { ID3D11Resource* r = nullptr; rt[0]->GetResource(&r);
        if (r) { ID3D11Texture2D* t = nullptr; if (SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); W = d.Width; H = d.Height; t->Release(); } r->Release(); } }
    if (W > 64 && H > 64) { s_compW.store(W); s_compH.store(H); }
    { static std::atomic<int> lg{0}; if (lg.fetch_add(1) < 2) Log("cejas: pasada propia: composicion %ux%u, w382 %.2f, hebras en el flujo %d, VS %d PS %d IL %d", W, H, g_cfg.live[382][3], s_hfStreamed.load(), s_hfVS ? 1 : 0, s_hfPS ? 1 : 0, s_hfIL ? 1 : 0); }
    bool sh2 = s_hfStreamed2.exchange(0) != 0;
    bool st1 = s_hfStreamed.exchange(0) != 0;
    bool go = W > 64 && HairFwdWanted() && (st1 || sh2) && s_hfVS && s_hfPS && s_hfIL && s_hfSO && s_hfCB && s_hfBlend && s_hfDSS && s_hfSamp && s_browRS;
    ID3D11ShaderResourceView* dep = nullptr; ID3D11ShaderResourceView* shm = nullptr;
    if (go) { c->PSGetShaderResources(11, 1, &dep); c->PSGetShaderResources(14, 1, &shm); go = dep && shm; }
    if (go) {
        // guardar
        ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL);
        D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
        ID3D11Buffer* oVB = nullptr; UINT oStr = 0, oOff = 0; c->IAGetVertexBuffers(0, 1, &oVB, &oStr, &oOff);
        ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr; ID3D11HullShader* oHS = nullptr; ID3D11DomainShader* oDS = nullptr;
        c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr); c->HSGetShader(&oHS, nullptr, nullptr); c->DSGetShader(&oDS, nullptr, nullptr);
        ID3D11ShaderResourceView* oT30 = nullptr; ID3D11ShaderResourceView* oT120 = nullptr; c->PSGetShaderResources(30, 1, &oT30); c->PSGetShaderResources(120, 1, &oT120);
        ID3D11Buffer* oB5 = nullptr; c->PSGetConstantBuffers(5, 1, &oB5);
        ID3D11SamplerState* oS15 = nullptr; c->PSGetSamplers(15, 1, &oS15);
        ID3D11BlendState* oBl = nullptr; float oBf[4]; UINT oBm = 0; c->OMGetBlendState(&oBl, oBf, &oBm);
        ID3D11DepthStencilState* oDss = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDss, &oRef);
        ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS);
        // constantes: color de la luz del cuadro (LightCB), fuerza, profundidad invertida, tamano
        float cb[8] = { 1.f, 1.f, 1.f, g_cfg.live[382][3], 0.f, (float)W, (float)H, 0.f };
        { std::vector<float> L; if (NamedCBRead("LightCB", L) && L.size() >= 8) { cb[0] = L[4]; cb[1] = L[5]; cb[2] = L[6]; cb[7] = 1.f; } }
        cb[4] = g_depthClearZero.load() > g_depthClearOne.load() ? 1.f : 0.f;
        c->UpdateSubresource(s_hfCB, 0, nullptr, cb, 0, 0);
        // dibujar
        c->OMSetRenderTargets(1, &rt[0], nullptr);
        c->IASetInputLayout(s_hfIL); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        UINT st = kHfStride, of = 0; c->IASetVertexBuffers(0, 1, &s_hfSO, &st, &of);
        c->VSSetShader(s_hfVS, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
        c->PSSetShader(s_hfPS, nullptr, 0);
        { ID3D11ShaderResourceView* hs = HeadShadowSRV(); c->PSSetShaderResources(30, 1, &hs); }
        { ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->PSSetShaderResources(120, 1, &prm); }
        c->PSSetConstantBuffers(5, 1, &s_hfCB); c->PSSetSamplers(15, 1, &s_hfSamp);
        float bf[4] = { 0, 0, 0, 0 }; c->OMSetBlendState(s_hfBlend, bf, 0xffffffff);
        c->OMSetDepthStencilState(s_hfDSS, 0); c->RSSetState(s_browRS);
        if (sh2 && s_hsPS && s_hsBlend && s_hfSO2) {   // 1) sombra de contacto de los pelos sobre la piel (multiplica, solo luz directa)
            c->IASetVertexBuffers(0, 1, &s_hfSO2, &st, &of);
            c->PSSetShader(s_hsPS, nullptr, 0); c->OMSetBlendState(s_hsBlend, bf, 0xffffffff);
            c->DrawAuto();
            UINT z = 0; c->SOSetTargets(1, &s_hfSO2, &z); ID3D11Buffer* nb = nullptr; c->SOSetTargets(1, &nb, &z);
            c->IASetVertexBuffers(0, 1, &s_hfSO, &st, &of); c->PSSetShader(s_hfPS, nullptr, 0); c->OMSetBlendState(s_hfBlend, bf, 0xffffffff);
        }
        if (st1) c->DrawAuto();   // 2) las hebras con su luz de pelo (suma)
        s_hfDraws++; s_hfLastDraw.store(Now());
        { static std::atomic<int> lg{0}; if (lg.fetch_add(1) < 1) Log("cejas: pasada propia del pelo: primer dibujo (%ux%u)", W, H); }
        // vaciar la salida de flujo para el cuadro siguiente
        { UINT z = 0; c->SOSetTargets(1, &s_hfSO, &z); ID3D11Buffer* nb = nullptr; c->SOSetTargets(1, &nb, &z); }
        // devolver
        c->OMSetRenderTargets(8, rt, dsv);
        c->IASetInputLayout(oIL); c->IASetPrimitiveTopology(oTopo); c->IASetVertexBuffers(0, 1, &oVB, &oStr, &oOff);
        c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0); c->HSSetShader(oHS, nullptr, 0); c->DSSetShader(oDS, nullptr, 0);
        c->PSSetShaderResources(30, 1, &oT30); c->PSSetShaderResources(120, 1, &oT120); c->PSSetConstantBuffers(5, 1, &oB5); c->PSSetSamplers(15, 1, &oS15);
        c->OMSetBlendState(oBl, oBf, oBm); c->OMSetDepthStencilState(oDss, oRef); c->RSSetState(oRS);
        if (oIL) oIL->Release(); if (oVB) oVB->Release(); if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release(); if (oHS) oHS->Release(); if (oDS) oDS->Release();
        if (oT30) oT30->Release(); if (oT120) oT120->Release(); if (oB5) oB5->Release(); if (oS15) oS15->Release();
        if (oBl) oBl->Release(); if (oDss) oDss->Release(); if (oRS) oRS->Release();
    }
    if (dep) dep->Release(); if (shm) shm->Release();
    for (auto* r : rt) if (r) r->Release(); if (dsv) dsv->Release();
}

// (04/11) "Guardar cejas de este jugador": escribe [cejas_cara] <id> = cantidad, largo en PES21FX_hair.ini (reemplaza la linea si ya estaba)
std::string BrowSaveCurrent() {
    std::string cur; { std::lock_guard<std::mutex> lk(s_browMx); cur = s_browCur; }
    if (cur.empty()) return "Cejas: no hay un jugador solo en pantalla (abri el editor con un jugador de cerca)";
    float c = g_cfg.live[77][2], l = g_cfg.live[77][3];
    const float* K = g_cfg.live[367];   // (04/11 i) color: mezcla, r, g, b
    const float* B = g_cfg.live[369]; float X = g_cfg.live[370][0], E = g_cfg.live[370][1], G = g_cfg.live[370][2], D = g_cfg.live[370][3];   // (04/11 l) contraluz: mezcla, r, g, b; hebras extra
    const std::wstring path = g_cfg.baseDir + L"PES21FX_hair.ini";
    std::vector<std::string> lines; { std::ifstream f(path); std::string ln; while (std::getline(f, ln)) { if (!ln.empty() && ln.back() == '\r') ln.pop_back(); lines.push_back(ln); } }
    char val[256]; snprintf(val, sizeof(val), "%s = %.2f, %.2f, %.2f, %.3f, %.3f, %.3f, %.2f, %.3f, %.3f, %.3f, %.0f, %.2f, %.2f, %.2f", cur.c_str(), c, l, K[0], K[1], K[2], K[3], B[0], B[1], B[2], B[3], X, E, G, D);
    int sec = -1, end = (int)lines.size(), hit = -1;
    for (int i = 0; i < (int)lines.size(); i++) {
        std::string t = BLower(BTrim(lines[i]));
        if (!t.empty() && t[0] == '[') { if (sec >= 0 && end == (int)lines.size()) end = i; if (t.rfind("[cejas_cara]", 0) == 0) { sec = i; end = (int)lines.size(); } continue; }
        if (sec >= 0 && i < end) { size_t eq = t.find('='); if (eq != std::string::npos && BTrim(t.substr(0, eq)) == BLower(cur)) hit = i; }
    }
    if (hit >= 0) lines[hit] = val;
    else if (sec >= 0) { int at = end; while (at > sec + 1 && BTrim(lines[at - 1]).empty()) at--; lines.insert(lines.begin() + at, val); }
    else { lines.push_back(""); lines.push_back("[cejas_cara]"); lines.push_back("; (04/11) cejas por jugador: id = cantidad, largo, mezcla de color, rojo, verde, azul, mezcla contraluz, rojo, verde, azul, hebras extra, elevacion, grosor, distancia a la piel mm (los escribe el menu Cejas con hebras > Guardar)"); lines.push_back(val); }
    { std::ofstream f(path, std::ios::binary | std::ios::trunc); if (!f) return "Cejas: no se pudo escribir PES21FX_hair.ini";
      for (auto& ln : lines) f << ln << "\r\n"; }
    { std::lock_guard<std::mutex> lk(g_cfgMutex); g_cfg.hair.browFace[BLower(cur)] = { c, l, K[0], K[1], K[2], K[3], B[0], B[1], B[2], B[3], X, E, G }; }
    Log("cejas: guardado jugador %s: cantidad %.2f, largo %.2f, color %.2f: %.3f %.3f %.3f", cur.c_str(), c, l, K[0], K[1], K[2], K[3]);
    char msg[240]; snprintf(msg, sizeof(msg), "Cejas guardadas para la cara %s: cantidad %.2f, largo %.2f, color %.0f %%, contraluz %.0f %%, +%.0f hebras", cur.c_str(), c, l, K[0] * 100.f, B[0] * 100.f, X);
    return msg;
}

} // namespace fx
