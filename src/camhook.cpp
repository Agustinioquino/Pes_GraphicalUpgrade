// PES21 FX - camara del motor (23/10). Ingenieria inversa de PES2021.exe 1.07.02 (sello 62787C6A, sobre el volcado del
// exe desempaquetado que hace exedump.cpp). El exe en disco no se toca: todo es en memoria y solo con esa version.
//
//  * cobra::rd::CameraSystem aplica la camara principal en 0x14198d3b0 (parametro, reiniciar) una vez por cuadro de
//    render (desde el manejador de comandos de camara, con la camara interpolada entre dos cuadros del juego). El
//    parametro (0x104 bytes, se copia en 0x14198ce20) trae posicion (+0x00), objetivo (+0x0c), arriba (+0x18),
//    cerca / lejos (+0x24 / +0x28), ..., +0x48 = camara continua y +0x88 = id de la camara. Si la camara NO es continua
//    (corte) o se pide reinicio, el motor prende el bit 0x80 de las banderas (+0xf0) de la camara de render (y el 0x2 si
//    es un corte).
//  * fox::gr::dg::plugins::AntiAliasing (vtable 0x142a2b520, entrada [4] = 0x141754850) es el TAA del juego: lee ese bit
//    de la camara de su vista ([vista+0x4a0]+0xf0); prendido -> dibuja "Cancel Temporal Pass" (descarta la historia) en
//    vez de "Temporal Pass", y despues lo apaga. El cambio de tamano de la vista (0x141687940) tambien lo prende.
//
// La DLL engancha los dos: la funcion de la camara con MinHook y la entrada de la vtable cambiando el puntero (sin tocar
// codigo), solo si coinciden el sello del exe y la firma de bytes. Con eso (x161 = 1, menu Motor):
//   - el SSGI y el GTAO temporales descartan su historia en el mismo cuadro del corte (antes lo adivinaban por la
//     profundidad y en los cortes quedaban restos de la toma anterior),
//   - la exposicion estable entre camaras (x129) salta a la toma nueva en vez de acomodarse en ~1 s,
//   - 160 (lo escribe la DLL): x = cuadros desde el ultimo corte, y = cortes (contador), z = cuadros desde el ultimo
//     reinicio del TAA del juego, w = 1 si los ganchos estan puestos.
// El log anota cada corte (cuadro, camara, posicion, cuando reinicio el TAA del juego) y cada 30 s un resumen.
#include "core.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <cstdio>
#include <vector>

