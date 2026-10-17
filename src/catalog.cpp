// PES21 FX - catalogo de shaders: que hace cada shader del juego.
// "Capturar shaders" en el menu: mientras esta activo se anota cada dibujo (VS/PS, render target, vertices, orden).
// "Detener" escribe catalog\shaders.txt (una ficha por shader: reflexion del bytecode -cbuffers, texturas, salidas-,
// uso real y rol inferido) y catalog\frame.txt (linea de tiempo del ultimo cuadro capturado).
#include "core.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fx {

std::atomic<bool> g_catalogActive{false};
std::atomic<bool> g_catalogHook{false};   // activa el gancho de dibujo (captura o volcado pendiente)
std::string g_catalogStatus;

struct DrawRec {
    const ShaderEntry* vs; const ShaderEntry* ps;
    UINT verts; int kind;          // 0 Draw, 1 DrawIndexed, 2 DrawInstanced, 3 DrawIndexedInstanced
    UINT inst;
    int ctx;                       // 0 = contexto inmediato, 1.. = contextos diferidos (listas grabadas en otros hilos)
    std::string rt;                // "1920x1080 R11G11B10 x3 +Z" / "sin RT, Z 4096x12288"
};
static std::map<ID3D11DeviceContext*, int> s_ctxIds;
static int CtxId(ID3D11DeviceContext* c) {
    auto it = s_ctxIds.find(c); if (it != s_ctxIds.end()) return it->second;
    int id = (g_dev.imm && c == g_dev.imm) ? 0 : (int)s_ctxIds.size() + 1;
    s_ctxIds[c] = id; return id;
}
static std::string Key(const ShaderEntry* e) { char b[32]; snprintf(b, sizeof(b), "%016llx-%c", (unsigned long long)e->hash, e->type); return b; }
struct ShaderStat {
    const ShaderEntry* e = nullptr;
    long draws = 0; int frames = 0; int lastFrame = -1; int firstOrder = 1 << 30;
    std::map<std::string, long> rts; long fullscreen = 0, instanced = 0; UINT maxVerts = 0;
    std::set<std::string> partners;          // VS <-> PS con los que se uso
};

static std::mutex s_capMutex;
static std::vector<DrawRec> s_frame, s_lastFrame;
static std::map<std::string, ShaderStat> s_stats;      // por hash+tipo (el juego crea el mismo shader varias veces)
static int s_frameNo = 0; static long s_totalDraws = 0;
static std::set<const ShaderEntry*> s_csSeen;

static const char* FmtName(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS: return "RGBA8";
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS: return "BGRA8";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS: return "RGBA16F";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10F";
    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS: return "RGB10A2";
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return "RGBA32F";
    case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_TYPELESS: return "RG16F";
    case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_TYPELESS: return "R32F";
    case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_TYPELESS: return "R16F";
    case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_TYPELESS: return "R8";
    case DXGI_FORMAT_R8G8_UNORM: return "RG8";
    case DXGI_FORMAT_R16_UNORM: return "R16";
    case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS: return "D24S8";
    case DXGI_FORMAT_D32_FLOAT: return "D32F";
    case DXGI_FORMAT_D16_UNORM: return "D16";
    default: { static char b[16]; snprintf(b, sizeof(b), "fmt%d", (int)f); return b; }
    }
}

static std::string RtDesc(ID3D11DeviceContext* c) {
    ID3D11RenderTargetView* rtv[8] = {}; ID3D11DepthStencilView* dsv = nullptr;
    c->OMGetRenderTargets(8, rtv, &dsv);
    int n = 0; std::string s;
    for (int i = 0; i < 8; i++) if (rtv[i]) n++;
    if (n) {
        ID3D11Resource* res = nullptr; rtv[0]->GetResource(&res);
        ID3D11Texture2D* t = nullptr;
        if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) {
            D3D11_TEXTURE2D_DESC d; t->GetDesc(&d);
            char b[96]; snprintf(b, sizeof(b), "%ux%u %s", d.Width, d.Height, FmtName(d.Format)); s = b;
            t->Release();
        } else s = "RT?";
        if (res) res->Release();
        if (n > 1) s += " x" + std::to_string(n);
        if (dsv) s += " +Z";
    } else if (dsv) {
        ID3D11Resource* res = nullptr; dsv->GetResource(&res);
        ID3D11Texture2D* t = nullptr;
        if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)) && t) {
            D3D11_TEXTURE2D_DESC d; t->GetDesc(&d);
            char b[96]; snprintf(b, sizeof(b), "solo Z %ux%u %s", d.Width, d.Height, FmtName(d.Format)); s = b;
            t->Release();
        } else s = "solo Z";
        if (res) res->Release();
    } else s = "sin RT";
    for (int i = 0; i < 8; i++) if (rtv[i]) rtv[i]->Release();
    if (dsv) dsv->Release();
    return s;
}

