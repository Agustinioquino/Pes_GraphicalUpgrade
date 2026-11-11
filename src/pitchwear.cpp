// PES21 FX - desgaste de la cancha en tiempo real (20/10), como en FC 24: un mapa de desgaste (R16, 1024x512, en uv de
// la cancha: 140 x 79 m) que la DLL va pintando con las posiciones de los jugadores durante el partido. Cada dibujo de
// short / medias (PS 5529...) copia la traslacion de m_world (cVSObject b5, fila 3) a un buffer de staging; en Present se
// leen las posiciones (2 cuadros de atraso), se estima la velocidad por el cuadro anterior y se dibujan pinceles
// gaussianos sumados sobre el mapa. El mapa se publica como recurso con nombre "pes_wear" (reglas: srv = 18 pes_wear en
// la base de la cancha, srv = 8 pes_wear en el VS de las hojas) y los shaders lo suman al desgaste del pasto v3.
//   x115 velocidad del desgaste, y115 radio del pincel (m), z115 pelado al maximo, w115 on/off. Se borra al empezar
//   otro partido (deteccion de "partido nuevo" del reloj del cesped).
#include "core.h"
#include <d3d11_1.h>
#include <atomic>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
#include <mutex>

namespace fx {

static const int kMaxPlayers = 48, kStages = 3;
static ID3D11Buffer* s_stage[kStages] = {}; static int s_stageFrame[kStages] = { -1, -1, -1 }; static std::atomic<int> s_stageW{0};
static std::atomic<int> s_count{0};          // dibujos anotados en el cuadro actual
static std::atomic<int> s_noteCalls{0}, s_noteOk{0}, s_noteShort{0}, s_noteNoStage{0}, s_noteBone{0}, s_boneShort{0}, s_boneNone{0}; static int s_posBad = 0, s_posZero = 0, s_posOk = 0, s_rowPick[4] = {}; static ID3D11Buffer* s_zeroBuf = nullptr;   // (08/11 aa) s_zeroBuf: 64 B en cero para limpiar el hueso del slot cuando el dibujo no tiene b6   // (08/11 z) diagnostico
static int s_frame = 0;
static ID3D11Texture2D* s_tex = nullptr; static ID3D11RenderTargetView* s_rtv = nullptr; static ID3D11ShaderResourceView* s_srv = nullptr;
static ID3D11VertexShader* s_vs = nullptr; static ID3D11PixelShader* s_ps = nullptr; static bool s_tried = false;
static ID3D11Buffer* s_cb = nullptr; static ID3D11BlendState* s_bsAdd = nullptr; static ID3D11RasterizerState* s_rs = nullptr; static ID3D11DepthStencilState* s_dss = nullptr;
static std::vector<float> s_prev;           // posiciones del cuadro anterior (x, z)
// (2/11) tanda 14 "reacciones" (pedido de Agustin: mas rastro, mas desgaste por los jugadores, pelota manchada). Fila 177:
//   x manchas de la pelota (verde / tierra / cal; 0 = no, 1 = natural), y frenadas y arranques marcan mas (0..2), z rastro de la
//   pelota segun su velocidad (0..2). El desgaste se duplica en la CPU (s_wearCpu, 256 x 128 celdas de 55 x 62 cm) para saber cuanta
//   tierra hay bajo la pelota sin leer la GPU; la pelota se reconoce entre los dibujos del PS de botines/pelota (aadffecf) porque la
//   traslacion de su m_world coincide con la capsula de la pelota del pasto (s_ball): el indice de ese dibujo queda como "la pelota"
//   y en sus dibujos se enlaza b12 (PESBallC: x 1, y verde, z tierra, w cal).
struct PlayerPrev { float x, z, vx, vz, skid; float stride = 0.f; int foot = 0; float printX = 0.f, printZ = 0.f; int printDue = 0; float svx = 0.f, svz = 0.f, sax = 0.f, saz = 0.f; int trk = 0; };   // (11/11) sv / sa: velocidad y aceleracion suavizadas (m/s, m/s2); trk: cuadros seguidos
// (08/11, Agustin: "las pisadas tienen que ser mejoradas ... desprendimiento realista del pasto con mantos de tierra") fila 389:
//   x huellas marcadas (pisada por pisada, pie izquierdo / derecho, en vez de una mancha continua) | y fuerza de la huella |
//   z cantidad de pasto que salta | w segundos que los terrones quedan en el piso
// fila 390: x reaccion (sensibilidad a frenadas, giros y remates) | y tamano de los terrones (cm) | z polvo y tierra | w calidad (0 baja, 1 normal, 2 alta)
static inline float FpOn()      { return g_cfg.live[389][0] > 0.5f ? 1.f : 0.f; }
static inline float FpForce()   { return g_cfg.live[389][1] > 0.f ? std::clamp(g_cfg.live[389][1], 0.f, 2.f) : 1.f; }
static inline float ClodAmt()   { return std::clamp(g_cfg.live[389][2], 0.f, 3.f); }
static inline float ClodRest()  { return std::clamp(g_cfg.live[389][3], 0.f, 40.f); }
static inline float ReactK()    { return g_cfg.live[390][0] > 0.05f ? std::clamp(g_cfg.live[390][0], 0.3f, 2.f) : 1.f; }
static inline float ClodSize()  { return (g_cfg.live[390][1] > 0.5f ? std::clamp(g_cfg.live[390][1], 2.f, 12.f) : 6.f) * 0.01f; }
static inline float DustAmt()   { return std::clamp(g_cfg.live[390][2], 0.f, 2.f); }
static inline int   ClipCap()   { int q = (int)(g_cfg.live[390][3] + 0.5f); return q <= 0 ? 512 : (q == 1 ? 1024 : 2048); }
// pinceles de desgaste pendientes (huecos que deja el pasto arrancado), los consume la pasada del desgaste
struct WearBrush { float u, v, k, rad; };
static std::vector<WearBrush> s_wearQueue;
static std::vector<PlayerPrev> s_pl;        // jugadores del cuadro anterior con su velocidad (m/s) y cuanto frenaban (0..1)
static float s_wearCpu[128][256] = {};
static float s_stain[3] = {};               // verde, tierra, cal (0..1)
static ID3D11Buffer* s_ballCb = nullptr;    // b12 del PS de la pelota
static const int kBallSlots = 128;   // (2/11 paso 10) 64 desbordaba en silencio (22 x 2 botines + arbitros + pelota + repeticiones)
static ID3D11Buffer* s_bdStage[kStages] = {}; static int s_bdFrame[kStages] = { -1, -1, -1 }; static std::atomic<int> s_bdW{0}; static std::atomic<int> s_bdCount{0};
static UINT s_bdIdx[kStages][kBallSlots] = {};   // indice (vertices) de cada dibujo anotado, por anillo
static std::atomic<UINT> s_ballIdx{0};      // indice de vertices del dibujo de la pelota (0 = aun no)
static std::atomic<int> s_bdCalls{0}, s_bdOffX177{0}, s_bdNoStage{0}, s_bdNoCb{0}, s_bdNoted{0};   // (2/11 b) diagnostico: por que no se anota
static std::atomic<int> s_ballIdxVotes{0}; static int s_ballLogged = 0;
static std::atomic<int> s_bdOverflow{0}; static int s_ballChanges = 0; static UINT s_ballHist[10] = {}; static int s_ballHp = 0;   // (2/11 paso 10) dibujos que no entraron en los slots; cambios del indice votado
static void WearCpuAdd(float u, float v, float k, float radM) {
    // mismo pincel que pes_wear_ps.hlsl: k * exp(-r2 * 2.2), r en radios, hasta 1.5 radios
    float cu = u * 256.f, cv = v * 128.f, ru = radM / 140.f * 256.f, rv = radM / 79.f * 128.f;
    int u0 = std::max(0, (int)(cu - 1.5f * ru)), u1 = std::min(255, (int)(cu + 1.5f * ru) + 1), v0 = std::max(0, (int)(cv - 1.5f * rv)), v1 = std::min(127, (int)(cv + 1.5f * rv) + 1);
    for (int j = v0; j <= v1; j++) for (int i = u0; i <= u1; i++) {
        float dx = (i + 0.5f - cu) / std::max(ru, 0.01f), dy = (j + 0.5f - cv) / std::max(rv, 0.01f); float r2 = dx * dx + dy * dy;
        if (r2 < 2.25f) s_wearCpu[j][i] += k * expf(-r2 * 2.2f);
    }
}
static float WearCpuAt(float x, float z) {
    int i = std::clamp((int)((x / 140.f + 0.5f) * 256.f), 0, 255), j = std::clamp((int)((z / 79.f + 0.5f) * 128.f), 0, 127);
    return s_wearCpu[j][i];
}
// distancia (m) a la linea de cal mas cercana de una cancha de 105 x 68 centrada (x a lo largo, z a lo ancho)
static float SegDist(float px, float pz, float ax, float az, float bx, float bz) {
    float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz; float t = l2 > 1e-6f ? std::clamp(((px - ax) * dx + (pz - az) * dz) / l2, 0.f, 1.f) : 0.f;
    float qx = ax + dx * t - px, qz = az + dz * t - pz; return sqrtf(qx * qx + qz * qz);
}
static float LineDist(float x, float z) {
    const float L = 52.5f, W = 34.f; float d = 1e9f;
    auto seg = [&](float ax, float az, float bx, float bz) { d = std::min(d, SegDist(x, z, ax, az, bx, bz)); };
    seg(-L, -W, L, -W); seg(-L, W, L, W); seg(-L, -W, -L, W); seg(L, -W, L, W); seg(0, -W, 0, W);
    d = std::min(d, fabsf(sqrtf(x * x + z * z) - 9.15f));
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float gx = sgn * L, bx = sgn * (L - 16.5f), ax = sgn * (L - 5.5f), px = sgn * (L - 11.f);
        seg(gx, -20.16f, bx, -20.16f); seg(gx, 20.16f, bx, 20.16f); seg(bx, -20.16f, bx, 20.16f);
        seg(gx, -9.16f, ax, -9.16f); seg(gx, 9.16f, ax, 9.16f); seg(ax, -9.16f, ax, 9.16f);
        if (sgn * (x - bx) < 0.f) d = std::min(d, fabsf(sqrtf((x - px) * (x - px) + z * z) - 9.15f));   // semicirculo del area, fuera del area
    }
    return d;
}
static int s_matchGenSeen = -1; static bool s_cleared = false;
static double s_lastT = 0; static int s_logged = 0;
// ---- recortes de pasto (particulas en CPU, dibujadas con el PS de las hojas en la pasada del pasto)
struct Clip { float x, y, z, vx, vy, vz, age, life, size, rot; int kind = 0; int rest = 0; float spin = 0.f, ph = 0.f; };   // (11/11) spin: giro propio (rad/s), ph: fase del aleteo de las hojas   // (08/11) rest = 1: quedo en el piso (tarjeta acostada, w = giro + 10 * (tipo + 5))   // (2/11) kind: 0 manojo arrancado, 1 terron de tierra, 2 polvo, 3 pedazo de hoja (bandas de textures\pes_debris.dds)
static thread_local bool t_clipDraw = false; bool PitchClipDrawActive() { return t_clipDraw; }
static const int kMaxClips = 2048;
static std::vector<Clip> s_clips;
static float Rand01();
// (11/11, Agustin: "como se calculan estos pedazitos que vuelan ... debe ser rehecho") RECORTES v2. Antes salian CADA CUADRO detras de todo
// jugador que corria (90 por segundo a 60 FPS, mas con mas FPS), de la cadera y no del pie, con la velocidad cruda (ruido = frenadas falsas),
// desaparecian en el aire a los 0.5-1.2 s y la pelota rodando tiraba pasto. Ahora salen SOLO en la pisada (una por media zancada) y segun lo
// que hace ese pie: impulso / sprint -> hacia atras; frenada -> hacia adelante y bajo; giro -> hacia afuera de la curva; remate -> hacia
// donde sale la pelota. Trotar o caminar no arranca nada. Cantidad por pisada (no depende de los FPS).
//   tipos: 0 manojo arrancado con raiz, 1 terron de tierra, 2 polvo, 3 pedazo de hoja (bandas de textures\pes_debris.dds)
enum { kClipTuft = 0, kClipClod = 1, kClipDust = 2, kClipBlade = 3 };
static std::atomic<int> s_strikeN{0}; static std::atomic<int> s_bornN[4];
static void PushClip(int kind, float x, float y, float z, float vx, float vy, float vz, float size) {
    if ((int)s_clips.size() >= ClipCap()) return;
    Clip c; c.x = x; c.y = y; c.z = z; c.vx = vx; c.vy = vy; c.vz = vz; c.age = 0.f; c.size = size; c.rot = Rand01() * 6.2832f; c.kind = kind;
    float sg = Rand01() < 0.5f ? -1.f : 1.f;
    c.spin = sg * (kind == kClipBlade ? 8.f + 14.f * Rand01() : kind == kClipTuft ? 4.f + 6.f * Rand01() : kind == kClipClod ? 2.f + 5.f * Rand01() : 0.3f);
    c.ph = Rand01() * 6.2832f;
    c.life = kind == kClipDust ? 0.7f + 0.7f * Rand01() : 4.0f;   // en el aire viven hasta tocar el piso (4 s de seguridad); el polvo se disipa
    s_clips.push_back(c); s_bornN[kind & 3]++;
}
// (x, z) donde pisa el pie, (tx, tz) hacia donde sale despedido (unitario), inten 0..~1.5 (fuerza de la accion)
static void EmitStrike(float x, float z, float tx, float tz, float inten, bool fromBall) {
    const float* C = g_cfg.live[116]; if (C[3] < 0.5f || inten < 0.05f) return;
    if ((int)s_clips.size() >= ClipCap() - 4) return;
    s_strikeN++;
    const float amount = C[0] > 0.f ? C[0] : 1.0f, bsz = (C[1] > 0.f ? C[1] : 3.0f) * 0.01f;
    const float w = WearCpuAt(x, z); const bool dry = g_cfg.live[114][0] > 1.5f, wet = g_cfg.live[114][0] > 0.5f && !dry;
    const float px = -tz, pz = tx;   // costado del tiro
    const float I1 = std::min(inten, 1.f);
    // pedazos de hoja sueltos: pocos en el sprint (~0.5 por pisada), muchos en la frenada / el giro fuerte. Abanico mas cerrado con mas fuerza
    float want = amount * 4.5f * inten; int n = (int)want; if (Rand01() < want - n) n++; n = std::min(n, 14);
    for (int e = 0; e < n; e++) {
        float a = (Rand01() - 0.5f) * (1.6f - 0.7f * I1);
        float dx = tx + px * a, dz = tz + pz * a, l = sqrtf(dx * dx + dz * dz) + 1e-4f; dx /= l; dz /= l;
        float hs = (0.5f + 2.0f * inten) * (0.5f + Rand01()), vy = (0.6f + 1.5f * inten) * (0.6f + 0.8f * Rand01());
        PushClip(kClipBlade, x + (Rand01() - 0.5f) * 0.12f, 0.03f, z + (Rand01() - 0.5f) * 0.12f, dx * hs, vy, dz * hs, bsz * (0.6f + 0.8f * Rand01()));
    }
    // manojos con raiz y terrones (el "divot"): solo acciones fuertes y con x389 z > 0; en lo pelado sale tierra, con cancha humeda mas
    const float clodA = ClodAmt();
    if (clodA > 0.001f && inten > 0.45f) {
        float wantC = clodA * (inten - 0.45f) * (wet ? 3.0f : 2.0f); int m = (int)wantC; if (Rand01() < wantC - m) m++; m = std::min(m, 4);
        const float csz = ClodSize(), dust = DustAmt();
        for (int e = 0; e < m; e++) {
            bool soil = (w > 0.25f && Rand01() < w) || Rand01() < 0.25f;
            float a = (Rand01() - 0.5f) * 0.7f; float dx = tx + px * a, dz = tz + pz * a, l = sqrtf(dx * dx + dz * dz) + 1e-4f; dx /= l; dz /= l;
            float hs = (0.8f + 1.6f * inten) * (0.6f + 0.8f * Rand01()), vy = (1.0f + 1.6f * inten) * (0.6f + 0.7f * Rand01());
            PushClip(soil ? kClipClod : kClipTuft, x, 0.02f, z, dx * hs, vy, dz * hs, csz * (0.6f + 0.8f * Rand01()) * (soil ? 0.8f : 1.f));
            if (dust > 0.001f && (soil || dry) && Rand01() < dust * (dry ? 0.9f : 0.4f))
                PushClip(kClipDust, x, 0.03f, z, dx * hs * 0.4f, vy * 0.35f, dz * hs * 0.4f, csz * 2.5f * (0.7f + 0.6f * Rand01()));
        }
        // hueco en el desgaste: donde se arranco el pasto queda la tierra
        if (m > 0 && g_cfg.live[115][3] > 0.5f) { float k = 0.03f * std::min((float)m * 1.5f, 4.f) * (fromBall ? 1.5f : 1.f); s_wearQueue.push_back({ x / 140.f + 0.5f, z / 79.f + 0.5f, k, 0.14f }); WearCpuAdd(x / 140.f + 0.5f, z / 79.f + 0.5f, k, 0.14f); }
    }
    // tierra seca / pelada: polvo aunque no salga terron
    if (DustAmt() > 0.001f && (dry || w > 0.4f) && Rand01() < DustAmt() * 0.5f * I1)
        PushClip(kClipDust, x, 0.03f, z, tx * 0.6f, 0.25f, tz * 0.6f, ClodSize() * 2.5f * (0.7f + 0.6f * Rand01()));
}
static ID3D11Buffer* s_clipBuf = nullptr; static ID3D11ShaderResourceView* s_clipSRV = nullptr; static ID3D11VertexShader* s_clipVS = nullptr; static bool s_clipTried = false;
static std::atomic<int> s_clipCount{0}; static std::atomic<int> s_clipFrame{-1};
static float Rand01() { static uint32_t st = 0x9E3779B9u; st ^= st << 13; st ^= st >> 17; st ^= st << 5; return (st & 0xffffff) / 16777216.0f; }
static void ClipSimAndUpload(float dt) {
    if (!g_dev.dev || !g_dev.imm) return;
    if (!s_clipBuf) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxClips * 32; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_SHADER_RESOURCE; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; d.StructureByteStride = 16;
        if (SUCCEEDED(g_dev.dev->CreateBuffer(&d, nullptr, &s_clipBuf))) {
            D3D11_SHADER_RESOURCE_VIEW_DESC v = {}; v.Format = DXGI_FORMAT_UNKNOWN; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; v.Buffer.FirstElement = 0; v.Buffer.NumElements = kMaxClips * 2;
            g_dev.dev->CreateShaderResourceView(s_clipBuf, &v, &s_clipSRV);
        }
    }
    if (!s_clipTried) { s_clipTried = true; ID3DBlob* vb = CompileNamedShader(L"pes_clip_vs.hlsl", "vs_5_0");
        if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_clipVS); vb->Release(); }
        Log("recortes de pasto: shader %s", s_clipVS ? "listo" : "NO compilo (ver errores)"); }
    // viento de las nubes (z84 direccion, y84 velocidad)
    float ang = g_cfg.live[84][2] * 0.0174533f, wsp = std::min(std::max(g_cfg.live[84][1], 0.f), 60.f) * 0.03f;
    float wx = cosf(ang) * wsp, wz = sinf(ang) * wsp;
    // (11/11) RECORTES v2: gravedad real y arrastre del aire por tipo (velocidad terminal: hoja ~1.2 m/s, manojo ~5, terron ~12): la hoja
    // planea y aletea de costado, el terron sigue una parabola y pica. Arrastre implicito (estable con cualquier dt)
    size_t n = 0;
    for (size_t i = 0; i < s_clips.size(); i++) {
        Clip& c = s_clips[i];
        if (c.rest) { c.age += dt; if (c.age < c.life) s_clips[n++] = c; continue; }   // (08/11) en el piso: quieto hasta que se desvanece
        c.age += dt;
        if (c.kind == kClipDust) {   // polvo: casi sin peso, lo lleva el aire, se frena rapido, crece y se va
            float a = std::min(1.f, 3.0f * dt); c.vx += (wx - c.vx) * a; c.vz += (wz - c.vz) * a; c.vy += (0.12f - c.vy) * a;
            c.size *= 1.0f + 0.7f * dt; c.x += c.vx * dt; c.y += c.vy * dt; c.z += c.vz * dt; c.rot += c.spin * dt;
            if (c.age < c.life) s_clips[n++] = c;
            continue;
        }
        const float kD = c.kind == kClipBlade ? 6.5f : c.kind == kClipTuft ? 0.35f : 0.07f;   // arrastre cuadratico (vt = sqrt(9.81 / kD))
        const float kW = c.kind == kClipBlade ? 1.0f : c.kind == kClipTuft ? 0.15f : 0.0f;    // cuanto lo lleva el viento
        float rx = c.vx - wx * kW, rz = c.vz - wz * kW, ry = c.vy; float sp = sqrtf(rx * rx + ry * ry + rz * rz);
        float f = 1.0f / (1.0f + kD * sp * dt);
        c.vx = wx * kW + rx * f; c.vz = wz * kW + rz * f; c.vy = ry * f - 9.81f * dt;
        if (c.kind == kClipBlade) {   // aleteo: la hoja planea de costado (perpendicular a su avance) y se mece
            c.ph += (6.0f + 0.25f * fabsf(c.spin)) * dt;
            float hs = sqrtf(c.vx * c.vx + c.vz * c.vz) + 1e-3f, sx = -c.vz / hs, sz = c.vx / hs, fl = 2.8f * sinf(c.ph);
            c.vx += sx * fl * dt; c.vz += sz * fl * dt; c.vy += 1.4f * cosf(c.ph * 1.3f) * dt * std::min(1.f, sp);
        }
        c.x += c.vx * dt; c.y += c.vy * dt; c.z += c.vz * dt; c.rot += c.spin * dt;
        if (c.y <= 0.010f && c.vy < 0.f) {
            // toca el pasto: el terron pica (y rueda un poco), el manojo apenas, la hoja se queda; despues quedan acostados (w389 s)
            float restS = ClodRest();
            if (c.kind == kClipClod && c.vy < -1.0f) { c.y = 0.011f; c.vy = -c.vy * 0.30f; c.vx *= 0.55f; c.vz *= 0.55f; c.spin *= 0.5f; s_clips[n++] = c; continue; }
            if (c.kind == kClipTuft && c.vy < -1.6f) { c.y = 0.011f; c.vy = -c.vy * 0.15f; c.vx *= 0.4f; c.vz *= 0.4f; c.spin *= 0.4f; s_clips[n++] = c; continue; }
            if (restS < 0.1f) continue;
            if (fabsf(c.x) > 55.f || fabsf(c.z) > 36.f) continue;   // fuera de la cancha no queda nada
            c.rest = 1; c.y = c.kind == kClipBlade ? 0.012f + 0.010f * Rand01() : 0.008f; c.vx = c.vz = c.vy = 0.f; c.age = 0.f;
            c.life = restS * (0.6f + 0.8f * Rand01()) * (c.kind == kClipBlade ? 0.35f : 1.f);   // la hoja suelta se pierde entre el pasto antes
            s_clips[n++] = c; continue;
        }
        if (c.age < c.life) s_clips[n++] = c;
    }
    s_clips.resize(n);
    if (!s_clipBuf || !s_clipSRV) return;
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_dev.imm->Map(s_clipBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    float* q = (float*)m.pData;
    for (size_t i = 0; i < n; i++) { const Clip& c = s_clips[i]; float fade = std::min(1.0f, (c.life - c.age) * (c.rest ? 1.0f : (c.kind == kClipDust ? 2.0f : 4.0f)));
        q[i * 8 + 0] = c.x; q[i * 8 + 1] = c.y; q[i * 8 + 2] = c.z; q[i * 8 + 3] = c.size * fade;
        q[i * 8 + 4] = c.vx; q[i * 8 + 5] = c.vy; q[i * 8 + 6] = c.vz; q[i * 8 + 7] = fmodf(c.rot, 6.2832f) + 10.0f * (float)(c.kind + (c.rest ? 5 : 0)); }   // (2/11) w = giro + 10 * tipo; (08/11) + 50 si esta acostado en el piso
    g_dev.imm->Unmap(s_clipBuf, 0);
    s_clipCount = (int)n;
}
// desde GrassOnDraw (estado del pasto puesto: G-buffer, profundidad, mezcla): un dibujo instanciado con nuestro VS y el PS de las hojas
void PitchClipsDraw(ID3D11DeviceContext* c, int frame) {
    int n = s_clipCount.load(); if (n <= 0 || !s_clipVS || !s_clipSRV || g_cfg.live[116][3] < 0.5f) return;
    s_clipFrame = frame;   // (26/10 d) "una vez por grabacion" lo decide GrassOnDraw (el cuadro de grabacion no identifica el de la GPU)
    ID3D11VertexShader* oVS = nullptr; c->VSGetShader(&oVS, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL);
    ID3D11ShaderResourceView* oT[2] = {}; c->VSGetShaderResources(120, 1, &oT[0]); c->VSGetShaderResources(122, 1, &oT[1]);
    ID3D11ShaderResourceView* prm = g_dev.paramSRV; c->VSSetShaderResources(120, 1, &prm); c->VSSetShaderResources(122, 1, &s_clipSRV);
    c->VSSetShader(s_clipVS, nullptr, 0); c->IASetInputLayout(nullptr);
    t_clipDraw = true;   // (2/11) BeforeDraw enlaza el atlas de particulas (pes_debris) en t8 / t9 en vez del de hojas
    c->DrawInstanced(6, (UINT)n, 0, 0);   // pasa por el gancho: PS de las hojas reemplazado + reglas (atlas t8/t9, t120)
    t_clipDraw = false;
    c->VSSetShader(oVS, nullptr, 0); if (oVS) oVS->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release();
    c->VSSetShaderResources(120, 1, &oT[0]); c->VSSetShaderResources(122, 1, &oT[1]); for (auto* v : oT) if (v) v->Release();
}
void PitchClipsReloadShaders() { if (s_clipVS) { s_clipVS->Release(); s_clipVS = nullptr; } s_clipTried = false; }