namespace fx {

static const unsigned kStamp = 0x62787C6A;
static const uintptr_t kCamApply = 0x14198d3b0ull;            // CameraSystem: aplicar la camara principal
static const uintptr_t kAAVtbl = 0x142a2b520ull;              // vtable de fox::gr::dg::plugins::AntiAliasing
static const uintptr_t kAAExec = 0x141754850ull;              // su entrada [4] (TAA del juego)
// prologo de 0x14198d3b0: mov rax,rsp; push rbp; push rdi; push r12; push r14; push r15; lea rbp,[rax-138h]
static const unsigned char kCamSig[16] = { 0x48,0x8b,0xc4,0x55,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0xc8,0xfe };
static const unsigned char kCamSig2[5] = { 0x44,0x0f,0xb6,0x77,0x48 };                        // 0x14198d44c movzx r14d,byte [rdi+48h]
static const unsigned char kCamSig3[10] = { 0x81,0x89,0xf0,0x00,0x00,0x00,0x80,0x00,0x00,0x00 }; // 0x14198dddb or dword [rcx+0F0h],80h
// prologo de 0x141754850: mov rax,rsp; push rbp; push rsi; push rdi; push r14; push r15; sub rsp,50h; mov rdi,[...]
static const unsigned char kAASig[16] = { 0x48,0x8b,0xc4,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x50,0x48,0x8b };
static const unsigned char kAASig2[10] = { 0x81,0xa0,0xf0,0x00,0x00,0x00,0x7f,0xfe,0xff,0xff };  // 0x1417549cd and dword [rax+0F0h],0FFFFFE7Fh

typedef uintptr_t (__fastcall* CamApplyFn)(void* param, uintptr_t reset);
typedef uintptr_t (__fastcall* AAExecFn)(void* self, void* a2, void* a3, void* view);
static CamApplyFn o_cam = nullptr; static AAExecFn o_aa = nullptr;
static bool s_tried = false, s_camOk = false, s_aaOk = false;

// estado (lo escriben los ganchos en el hilo de render, lo leen Present y los efectos)
static std::atomic<int> s_cuts{0}, s_usable{0}, s_cutFrame{-1000000}, s_streak{0}, s_maxStreak{0};
static std::atomic<int> s_applies{0}, s_aaCalls{0}, s_aaResets{0}, s_aaResetFrame{-1000000};
static std::atomic<unsigned> s_camTid{0}, s_aaTid{0};
static std::atomic<int> s_aaRing[16]; static std::atomic<unsigned> s_aaRingW{0};   // cuadros de los ultimos reinicios del TAA
struct CutRec { int frame; float pos[3], tgt[3]; int id; unsigned char cont, reset, usable; };
static CutRec s_ring[32]; static std::atomic<unsigned> s_ringW{0}; static unsigned s_ringR = 0;
static std::mutex s_camMx; static float s_pos[3] = {}, s_tgt[3] = {}, s_up[3] = {}, s_nf[2] = {}; static int s_camId = -1, s_camFrame = -1;
static std::atomic<int> s_dumpLeft{2}; static std::atomic<bool> s_dumpReady{false}; static unsigned char s_dump[0x104];

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!p || !VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return (uintptr_t)p + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static bool SigAt(uintptr_t a, const unsigned char* sig, size_t n) { return Readable((const void*)a, n) && memcmp((const void*)a, sig, n) == 0; }

static bool UseCuts() { return g_cfg.live[161][0] > 0.5f; }

static uintptr_t __fastcall hk_Cam(void* param, uintptr_t reset) {
    const unsigned char* b = (const unsigned char*)param;
    if (b) {
        int f = PresentNo();
        bool cont = b[0x48] != 0, rs = (reset & 0xff) != 0;
        s_applies++;
        if (!s_camTid.load()) s_camTid = GetCurrentThreadId();
        float pos[3], tgt[3], up[3], nf[2]; int id;
        memcpy(pos, b, 12); memcpy(tgt, b + 0x0c, 12); memcpy(up, b + 0x18, 12); memcpy(nf, b + 0x24, 8); memcpy(&id, b + 0x88, 4);
        { std::lock_guard<std::mutex> lk(s_camMx); memcpy(s_pos, pos, 12); memcpy(s_tgt, tgt, 12); memcpy(s_up, up, 12); memcpy(s_nf, nf, 8); s_camId = id; s_camFrame = f; }
        if (!cont || rs) {
            int last = s_cutFrame.exchange(f);
            int st = (f - last <= 1) ? s_streak.load() + (f != last ? 1 : 0) : 1;
            s_streak = st; if (st > s_maxStreak.load()) s_maxStreak = st;
            // si la camara se "corta" en todos los cuadros (algun menu 3D) no se usa: los efectos no acumularian nunca
            bool usable = st <= 2;
            s_cuts++; if (usable) s_usable++;
            unsigned w = s_ringW.load();
            CutRec& r = s_ring[w % 32];
            r.frame = f; memcpy(r.pos, pos, 12); memcpy(r.tgt, tgt, 12); r.id = id; r.cont = cont; r.reset = rs; r.usable = usable;
            s_ringW = w + 1;
            if (s_dumpLeft.load() > 0 && !s_dumpReady.load()) { memcpy(s_dump, b, sizeof s_dump); s_dumpLeft--; s_dumpReady = true; }
        }
    }
    return o_cam(param, reset);
}

static bool ReadView(void* view, unsigned* fl, int* w, int* h) {
    __try {
        if (!view) return false;
        void* cam = *(void* volatile*)((char*)view + 0x4a0);
        if (!cam) return false;
        *fl = *(volatile unsigned*)((char*)cam + 0xf0);
        *w = *(volatile int*)((char*)view + 0x584); *h = *(volatile int*)((char*)view + 0x588);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static std::atomic<int> s_aaW{0}, s_aaH{0};
static uintptr_t __fastcall hk_AA(void* self, void* a2, void* a3, void* view) {
    unsigned fl = 0; int w = 0, h = 0;
    s_aaCalls++;
    if (!s_aaTid.load()) s_aaTid = GetCurrentThreadId();
    if (ReadView(view, &fl, &w, &h)) {
        s_aaW = w; s_aaH = h;
        if (fl & 0x80) {
            int f = PresentNo();
            s_aaResetFrame = f; s_aaResets++;
            unsigned k = s_aaRingW.fetch_add(1); s_aaRing[k % 16] = f;
        }
    }
    return o_aa(self, a2, a3, view);
}

static void Install() {
    s_tried = true;
    for (auto& a : s_aaRing) a = -1000000;
    if (!g_cfg.motorPatch) { Log("camara del motor: parches en memoria apagados (motor = 0)"); return; }
    HMODULE mod = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)mod; auto nt = (const IMAGE_NT_HEADERS64*)((const char*)mod + dos->e_lfanew);
    if ((uintptr_t)mod != 0x140000000ull || nt->FileHeader.TimeDateStamp != kStamp) {
        Log("camara del motor: exe distinto (base %p, sello %08X): sin ganchos", (void*)mod, (unsigned)nt->FileHeader.TimeDateStamp); return;
    }
    // 1) aplicar camara (MinHook)
    if (SigAt(kCamApply, kCamSig, sizeof kCamSig) && SigAt(0x14198d44cull, kCamSig2, sizeof kCamSig2) && SigAt(0x14198dddbull, kCamSig3, sizeof kCamSig3)) {
        if (MH_CreateHook((void*)kCamApply, (void*)&hk_Cam, (void**)&o_cam) == MH_OK && MH_EnableHook((void*)kCamApply) == MH_OK) s_camOk = true;
        else Log("camara del motor: no se pudo enganchar 0x14198d3b0");
    } else Log("camara del motor: la firma de 0x14198d3b0 no coincide, se omite");
    // 2) TAA del juego: entrada [4] de la vtable de AntiAliasing (solo lectura: VirtualProtect)
    void** slot = (void**)(kAAVtbl + 4 * sizeof(void*));
    if (Readable(slot, sizeof(void*)) && (uintptr_t)*slot == kAAExec && SigAt(kAAExec, kAASig, sizeof kAASig) && SigAt(0x1417549cdull, kAASig2, sizeof kAASig2)) {
        DWORD old = 0;
        if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
            o_aa = (AAExecFn)*slot;
            InterlockedExchangePointer(slot, (void*)&hk_AA);
            VirtualProtect(slot, sizeof(void*), old, &old);
            s_aaOk = true;
        } else Log("camara del motor: no se pudo cambiar la vtable del TAA (%lu)", GetLastError());
    } else Log("camara del motor: la vtable del TAA no coincide (%p), se omite", Readable(slot, 8) ? *slot : nullptr);
    Log("camara del motor: %s; TAA del juego: %s (hilo de Present %lu)", s_camOk ? "enganchada (cortes de camara)" : "NO", s_aaOk ? "enganchado (reinicios de historia)" : "NO", GetCurrentThreadId());
}

bool CamCutSince(int& seen) {
    if (!s_camOk || !UseCuts()) { seen = s_usable.load(); return false; }
    int n = s_usable.load(); if (n == seen) return false;
    seen = n; return true;
}
bool CamCutPeek(int seen) { return s_camOk && UseCuts() && s_usable.load() != seen; }
int CamLastCutFrame() { return s_cutFrame.load(); }
std::atomic<int> g_cutUse[3] = {};
void ZeroBuffer(ID3D11DeviceContext* c, ID3D11Buffer* b) {
    if (!c || !b) return;
    D3D11_BUFFER_DESC d; b->GetDesc(&d);
    if (d.Usage != D3D11_USAGE_DEFAULT) return;
    static std::vector<char> z; if (z.size() < d.ByteWidth) z.assign(d.ByteWidth, 0);
    c->UpdateSubresource(b, 0, nullptr, z.data(), 0, 0);
}
bool EngineCamera(float pos[3], float tgt[3], int* frame) {
    std::lock_guard<std::mutex> lk(s_camMx);
    if (s_camFrame < 0) return false;
    memcpy(pos, s_pos, 12); memcpy(tgt, s_tgt, 12); if (frame) *frame = s_camFrame; return true;
}

static int AAResetNear(int f) {   // el primer reinicio del TAA en f-1 .. f+3 (-1000000 si no hubo)
    int best = -1000000;
    for (auto& a : s_aaRing) { int v = a.load(); if (v >= f - 1 && v <= f + 3 && (best < f - 1 || v < best)) best = v; }
    return best;
}

void CamHookPresent() {
    if (!s_tried) Install();
    int now = PresentNo();
    float* p = g_cfg.live[160];
    p[0] = s_camOk ? (float)(std::min)(now - s_cutFrame.load(), 9999) : 9999.0f;
    p[1] = (float)(s_cuts.load() % 4096);
    p[2] = s_aaOk ? (float)(std::min)(now - s_aaResetFrame.load(), 9999) : 9999.0f;
    p[3] = (s_camOk || s_aaOk) ? 1.0f : 0.0f;
    if (!s_camOk && !s_aaOk) return;
    // cortes anotados (con 3 cuadros de atraso para ver si el TAA del juego reinicio en el mismo cuadro)
    static int s_logged = 0, s_matchSeen = -1;
    if (g_matchGen.load() != s_matchSeen) { s_matchSeen = g_matchGen.load(); s_logged = 0; }
    unsigned w = s_ringW.load();
    if (w - s_ringR > 32) s_ringR = w - 32;
    while (s_ringR != w) {
        const CutRec& r = s_ring[s_ringR % 32];
        if (r.frame > now - 3) break;
        if (s_logged < 40) {
            int g = AAResetNear(r.frame);
            char ta[48]; if (g > -1000000) snprintf(ta, sizeof ta, "TAA del juego %+d", g - r.frame); else snprintf(ta, sizeof ta, "TAA del juego sin reinicio");
            Log("camara: corte en el cuadro %d (%s%s%s, id %d) pos (%.1f, %.1f, %.1f) mira (%.1f, %.1f, %.1f) | %s", r.frame,
                r.cont ? "continua" : "no continua", r.reset ? ", reinicio" : "", r.usable ? "" : ", seguido: no se usa", r.id,
                r.pos[0], r.pos[1], r.pos[2], r.tgt[0], r.tgt[1], r.tgt[2], ta);
            if (++s_logged == 40) Log("camara: (40 cortes anotados en este partido; los siguientes solo en el resumen de 30 s)");
        }
        s_ringR++;
    }
    if (s_dumpReady.load()) {   // estructura completa del parametro de camara (para seguir identificando campos)
        std::string line; char t[24];
        for (int o = 0; o < 0x104; o += 4) { float v; memcpy(&v, s_dump + o, 4); unsigned u; memcpy(&u, s_dump + o, 4);
            if (std::isfinite(v) && fabsf(v) > 1e-6f && fabsf(v) < 1e6f) snprintf(t, sizeof t, " %02x:%.4g", o, v); else if (u) snprintf(t, sizeof t, " %02x:#%x", o, u); else continue; line += t; }
        Log("camara: parametro del corte:%s", line.c_str());
        s_dumpReady = false;
    }
    // resumen cada 30 s
    static double s_t = 0; static int s_c0 = 0, s_a0 = 0, s_ap0 = 0, s_aac0 = 0, s_f0 = 0;
    double t = Now();
    if (s_t == 0) { s_t = t; s_c0 = s_cuts.load(); s_a0 = s_aaResets.load(); s_ap0 = s_applies.load(); s_aac0 = s_aaCalls.load(); s_f0 = now; return; }
    if (t - s_t < 30.0) return;
    int fr = (std::max)(now - s_f0, 1);
    float pos[3] = {}, tgt[3] = {}; int cf = -1; bool have = EngineCamera(pos, tgt, &cf);
    Log("camara del motor (30 s): %d cortes (ultimo hace %d cuadros, racha maxima %d), TAA del juego reinicio %d veces, %.2f camaras y %.2f TAA por cuadro, vista %dx%d, hilos camara %lu / TAA %lu | usados: SSGI %d, GTAO %d, exposicion %d%s | camara (%.1f, %.1f, %.1f) -> (%.1f, %.1f, %.1f)%s",
        s_cuts.load() - s_c0, (std::min)(now - s_cutFrame.load(), 99999), s_maxStreak.load(), s_aaResets.load() - s_a0,
        (s_applies.load() - s_ap0) / (float)fr, (s_aaCalls.load() - s_aac0) / (float)fr, s_aaW.load(), s_aaH.load(), s_camTid.load(), s_aaTid.load(),
        g_cutUse[0].load(), g_cutUse[1].load(), g_cutUse[2].load(), UseCuts() ? "" : " (x161 = 0: no se usan)",
        pos[0], pos[1], pos[2], tgt[0], tgt[1], tgt[2], have ? "" : " (sin datos)");
    s_t = t; s_c0 = s_cuts.load(); s_a0 = s_aaResets.load(); s_ap0 = s_applies.load(); s_aac0 = s_aaCalls.load(); s_f0 = now; s_maxStreak = 0;
}

} // namespace fx
