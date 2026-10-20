// PES21 FX - capturas asincronicas (20/10). SaveWICTextureToFile copia el cuadro, espera a la GPU (Map bloqueante) y
// codifica el JPEG en el hilo de render: un tiron de 30-60 ms cada captura (se veia como un parpadeo cada 20 s). Aca:
// la copia va a un staging propio, se lee 2+ cuadros despues sin esperar (DO_NOT_WAIT) y el JPEG se codifica en un hilo
// aparte con WIC. El juego no se detiene.
#include "core.h"
#include <wincodec.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <string>

namespace fx {

struct ShotJob { std::wstring path; UINT w = 0, h = 0; std::vector<unsigned char> bgr; };
struct ShotSlot { ID3D11Texture2D* st = nullptr; UINT w = 0, h = 0; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; std::wstring path; int frame = -1; bool busy = false; };
static ShotSlot s_slots[3];
static std::mutex s_qmx; static std::condition_variable s_qcv; static std::deque<ShotJob> s_queue; static bool s_worker = false;
static int s_frame = 0;

static void EncodeJpeg(const ShotJob& j) {
    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) || !f) return;
    IWICStream* s = nullptr; IWICBitmapEncoder* e = nullptr; IWICBitmapFrameEncode* fr = nullptr; IPropertyBag2* props = nullptr;
    bool ok = SUCCEEDED(f->CreateStream(&s)) && SUCCEEDED(s->InitializeFromFilename(j.path.c_str(), GENERIC_WRITE))
           && SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &e)) && SUCCEEDED(e->Initialize(s, WICBitmapEncoderNoCache))
           && SUCCEEDED(e->CreateNewFrame(&fr, &props));
    if (ok && props) { PROPBAG2 opt = {}; opt.pstrName = (LPOLESTR)L"ImageQuality"; VARIANT v; VariantInit(&v); v.vt = VT_R4; v.fltVal = 0.9f; props->Write(1, &opt, &v); }
    if (ok) {
        WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
        ok = SUCCEEDED(fr->Initialize(props)) && SUCCEEDED(fr->SetSize(j.w, j.h)) && SUCCEEDED(fr->SetPixelFormat(&pf))
          && SUCCEEDED(fr->WritePixels(j.h, j.w * 3, (UINT)j.bgr.size(), (BYTE*)j.bgr.data())) && SUCCEEDED(fr->Commit()) && SUCCEEDED(e->Commit());
    }
    if (props) props->Release(); if (fr) fr->Release(); if (e) e->Release(); if (s) s->Release(); f->Release();
    if (!ok) Log("captura: no se pudo escribir el JPEG");
}

static void Worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        ShotJob j;
        { std::unique_lock<std::mutex> lk(s_qmx); s_qcv.wait(lk, [] { return !s_queue.empty(); }); j = std::move(s_queue.front()); s_queue.pop_front(); }
        EncodeJpeg(j);
    }
}

// pide guardar el cuadro (s_frameTex de la DLL) como JPEG; devuelve false si no hay lugar (3 pendientes)
bool AsyncShot(ID3D11Texture2D* frame, const std::wstring& path) {
    if (!frame || !g_dev.dev || !g_dev.imm) return false;
    D3D11_TEXTURE2D_DESC d; frame->GetDesc(&d);
    for (auto& sl : s_slots) {
        if (sl.busy) continue;
        if (!sl.st || sl.w != d.Width || sl.h != d.Height || sl.fmt != d.Format) {
            if (sl.st) { sl.st->Release(); sl.st = nullptr; }
            D3D11_TEXTURE2D_DESC sd = d; sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0; sd.MipLevels = 1; sd.ArraySize = 1;
            if (FAILED(g_dev.dev->CreateTexture2D(&sd, nullptr, &sl.st))) { sl.st = nullptr; return false; }
            sl.w = d.Width; sl.h = d.Height; sl.fmt = d.Format;
        }
        g_dev.imm->CopyResource(sl.st, frame);
        sl.path = path; sl.frame = s_frame; sl.busy = true;
        return true;
    }
    return false;
}

static inline unsigned char U8(float v) { return (unsigned char)(v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255.0f + 0.5f)); }

void AsyncShotPresent() {
    s_frame++;
    if (!g_dev.imm) return;
    for (auto& sl : s_slots) {
        if (!sl.busy || s_frame - sl.frame < 2) continue;
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(g_dev.imm->Map(sl.st, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) { if (s_frame - sl.frame > 30) sl.busy = false; continue; }
        ShotJob j; j.path = sl.path; j.w = sl.w; j.h = sl.h; j.bgr.resize((size_t)sl.w * sl.h * 3);
        for (UINT y = 0; y < sl.h; y++) {
            const unsigned char* row = (const unsigned char*)m.pData + (size_t)y * m.RowPitch;
            unsigned char* o = j.bgr.data() + (size_t)y * sl.w * 3;
            switch (sl.fmt) {
            case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_UNORM:
                for (UINT x = 0; x < sl.w; x++) { o[x * 3] = row[x * 4]; o[x * 3 + 1] = row[x * 4 + 1]; o[x * 3 + 2] = row[x * 4 + 2]; } break;
            case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS:
                for (UINT x = 0; x < sl.w; x++) { uint32_t p = ((const uint32_t*)row)[x]; o[x * 3] = (unsigned char)(((p >> 20) & 1023) >> 2); o[x * 3 + 1] = (unsigned char)(((p >> 10) & 1023) >> 2); o[x * 3 + 2] = (unsigned char)((p & 1023) >> 2); } break;
            case DXGI_FORMAT_R16G16B16A16_FLOAT: {
                auto h2f = [](uint16_t h) { uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, mt = h & 1023; float v = e == 0 ? ldexpf((float)mt, -24) : e == 31 ? 1.0f : ldexpf((float)(mt | 1024), (int)e - 25); return s ? -v : v; };
                for (UINT x = 0; x < sl.w; x++) { const uint16_t* q = (const uint16_t*)row + x * 4; o[x * 3] = U8(h2f(q[2])); o[x * 3 + 1] = U8(h2f(q[1])); o[x * 3 + 2] = U8(h2f(q[0])); } break; }
            default:   // R8G8B8A8
                for (UINT x = 0; x < sl.w; x++) { o[x * 3] = row[x * 4 + 2]; o[x * 3 + 1] = row[x * 4 + 1]; o[x * 3 + 2] = row[x * 4]; } break;
            }
        }
        g_dev.imm->Unmap(sl.st, 0);
        sl.busy = false;
        { std::lock_guard<std::mutex> lk(s_qmx); if (s_queue.size() < 6) s_queue.push_back(std::move(j));
          if (!s_worker) { s_worker = true; std::thread(Worker).detach(); } }
        s_qcv.notify_one();
    }
}

} // namespace fx