static void EnsureRes() {
    if (!g_dev.dev) return;
    for (int i = 0; i < kStages; i++) if (!s_stage[i]) {
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxPlayers * 192; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;   // (08/11 z) 8 filas de b5 por dibujo + (08/11 aa) 64 B del hueso 0 de b6. OJO (08/11 aa): desde 4c92e60 el CPUAccessFlags habia quedado dentro del comentario -> el Map fallaba y NINGUNA posicion se leia (por eso los jugadores no disparaban nada)
        g_dev.dev->CreateBuffer(&d, nullptr, &s_stage[i]);
    }
    if (!s_zeroBuf) { float z[16] = {}; D3D11_BUFFER_DESC d = {}; d.ByteWidth = 64; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE; D3D11_SUBRESOURCE_DATA sd = { z, 0, 0 }; g_dev.dev->CreateBuffer(&d, &sd, &s_zeroBuf); }
    if (!s_tex) {
        D3D11_TEXTURE2D_DESC td{}; td.Width = 1024; td.Height = 512; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R16_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (SUCCEEDED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_tex))) {
            g_dev.dev->CreateRenderTargetView(s_tex, nullptr, &s_rtv); g_dev.dev->CreateShaderResourceView(s_tex, nullptr, &s_srv);
            if (s_srv) NamedSetSRV("pes_wear", s_srv);
        }
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxPlayers * 16 + 16; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_dev.dev->CreateBuffer(&d, nullptr, &s_cb);
        D3D11_BLEND_DESC bd{}; auto& rt = bd.RenderTarget[0]; rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_ONE; rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_ONE; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        g_dev.dev->CreateBlendState(&bd, &s_bsAdd);
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; g_dev.dev->CreateRasterizerState(&rd, &s_rs);
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = FALSE; g_dev.dev->CreateDepthStencilState(&dd, &s_dss);
    }
    if (!s_tried) { s_tried = true;
        ID3DBlob* vb = CompileNamedShader(L"pes_wear_vs.hlsl", "vs_5_0"); ID3DBlob* pb = CompileNamedShader(L"pes_wear_ps.hlsl", "ps_5_0");
        if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_vs); vb->Release(); }
        if (pb) { g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_ps); pb->Release(); }
        Log("desgaste en vivo: shaders %s", (s_vs && s_ps) ? "listos" : "NO compilaron (ver errores)");
    }
}
// ---------------------------------------------------------------- pasto aplastado temporal (20/10)
// Segundo mapa ("pes_flat", R16, mismas uv que el desgaste) que se apaga solo: cada cuadro se multiplica por exp(-dt/tau)
// y se suman pinceles donde estan los jugadores y donde rueda la pelota (con el rastro entre la posicion anterior y la
// actual). Las hojas lo leen para agacharse y abrirse hacia afuera del rastro y la base de la cancha para cambiar el grano
// (pisadas y rastro de la pelota que se ven y se recuperan en ~2 s). La pelota sale del dibujo del pasto del juego:
// cb4[3] y cb4[4] del VS de las hojas son la capsula de la pelota (posicion actual y anterior, radio 0.112 m).
//   136: x on | y fuerza | z recuperacion (s) | w fuerza de la pelota
static ID3D11Texture2D* s_flatTex = nullptr; static ID3D11RenderTargetView* s_flatRTV = nullptr; static ID3D11ShaderResourceView* s_flatSRV = nullptr;
static ID3D11VertexShader* s_fsVS = nullptr; static ID3D11PixelShader* s_zeroPS = nullptr; static bool s_flatTried = false;
static ID3D11BlendState* s_bsDecay = nullptr; static ID3D11Buffer* s_flatCB = nullptr; static ID3D11BlendState* s_bsAddR = nullptr; static ID3D11BlendState* s_bsAddG = nullptr;   // (27/10 n) pinceles al canal R (jugadores) o G (rastro de la pelota)
static ID3D11Buffer* s_ballStage[kStages] = {}; static int s_ballStageFrame[kStages] = { -1, -1, -1 }; static std::atomic<int> s_ballW{0}; static std::atomic<int> s_ballNoted{-1};
static float s_ball[3] = {}, s_ballPrev[3] = {}; static bool s_ballOk = false; static float s_ballSpPrev = 0.f; static int s_flatFrame = 0; static double s_flatT = 0; static int s_flatMatch = -1;
static void EnsureFlat() {
    if (!g_dev.dev) return;
    if (!s_flatTex) {
        // (27/10 n) tanda 11: R = pisadas de los jugadores (se levanta en z136 s), G = rastro de la pelota (dura z329 s, 30 por defecto):
        // Agustin queria ver por donde fue la pelota; con 2 s de recuperacion no quedaba rastro
        D3D11_TEXTURE2D_DESC td{}; td.Width = 1024; td.Height = 512; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R16G16_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (SUCCEEDED(g_dev.dev->CreateTexture2D(&td, nullptr, &s_flatTex))) {
            g_dev.dev->CreateRenderTargetView(s_flatTex, nullptr, &s_flatRTV); g_dev.dev->CreateShaderResourceView(s_flatTex, nullptr, &s_flatSRV);
            if (s_flatSRV) NamedSetSRV("pes_flat", s_flatSRV);
            if (s_flatRTV && g_dev.imm) { float z[4] = {}; g_dev.imm->ClearRenderTargetView(s_flatRTV, z); }
        }
        D3D11_BLEND_DESC bd{}; auto& rt = bd.RenderTarget[0]; rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_ZERO; rt.DestBlend = D3D11_BLEND_BLEND_FACTOR; rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ZERO; rt.DestBlendAlpha = D3D11_BLEND_BLEND_FACTOR; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        g_dev.dev->CreateBlendState(&bd, &s_bsDecay);
        { D3D11_BLEND_DESC ba{}; auto& r2 = ba.RenderTarget[0]; r2.BlendEnable = TRUE; r2.SrcBlend = D3D11_BLEND_ONE; r2.DestBlend = D3D11_BLEND_ONE; r2.BlendOp = D3D11_BLEND_OP_ADD;
          r2.SrcBlendAlpha = D3D11_BLEND_ONE; r2.DestBlendAlpha = D3D11_BLEND_ONE; r2.BlendOpAlpha = D3D11_BLEND_OP_ADD;
          r2.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED; g_dev.dev->CreateBlendState(&ba, &s_bsAddR);
          r2.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_GREEN; g_dev.dev->CreateBlendState(&ba, &s_bsAddG); }
        D3D11_BUFFER_DESC d = {}; d.ByteWidth = kMaxPlayers * 16 + 16; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_dev.dev->CreateBuffer(&d, nullptr, &s_flatCB);
    }
    for (int i = 0; i < kStages; i++) if (!s_ballStage[i]) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 32; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &s_ballStage[i]); }
    if (!s_flatTried) { s_flatTried = true;
        ID3DBlob* vb = CompileNamedShader(L"pes_fs_vs.hlsl", "vs_5_0"); ID3DBlob* pb = CompileNamedShader(L"pes_zero_ps.hlsl", "ps_5_0");
        if (vb) { g_dev.dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &s_fsVS); vb->Release(); }
        if (pb) { g_dev.dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &s_zeroPS); pb->Release(); }
        Log("pasto aplastado: shaders %s", (s_fsVS && s_zeroPS) ? "listos" : "NO compilaron");
    }
}
// desde GrassOnDraw (una vez por cuadro): la capsula de la pelota (cb4[3], cb4[4] del VS de las hojas) al staging
void PitchBallNote(ID3D11DeviceContext* c, int frame) {
    if (!g_dev.dev || (g_cfg.live[136][0] < 0.5f && !(g_cfg.live[139][0] > 0.5f && g_cfg.live[139][2] > 0.001f))) return;
    s_ballNoted = frame;   // (26/10 d) una vez por grabacion (lo decide GrassOnDraw)
    int w = s_ballW.load(); if (!s_ballStage[w]) return;
    ID3D11Buffer* cb = nullptr; UINT first = 0, num = 0; ID3D11DeviceContext1* c1 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) { c1->VSGetConstantBuffers1(4, 1, &cb, &first, &num); c1->Release(); }
    else c->VSGetConstantBuffers(4, 1, &cb);
    if (!cb) return;
    D3D11_BUFFER_DESC d; cb->GetDesc(&d); UINT off = first * 16 + 48;
    if (d.ByteWidth >= off + 32) { D3D11_BOX bx = { off, 0, 0, off + 32, 1, 1 }; c->CopySubresourceRegion(s_ballStage[w], 0, 0, 0, 0, cb, 0, &bx); s_ballStageFrame[w] = frame; }
    cb->Release();
}
// (11/11, Agustin: "estos deberian seguir el gameclock para que no se borren en la repeticion") RELOJ DEL JUEGO para los recortes, el
// seguimiento de los jugadores (pisadas, frenadas) y el pasto aplastado: el mismo de las nubes (fila 157, config.cpp: m_timeParam del
// juego; la pausa lo congela, la camara lenta de la repeticion lo frena, el salto hacia atras al empezarla no cuenta). En pausa los
// pedacitos quedan quietos en el aire y no se desvanecen; en la repeticion en camara lenta vuelan despacio y las acciones repetidas los
// vuelven a arrancar (la velocidad de los jugadores se mide en tiempo de juego). Hasta ver ese reloj avanzar (w157 = 0): el real.
static float GameStep(double& lastG, double& lastR) {
    const double t = Now(), g = (double)g_cfg.live[157][0];
    const double dR = lastR < 0.0 ? 0.0 : t - lastR; lastR = t;
    double dG = lastG < -1e8 ? 0.0 : g - lastG; lastG = g; if (dG < -50000.0) dG += 100000.0;   // vuelta del reloj de las nubes (100000 s)
    if (g_cfg.live[157][3] < 0.5f) return (float)std::clamp(dR, 0.0, 0.1);
    return (float)std::clamp(dG, 0.0, 0.1);
}
static int s_plGen = 0;   // (11/11) actualizaciones de s_pl: la huella de cada pisada se estampa una sola vez aunque el seguimiento se saltee cuadros (pausa)
static void PitchFlatPresent() {
    const float* F = g_cfg.live[136];
    bool v5 = g_cfg.live[139][0] > 0.5f && g_cfg.live[139][2] > 0.001f;   // (20/10) pasto v5: las hojas se doblan con el mapa
    if ((F[0] < 0.5f && !v5) || !g_dev.imm) return;
    EnsureFlat(); if (!s_flatRTV || !s_fsVS || !s_zeroPS || !s_vs || !s_ps || !s_flatCB || !s_bsAddR || !s_bsAddG) return;
    static double s_fG = -1e9, s_fR = -1.0; float dt = GameStep(s_fG, s_fR); s_flatT = Now(); s_flatFrame++;   // (11/11) reloj del juego
    static int s_flatPlGen = -1; const bool newPl = s_plGen != s_flatPlGen; s_flatPlGen = s_plGen;
    if (g_matchGen.load() != s_flatMatch) { s_flatMatch = g_matchGen.load(); float z[4] = {}; g_dev.imm->ClearRenderTargetView(s_flatRTV, z); s_ballOk = false; }
    // pelota: el staging mas viejo ya listo (anillo de 3)
    { int w = s_ballW.load(), rd = (w + 1) % kStages; s_ballW = rd;
      if (s_ballStageFrame[rd] >= 0 && s_ballStage[rd]) {
          D3D11_MAPPED_SUBRESOURCE m;
          if (SUCCEEDED(g_dev.imm->Map(s_ballStage[rd], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
              const float* q = (const float*)m.pData;
              bool ok = fabsf(q[0]) < 70.f && fabsf(q[2]) < 45.f && q[1] > -0.5f && q[1] < 20.f && (fabsf(q[0]) + fabsf(q[2]) > 0.01f);
              if (ok) { if (s_ballOk) { s_ballPrev[0] = s_ball[0]; s_ballPrev[1] = s_ball[1]; s_ballPrev[2] = s_ball[2]; } else { s_ballPrev[0] = q[0]; s_ballPrev[1] = q[1]; s_ballPrev[2] = q[2]; }
                        s_ball[0] = q[0]; s_ball[1] = q[1]; s_ball[2] = q[2]; s_ballOk = true; }
              g_dev.imm->Unmap(s_ballStage[rd], 0);
          }
          s_ballStageFrame[rd] = -1;
      } }
    float brush[kMaxPlayers][4]; int nb = 0; int nbP = 0;   // (27/10 n) los primeros nbP pinceles son de jugadores (canal R), el resto de la pelota (G)
    float k = std::clamp(F[1] > 0.f ? F[1] : 1.0f, 0.f, 2.f);
    // jugadores (ultimas posiciones conocidas del desgaste en vivo): pisada suave
    const float kAct = std::clamp(g_cfg.live[177][1], 0.f, 2.f);   // (2/11) frenadas: pisada mas fuerte y alargada hacia atras
    for (size_t i = 0; i < s_pl.size() && nb < kMaxPlayers - 16; i++) {
        const PlayerPrev& p = s_pl[i]; float sp = sqrtf(p.vx * p.vx + p.vz * p.vz);
        const float fpOn = FpOn();
        brush[nb][0] = p.x / 140.f + 0.5f; brush[nb][1] = p.z / 79.f + 0.5f; brush[nb][2] = 2.5f * dt * k * (1.f + 2.5f * p.skid * kAct) * (fpOn > 0.5f ? 0.35f : 1.f); brush[nb][3] = 0.45f; nb++;
        // (08/11) huella: una marca por pisada (pie izquierdo / derecho alternados, zancada segun la velocidad), con forma de botin
        // (talon mas fuerte + punta hacia adelante). La marca entera en un cuadro (no por dt): se ve cada pisada por separado
        if (fpOn > 0.5f && p.printDue && newPl && sp > 0.3f && nb < kMaxPlayers - 16) {
            float ux = p.vx / sp, uz = p.vz / sp, kf = FpForce() * k * (0.55f + 0.45f * std::min(sp / 6.f, 1.f)) * (1.f + 1.5f * p.skid * kAct);
            brush[nb][0] = p.printX / 140.f + 0.5f; brush[nb][1] = p.printZ / 79.f + 0.5f; brush[nb][2] = 0.75f * kf; brush[nb][3] = 0.13f; nb++;
            if (nb < kMaxPlayers - 16) { brush[nb][0] = (p.printX + ux * 0.12f) / 140.f + 0.5f; brush[nb][1] = (p.printZ + uz * 0.12f) / 79.f + 0.5f; brush[nb][2] = 0.55f * kf; brush[nb][3] = 0.10f; nb++; }
        }
        if (p.skid > 0.2f && kAct > 0.01f && sp > 1.f && nb < kMaxPlayers - 16) {   // marca de frenada: 2 pinceles detras, en la direccion de la que venia
            for (int e = 1; e <= 2 && nb < kMaxPlayers - 16; e++) { float bx = p.x - p.vx / sp * 0.35f * e, bz = p.z - p.vz / sp * 0.35f * e;
                brush[nb][0] = bx / 140.f + 0.5f; brush[nb][1] = bz / 79.f + 0.5f; brush[nb][2] = 2.0f * dt * k * p.skid * kAct; brush[nb][3] = 0.35f; nb++; }
        }
    }
    nbP = nb;
    // pelota en el piso: rastro entre la posicion anterior y la actual
    if (s_ballOk) {
        float onGround = std::clamp(1.0f - (s_ball[1] - 0.12f) / 0.25f, 0.0f, 1.0f);
        // (27/10 n) pique: la pelota venia del aire (> 40 cm) y toca el piso: marca redonda mas fuerte
        if (onGround > 0.5f && s_ballPrev[1] > 0.4f && nb < kMaxPlayers) {
            brush[nb][0] = s_ball[0] / 140.f + 0.5f; brush[nb][1] = s_ball[2] / 79.f + 0.5f; brush[nb][2] = 0.6f * k * (F[3] > 0.f ? F[3] : 1.0f); brush[nb][3] = 0.26f; nb++;
        }
        float dx = s_ball[0] - s_ballPrev[0], dz = s_ball[2] - s_ballPrev[2]; float dist = sqrtf(dx * dx + dz * dz);
        if (dist > 4.0f) dist = 0.0f;   // salto de camara / saque
        int steps = std::clamp((int)(dist / 0.08f) + 1, 1, 15);
        float kb = (F[3] > 0.f ? F[3] : 1.0f) * k * onGround;
        { const float kv = std::clamp(g_cfg.live[177][2], 0.f, 2.f); float spd = dt > 0.002f ? std::min(dist / dt / 12.f, 1.5f) : 0.f;   // (2/11) rapida: aplasta mas; lenta: menos
          kb *= std::max(0.25f, 1.f + kv * (spd - 0.5f)); }
        // (2/11) manchas de la pelota (fila 177 x). Agustin: PROGRESIVAS y MUY SUTILES, con los minutos de partido y segun la humedad de la
        // cancha; nada por contacto. La pelota solo "recuerda" por donde rueda (promedios lentos de tierra bajo ella y de cruces de cal) y
        // la mancha crece con el reloj: verde ~ 25 min, tierra ~ 30 min (x2 con cancha humeda), cal ~ 20 min (menos con humedad)
        { const float kS = std::clamp(g_cfg.live[177][0], 0.f, 2.f);
          if (kS > 0.001f) {
              static float wearEma = 0.f, lineEma = 0.f;
              float roll = (dist > 0.005f && dist < 4.f) ? onGround : 0.f;
              if (roll > 0.f) { float a = std::min(1.f, dt / 20.f);   // promedios de ~20 s de rodado
                  wearEma += (std::clamp(WearCpuAt(s_ball[0], s_ball[2]), 0.f, 1.f) - wearEma) * a;
                  lineEma += ((LineDist(s_ball[0], s_ball[2]) < 0.12f ? 1.f : 0.f) - lineEma) * a; }
              float mins = std::max(g_cfg.live[10][0], 0.f);                   // minutos reales de partido (0 fuera del partido)
              float wet = g_cfg.live[114][0] > 0.5f && g_cfg.live[114][0] < 1.5f ? 1.f : 0.f, dry = g_cfg.live[114][0] > 1.5f ? 1.f : 0.f;
              float tg = 0.30f * (1.f - expf(-mins / 25.f)) * (1.f + 0.6f * wet);
              float td = 0.35f * (1.f - expf(-mins / 30.f)) * (0.3f + 0.7f * wearEma) * (1.f + 1.5f * wet + 0.3f * dry);
              float tc = 0.20f * (1.f - expf(-mins / 20.f)) * (0.2f + 0.8f * std::min(lineEma * 6.f, 1.f)) * (1.f - 0.7f * wet);
              float a2 = std::min(1.f, dt / 8.f);                                // se acerca al objetivo en ~8 s: sin saltos
              s_stain[0] += (tg - s_stain[0]) * a2; s_stain[1] += (td - s_stain[1]) * a2; s_stain[2] += (tc - s_stain[2]) * a2;
              if (mins < 0.01f) { wearEma *= 0.99f; lineEma *= 0.99f; }
              for (float& v : s_stain) v = std::clamp(v, 0.f, 1.f);
          } else s_stain[0] = s_stain[1] = s_stain[2] = 0.f; }
        for (int sIdx = 0; sIdx < steps && nb < kMaxPlayers; sIdx++) {
            float a = steps > 1 ? (float)sIdx / (steps - 1) : 1.0f;
            float bx = s_ballPrev[0] + dx * a, bz = s_ballPrev[2] + dz * a;
            brush[nb][0] = bx / 140.f + 0.5f; brush[nb][1] = bz / 79.f + 0.5f; brush[nb][2] = std::min(0.2f, 0.04f + 2.5f * dt) * kb; brush[nb][3] = 0.21f; nb++;   // (2/11) Agustin: que marque mas al rodar. (2/11 c) antes 0.12 + 9 dt (~0.27 por cuadro): saturaba el rastro y el pasto quedaba 'aplastado todo el tiempo'
        }
        // (08/11, Agustin: "desprendimiento ... chutando") REMATE: la pelota pasa de lenta a rapida en un cuadro, estando en el piso.
        // El pie de apoyo y el botin arrancan pasto detras de donde estaba la pelota, hacia adelante (el golpe) y un poco hacia los costados
        float sp = dt > 0.002f ? dist / dt : 0.f;   // (11/11) en pausa (reloj del juego quieto) no hay remate
        { float rk = ReactK(); float thr = 9.0f / rk, jump = 5.0f / rk;
          if (sp > thr && sp - s_ballSpPrev > jump && sp < 60.f && s_ballPrev[1] < 0.35f && dist > 0.01f) {
              float ix = dx / dist, iz = dz / dist; float strength = std::min((sp - thr) / 14.f + 0.5f, 2.0f) * rk;
              // (11/11) recortes v2: el botin raspa el pasto debajo / detras de la pelota y salen hacia donde va el tiro
              EmitStrike(s_ballPrev[0] - ix * 0.25f, s_ballPrev[2] - iz * 0.25f, ix, iz, std::min(0.30f + 0.35f * strength, 1.2f), true);
              static int kickLog = 0; if (kickLog < 3) { kickLog++; Log("pasto: remate a %.1f m/s (antes %.1f): terrones en (%.1f, %.1f)", sp, s_ballSpPrev, s_ballPrev[0], s_ballPrev[2]); }
          }
          s_ballSpPrev = (dist > 0.f && sp < 60.f) ? sp : 0.f; }
        // (11/11) recortes v2: la pelota rodando ya no tira pasto (antes un recorte el 60 % de los cuadros a mas de 9 m/s); solo el remate
    }
    ID3D11DeviceContext* c = g_dev.imm;
    ID3D11RenderTargetView* oRT[8] = {}; ID3D11DepthStencilView* oDS = nullptr; c->OMGetRenderTargets(8, oRT, &oDS);
    UINT nvp = 8; D3D11_VIEWPORT ovp[8]; c->RSGetViewports(&nvp, ovp);
    ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr;
    c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL); D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS); ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDSS, &oRef);
    ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0; c->OMGetBlendState(&oBS, oBF, &oSM);
    ID3D11Buffer* oCB = nullptr; c->VSGetConstantBuffers(0, 1, &oCB);
    c->OMSetRenderTargets(1, &s_flatRTV, nullptr); D3D11_VIEWPORT vp{ 0, 0, 1024, 512, 0, 1 }; c->RSSetViewports(1, &vp);
    c->GSSetShader(nullptr, nullptr, 0); c->IASetInputLayout(nullptr);
    c->RSSetState(s_rs); c->OMSetDepthStencilState(s_dss, 0);
    // 1) se apaga de a poco: dest *= exp(-dt / tau)
    float tau = std::clamp(F[2] > 0.1f ? F[2] : 2.0f, 0.3f, 10.0f); float f = expf(-dt / tau);
    // (27/10 n) el rastro de la pelota (canal G) se desvanece en z329 s (0 = 30)
    float tauB = std::clamp(g_cfg.live[329][2] > 0.5f ? g_cfg.live[329][2] : 30.0f, 1.0f, 300.0f); float fB = expf(-dt / tauB); float bfD[4] = { f, fB, fB, fB };
    c->VSSetShader(s_fsVS, nullptr, 0); c->PSSetShader(s_zeroPS, nullptr, 0); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->OMSetBlendState(s_bsDecay, bfD, 0xffffffff); c->Draw(3, 0);
    // 2) pinceles nuevos (suma)
    if (nb > 0) {
        D3D11_MAPPED_SUBRESOURCE mc;
        if (SUCCEEDED(c->Map(s_flatCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) {
            float* cbp = (float*)mc.pData; cbp[0] = (float)nb; cbp[1] = 140.f; cbp[2] = 79.f; cbp[3] = 0.f; memcpy(cbp + 4, brush, nb * 16); c->Unmap(s_flatCB, 0);
            float bf[4] = {};
            c->VSSetShader(s_vs, nullptr, 0); c->PSSetShader(s_ps, nullptr, 0); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            c->VSSetConstantBuffers(0, 1, &s_flatCB);
            // (27/10 n) jugadores al canal R, pelota al canal G (el pincel escribe los 4 canales; la mascara elige)
            // (SV_InstanceID arranca en 0 en cada dibujo: el primer pincel de la pelota va en hdr.w y el VS lo suma)
            if (nbP > 0) { c->OMSetBlendState(s_bsAddR, bf, 0xffffffff); c->DrawInstanced(4, (UINT)nbP, 0, 0); }
            if (nb > nbP) {
                if (SUCCEEDED(c->Map(s_flatCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) {
                    float* cb2 = (float*)mc.pData; cb2[0] = (float)nb; cb2[1] = 140.f; cb2[2] = 79.f; cb2[3] = (float)nbP; memcpy(cb2 + 4, brush, nb * 16); c->Unmap(s_flatCB, 0);
                    c->OMSetBlendState(s_bsAddG, bf, 0xffffffff); c->DrawInstanced(4, (UINT)(nb - nbP), 0, 0);
                }
            }
        }
    }
    c->OMSetRenderTargets(8, oRT, oDS); for (auto* v : oRT) if (v) v->Release(); if (oDS) oDS->Release();
    c->RSSetViewports(nvp, ovp);
    c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0); if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release(); c->IASetPrimitiveTopology(oTopo);
    c->RSSetState(oRS); if (oRS) oRS->Release(); c->OMSetDepthStencilState(oDSS, oRef); if (oDSS) oDSS->Release();
    c->OMSetBlendState(oBS, oBF, oSM); if (oBS) oBS->Release();
    c->VSSetConstantBuffers(0, 1, &oCB); if (oCB) oCB->Release();
    static int logged = 0; if (logged < 2 && s_ballOk) { logged++; Log("pasto aplastado: pelota en (%.1f, %.2f, %.1f), %d pinceles", s_ball[0], s_ball[1], s_ball[2], nb); }
}

void PitchClipsReloadShaders();
void PitchWearReloadShaders() { if (s_vs) { s_vs->Release(); s_vs = nullptr; } if (s_ps) { s_ps->Release(); s_ps = nullptr; } s_tried = false; if (s_fsVS) { s_fsVS->Release(); s_fsVS = nullptr; } if (s_zeroPS) { s_zeroPS->Release(); s_zeroPS = nullptr; } s_flatTried = false; PitchClipsReloadShaders(); }

// en el dibujo del short / medias: copiar la traslacion de m_world (b5 del VS, fila 3 = bytes 112..127) al staging del cuadro
void PitchWearNotePlayer(ID3D11DeviceContext* c) {
    if (!g_dev.dev || g_cfg.live[115][3] < 0.5f) return;
    s_noteCalls++;
    int w = s_stageW.load(); if (!s_stage[w]) { s_noteNoStage++; return; }
    int i = s_count.fetch_add(1); if (i >= kMaxPlayers) return;
    ID3D11Buffer* cb = nullptr; UINT first = 0, num = 0; ID3D11DeviceContext1* c1 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) { c1->VSGetConstantBuffers1(5, 1, &cb, &first, &num); c1->Release(); }
    else c->VSGetConstantBuffers(5, 1, &cb);
    if (!cb) return;
    D3D11_BUFFER_DESC d; cb->GetDesc(&d); UINT off = first * 16;
    // (08/11 z) se copian las 8 primeras filas de b5 (128 B) y en Present se elige la que parece una posicion de la cancha (fila 7, fila 3 o la
    // columna w de las filas 4..6, como con la pelota): antes solo la fila 7 y, segun la malla / el mod de kits, la posicion caia fuera de rango
    if (d.ByteWidth >= off + 128) { D3D11_BOX bx = { off, 0, 0, off + 128, 1, 1 }; c->CopySubresourceRegion(s_stage[w], 0, (UINT)i * 192, 0, 0, cb, 0, &bx); s_noteOk++; }
    else s_noteShort++;
    cb->Release();
    // (08/11 aa, Agustin: "los jugadores no disparan nada") el cuerpo va con esqueleto (VSBones b6, float4x3 m_boneMatrices[32]): si m_world
    // (b5) es la identidad la posicion del jugador esta en el hueso 0 (pelvis). Se copian sus 3 registros (48 B; la traslacion es la columna w,
    // como lee sb0 el VS del cuerpo 1c02) y en Present se calcula m_world * hueso0. Si no hay b6 (malla rigida) quedan los candidatos de b5.
    { ID3D11Buffer* bb = nullptr; UINT f6 = 0, n6 = 0; ID3D11DeviceContext1* c1b = nullptr;
      if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1b)) && c1b) { c1b->VSGetConstantBuffers1(6, 1, &bb, &f6, &n6); c1b->Release(); }
      else c->VSGetConstantBuffers(6, 1, &bb);
      D3D11_BOX bz = { 0, 0, 0, 64, 1, 1 };
      if (bb) { D3D11_BUFFER_DESC d6; bb->GetDesc(&d6); UINT o6 = f6 * 16;
          if (d6.ByteWidth >= o6 + 48) { D3D11_BOX b6 = { o6, 0, 0, o6 + 48, 1, 1 }; c->CopySubresourceRegion(s_stage[w], 0, (UINT)i * 192 + 128, 0, 0, bb, 0, &b6); s_noteBone++; }
          else s_boneShort++;
          bb->Release(); }
      else { s_boneNone++; if (s_zeroBuf) c->CopySubresourceRegion(s_stage[w], 0, (UINT)i * 192 + 128, 0, 0, s_zeroBuf, 0, &bz); } }
}