// ---------------------------------------------------------------- volcado de constant buffers
// "Volcar cbuffers": en el proximo dibujo de la composicion (619b...) y de la luz del sol (a748...) se retienen los
// cbuffers del PS; en Present (contexto inmediato) se copian a staging, se leen y se escriben con los nombres de
// campo que da la reflexion -> catalog\cbuffers.txt. Es la foto de lo que el motor le pasa a los shaders.
static bool s_cbPending = false;
static const uint64_t s_cbTarget1 = 0x619b86cb5691cdceULL, s_cbTarget2 = 0xa748f426e9b978ccULL;
struct CBGrab { const ShaderEntry* ps; ID3D11Buffer* bufs[14]; };
static std::vector<CBGrab> s_cbGrabs;
static void GrabCBs(ID3D11DeviceContext* c, const ShaderEntry* ps) {
    for (auto& g : s_cbGrabs) if (g.ps->hash == ps->hash) return;
    CBGrab g{}; g.ps = ps;
    c->PSGetConstantBuffers(0, 14, g.bufs);   // AddRef implicito
    s_cbGrabs.push_back(g);
}
void CatalogDumpCBs() { std::lock_guard<std::mutex> lk(s_capMutex); s_cbPending = true; g_catalogHook = true; g_catalogStatus = "Esperando un dibujo de composicion / luz del sol para volcar cbuffers..."; }
static void WriteCBs();

void CatalogDraw(ID3D11DeviceContext* c, const ShaderEntry* vs, const ShaderEntry* ps, UINT verts, int kind, UINT inst) {
    if (s_cbPending && ps && (ps->hash == s_cbTarget1 || ps->hash == s_cbTarget2)) { std::lock_guard<std::mutex> lk(s_capMutex); GrabCBs(c, ps); }
    if (!g_catalogActive) return;
    DrawRec r; r.vs = vs; r.ps = ps; r.verts = verts; r.kind = kind; r.inst = inst; r.rt = RtDesc(c);
    std::lock_guard<std::mutex> lk(s_capMutex);
    r.ctx = CtxId(c);
    if (s_frame.size() < 20000) s_frame.push_back(std::move(r));
}
void CatalogCS(const ShaderEntry* cs) {
    if (!g_catalogActive || !cs) return;
    std::lock_guard<std::mutex> lk(s_capMutex);
    s_csSeen.insert(cs);
}

void CatalogPresent() {
    { std::lock_guard<std::mutex> lk(s_capMutex); if (s_cbPending && !s_cbGrabs.empty()) { WriteCBs(); s_cbPending = false; if (!g_catalogActive) g_catalogHook = false; } }
    if (!g_catalogActive) return;
    std::lock_guard<std::mutex> lk(s_capMutex);
    if (s_frame.empty()) return;
    s_frameNo++;
    int order = 0;
    std::set<std::string> seenThisFrame;
    for (auto& d : s_frame) {
        const ShaderEntry* both[2] = { d.vs, d.ps };
        for (int i = 0; i < 2; i++) {
            const ShaderEntry* e = both[i]; if (!e) continue;
            std::string k = Key(e);
            ShaderStat& st = s_stats[k]; st.e = e;
            st.draws++; st.rts[d.rt]++; st.firstOrder = (std::min)(st.firstOrder, order);
            if (d.kind >= 2) st.instanced++;
            if (d.kind < 2 && d.verts <= 6) st.fullscreen++;
            st.maxVerts = (std::max)(st.maxVerts, d.verts);
            if (seenThisFrame.insert(k).second) st.frames++;
            st.lastFrame = s_frameNo;
            if (both[1 - i]) st.partners.insert(Key(both[1 - i]));
        }
        order++;
    }
    s_totalDraws += (long)s_frame.size();
    s_lastFrame.swap(s_frame); s_frame.clear();
    char b[128]; snprintf(b, sizeof(b), "Capturando: %d cuadros, %ld dibujos, %d shaders distintos", s_frameNo, s_totalDraws, (int)s_stats.size());
    g_catalogStatus = b;
}

void CatalogStart() {
    std::lock_guard<std::mutex> lk(s_capMutex);
    s_frame.clear(); s_lastFrame.clear(); s_stats.clear(); s_csSeen.clear(); s_ctxIds.clear(); s_frameNo = 0; s_totalDraws = 0;
    g_catalogStatus = "Capturando... (jugá / mové la cámara para que pasen todos los shaders)";
    g_catalogActive = true; g_catalogHook = true;
    Log("catalogo: captura iniciada");
}

// ---------------------------------------------------------------- reflexion
typedef HRESULT(WINAPI* PFN_Reflect)(LPCVOID, SIZE_T, REFIID, void**);
typedef HRESULT(WINAPI* PFN_Disasm)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob**);
static PFN_Reflect s_reflect = nullptr; static PFN_Disasm s_disasm = nullptr;
static void LoadReflect() {
    if (s_reflect) return;
    HMODULE m = GetModuleHandleW(L"d3dcompiler_47.dll"); if (!m) m = GetModuleHandleW(L"d3dcompiler_46.dll");
    if (!m) m = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!m) return;
    s_reflect = (PFN_Reflect)GetProcAddress(m, "D3DReflect");
    s_disasm = (PFN_Disasm)GetProcAddress(m, "D3DDisassemble");
}