// (2/11) desde BeforeDraw, en cada dibujo del PS de botines / pelota (aadffecf): anota la traslacion de su m_world (b5 fila 3) con el
// indice del dibujo para reconocer la pelota en Present; devuelve el b12 a enlazar si este dibujo ya es "la pelota" (con manchas) o nullptr
ID3D11Buffer* PitchBallDrawNote(ID3D11DeviceContext* c, UINT verts) {
    if (!g_dev.dev || verts == 0) return nullptr;
    s_bdCalls++; if (g_cfg.live[177][0] < 0.001f) { s_bdOffX177++; return nullptr; }
    UINT bi = s_ballIdx.load(std::memory_order_relaxed);
    {   // (2/11 paso 10) se anota siempre (antes se dejaba de anotar con 3 votos y un botin votado quedaba fijo para todo el partido)
        int w = s_bdW.load(); if (!s_bdStage[w]) s_bdNoStage++; else { int i = s_bdCount.fetch_add(1);
            if (i >= kBallSlots) s_bdOverflow++; else { ID3D11Buffer* cb = nullptr; UINT first = 0, num = 0; ID3D11DeviceContext1* c1 = nullptr;
                if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) { c1->VSGetConstantBuffers1(5, 1, &cb, &first, &num); c1->Release(); } else c->VSGetConstantBuffers(5, 1, &cb);
                // (2/11 e) se copian las 8 primeras filas de b5 (128 B): la traslacion puede estar en la fila 3, en la 7 o en la columna w de las
                // filas 4..6 (matriz traspuesta, como la del pasto); antes solo la fila 7 y la pelota nunca se reconocio
                if (!cb) s_bdNoCb++; else { D3D11_BUFFER_DESC d; cb->GetDesc(&d); UINT off = first * 16;
                    if (d.ByteWidth >= off + 128) { D3D11_BOX bx = { off, 0, 0, off + 128, 1, 1 }; c->CopySubresourceRegion(s_bdStage[w], 0, (UINT)i * 128, 0, 0, cb, 0, &bx); s_bdIdx[w][i] = verts; s_bdNoted++; }
                    else s_bdNoCb++;
                    cb->Release(); } } }
    }
    return (bi != 0 && verts == bi && s_ballIdxVotes.load(std::memory_order_relaxed) >= 3) ? s_ballCb : nullptr;   // (2/11 paso 10) b12 solo con el indice confirmado
}
static void BallDrawPresent() {
    if (!g_dev.dev || !g_dev.imm) return;
    for (int i = 0; i < kStages; i++) if (!s_bdStage[i]) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 128 * kBallSlots; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev.dev->CreateBuffer(&d, nullptr, &s_bdStage[i]); }
    if (!s_ballCb) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 16; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; g_dev.dev->CreateBuffer(&d, nullptr, &s_ballCb); }
    // publicar las manchas (b12: x 1 pelota, y verde, z tierra, w cal), escaladas por x177
    if (s_ballCb) { const float kS = std::clamp(g_cfg.live[177][0], 0.f, 2.f); float v[4] = { 1.f, std::min(1.f, s_stain[0] * kS), std::min(1.f, s_stain[1] * kS), std::min(1.f, s_stain[2] * kS) }; g_dev.imm->UpdateSubresource(s_ballCb, 0, nullptr, v, 0, 0); }
    // anillo de 3 como el desgaste: leer el de hace 2 cuadros y comparar con la pelota del pasto
    int w = s_bdW.load(); int n = std::min(s_bdCount.exchange(0), kBallSlots); s_bdFrame[w] = n > 0 ? 1 : -1;
    int rd = (w + 1) % kStages; int pending = s_bdFrame[rd]; s_bdFrame[rd] = -1; s_bdW = rd;
    // (2/11 b) diagnostico mientras no se reconoce: cada 30 s, hasta 4 veces por partido, por que no se anota o no se lee
    { static int presents = 0, noPending = 0, noBall = 0, mapFail = 0, logs = 0, gen = -1;
      if (g_matchGen.load() != gen) { gen = g_matchGen.load(); presents = noPending = noBall = mapFail = logs = 0;
          s_ballIdx = 0; s_ballIdxVotes = 0; s_ballChanges = 0; s_ballLogged = 0; memset(s_ballHist, 0, sizeof s_ballHist); s_ballHp = 0; }   // (2/11 paso 10) partido nuevo: se vuelve a buscar la pelota
      presents++; if (pending < 0) noPending++; if (!s_ballOk) noBall++;
      if (presents % 1800 == 0 && logs < 4) { logs++;   // (2/11 paso 10) tambien con indice ya votado (antes se callaba justo cuando el indice alternaba)
          Log("pelota: diagnostico (%d cuadros): dibujos aadffecf %d, saltados por x177 %d, sin staging %d, sin b5 %d, anotados %d, desbordes %d; cuadros sin anotaciones %d, sin pelota del pasto %d, lecturas que fallaron %d; indice %u votos %d cambios %d; x177 %.2f capsula %s (%.1f, %.2f, %.1f)",
              presents, s_bdCalls.load(), s_bdOffX177.load(), s_bdNoStage.load(), s_bdNoCb.load(), s_bdNoted.load(), s_bdOverflow.load(), noPending, noBall, mapFail, s_ballIdx.load(), s_ballIdxVotes.load(), s_ballChanges, g_cfg.live[177][0], s_ballOk ? "si" : "no", s_ball[0], s_ball[1], s_ball[2]); }
      if (pending < 0 || !s_bdStage[rd] || !s_ballOk) return;
      D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_dev.imm->Map(s_bdStage[rd], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) { mapFail++; memset(s_bdIdx[rd], 0, sizeof s_bdIdx[rd]); return; }   // (2/11 paso 10) sin el memset los slots viejos volvian como candidatos
    const float* q = (const float*)m.pData; UINT found = 0; int nf = 0; static int s_noMatch = 0; int nCand = 0; float bestD = 1e9f; int bestH = -1;
    for (int i = 0; i < kBallSlots; i++) { if (!s_bdIdx[rd][i]) continue; nCand++; const float* r = q + i * 32;
        // candidatos a traslacion: fila 3 (m_world por filas), fila 7 (matriz en 4..7), columna w de 0..2 y de 4..6 (matriz traspuesta)
        const float cand[4][3] = { { r[12], r[13], r[14] }, { r[28], r[29], r[30] }, { r[3], r[7], r[11] }, { r[19], r[23], r[27] } };
        for (int k = 0; k < 4; k++) { float dx = cand[k][0] - s_ball[0], dy = cand[k][1] - s_ball[1], dz = cand[k][2] - s_ball[2];
            float d2 = dx * dx + dy * dy + dz * dz;
            // (2/11 paso 10) antes se quedaba el ULTIMO dentro de 0.3 m: con la pelota al pie los botines (b5 a y 0..0.05, la capsula a ~0.11)
            // entraban en el radio y el indice alternaba pelota <-> botin cada cuadro. Ahora: primero los que estan a la altura de la
            // capsula (|dy| < 6 cm), y entre esos el mas cercano
            if (d2 < 0.3f * 0.3f) { nf++; int h = fabsf(dy) < 0.06f ? 1 : 0; if (h > bestH || (h == bestH && d2 < bestD)) { bestH = h; bestD = d2; found = s_bdIdx[rd][i]; } break; } } }
    if (!found && nCand > 0) { if (++s_noMatch == 600) {   // ~10 s sin reconocerla: volcar que se leyo (una vez)
            Log("pelota: sin dibujo reconocido tras 600 cuadros (%d dibujos de botines/pelota por cuadro); capsula del pasto (%.2f, %.2f, %.2f)", nCand, s_ball[0], s_ball[1], s_ball[2]);
            for (int i = 0, n = 0; i < kBallSlots && n < 3; i++) { if (!s_bdIdx[rd][i]) continue; n++; const float* r = q + i * 32;
                Log("pelota: candidato %u indices b5 f0 (%.2f %.2f %.2f %.2f) f3 (%.2f %.2f %.2f %.2f) f4 (%.2f %.2f %.2f %.2f) f7 (%.2f %.2f %.2f %.2f)", s_bdIdx[rd][i],
                    r[0], r[1], r[2], r[3], r[12], r[13], r[14], r[15], r[16], r[17], r[18], r[19], r[28], r[29], r[30], r[31]); } } }
    {   // (11/11 n, Agustin: "los pastos alrededor de los botines no se deforman o hunden") DIAGNOSTICO para la proxima tanda: si los dibujos de
        // botines traen su propia posicion en b5 (cerca del suelo, separados de la pelota) se pueden pasar al pasto como la pelota. Cada ~30 s
        // (6 veces) se anotan los dibujos a menos de 2.5 m de la pelota con sus 4 lecturas de traslacion y la fila 0
        static int s_bootLogs = 0; static UINT s_bootF = 0;
        if (s_bootLogs < 6 && (++s_bootF % 1800u) == 900u) { s_bootLogs++; int n = 0;
            for (int i = 0; i < kBallSlots && n < 8; i++) { if (!s_bdIdx[rd][i]) continue; const float* r = q + i * 32;
                const float cand[4][3] = { { r[12], r[13], r[14] }, { r[28], r[29], r[30] }, { r[3], r[7], r[11] }, { r[19], r[23], r[27] } };
                int kc = -1; for (int k = 0; k < 4 && kc < 0; k++) { const float dx = cand[k][0] - s_ball[0], dz = cand[k][2] - s_ball[2]; if (dx * dx + dz * dz < 2.5f * 2.5f && cand[k][1] > -0.3f && cand[k][1] < 1.2f) kc = k; }
                if (kc < 0) continue; n++;
                Log("botines (diag): dibujo %u lectura %d (%.3f %.3f %.3f) pelota (%.3f %.3f %.3f) f0 (%.2f %.2f %.2f %.2f) f1 (%.2f %.2f %.2f %.2f) f2 (%.2f %.2f %.2f %.2f)", s_bdIdx[rd][i], kc,
                    cand[kc][0], cand[kc][1], cand[kc][2], s_ball[0], s_ball[1], s_ball[2], r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11]); }
            Log("botines (diag): %d de %d dibujos de botines / pelota a menos de 2.5 m de la pelota", n, nCand); }
    }
    g_dev.imm->Unmap(s_bdStage[rd], 0); memset(s_bdIdx[rd], 0, sizeof s_bdIdx[rd]);
    if (found) {   // (2/11 paso 10) voto por el indice mas frecuente de los ultimos 10 cuadros con candidato (antes 3 seguidos, que un botin intercalado reiniciaba)
        UINT* hist = s_ballHist; s_ballHist[s_ballHp] = found; s_ballHp = (s_ballHp + 1) % 10;
        UINT best = 0; int bestN = 0; for (int a = 0; a < 10; a++) { if (!hist[a]) continue; int n = 0; for (int b = 0; b < 10; b++) n += hist[b] == hist[a]; if (n > bestN) { bestN = n; best = hist[a]; } }
        UINT cur = s_ballIdx.load(); int prevVotes = s_ballIdxVotes.load();
        if (best != cur) { s_ballChanges++; if (s_ballChanges <= 5) Log("pelota: indice candidato %u -> %u (%d de 10 cuadros, %d dibujos cerca de la capsula, dy %s)", cur, best, bestN, nf, bestH > 0 ? "a la altura de la capsula" : "por debajo"); }
        s_ballIdx = best; s_ballIdxVotes = bestN;
        if (s_ballLogged < 2 && prevVotes < 3 && bestN >= 3) { s_ballLogged++; Log("pelota: dibujo reconocido (indice %u, %d de 10 cuadros, %d dibujos cerca de la capsula); manchas verde %.2f tierra %.2f cal %.2f", best, bestN, nf, s_stain[0], s_stain[1], s_stain[2]); } }
    }
}
void PitchWearPresent() {
    if (!g_dev.dev || !g_dev.imm) return;
    EnsureRes();
    PitchFlatPresent();   // (20/10) pasto aplastado temporal (jugadores + pelota)
    BallDrawPresent();    // (2/11) reconocer el dibujo de la pelota y publicar sus manchas
    const float* P = g_cfg.live[115];
    // (08/11 z, Agustin: "no se aprecian las huellas ni el desgaste") diagnostico cada 15 s: en su partido no hubo ni una pisada en el log
    { static double nextD = 0.0; double tn = Now(); if (tn >= nextD && g_cfg.live[58][3] > 0.5f) { nextD = tn + 15.0;
        Log("desgaste en vivo (15 s): dibujos de short 5529 %d (anotados %d, b5 corto %d, sin staging %d; hueso 0 de b6 %d, b6 corto %d, sin b6 %d) | posiciones: validas %d (hueso 0 %d, fila 7 %d, fila 3 %d, columna w %d), fuera de rango %d, en cero %d | jugadores seguidos %d, recortes vivos %d | x115 %.2f w115 %.0f x136 %.0f x389 %.0f",
            s_noteCalls.exchange(0), s_noteOk.exchange(0), s_noteShort.exchange(0), s_noteNoStage.exchange(0), s_noteBone.exchange(0), s_boneShort.exchange(0), s_boneNone.exchange(0), s_posOk, s_rowPick[0], s_rowPick[1], s_rowPick[2], s_rowPick[3], s_posBad, s_posZero, (int)s_pl.size(), (int)s_clips.size(), P[0], P[3], g_cfg.live[136][0], g_cfg.live[389][0]);
        s_posOk = s_posBad = s_posZero = 0; s_rowPick[0] = s_rowPick[1] = s_rowPick[2] = s_rowPick[3] = 0;
        Log("recortes v2 (15 s): pisadas que arrancaron pasto %d | nacidos: hojas %d, manojos %d, terrones %d, polvo %d | vivos %d (tope %d) | x116 %.2f y116 %.1f w116 %.0f z389 %.2f",
            s_strikeN.exchange(0), s_bornN[kClipBlade].exchange(0), s_bornN[kClipTuft].exchange(0), s_bornN[kClipClod].exchange(0), s_bornN[kClipDust].exchange(0), (int)s_clips.size(), ClipCap(),
            g_cfg.live[116][0], g_cfg.live[116][1], g_cfg.live[116][3], g_cfg.live[389][2]); } }
    if (P[3] < 0.5f) { s_count = 0; return; }
    EnsureRes(); if (!s_tex || !s_cb) return;
    // partido nuevo: borrar
    if (g_matchGen.load() != s_matchGenSeen || !s_cleared) { s_matchGenSeen = g_matchGen.load(); float z[4] = {}; g_dev.imm->ClearRenderTargetView(s_rtv, z); s_cleared = true; s_prev.clear(); s_pl.clear();
        memset(s_wearCpu, 0, sizeof s_wearCpu); s_stain[0] = s_stain[1] = s_stain[2] = 0.f; }
    // anillo de 3: este cuadro escribio en w; el siguiente escribe en (w+1); se lee el de hace 2 cuadros = (w+1) (la GPU ya lo termino)
    int w = s_stageW.load(); int n = std::min(s_count.exchange(0), kMaxPlayers);
    s_stageFrame[w] = n > 0 ? s_frame : -1; s_frame++;
    int rd = (w + 1) % kStages;
    double t = Now(); s_lastT = t;
    static double s_wG = -1e9, s_wR = -1.0; const float gdt = GameStep(s_wG, s_wR);   // (11/11) reloj del juego (pausa: 0; camara lenta: menos)
    static float s_plAcc = 0.f; s_plAcc += gdt;   // tiempo de juego desde la ultima lectura de posiciones
    ClipSimAndUpload(gdt);
    int pending = s_stageFrame[rd]; s_stageFrame[rd] = -1; s_stageW = rd;
    if (pending < 0 || !s_stage[rd]) return;
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_dev.imm->Map(s_stage[rd], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) return;
    const float* q = (const float*)m.pData; int cnt = 0; float pos[kMaxPlayers][2];
    for (int i = 0; i < kMaxPlayers && cnt < kMaxPlayers; i++) {
        // (08/11 z) candidatos de posicion dentro de las 8 filas copiadas: fila 7 (xyz), fila 3 (xyz), columna w de las filas 4..6
        const float* r = q + i * 48; float x = 0, y = 0, z = 0; int pick = -1;
        // (08/11 aa) primero m_world (b5 c4..c7, filas r[16..31]) * hueso 0 (b6: traslacion en la columna w de sus 3 registros, r[32+3], r[32+7], r[32+11]):
        // vale tanto si m_world es la identidad (todo en los huesos) como si el hueso es local al jugador
        const float* B = r + 32; float bx = B[3], by = B[7], bz = B[11]; bool hasB = fabsf(bx) + fabsf(by) + fabsf(bz) > 1e-4f && fabsf(bx) < 200.f && fabsf(by) < 200.f && fabsf(bz) < 200.f;
        float hx = 0, hy = 0, hz = 0;
        if (hasB) { hx = bx * r[16] + by * r[20] + bz * r[24] + r[28]; hy = bx * r[17] + by * r[21] + bz * r[25] + r[29]; hz = bx * r[18] + by * r[22] + bz * r[26] + r[30]; }
        const float cand[4][3] = { { hx, hy, hz }, { r[28], r[29], r[30] }, { r[12], r[13], r[14] }, { r[19], r[23], r[27] } };
        for (int k = (hasB ? 0 : 1); k < 4 && pick < 0; k++) { float cx = cand[k][0], cy = cand[k][1], cz = cand[k][2];
            if (fabsf(cx) < 80.f && fabsf(cz) < 50.f && fabsf(cy) < 5.f && !(fabsf(cx) < 0.01f && fabsf(cz) < 0.01f)) { x = cx; y = cy; z = cz; pick = k; } }
        if (pick < 0) { if (fabsf(r[28]) < 0.01f && fabsf(r[30]) < 0.01f) s_posZero++; else s_posBad++; continue; }
        s_rowPick[pick]++; s_posOk++;   // (08/11 aa) 0 = hueso 0, 1 = fila 7, 2 = fila 3, 3 = columna w
        bool dup = false; for (int k = 0; k < cnt; k++) if (fabsf(pos[k][0] - x) < 0.05f && fabsf(pos[k][1] - z) < 0.05f) { dup = true; break; }
        if (dup) continue;
        pos[cnt][0] = x; pos[cnt][1] = z; cnt++;
    }
    g_dev.imm->Unmap(s_stage[rd], 0);
    if (cnt == 0) return;
    // (11/11) recortes v2: tiempo real desde la ultima lectura de posiciones (si un cuadro no se pudo leer -DO_NOT_WAIT- la distancia era
    // de 2 cuadros sobre el dt de 1 y la velocidad salia doble); un hueco largo (pausa, repeticion, carga) reinicia las velocidades
    // (11/11) en tiempo de juego: en pausa (no avanzo) no se toca nada y se sigue sumando; en la camara lenta las velocidades salen bien
    if (s_plAcc < 0.002f) return;
    static double s_plT = 0.0; const bool gapP = (t - s_plT) > 0.25 || s_plAcc > 0.1f; s_plT = t;
    float dtP = std::clamp(s_plAcc, 0.002f, 0.1f); s_plAcc = 0.f;
    // velocidad: el mas cercano del cuadro anterior
    float brush[kMaxPlayers][4]; int nb = 0;
    float rate = P[0] > 0.f ? P[0] : 1.0f, rad = std::clamp(P[1], 0.3f, 2.0f);
    std::vector<PlayerPrev> plNow; plNow.reserve(cnt);
    const float kAct = std::clamp(g_cfg.live[177][1], 0.f, 2.f);   // (2/11) frenadas y arranques desgastan mas (y arrastran la marca)
    for (int i = 0; i < cnt; i++) {
        float best = 1e9f; int bi = -1; for (size_t k = 0; k < s_pl.size(); k++) { float dx = s_pl[k].x - pos[i][0], dz = s_pl[k].z - pos[i][1]; float d2 = dx * dx + dz * dz; if (d2 < best) { best = d2; bi = (int)k; } }
        float speed = best < 1e8f ? sqrtf(best) / dtP : 0.f;           // m/s
        if (speed > 15.f || gapP) { speed = 0.f; bi = -1; }               // salto de camara / teletransporte / hueco
        PlayerPrev pn{ pos[i][0], pos[i][1], 0.f, 0.f, 0.f };
        if (bi >= 0) { const PlayerPrev& pp = s_pl[bi];
            pn.vx = (pos[i][0] - pp.x) / dtP; pn.vz = (pos[i][1] - pp.z) / dtP;
            { float r = sqrtf(pn.vx * pn.vx + pn.vz * pn.vz); if (r > 11.f) { pn.vx *= 11.f / r; pn.vz *= 11.f / r; } }   // nadie corre a mas de 11 m/s (cruce de dos jugadores)
            pn.trk = std::min(pp.trk + 1, 1000);
            // (11/11) recortes v2: velocidad (~80 ms) y aceleracion (~120 ms) suavizadas: la resta cruda de posiciones cuadro a cuadro tenia
            // ruido (0.1 m/s de error = 6 m/s2 de "aceleracion") y daba frenadas y giros falsos que tiraban pasto al azar
            float aV = 1.f - expf(-dtP / 0.08f), aA = 1.f - expf(-dtP / 0.12f);
            if (pp.trk == 0) { pn.svx = pn.vx; pn.svz = pn.vz; pn.sax = pn.saz = 0.f; }   // recien aparecido: arranca con su velocidad (sin "arranque" falso)
            else { pn.svx = pp.svx + (pn.vx - pp.svx) * aV; pn.svz = pp.svz + (pn.vz - pp.svz) * aV;
                pn.sax = pp.sax + ((pn.svx - pp.svx) / dtP - pp.sax) * aA; pn.saz = pp.saz + ((pn.svz - pp.svz) / dtP - pp.saz) * aA;
                float am = sqrtf(pn.sax * pn.sax + pn.saz * pn.saz); if (am > 15.f) { pn.sax *= 15.f / am; pn.saz *= 15.f / am; } }
            float sS = sqrtf(pn.svx * pn.svx + pn.svz * pn.svz), sP = sqrtf(pp.svx * pp.svx + pp.svz * pp.svz);
            float hx = sS > 0.3f ? pn.svx / sS : 0.f, hz = sS > 0.3f ? pn.svz / sS : 0.f;
            float aL = pn.sax * hx + pn.saz * hz, aT = -pn.sax * hz + pn.saz * hx;   // a lo largo (+ acelera, - frena) y de costado (+ hacia la izquierda)
            const float rk = ReactK();
            float brake  = std::clamp((-aL - 4.f / rk) / 10.f * rk, 0.f, 1.f) * std::min(std::max(sS, sP) / 4.f, 1.f);   // frenada
            float push   = std::clamp((aL - 3.f / rk) / 8.f * rk, 0.f, 1.f);                                               // arranque
            float cutK   = std::clamp((fabsf(aT) - 4.f / rk) / 10.f * rk, 0.f, 1.f) * std::min(sS / 3.f, 1.f);             // giro / cambio de direccion
            float sprint = sS > 6.0f ? std::min(0.06f + 0.1f * (sS - 6.0f) / 3.0f, 0.2f) * rk : 0.f;                        // sprint: pocos y chicos
            float skid = std::max(std::max(brake, cutK), std::max(push * 0.6f, sS > 7.0f ? 0.12f * rk : 0.f));
            pn.skid = std::max(skid, pp.skid * expf(-dtP / 0.1f));
            // (08/11) zancada: una huella cada media zancada (0.75 m parado / caminando .. 1.1 m al sprint), alternando el pie
            pn.stride = pp.stride + sS * dtP; pn.foot = pp.foot;
            float half = std::clamp(0.55f + 0.08f * sS, 0.6f, 1.15f);
            if (pn.stride >= half && sS > 0.3f) { pn.stride -= half; pn.foot ^= 1; float side = pn.foot ? 0.11f : -0.11f;
                pn.printX = pos[i][0] + (-hz) * side; pn.printZ = pos[i][1] + hx * side; pn.printDue = 1;
                // (11/11) recortes v2: lo que arranca ESTE pie al pisar. Impulso / sprint -> atras; frenada -> adelante (patina); giro -> hacia
                // afuera de la curva. Trotar o caminar (sin aceleracion) no arranca nada
                float inten = std::max(std::max(push * 0.9f, brake * 1.1f), cutK) + sprint;
                if (inten >= 0.05f && pn.trk >= 4) {
                    float wB = push + sprint, wF = brake * 1.2f, wC = cutK, sg = aT > 0.f ? -1.f : 1.f;
                    float tx = -hx * wB + hx * wF + (-hz) * sg * wC, tz = -hz * wB + hz * wF + hx * sg * wC;
                    float l = sqrtf(tx * tx + tz * tz); if (l < 1e-3f) { tx = -hx; tz = -hz; } else { tx /= l; tz /= l; }
                    EmitStrike(pn.printX + tx * 0.08f, pn.printZ + tz * 0.08f, tx, tz, inten, false);
                }
            }
        }
        float k = (0.25f + 0.75f * std::min(speed / 7.f, 1.f)) * rate * dtP * 0.08f * (1.f + 3.f * pn.skid * kAct);   // parado: poco; corriendo: mas; frenando: mucho
        brush[nb][0] = pos[i][0] / 140.f + 0.5f; brush[nb][1] = pos[i][1] / 79.f + 0.5f; brush[nb][2] = k; brush[nb][3] = rad; nb++;
        WearCpuAdd(brush[nb - 1][0], brush[nb - 1][1], k, rad);
        if (pn.skid > 0.2f && kAct > 0.01f && speed > 1.f && nb < kMaxPlayers) {   // arrastre detras del jugador que frena
            float ux = pn.vx / speed, uz = pn.vz / speed, bx = pos[i][0] - ux * 0.4f, bz = pos[i][1] - uz * 0.4f;
            brush[nb][0] = bx / 140.f + 0.5f; brush[nb][1] = bz / 79.f + 0.5f; brush[nb][2] = k * 0.7f; brush[nb][3] = rad * 0.8f; nb++;
            WearCpuAdd(brush[nb - 1][0], brush[nb - 1][1], k * 0.7f, rad * 0.8f);
        }
        plNow.push_back(pn);
    }
    // (11/11) recortes v2: los recortes salen en la pisada (EmitStrike, arriba); w116 = 0 los borra
    if (g_cfg.live[116][3] < 0.5f) s_clips.clear();
    s_prev.assign(&pos[0][0], &pos[0][0] + cnt * 2); s_pl.swap(plNow); s_plGen++;
    // (08/11) huecos del pasto arrancado (cola de EmitStrike) y huellas en el desgaste (marcas que quedan)
    for (const WearBrush& wb : s_wearQueue) { if (nb >= kMaxPlayers) break; brush[nb][0] = wb.u; brush[nb][1] = wb.v; brush[nb][2] = wb.k * rate; brush[nb][3] = wb.rad; nb++; }
    s_wearQueue.clear();
    if (FpOn() > 0.5f) for (const PlayerPrev& pp : s_pl) { if (nb >= kMaxPlayers) break; if (!pp.printDue) continue;
        float kf = 0.012f * FpForce() * rate * (1.f + 2.f * pp.skid * kAct); brush[nb][0] = pp.printX / 140.f + 0.5f; brush[nb][1] = pp.printZ / 79.f + 0.5f; brush[nb][2] = kf; brush[nb][3] = 0.14f; nb++;
        WearCpuAdd(brush[nb - 1][0], brush[nb - 1][1], kf, 0.14f); }
    if (!s_vs || !s_ps) return;
    // subir pinceles y dibujar
    D3D11_MAPPED_SUBRESOURCE mc; if (FAILED(g_dev.imm->Map(s_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) return;
    float* cbp = (float*)mc.pData; cbp[0] = (float)nb; cbp[1] = 140.f; cbp[2] = 79.f; cbp[3] = 0.f;
    memcpy(cbp + 4, brush, nb * 16); g_dev.imm->Unmap(s_cb, 0);
    ID3D11DeviceContext* c = g_dev.imm;
    ID3D11RenderTargetView* oRT[8] = {}; ID3D11DepthStencilView* oDS = nullptr; c->OMGetRenderTargets(8, oRT, &oDS);
    UINT nvp = 8; D3D11_VIEWPORT ovp[8]; c->RSGetViewports(&nvp, ovp);
    ID3D11VertexShader* oVS = nullptr; ID3D11PixelShader* oPS = nullptr; ID3D11GeometryShader* oGS = nullptr;
    c->VSGetShader(&oVS, nullptr, nullptr); c->PSGetShader(&oPS, nullptr, nullptr); c->GSGetShader(&oGS, nullptr, nullptr);
    ID3D11InputLayout* oIL = nullptr; c->IAGetInputLayout(&oIL); D3D11_PRIMITIVE_TOPOLOGY oTopo; c->IAGetPrimitiveTopology(&oTopo);
    ID3D11RasterizerState* oRS = nullptr; c->RSGetState(&oRS); ID3D11DepthStencilState* oDSS = nullptr; UINT oRef = 0; c->OMGetDepthStencilState(&oDSS, &oRef);
    ID3D11BlendState* oBS = nullptr; float oBF[4]; UINT oSM = 0; c->OMGetBlendState(&oBS, oBF, &oSM);
    ID3D11Buffer* oCB = nullptr; c->VSGetConstantBuffers(0, 1, &oCB);
    c->OMSetRenderTargets(1, &s_rtv, nullptr); D3D11_VIEWPORT vp{ 0, 0, 1024, 512, 0, 1 }; c->RSSetViewports(1, &vp);
    c->VSSetShader(s_vs, nullptr, 0); c->PSSetShader(s_ps, nullptr, 0); c->GSSetShader(nullptr, nullptr, 0);
    c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    c->RSSetState(s_rs); c->OMSetDepthStencilState(s_dss, 0); float bf[4] = {}; c->OMSetBlendState(s_bsAdd, bf, 0xffffffff);
    c->VSSetConstantBuffers(0, 1, &s_cb);
    c->DrawInstanced(4, (UINT)nb, 0, 0);
    // restaurar
    c->OMSetRenderTargets(8, oRT, oDS); for (auto* v : oRT) if (v) v->Release(); if (oDS) oDS->Release();
    c->RSSetViewports(nvp, ovp);
    c->VSSetShader(oVS, nullptr, 0); c->PSSetShader(oPS, nullptr, 0); c->GSSetShader(oGS, nullptr, 0); if (oVS) oVS->Release(); if (oPS) oPS->Release(); if (oGS) oGS->Release();
    c->IASetInputLayout(oIL); if (oIL) oIL->Release(); c->IASetPrimitiveTopology(oTopo);
    c->RSSetState(oRS); if (oRS) oRS->Release(); c->OMSetDepthStencilState(oDSS, oRef); if (oDSS) oDSS->Release();
    c->OMSetBlendState(oBS, oBF, oSM); if (oBS) oBS->Release();
    c->VSSetConstantBuffers(0, 1, &oCB); if (oCB) oCB->Release();
    if (s_logged < 3 && nb > 0) { s_logged++; Log("desgaste en vivo: %d jugadores pintados (ej. %.1f, %.1f)", nb, pos[0][0], pos[0][1]); }
}

} // namespace fx