struct Refl {
    bool ok = false;
    UINT instr = 0, outputs = 0, inputs = 0, texSlots = 0;
    std::vector<std::string> cbs;        // "b2 cPSScene {m_projectionView, m_view, ...}"
    std::vector<std::string> texs;       // "t0 inGBufferNormal"
    std::vector<std::string> samplers;
    std::string sem;                     // semanticas de entrada/salida
    std::string allNames;                // tokens en minusculas separados por espacios (" nombre ")
};
static std::string LowerS(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
// "m_lightDir0" -> " m light dir0 " : se parte en palabras (camelCase y guiones bajos) para buscar sin falsos positivos
static std::string Tokens(const char* n) {
    std::string out = " ", cur;
    for (const char* p = n; *p; p++) {
        char c = *p;
        if (c == '_' || c == '.') { if (!cur.empty()) { out += cur + " "; cur.clear(); } continue; }
        if (isupper((unsigned char)c) && !cur.empty() && !isupper((unsigned char)cur.back())) { out += cur + " "; cur.clear(); }
        cur += (char)tolower((unsigned char)c);
    }
    if (!cur.empty()) out += cur + " ";
    return out;
}
static Refl Reflect(const ShaderEntry* e) {
    Refl r; LoadReflect();
    if (!s_reflect || !e->code || e->code->empty()) return r;
    ID3D11ShaderReflection* sr = nullptr;
    // IID_ID3D11ShaderReflection
    static const GUID iid = { 0x8d536ca1, 0x0cca, 0x4956, { 0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84 } };
    if (FAILED(s_reflect(e->code->data(), e->code->size(), iid, (void**)&sr)) || !sr) return r;
    D3D11_SHADER_DESC sd; if (FAILED(sr->GetDesc(&sd))) { sr->Release(); return r; }
    r.ok = true; r.instr = sd.InstructionCount; r.outputs = sd.OutputParameters; r.inputs = sd.InputParameters;
    for (UINT i = 0; i < sd.BoundResources; i++) {
        D3D11_SHADER_INPUT_BIND_DESC bd; if (FAILED(sr->GetResourceBindingDesc(i, &bd))) continue;
        std::string nm = bd.Name ? bd.Name : "?";
        char b[32];
        if (bd.Type == D3D_SIT_CBUFFER) {
            snprintf(b, sizeof(b), "b%u ", bd.BindPoint);
            std::string line = b + nm + " {";
            ID3D11ShaderReflectionConstantBuffer* cb = sr->GetConstantBufferByName(bd.Name);
            D3D11_SHADER_BUFFER_DESC cbd;
            if (cb && SUCCEEDED(cb->GetDesc(&cbd))) {
                for (UINT v = 0; v < cbd.Variables; v++) {
                    ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(v);
                    D3D11_SHADER_VARIABLE_DESC vd;
                    if (var && SUCCEEDED(var->GetDesc(&vd)) && vd.Name) {
                        // los cbuffers de Konami son un struct con un solo miembro: bajamos a sus campos
                        ID3D11ShaderReflectionType* ty = var->GetType(); D3D11_SHADER_TYPE_DESC td;
                        if (ty && SUCCEEDED(ty->GetDesc(&td)) && td.Class == D3D_SVC_STRUCT && td.Members > 0) {
                            for (UINT m = 0; m < td.Members; m++) { const char* mn = ty->GetMemberTypeName(m); if (mn) { line += mn; line += ", "; r.allNames += Tokens(mn); } }
                        } else { line += vd.Name; line += ", "; r.allNames += Tokens(vd.Name); }
                    }
                }
            }
            if (line.size() > 2 && line.substr(line.size() - 2) == ", ") line.resize(line.size() - 2);
            line += "}";
            r.cbs.push_back(line); r.allNames += Tokens(nm.c_str());
        } else if (bd.Type == D3D_SIT_TEXTURE || bd.Type == D3D_SIT_STRUCTURED || bd.Type == D3D_SIT_BYTEADDRESS || bd.Type == D3D_SIT_UAV_RWTYPED || bd.Type == D3D_SIT_UAV_RWSTRUCTURED) {
            snprintf(b, sizeof(b), "%s%u ", bd.Type == D3D_SIT_TEXTURE ? "t" : bd.Type == D3D_SIT_STRUCTURED || bd.Type == D3D_SIT_BYTEADDRESS ? "t(buf)" : "u", bd.BindPoint);
            r.texs.push_back(b + nm); r.texSlots++; r.allNames += Tokens(nm.c_str());
        } else if (bd.Type == D3D_SIT_SAMPLER) {
            snprintf(b, sizeof(b), "s%u ", bd.BindPoint); r.samplers.push_back(b + nm);
        }
    }
    for (UINT i = 0; i < sd.InputParameters; i++) { D3D11_SIGNATURE_PARAMETER_DESC p; if (SUCCEEDED(sr->GetInputParameterDesc(i, &p)) && p.SemanticName) { r.sem += p.SemanticName; r.sem += std::to_string(p.SemanticIndex); r.sem += " "; } }
    r.sem += "-> ";
    for (UINT i = 0; i < sd.OutputParameters; i++) { D3D11_SIGNATURE_PARAMETER_DESC p; if (SUCCEEDED(sr->GetOutputParameterDesc(i, &p)) && p.SemanticName) { r.sem += p.SemanticName; r.sem += std::to_string(p.SemanticIndex); r.sem += " "; } }
    sr->Release();
    return r;
}

// rol en texto: pistas por nombres de recursos + por como se usa
static std::string InferRole(const ShaderEntry* e, const Refl& r, const ShaderStat* st, UINT bbW, UINT bbH) {
    std::vector<std::string> out;
    const std::string& n = r.allNames;
    auto has = [&](const char* k) { return n.find(std::string(" ") + k + " ") != std::string::npos; };   // palabra completa
    bool fullscreen = st && st->draws > 0 && st->fullscreen * 2 >= st->draws;
    std::string rtMain; long rtMax = -1; if (st) for (auto& kv : st->rts) if (kv.second > rtMax) { rtMax = kv.second; rtMain = kv.first; }
    bool depthOnly = rtMain.rfind("solo Z", 0) == 0;
    bool noRT = rtMain == "sin RT";
    if (e->type == 'c') out.push_back("compute shader (calculo en GPU fuera de la cadena de dibujo: fisica, particulas, culling o post)");
    if (depthOnly && e->type == 'v') out.push_back("escribe solo profundidad: pasada de SOMBRAS (shadow map) o pre-pasada de Z");
    if (depthOnly && rtMain.find("4096") != std::string::npos) out.push_back("el atlas 4096 es el shadow map del sol (3 cascadas apiladas)");
    if (e->type == 'p' && r.outputs >= 3) out.push_back("escribe " + std::to_string(r.outputs) + " targets: pasada de G-BUFFER (material: normal, parametros, albedo)");
    if (has("albedo") || has("in light diffuse") || (has("light") && has("diffuse") && has("in"))) out.push_back("lee albedo + luz difusa/especular: COMPOSICION diferida (junta material y luz)");
    if (has("shadow") && has("texture") && has("light")) out.push_back("lee shadow map + direccion de luz: LUZ DIRECCIONAL del sol/torre (pasada de luz)");
    else if (has("ps light") || has("light dir0")) out.push_back("usa cPSLight: pasada de LUZ (puntual/foco o direccional)");
    if (has("in g buffer normal") || has("g normal texture") || (has("gbuffer"))) out.push_back("lee el G-buffer (normal/material): pasada de pantalla posterior al material");
    if (has("occlusion") || has("ssao")) out.push_back("oclusion ambiental (SSAO)");
    if (has("hair") || has("pelo")) out.push_back("PELO");
    if (has("skin") || has("sss")) out.push_back("PIEL");
    if (has("cloth") || has("uniform") || has("kit")) out.push_back("ropa / kits");
    if (has("eye") || has("iris") || has("cornea")) out.push_back("ojos");
    if (has("turf") || has("grass") || has("lawn") || has("pitch")) out.push_back("PASTO / cancha");
    if (has("sky") || has("cloud")) out.push_back("CIELO");
    if (has("crowd") || has("audience") || has("spectator")) out.push_back("PUBLICO");
    if (has("water") || has("rain") || has("puddle") || has("wet")) out.push_back("lluvia / agua");
    if (has("bloom") || has("glare")) out.push_back("BLOOM");
    if (has("dof") || has("blur") || has("coc")) out.push_back("desenfoque (DOF / blur)");
    if (has("tonemap") || has("tone") || has("lut") || has("grading")) out.push_back("tonemapping / color final");
    if (has("taa") || has("fxaa") || has("smaa") || has("temporal") || has("velocity") || has("motion")) out.push_back("antialiasing temporal / motion blur");
    if (has("font") || has("sprite") || has("glyph") || has("hud")) out.push_back("UI / texto");
    if (has("particle") || has("effect") || has("smoke")) out.push_back("particulas / efectos");
    if (has("shadow projection") && e->type == 'v' && !depthOnly) out.push_back("proyecta a espacio de sombra: recibe sombras");
    if (has("world") || has("view world")) out.push_back("objeto 3D (tiene matriz de mundo)");
    if (has("bone") || has("bones") || has("skinning") || has("palette")) out.push_back("malla ANIMADA por huesos (jugadores / arbitros / publico)");
    if (fullscreen && e->type == 'p') out.push_back("pasada de PANTALLA COMPLETA (post-procesado o luz diferida)");
    if (st && !rtMain.empty() && !depthOnly && !noRT) {
        char b[64]; snprintf(b, sizeof(b), "%ux%u", bbW, bbH);
        if (rtMain.rfind(b, 0) == 0) out.push_back("dibuja a resolucion completa (" + std::string(b) + ")");
        else if (rtMain.find("x") != std::string::npos) {
            UINT w = (UINT)atoi(rtMain.c_str());
            if (bbW && w && w * 2 <= bbW) out.push_back("dibuja a RESOLUCION REDUCIDA (" + rtMain.substr(0, rtMain.find(' ')) + "): bloom, DOF, SSAO o similar");
        }
        if (rtMain.find("RGBA8") != std::string::npos || rtMain.find("BGRA8") != std::string::npos) out.push_back("target de 8 bits (imagen final / UI)");
        if (rtMain.find("16F") != std::string::npos || rtMain.find("R11G11B10F") != std::string::npos) out.push_back("target HDR");
    }
    if (st && st->instanced * 2 >= st->draws && st->draws) out.push_back("dibujo INSTANCIADO (muchas copias: publico, pasto, banderines)");
    if (out.empty()) out.push_back(e->type == 'v' ? "vertex shader generico" : "pixel shader generico");
    std::string s; for (size_t i = 0; i < out.size(); i++) { if (i) s += "; "; s += out[i]; }
    return s;
}

static std::string Hex(uint64_t h) { char b[24]; snprintf(b, sizeof(b), "%016llx", (unsigned long long)h); return b; }

static bool CatalogWrite(UINT bbW, UINT bbH, std::vector<const ShaderEntry*> allShaders);
#include <thread>
// detener: se deja de capturar al instante y la escritura (reflexion de miles de shaders, ~segundos) corre en un hilo
// aparte para no frenar el render del juego
bool CatalogStop(UINT bbW, UINT bbH, std::vector<const ShaderEntry*> allShaders) {
    g_catalogActive = false;
    { std::lock_guard<std::mutex> lk(s_capMutex); if (!s_cbPending) g_catalogHook = false; }
    g_catalogStatus = "Escribiendo el catalogo en segundo plano...";
    std::thread([bbW, bbH, allShaders]() { CatalogWrite(bbW, bbH, allShaders); }).detach();
    return true;
}
static bool CatalogWrite(UINT bbW, UINT bbH, std::vector<const ShaderEntry*> allShaders) {
    std::lock_guard<std::mutex> lk(s_capMutex);
    std::wstring dir = g_cfg.baseDir + L"catalog\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::ofstream f(dir + L"shaders.txt", std::ios::binary | std::ios::trunc);
    if (!f) { g_catalogStatus = "No se pudo escribir catalog\\shaders.txt"; return false; }
    // orden: por primera aparicion en el cuadro (los no usados al final)
    std::vector<const ShaderEntry*> used;
    for (auto& kv : s_stats) used.push_back(kv.second.e);
    std::sort(used.begin(), used.end(), [&](const ShaderEntry* a, const ShaderEntry* b) { return s_stats[Key(a)].firstOrder < s_stats[Key(b)].firstOrder; });
    std::set<std::string> usedSet; for (auto* e : used) usedSet.insert(Key(e));
    std::set<std::string> csKeys; for (auto* e : s_csSeen) csKeys.insert(Key(e));
    // cargados: uno por hash
    std::map<std::string, const ShaderEntry*> loaded; for (auto* e : allShaders) loaded.emplace(Key(e), e);
    f << "PES21 FX - catalogo de shaders\r\n";
    f << "Captura: " << s_frameNo << " cuadros, " << s_totalDraws << " dibujos, " << used.size() << " shaders usados de " << loaded.size() << " cargados (distintos). Pantalla " << bbW << "x" << bbH << "\r\n";
    f << "Cada ficha: hash (= nombre del archivo en shaders\\), tipo, ROL inferido, reflexion del bytecode y uso real.\r\n";
    f << "'reemplazado' = ya tenemos shaders\\<hash>-xx_replace.txt. 'orden' = posicion del primer dibujo en el cuadro (menor = antes).\r\n\r\n";
    auto writeEntry = [&](const ShaderEntry* e, const ShaderStat* st) {
        Refl r = Reflect(e);
        const char* tn = e->type == 'p' ? "ps" : e->type == 'v' ? "vs" : "cs";
        f << "=== " << Hex(e->hash) << "-" << tn << (e->repl ? "   [reemplazado por el mod]" : "") << (e->rule ? "   [con regla]" : "") << "\r\n";
        f << "ROL: " << InferRole(e, r, st, bbW, bbH) << "\r\n";
        if (st) {
            f << "USO: " << st->draws << " dibujos en " << st->frames << " cuadros, orden " << st->firstOrder << ", max " << st->maxVerts << " vertices";
            if (st->instanced) f << ", " << st->instanced << " instanciados";
            if (st->fullscreen) f << ", " << st->fullscreen << " de pantalla completa";
            f << "\r\n";
            for (auto& kv : st->rts) f << "  render target: " << kv.first << "  (" << kv.second << " dibujos)\r\n";
            std::string ps; int k = 0;
            for (auto& p : st->partners) { if (k++ < 8) ps += p + " "; }
            if (!ps.empty()) f << "  se usa junto con: " << ps << (st->partners.size() > 8 ? "..." : "") << "\r\n";
        } else f << "USO: cargado pero no dibujo durante la captura\r\n";
        if (r.ok) {
            f << "REFLEXION: " << r.instr << " instrucciones, " << r.inputs << " entradas, " << r.outputs << " salidas, " << r.texSlots << " texturas. Semanticas: " << r.sem << "\r\n";
            for (auto& c : r.cbs) f << "  cbuffer " << c << "\r\n";
            for (auto& t : r.texs) f << "  textura " << t << "\r\n";
            if (!r.samplers.empty()) { f << "  samplers:"; for (auto& s : r.samplers) f << " " << s; f << "\r\n"; }
        } else f << "REFLEXION: no disponible\r\n";
        f << "\r\n";
    };
    for (auto* e : used) writeEntry(e, &s_stats[Key(e)]);
    // ensamblador de los usados (catalog\asm\<hash>-xx.txt): para traducir a HLSL los que no tenemos decompilados
    {
        std::wstring adir = dir + L"asm\\"; CreateDirectoryW(adir.c_str(), nullptr);
        int na = 0;
        for (auto* e : used) {
            if (!s_disasm || !e->code || e->repl) continue;   // los reemplazados ya tienen HLSL
            ID3DBlob* blob = nullptr;
            if (FAILED(s_disasm(e->code->data(), e->code->size(), 0, nullptr, &blob)) || !blob) continue;
            std::string name = Key(e) + ".txt";
            std::ofstream a(adir + W(name), std::ios::binary | std::ios::trunc);
            if (a) { a.write((const char*)blob->GetBufferPointer(), (std::streamsize)blob->GetBufferSize() - 1); na++; }
            blob->Release();
        }
        f << "\r\n(ensamblador de " << na << " shaders usados sin reemplazo en catalog\\asm\\)\r\n";
    }
    f << "\r\n##### compute shaders vistos #####\r\n\r\n";
    { std::set<std::string> done; for (auto* e : s_csSeen) if (done.insert(Key(e)).second) writeEntry(e, nullptr); }
    f << "\r\n##### cargados pero no usados en la captura #####\r\n\r\n";
    for (auto& kv : loaded) if (!usedSet.count(kv.first) && !csKeys.count(kv.first)) writeEntry(kv.second, nullptr);
    f.close();
    // linea de tiempo del ultimo cuadro
    std::ofstream t(dir + L"frame.txt", std::ios::binary | std::ios::trunc);
    if (t) {
        t << "PES21 FX - linea de tiempo del ultimo cuadro capturado (" << s_lastFrame.size() << " dibujos). Columnas: # | tipo verts | VS | PS\r\n";
        t << "El juego graba listas de comandos en varios hilos (contextos diferidos) y las ejecuta despues en el inmediato: por eso se agrupa por contexto; dentro de cada uno el orden es el de grabacion.\r\n\r\n";
        std::set<int> ctxs; for (auto& d : s_lastFrame) ctxs.insert(d.ctx);
        for (int cid : ctxs) {
        t << "\r\n######## contexto " << cid << (cid == 0 ? " (inmediato)" : " (diferido)") << "\r\n";
        int i = 0; std::string lastRt;
        for (auto& d : s_lastFrame) {
            if (d.ctx != cid) continue;
            if (d.rt != lastRt) { t << "--- " << d.rt << "\r\n"; lastRt = d.rt; }
            const char* kn[] = { "Draw", "DrawIdx", "DrawInst", "DrawIdxInst" };
            t << i++ << " | " << kn[d.kind] << " " << d.verts << (d.kind >= 2 ? " x" + std::to_string(d.inst) : "") << " | "
              << (d.vs ? Hex(d.vs->hash) : std::string("-")) << (d.vs && d.vs->repl ? "*" : "") << " | "
              << (d.ps ? Hex(d.ps->hash) : std::string("(sin PS)")) << (d.ps && d.ps->repl ? "*" : "") << "\r\n";
        }
        }
        t << "\r\n* = reemplazado por el mod\r\n";
    }
    char b[160]; snprintf(b, sizeof(b), "Escrito catalog\\shaders.txt (%d usados + %d sin usar) y catalog\\frame.txt", (int)used.size(), (int)(allShaders.size() - used.size()));
    g_catalogStatus = b;
    Log("catalogo: %s", b);
    return true;
}

// escribe los cbuffers retenidos con nombres de campo (reflexion del PS)
struct CBField { std::string name; UINT offset, size, rows, cols, elems; };
static void CBLayout(const ShaderEntry* e, std::map<UINT, std::pair<std::string, std::vector<CBField>>>& out) {
    LoadReflect(); if (!s_reflect || !e->code) return;
    ID3D11ShaderReflection* sr = nullptr;
    static const GUID iid = { 0x8d536ca1, 0x0cca, 0x4956, { 0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84 } };
    if (FAILED(s_reflect(e->code->data(), e->code->size(), iid, (void**)&sr)) || !sr) return;
    D3D11_SHADER_DESC sd; sr->GetDesc(&sd);
    for (UINT i = 0; i < sd.BoundResources; i++) {
        D3D11_SHADER_INPUT_BIND_DESC bd; if (FAILED(sr->GetResourceBindingDesc(i, &bd)) || bd.Type != D3D_SIT_CBUFFER) continue;
        ID3D11ShaderReflectionConstantBuffer* cb = sr->GetConstantBufferByName(bd.Name); D3D11_SHADER_BUFFER_DESC cbd;
        if (!cb || FAILED(cb->GetDesc(&cbd))) continue;
        auto& slot = out[bd.BindPoint]; slot.first = bd.Name ? bd.Name : "?";
        for (UINT v = 0; v < cbd.Variables; v++) {
            ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(v); D3D11_SHADER_VARIABLE_DESC vd;
            if (!var || FAILED(var->GetDesc(&vd))) continue;
            ID3D11ShaderReflectionType* ty = var->GetType(); D3D11_SHADER_TYPE_DESC td;
            if (ty && SUCCEEDED(ty->GetDesc(&td)) && td.Class == D3D_SVC_STRUCT) {
                for (UINT m = 0; m < td.Members; m++) {
                    ID3D11ShaderReflectionType* mt = ty->GetMemberTypeByIndex(m); D3D11_SHADER_TYPE_DESC md;
                    if (!mt || FAILED(mt->GetDesc(&md))) continue;
                    UINT sz = md.Rows * md.Columns * 4 * (md.Elements ? md.Elements : 1);
                    if (md.Rows > 1 || md.Elements) sz = ((md.Elements ? md.Elements : 1) * md.Rows) * 16;
                    slot.second.push_back({ ty->GetMemberTypeName(m) ? ty->GetMemberTypeName(m) : "?", vd.StartOffset + md.Offset, sz, md.Rows, md.Columns, md.Elements ? md.Elements : 1 });
                }
            } else slot.second.push_back({ vd.Name ? vd.Name : "?", vd.StartOffset, vd.Size, 1, vd.Size / 4, 1 });
        }
    }
    sr->Release();
}
static void WriteCBs() {
    if (!g_dev.dev || !g_dev.imm) return;
    std::wstring dir = g_cfg.baseDir + L"catalog\\"; CreateDirectoryW(dir.c_str(), nullptr);
    std::ofstream f(dir + L"cbuffers.txt", std::ios::binary | std::ios::trunc);
    int written = 0;
    for (auto& g : s_cbGrabs) {
        std::map<UINT, std::pair<std::string, std::vector<CBField>>> lay; CBLayout(g.ps, lay);
        f << "==================== PS " << Hex(g.ps->hash) << "\r\n";
        for (UINT slot = 0; slot < 14; slot++) {
            ID3D11Buffer* b = g.bufs[slot]; if (!b) continue;
            D3D11_BUFFER_DESC bd; b->GetDesc(&bd);
            D3D11_BUFFER_DESC sdc = bd; sdc.Usage = D3D11_USAGE_STAGING; sdc.BindFlags = 0; sdc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sdc.MiscFlags = 0;
            ID3D11Buffer* st = nullptr;
            if (FAILED(g_dev.dev->CreateBuffer(&sdc, nullptr, &st)) || !st) { f << "b" << slot << ": no se pudo copiar\r\n"; continue; }
            g_dev.imm->CopyResource(st, b);
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(g_dev.imm->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
                const float* fl = (const float*)m.pData; UINT nf = bd.ByteWidth / 4;
                auto it = lay.find(slot);
                f << "--- b" << slot << " " << (it != lay.end() ? it->second.first : std::string("(sin reflexion)")) << "  (" << bd.ByteWidth << " bytes)\r\n";
                if (it != lay.end()) {
                    for (auto& fd : it->second.second) {
                        f << "  " << fd.name << " =";
                        UINT n = fd.size / 4; if (fd.offset / 4 + n > nf) n = nf > fd.offset / 4 ? nf - fd.offset / 4 : 0;
                        UINT perRow = fd.rows > 1 || fd.elems > 1 ? 4 : fd.cols;
                        for (UINT k = 0; k < n; k++) { if (perRow && k % perRow == 0 && k) f << " |"; char v[32]; snprintf(v, sizeof(v), " %g", fl[fd.offset / 4 + k]); f << v; }
                        f << "\r\n";
                    }
                } else {
                    for (UINT k = 0; k < nf; k++) { if (k % 4 == 0) f << (k ? "\r\n  " : "  "); char v[32]; snprintf(v, sizeof(v), "%g ", fl[k]); f << v; }
                    f << "\r\n";
                }
                g_dev.imm->Unmap(st, 0); written++;
            }
            st->Release();
        }
        for (auto* b : g.bufs) if (b) b->Release();
    }
    s_cbGrabs.clear();
    char b[128]; snprintf(b, sizeof(b), "Escrito catalog\\cbuffers.txt (%d buffers)", written); g_catalogStatus = b; Log("catalogo: %s", b);
}

} // namespace fx
