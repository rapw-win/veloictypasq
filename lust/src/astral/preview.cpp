// Astral-Remake - made by kdt255 (https://github.com/kdt255/Astral-Remake)
// Free software: MIT + Commons Clause, selling is not permitted. If you paid for this, you got scammed.
//
// lust: ESP preview built on the velocitypasq one. The agent is the velocitypasq 3D model (model3d.cpp, software
// rasterised on a worker thread and uploaded as an ImGui user texture), the chams list is the velocitypasq one, and
// the ESP overlay follows its logic: box / corner / 3D box from the model bounds, skeleton and hitboxes from the
// animated bones, outlined text on the model, animated health bar, and every element can be dragged to another side
// of the box (it snaps to the nearest side and slides into its slot).
#include "astral/preview.h"

#include "astral/draw.h"
#include "astral/ui.h"

#include "model3d.h"
#include "wicons.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace astral {

namespace {

// ---- chams (velocitypasq CHAMS_MAP without "Disabled": the Chams checkbox turns them off) -------------------------

constexpr int kChamsMat[] = { M3D_FLAT,    M3D_GLOW,     M3D_GLASS,    M3D_WIREFRAME, M3D_GALAXYWIRE,
                              M3D_WATERFLOW, M3D_GALAXY, M3D_GLITTER, M3D_LIGHTNING, M3D_FIRE,
                              M3D_STARNEST, M3D_DISSOLVE, M3D_CRYSTAL, M3D_MERCURY,  M3D_AURORA };
constexpr const char* kChamsNames[] = { "Flat",      "Glow",   "Glass",   "Wireframe", "Galaxy Wireframe",
                                        "Waterflow", "Galaxy", "Glitter", "Lightning", "Fire",
                                        "Star Nest", "Dissolve", "Crystal", "Liquid Metal", "Aurora" };
constexpr int kChamsCount = IM_ARRAYSIZE(kChamsMat);
static_assert(IM_ARRAYSIZE(kChamsMat) == IM_ARRAYSIZE(kChamsNames), "one name per material");

// ---- model renderer ------------------------------------------------------------------------------------------------

constexpr int kW = 480, kH = 924; // largest render buffer (velocitypasq size); the texture is allocated at this size
// The agent is rendered at the size it is shown (x kRenderScale), into the top-left corner of the texture: on a phone
// that is ~3x fewer pixels than 480 x 924, which keeps animated chams smooth.
constexpr float kRenderScale = 0.85f;

struct Renderer {
    ImTextureData* tex = nullptr;
    ImTextureData* icons = nullptr; // weapon icon atlas (white + alpha)
    std::mutex mx;
    std::condition_variable cv;
    bool job = false, ready = false, busy = false, have = false;
    M3DSettings job_s, req_s, shown_s, done_s;
    int job_w = kW, job_h = kH, req_w = 0, req_h = 0, done_w = kW, done_h = kH, shown_w = kW, shown_h = kH;
    M3DOut done_o{}, shown_o{};
    std::vector<std::uint8_t> done;
    double req_t = -1.0;
};

Renderer& R()
{
    static Renderer* r = new Renderer(); // never destroyed: the worker thread outlives every frame
    return *r;
}

bool Same(const M3DSettings& a, const M3DSettings& b)
{
    return a.yaw == b.yaw && a.pitch == b.pitch && a.mat == b.mat && std::memcmp(a.col, b.col, sizeof a.col) == 0 && a.halo == b.halo &&
           a.overlay == b.overlay && std::memcmp(a.haloCol, b.haloCol, sizeof a.haloCol) == 0 && a.time == b.time && a.bright == b.bright &&
           a.anim == b.anim && a.model == b.model && a.rag == b.rag && a.ragA == b.ragA && a.look == b.look;
}

void Worker()
{
    Renderer& r = R();
    std::vector<std::uint8_t> buf(static_cast<size_t>(kW) * kH * 4);
    for (;;) {
        M3DSettings js;
        int w = kW, h = kH;
        {
            std::unique_lock<std::mutex> lk(r.mx);
            r.cv.wait(lk, [&r] { return r.job; });
            js = r.job_s;
            w = r.job_w;
            h = r.job_h;
            r.job = false;
        }
        M3DOut o{};
        M3D_Render(buf.data(), w, h, js, &o);
        std::lock_guard<std::mutex> lk(r.mx);
        r.done.swap(buf);
        if (buf.size() != r.done.size()) buf.resize(r.done.size());
        r.done_s = js;
        r.done_o = o;
        r.done_w = w;
        r.done_h = h;
        r.ready = true;
        r.busy = false;
    }
}

#ifdef __wasi__
// Web build: no threads in wasm, so the "worker" is a second instance of this module in a Web Worker (web/shell.html).
// model_request hands it the settings; it calls lust_model_done() after writing the pixels into lust_model_buffer().
extern "C" __attribute__((import_module("env"), import_name("model_request"))) void js_model_request(const void* settings, int size, int w, int h);
#endif

void LoadIcons(Renderer& r)
{
    int w = 0, h = 0;
    unsigned char* px = M3D_DecodeImage(WI_PNG, static_cast<int>(sizeof(WI_PNG)), &w, &h, 1);
    if (px == nullptr) return;
    r.icons = IM_NEW(ImTextureData)();
    r.icons->Create(ImTextureFormat_RGBA32, w, h);
    unsigned char* d = static_cast<unsigned char*>(r.icons->Pixels);
    for (int i = 0; i < w * h; ++i) {
        d[i * 4 + 0] = d[i * 4 + 1] = d[i * 4 + 2] = 255;
        d[i * 4 + 3] = px[i];
    }
    M3D_FreeImage(px);
    ImGui::RegisterUserTexture(r.icons);
}

void Update(const M3DSettings& want, bool animated, int rw, int rh)
{
    Renderer& r = R();
    if (r.tex == nullptr) {
        M3D_Init();
        r.tex = IM_NEW(ImTextureData)();
        r.tex->Create(ImTextureFormat_RGBA32, kW, kH);
        std::memset(r.tex->Pixels, 0, static_cast<size_t>(kW) * kH * 4);
        ImGui::RegisterUserTexture(r.tex);
        r.done.resize(static_cast<size_t>(kW) * kH * 4);
#ifndef __wasi__
        std::thread(Worker).detach();
#endif
        LoadIcons(r);
    }
    const double now = ImGui::GetTime();
    rw = ImClamp(rw, 64, kW);
    rh = ImClamp(rh, 120, kH);
    const bool resized = rw != r.req_w || rh != r.req_h;
    bool need = !r.have || resized || !Same(want, r.req_s);
    if (animated && need && !resized && r.have && now - r.req_t < 1.0 / 45.0 && want.mat == r.req_s.mat) need = false; // ~45 fps
    bool fresh = false;
    {
        std::lock_guard<std::mutex> lk(r.mx);
        if (need && !r.busy) {
            r.job_s = want;
            r.job_w = r.req_w = rw;
            r.job_h = r.req_h = rh;
            r.job = true;
            r.busy = true;
            r.req_s = want;
            r.req_t = now;
            r.have = true;
#ifdef __wasi__
            js_model_request(&r.job_s, static_cast<int>(sizeof(M3DSettings)), rw, rh); // rendered by a Web Worker
#else
            r.cv.notify_one();
#endif
        }
        if (r.ready) {
            r.ready = false;
            fresh = true;
            auto* px = static_cast<std::uint8_t*>(r.tex->Pixels);
            for (int y = 0; y < r.done_h; ++y)
                std::memcpy(px + static_cast<size_t>(y) * kW * 4, r.done.data() + static_cast<size_t>(y) * r.done_w * 4, static_cast<size_t>(r.done_w) * 4);
            r.shown_s = r.done_s;
            r.shown_o = r.done_o;
            r.shown_w = r.done_w;
            r.shown_h = r.done_h;
        }
    }
    if (fresh && r.tex->Status == ImTextureStatus_OK) {
        const ImTextureRect rect = { 0, 0, static_cast<unsigned short>(r.shown_w), static_cast<unsigned short>(r.shown_h) };
        r.tex->Updates.resize(0);
        r.tex->Updates.push_back(rect);
        r.tex->UpdateRect = rect;
        r.tex->UsedRect = rect;
        r.tex->SetStatus(ImTextureStatus_WantUpdates);
    }
}

void ToF(float* d, const ImVec4& c)
{
    d[0] = c.x;
    d[1] = c.y;
    d[2] = c.z;
    d[3] = c.w;
}

// ---- ESP layout (velocitypasq: each element sits on a side of the box and can be dragged to another) ----------------

enum { EL_NAME, EL_HP, EL_AP, EL_AMMO, EL_WPN, EL_ICON, EL_DIST, EL_FLAGS, EL_N };
enum { SIDE_TOP, SIDE_BOTTOM, SIDE_LEFT, SIDE_RIGHT };
constexpr int kDefaultSide[EL_N] = { SIDE_TOP, SIDE_LEFT, SIDE_BOTTOM, SIDE_BOTTOM, SIDE_BOTTOM, SIDE_BOTTOM, SIDE_BOTTOM, SIDE_RIGHT };
constexpr float kDefaultOrder[EL_N] = { 0, 0, 0, 1, 2, 3, 4, 0 };

struct Interaction {
    int side[EL_N];
    float order[EL_N];
    ImRect hit[EL_N];
    bool hit_ok[EL_N] = {};
    ImRect box;
    int drag = -1;
    ImVec2 grab;
    bool held_prev = false;
    bool rotating = false;
    ImVec2 last_mouse;
    float yaw = 0.0f, vel = 0.0f;
    // slide animation when an element changes slot
    ImVec2 off[EL_N], prev_rel[EL_N];
    bool prev_ok[EL_N] = {};
    // animated health
    float hp_cur = 100.0f, hp_lag = 100.0f, hp_target = 100.0f, hp_timer = 1.0f;
    float spawn = 0.0f;
    Interaction()
    {
        for (int i = 0; i < EL_N; ++i) {
            side[i] = kDefaultSide[i];
            order[i] = kDefaultOrder[i];
        }
    }
};

Interaction& I()
{
    static Interaction s;
    return s;
}

bool IsBar(int i) { return i == EL_HP || i == EL_AP || i == EL_AMMO; }

void Renumber(Interaction& s)
{
    int idx[EL_N];
    for (int i = 0; i < EL_N; ++i) idx[i] = i;
    std::sort(idx, idx + EL_N, [&s](int a, int b) { return s.order[a] < s.order[b]; });
    for (int q = 0; q < EL_N; ++q) s.order[idx[q]] = static_cast<float>(q);
}

// Text with the 4-way 1px outline the velocitypasq ESP uses.
void OutlinedText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const ImVec4& col, const char* text)
{
    pos = ImFloor(pos);
    const float o = ImMax(1.0f, std::floor(ui::Px(1.0f)));
    const ImU32 sh = draw::Col(ImVec4(0, 0, 0, 0.85f * col.w));
    dl->AddText(font, size, ImVec2(pos.x - o, pos.y), sh, text);
    dl->AddText(font, size, ImVec2(pos.x + o, pos.y), sh, text);
    dl->AddText(font, size, ImVec2(pos.x, pos.y - o), sh, text);
    dl->AddText(font, size, ImVec2(pos.x, pos.y + o), sh, text);
    dl->AddText(font, size, pos, draw::Col(col), text);
}

} // namespace

#ifdef __wasi__
extern "C" __attribute__((export_name("lust_model_buffer"))) unsigned char* lust_model_buffer() { return R().done.data(); }
extern "C" __attribute__((export_name("lust_model_done"))) void lust_model_done(float x0, float y0, float x1, float y1, int valid)
{
    Renderer& r = R();
    r.done_s = r.job_s;
    r.done_w = r.job_w;
    r.done_h = r.job_h;
    r.done_o = M3DOut{ x0, y0, x1, y1, valid != 0 };
    r.ready = true;
    r.busy = false;
}
#endif

const char* const* ChamsMaterialNames(int* count)
{
    if (count != nullptr) *count = kChamsCount;
    return kChamsNames;
}

void DrawEspPreview(ImDrawList* dl, const ImRect& area, const PlayerVisuals& v, int target, double time)
{
    ui::Context& c = ui::Ctx();
    Interaction& s = I();
    Renderer& r = R();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiIO& io = ImGui::GetIO();
    const float dt = ImClamp(io.DeltaTime, 0.0f, 1.0f / 15.0f);
    auto P = [](float x) { return ui::Px(x); };

    // Model rect: keep the buffer aspect, leave room for the ESP text around it.
    const float mh = std::floor(ImMin(area.GetHeight() * 0.80f, area.GetWidth() * 0.74f * kH / kW));
    const float mw = std::floor(mh * kW / kH);
    const ImVec2 m0(std::floor(area.GetCenter().x - mw * 0.5f), std::floor(area.GetCenter().y - mh * 0.5f + P(4.0f)));
    const ImRect mr(m0, ImVec2(m0.x + mw, m0.y + mh));
    const int rh_want = static_cast<int>(mh * kRenderScale) & ~1;
    const int rw_want = static_cast<int>(std::lround(rh_want * static_cast<float>(kW) / kH));
    float k = 1.0f; // shown buffer pixel -> screen pixel, set once a render is available
    auto toL = [&](float bx, float by) { return ImVec2(mr.Min.x + bx * k, mr.Min.y + by * k); };

    // ---- input: press on an ESP element drags it, anywhere else rotates the model (with inertia)
    const ImGuiID id = window->GetID("##esp_preview");
    ImGui::ItemAdd(area, id); // no ItemSize: the pane must not grow a scroll range
    bool hovered = false, held = false;
    ImGui::ButtonBehavior(area, id, &hovered, &held, ImGuiButtonFlags_PressedOnClick);
    if (held && !s.held_prev) {
        s.drag = -1;
        for (int i = 0; i < EL_N; ++i)
            if (v.enable && s.hit_ok[i] && s.hit[i].Contains(io.MousePos)) s.drag = i;
        if (s.drag >= 0) s.grab = ImVec2(io.MousePos.x - s.hit[s.drag].Min.x, io.MousePos.y - s.hit[s.drag].Min.y);
        s.rotating = s.drag < 0;
        s.last_mouse = io.MousePos;
        s.vel = 0.0f;
    }
    if (held && s.rotating) {
        const float dx = (io.MousePos.x - s.last_mouse.x) / P(1.0f);
        s.last_mouse = io.MousePos;
        s.yaw += dx * 0.013f;
        if (dt > 0.0f) s.vel = ImLerp(s.vel, dx * 0.013f / dt, 0.5f);
    } else if (!held && std::fabs(s.vel) > 0.01f) {
        s.yaw += s.vel * dt;
        s.vel *= std::exp(-5.0f * dt);
    } else if (!held) {
        s.vel = 0.0f;
    }
    if (!held && s.held_prev && s.drag >= 0) Renumber(s);
    if (!held) {
        s.drag = -1;
        s.rotating = false;
    }
    s.held_prev = held;

    if (s.drag >= 0 && s.hit_ok[s.drag]) { // magnet: nearest box side, slot from the finger position
        const ImRect bx = s.box;
        const ImVec2 m = io.MousePos;
        auto seg = [](ImVec2 p, ImVec2 a, ImVec2 b) {
            const ImVec2 ab(b.x - a.x, b.y - a.y);
            const float t = ImClamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / ImMax(1e-3f, ab.x * ab.x + ab.y * ab.y), 0.0f, 1.0f);
            const ImVec2 q(a.x + ab.x * t - p.x, a.y + ab.y * t - p.y);
            return q.x * q.x + q.y * q.y;
        };
        const float d[4] = { seg(m, bx.Min, ImVec2(bx.Max.x, bx.Min.y)), seg(m, ImVec2(bx.Min.x, bx.Max.y), bx.Max),
                             seg(m, bx.Min, ImVec2(bx.Min.x, bx.Max.y)), seg(m, ImVec2(bx.Max.x, bx.Min.y), bx.Max) };
        int sd = 0;
        for (int q = 1; q < 4; ++q)
            if (d[q] < d[sd]) sd = q;
        const int dr = s.drag;
        const bool bar = IsBar(dr);
        s.side[dr] = sd;
        auto key = [&](ImVec2 cpt, bool isb) {
            return sd == SIDE_TOP ? bx.Min.y - cpt.y
                 : sd == SIDE_BOTTOM ? cpt.y - bx.Max.y
                 : (isb ? (sd == SIDE_LEFT ? bx.Min.x - cpt.x : cpt.x - bx.Max.x) : cpt.y);
        };
        const ImVec2 gc(m.x - s.grab.x + s.hit[dr].GetWidth() * 0.5f, m.y - s.grab.y + s.hit[dr].GetHeight() * 0.5f);
        const float mk = key(gc, bar);
        int others[EL_N], n = 0;
        for (int i = 0; i < EL_N; ++i)
            if (i != dr && s.side[i] == sd) others[n++] = i;
        std::sort(others, others + n, [&s](int a, int b) { return s.order[a] < s.order[b]; });
        int slot = 0;
        for (int q = 0; q < n; ++q) {
            const int o = others[q];
            if (sd >= SIDE_LEFT && IsBar(o) != bar) continue;
            if (s.hit_ok[o] && key(s.hit[o].GetCenter(), IsBar(o)) < mk) slot = q + 1;
        }
        for (int q = 0; q < n; ++q) s.order[others[q]] = static_cast<float>(q);
        s.order[dr] = static_cast<float>(slot) - 0.5f;
    }

    // ---- render request
    M3DSettings want;
    want.model = target == 1 ? 2 : 1; // Enemy / Local: CT agent + M4, Team: T agent + AKR
    want.yaw = s.yaw;
    const int mat_index = ImClamp(v.chams_material, 0, kChamsCount - 1);
    want.mat = (v.enable && v.chams) ? kChamsMat[mat_index] : M3D_TEXTURED;
    ToF(want.col, v.chams_visible.Resolve(time));
    want.overlay = v.chams_overlay;
    want.halo = v.enable && v.glow;
    ToF(want.haloCol, v.glow_color.Resolve(time));
    bool animated = want.mat == M3D_WATERFLOW || want.mat >= M3D_GALAXY;
    want.time = animated ? static_cast<float>(std::fmod(time, 1000.0)) : 0.0f;
    want.anim = static_cast<float>(std::fmod(time, 3600.0)); // lobby idle animation
    animated = true;
    Update(want, animated, rw_want, rh_want);
    if (!r.have) return;
    k = mw / static_cast<float>(r.shown_w);
    s.spawn = ImMin(1.0f, s.spawn + dt / 0.3f);
    dl->AddImage(r.tex->GetTexRef(), mr.Min, mr.Max, ImVec2(0, 0),
                 ImVec2(static_cast<float>(r.shown_w) / kW, static_cast<float>(r.shown_h) / kH), draw::Col(ImVec4(1, 1, 1, 1), s.spawn));

    for (bool& b : s.hit_ok) b = false;
    if (!v.enable || !r.shown_o.valid) return;

    const M3DSettings& ss = r.shown_s;
    const float kk = k / 0.475f; // velocitypasq sizes were tuned for a 228 px wide preview of the same buffer
    auto proj = [&](const float* p3) {
        float x = 0.0f, y = 0.0f;
        M3D_Project(p3[0], p3[1], p3[2], r.shown_w, r.shown_h, ss, &x, &y);
        return toL(x, y);
    };

    ImVec2 b0 = toL(r.shown_o.x0, r.shown_o.y0), b1 = toL(r.shown_o.x1, r.shown_o.y1);
    b0 = ImVec2(std::floor(b0.x - P(4.0f)) + 0.5f, std::floor(b0.y - P(4.0f)) + 0.5f);
    b1 = ImVec2(std::floor(b1.x + P(4.0f)) + 0.5f, std::floor(b1.y + P(3.0f)) + 0.5f);
    s.box = ImRect(b0, b1);
    const ImU32 blk = draw::Col(ImVec4(0, 0, 0, 0.78f));
    const float line = ImMax(1.0f, std::floor(P(1.0f)));

    // sound only: footstep rings under the model
    if (v.sound_only) {
        const ImVec2 feet((b0.x + b1.x) * 0.5f, b1.y - P(3.0f));
        const float h = b1.y - b0.y;
        for (int i = 0; i < 3; ++i) {
            const float t = static_cast<float>(std::fmod(time * 0.6 + i / 3.0, 1.0));
            const float rr = ImLerp(h * 0.05f, h * 0.24f, t);
            dl->AddEllipse(feet, ImVec2(rr, rr * 0.28f), draw::Col(c.pal.accent, (1.0f - t) * 0.7f), 0.0f, 0, P(1.5f));
        }
    }

    // hitboxes: capsules along the bones + head sphere
    static float bones[32][6];
    const int nb = ImMin(M3D_BoneCount(), 32);
    if (v.hitboxes) {
        M3D_Bones(ss, bones);
        const ImU32 fill = draw::Col(ImVec4(1.0f, 0.36f, 0.36f, 0.20f)), edge = draw::Col(ImVec4(1.0f, 0.43f, 0.43f, 0.78f));
        for (int i = 0; i < nb; ++i) {
            if (i == 3) continue; // neck -> head: covered by the head sphere
            const ImVec2 a = proj(bones[i]), b = proj(bones[i] + 3);
            const float rr = (i < 3 ? 6.5f : (i >= 10 && (i % 4 == 0 || i % 4 == 1) ? 4.5f : 3.5f)) * kk;
            const ImVec2 dd(b.x - a.x, b.y - a.y);
            const float L = std::sqrt(dd.x * dd.x + dd.y * dd.y);
            if (L < 0.5f) continue;
            const ImVec2 nrm(-dd.y / L * rr, dd.x / L * rr);
            dl->AddLine(a, b, fill, rr * 2.0f);
            dl->AddLine(ImVec2(a.x + nrm.x, a.y + nrm.y), ImVec2(b.x + nrm.x, b.y + nrm.y), edge, line);
            dl->AddLine(ImVec2(a.x - nrm.x, a.y - nrm.y), ImVec2(b.x - nrm.x, b.y - nrm.y), edge, line);
            dl->AddCircle(a, rr, edge, 16, line);
            dl->AddCircle(b, rr, edge, 16, line);
        }
        float hp[3];
        M3D_HeadPoint(ss, hp);
        ImVec2 hc = proj(hp);
        hc.y -= 6.0f * kk;
        dl->AddCircleFilled(hc, 8.5f * kk, fill, 24);
        dl->AddCircle(hc, 8.5f * kk, edge, 24, line);
    }

    // backtrack skeleton: earlier poses of the animation, trailing behind
    if (v.backtrack_skeleton) {
        const ImVec4 bc = v.backtrack_color.Resolve(time);
        for (int i = 3; i >= 1; --i) {
            M3DSettings past = ss;
            past.anim = ImMax(0.0f, ss.anim - 0.14f * static_cast<float>(i));
            M3D_Bones(past, bones);
            const float ox = -(b1.x - b0.x) * 0.07f * static_cast<float>(i);
            for (int q = 0; q < nb; ++q) {
                ImVec2 a = proj(bones[q]), b = proj(bones[q] + 3);
                a.x += ox;
                b.x += ox;
                dl->AddLine(a, b, draw::Col(bc, 0.55f / static_cast<float>(i)), line);
            }
        }
    }

    if (v.skeleton) {
        M3D_Bones(ss, bones);
        const ImU32 sc = draw::Col(v.skeleton_color.Resolve(time));
        for (int i = 0; i < nb; ++i) dl->AddLine(proj(bones[i]), proj(bones[i] + 3), sc, P(1.3f));
    }

    if (v.head_dot) {
        float hp[3];
        M3D_HeadPoint(ss, hp);
        const ImVec2 hc = proj(hp);
        dl->AddCircleFilled(hc, P(3.2f), blk, 16);
        dl->AddCircleFilled(hc, P(2.3f), draw::Col(v.head_dot_color.Resolve(time)), 16);
    }

    const ImU32 box_col = draw::Col(v.box_color.Resolve(time));
    if (v.box && v.box_type == 1) { // corner box
        const float lw = std::floor((b1.x - b0.x) * 0.28f), lh = std::floor((b1.y - b0.y) * 0.16f);
        auto corners = [&](ImU32 col, float o) {
            const ImVec2 p[4] = { b0, ImVec2(b1.x, b0.y), b1, ImVec2(b0.x, b1.y) };
            const float sx[4] = { 1, -1, -1, 1 }, sy[4] = { 1, 1, -1, -1 };
            for (int q = 0; q < 4; ++q) {
                const ImVec2 c0(p[q].x - sx[q] * o, p[q].y - sy[q] * o);
                dl->AddLine(c0, ImVec2(c0.x + sx[q] * (lw + o), c0.y), col, 1.0f);
                dl->AddLine(c0, ImVec2(c0.x, c0.y + sy[q] * (lh + o)), col, 1.0f);
            }
        };
        corners(blk, 1.0f);
        corners(blk, -1.0f);
        corners(box_col, 0.0f);
    } else if (v.box && v.box_type == 2) { // 3D box around the agent (rotates with the model)
        float hp3[3];
        M3D_HeadPoint(ss, hp3);
        const float hw = 0.28f, top = hp3[1] + 0.16f;
        const float cv[8][3] = { { -hw, 0, -hw }, { hw, 0, -hw }, { hw, 0, hw }, { -hw, 0, hw },
                                 { -hw, top, -hw }, { hw, top, -hw }, { hw, top, hw }, { -hw, top, hw } };
        ImVec2 sp[8];
        for (int q = 0; q < 8; ++q) sp[q] = proj(cv[q]);
        static const int ed[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        for (const auto& e : ed) dl->AddLine(sp[e[0]], sp[e[1]], draw::Col(ImVec4(0, 0, 0, 0.47f)), 3.0f);
        for (const auto& e : ed) dl->AddLine(sp[e[0]], sp[e[1]], box_col, 1.0f);
    } else if (v.box) {
        dl->AddRect(ImVec2(b0.x - 1, b0.y - 1), ImVec2(b1.x + 1, b1.y + 1), blk, 0.0f, 0, 1.0f);
        dl->AddRect(ImVec2(b0.x + 1, b0.y + 1), ImVec2(b1.x - 1, b1.y - 1), blk, 0.0f, 0, 1.0f);
        dl->AddRect(b0, b1, box_col, 0.0f, 0, 1.0f);
    }

    // ---- elements
    ImFont* font = v.esp_font == 1 ? c.fonts.verdana : (v.esp_font == 2 ? c.fonts.pixel : c.fonts.regular);
    if (font == nullptr) font = c.fonts.regular;
    const float fs = ui::FontPx(v.esp_font == 2 ? 10.0f : (v.esp_font == 1 ? 12.0f : 11.0f));
    const float line_h = std::floor(fs * 1.1f);
    const char* name = "t.me/lustriped";
    const char* weapon = target == 1 ? "akr" : "m4";
    const char* icon_name = target == 1 ? "AKR" : "M4";
    int wi = 0;
    for (int i = 0; i < WI_N; ++i)
        if (std::strcmp(WI_RECTS[i].name, icon_name) == 0) wi = i;
    static const char* const kFlagText[] = { "HK", "C4", "LONG A", "50MS", "RELOAD", "PIN", "SCOPE", "FLASHED", "DEFUSE" };
    const char* fl[IM_ARRAYSIZE(kFlagText)];
    int nfl = 0;
    for (int i = 0; i < IM_ARRAYSIZE(kFlagText); ++i)
        if ((v.flags & (1u << i)) != 0) fl[nfl++] = kFlagText[i];
    const bool want_icon = v.weapon_icon && r.icons != nullptr;
    const bool en[EL_N] = { v.name, v.health, v.armor, v.ammo, v.weapon_name, want_icon, v.distance, nfl > 0 };
    const float bar_t = ImMax(2.0f, std::floor(P(3.0f)));
    const float icon_h = std::floor(P(13.0f));
    const float cx = (b0.x + b1.x) * 0.5f;

    auto measure = [&](int i, int side) -> ImVec2 {
        const bool vert = side >= SIDE_LEFT;
        switch (i) {
        case EL_HP:
        case EL_AP:
        case EL_AMMO: return vert ? ImVec2(bar_t, b1.y - b0.y + 1.0f) : ImVec2(b1.x - b0.x + 1.0f, bar_t);
        case EL_NAME: return font->CalcTextSizeA(fs, FLT_MAX, 0.0f, name);
        case EL_WPN: return font->CalcTextSizeA(fs, FLT_MAX, 0.0f, weapon);
        case EL_ICON: return ImVec2(std::floor(WI_RECTS[wi].w * icon_h / WI_RECTS[wi].h), icon_h);
        case EL_DIST: return font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "12m");
        default: {
            float w = 0.0f;
            for (int q = 0; q < nfl; ++q) w = ImMax(w, font->CalcTextSizeA(fs, FLT_MAX, 0.0f, fl[q]).x);
            return ImVec2(w, line_h * static_cast<float>(ImMax(0, nfl - 1)) + font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "H").y);
        }
        }
    };

    ImRect tr[EL_N];
    {
        int idx[EL_N];
        for (int i = 0; i < EL_N; ++i) idx[i] = i;
        std::sort(idx, idx + EL_N, [&s](int a, int b) { return s.order[a] < s.order[b]; });
        float ct = b0.y, cb = b1.y, cl = b0.x, crt = b1.x; // outward cursors
        for (int q = 0; q < EL_N; ++q) {
            const int i = idx[q];
            if (!en[i]) continue;
            const int sd = s.side[i];
            const ImVec2 sz = measure(i, sd);
            const bool bar = IsBar(i);
            const float pb = P(bar ? 3.5f : (i == EL_ICON || i == EL_NAME ? 2.0f : 0.0f));
            const float pa = P(bar ? 2.5f : (i == EL_ICON ? 2.0f : 0.0f));
            const float half = bar ? 0.5f : 0.0f;
            if (sd == SIDE_TOP) {
                const float y1 = std::floor(ct - pb) + half;
                const float x0 = bar ? b0.x - 0.5f : std::floor(cx - sz.x * 0.5f);
                tr[i] = ImRect(ImVec2(x0, y1 - sz.y), ImVec2(x0 + sz.x, y1));
                ct = tr[i].Min.y - pa;
            } else if (sd == SIDE_BOTTOM) {
                const float y0 = std::floor(cb + pb) + half;
                const float x0 = bar ? b0.x - 0.5f : std::floor(cx - sz.x * 0.5f);
                tr[i] = ImRect(ImVec2(x0, y0), ImVec2(x0 + sz.x, y0 + sz.y));
                cb = tr[i].Max.y + pa;
            } else if (bar && sd == SIDE_LEFT) {
                const float x1 = cl - pb;
                tr[i] = ImRect(ImVec2(x1 - sz.x, b0.y - 0.5f), ImVec2(x1, b0.y - 0.5f + sz.y));
                cl = tr[i].Min.x - pa;
            } else if (bar) {
                const float x0 = crt + pb;
                tr[i] = ImRect(ImVec2(x0, b0.y - 0.5f), ImVec2(x0 + sz.x, b0.y - 0.5f + sz.y));
                crt = tr[i].Max.x + pa;
            }
        }
        float tl = b0.y - 1.0f, trr = b0.y - 1.0f; // text columns on the sides, below each other
        for (int q = 0; q < EL_N; ++q) {
            const int i = idx[q];
            if (!en[i] || IsBar(i) || s.side[i] < SIDE_LEFT) continue;
            const ImVec2 sz = measure(i, s.side[i]);
            const float gap = P(i == EL_ICON ? 3.0f : 1.0f);
            if (s.side[i] == SIDE_LEFT) {
                const float x1 = std::floor(cl - P(2.0f));
                tr[i] = ImRect(ImVec2(x1 - sz.x, tl), ImVec2(x1, tl + sz.y));
                tl += sz.y + gap;
            } else {
                const float x0 = std::floor(crt + P(2.0f));
                tr[i] = ImRect(ImVec2(x0, trr), ImVec2(x0 + sz.x, trr + sz.y));
                trr += sz.y + gap;
            }
        }
    }

    // smooth "magnet" slide when an element changes side / slot
    ImRect lr[EL_N];
    for (int i = 0; i < EL_N; ++i) {
        if (!en[i]) {
            s.prev_ok[i] = false;
            continue;
        }
        const ImVec2 rel(tr[i].Min.x - b0.x, tr[i].Min.y - b0.y); // relative to the box: rotating never slides
        if (s.prev_ok[i] && ImLengthSqr(ImVec2(rel.x - s.prev_rel[i].x, rel.y - s.prev_rel[i].y)) > P(6.0f) * P(6.0f)) {
            s.off[i].x += s.prev_rel[i].x - rel.x;
            s.off[i].y += s.prev_rel[i].y - rel.y;
        }
        s.prev_rel[i] = rel;
        s.prev_ok[i] = true;
        const float decay = std::exp(-16.0f * dt);
        s.off[i] = ImVec2(s.off[i].x * decay, s.off[i].y * decay);
        if (ImLengthSqr(s.off[i]) < 0.04f) s.off[i] = ImVec2(0, 0);
        tr[i].Translate(ImFloor(ImVec2(s.off[i].x + 0.5f, s.off[i].y + 0.5f)));
        lr[i] = tr[i];
        const float pad = P(4.0f);
        s.hit[i] = ImRect(ImVec2(lr[i].Min.x - pad, lr[i].Min.y - pad), ImVec2(lr[i].Max.x + pad, lr[i].Max.y + pad));
        s.hit_ok[i] = true;
    }

    // drag feedback: target side highlighted and a ghost under the finger
    if (s.drag >= 0 && en[s.drag]) {
        const int ts = s.side[s.drag];
        const ImVec2 e0[4] = { b0, ImVec2(b0.x, b1.y), b0, ImVec2(b1.x, b0.y) };
        const ImVec2 e1[4] = { ImVec2(b1.x, b0.y), b1, ImVec2(b0.x, b1.y), b1 };
        dl->AddLine(e0[ts], e1[ts], draw::Col(c.pal.accent, 0.25f), P(6.0f));
        dl->AddLine(e0[ts], e1[ts], draw::Col(c.pal.accent, 0.85f), P(1.5f));
        const ImVec2 gm(io.MousePos.x - s.grab.x, io.MousePos.y - s.grab.y);
        dl->AddRectFilled(gm, ImVec2(gm.x + s.hit[s.drag].GetWidth(), gm.y + s.hit[s.drag].GetHeight()), draw::Col(ImVec4(0.6f, 0.62f, 0.66f, 0.18f)),
                          P(3.0f));
    }

    const ImVec4 white(1, 1, 1, 0.94f);
    if (en[EL_NAME]) OutlinedText(dl, font, fs, lr[EL_NAME].Min, v.name_color.Resolve(time), name);

    if (en[EL_HP]) { // animated: hits drop fast with a fading trail, heals rise slowly
        s.hp_timer -= dt;
        if (s.hp_timer <= 0.0f) {
            if (s.hp_target > 28.0f && (std::rand() % 3) != 0) {
                s.hp_target = ImMax(4.0f, s.hp_target - static_cast<float>(14 + std::rand() % 30));
                s.hp_timer = 0.9f + static_cast<float>(std::rand() % 60) / 100.0f;
            } else {
                s.hp_target = 100.0f;
                s.hp_timer = 2.2f;
            }
        }
        const float sp = s.hp_target < s.hp_cur ? 14.0f : 2.2f;
        s.hp_cur += (s.hp_target - s.hp_cur) * (1.0f - std::exp(-sp * dt));
        s.hp_lag = s.hp_lag < s.hp_cur ? s.hp_cur : s.hp_lag + (s.hp_cur - s.hp_lag) * (1.0f - std::exp(-3.0f * dt));
        const float hp = ImClamp(s.hp_cur, 0.0f, 100.0f) / 100.0f, lag = ImClamp(s.hp_lag, 0.0f, 100.0f) / 100.0f;
        const ImRect hb = lr[EL_HP];
        const bool vert = s.side[EL_HP] >= SIDE_LEFT;
        const ImU32 hc = draw::Col(draw::Mix(v.health_low_color.Resolve(time), v.health_color.Resolve(time), hp));
        dl->AddRectFilled(ImVec2(hb.Min.x - 1, hb.Min.y - 1), ImVec2(hb.Max.x + 1, hb.Max.y + 1), blk);
        const int hv = static_cast<int>(s.hp_cur + 0.5f);
        char t[8];
        std::snprintf(t, sizeof t, "%d", hv);
        const ImVec2 nts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, t);
        if (vert) {
            const float top = hb.Max.y - hb.GetHeight() * hp, ltop = hb.Max.y - hb.GetHeight() * lag;
            if (ltop < top - 0.5f) dl->AddRectFilled(ImVec2(hb.Min.x, ltop), ImVec2(hb.Max.x, top), draw::Col(ImVec4(1, 1, 1, 0.47f)));
            dl->AddRectFilled(ImVec2(hb.Min.x, top), hb.Max, hc);
            if (hv < 100) OutlinedText(dl, font, fs, ImVec2(hb.GetCenter().x - nts.x * 0.5f, top - nts.y * 0.5f), white, t);
        } else {
            const float rx = hb.Min.x + hb.GetWidth() * hp, lx = hb.Min.x + hb.GetWidth() * lag;
            if (lx > rx + 0.5f) dl->AddRectFilled(ImVec2(rx, hb.Min.y), ImVec2(lx, hb.Max.y), draw::Col(ImVec4(1, 1, 1, 0.47f)));
            dl->AddRectFilled(hb.Min, ImVec2(rx, hb.Max.y), hc);
            if (hv < 100) OutlinedText(dl, font, fs, ImVec2(rx - nts.x * 0.5f, hb.GetCenter().y - nts.y * 0.5f), white, t);
        }
    }

    auto bar_fill = [&](int el, float frac, const ImVec4& col, const char* label) {
        const ImRect ab = lr[el];
        dl->AddRectFilled(ImVec2(ab.Min.x - 1, ab.Min.y - 1), ImVec2(ab.Max.x + 1, ab.Max.y + 1), blk);
        if (s.side[el] >= SIDE_LEFT) {
            const float top = ab.Max.y - ab.GetHeight() * frac;
            dl->AddRectFilled(ImVec2(ab.Min.x, top), ab.Max, draw::Col(col));
            if (label != nullptr) {
                const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
                OutlinedText(dl, font, fs, ImVec2(ab.GetCenter().x - ts.x * 0.5f, top - ts.y * 0.5f), white, label);
            }
        } else {
            const float rx = ab.Min.x + ab.GetWidth() * frac;
            dl->AddRectFilled(ab.Min, ImVec2(rx, ab.Max.y), draw::Col(col));
            if (label != nullptr) {
                const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
                OutlinedText(dl, font, fs, ImVec2(rx - ts.x * 0.5f, ab.GetCenter().y - ts.y * 0.5f), white, label);
            }
        }
    };
    if (en[EL_AP]) bar_fill(EL_AP, 1.0f, v.armor_color.Resolve(time), nullptr);
    if (en[EL_AMMO]) bar_fill(EL_AMMO, 14.0f / 30.0f, v.ammo_color.Resolve(time), "14");
    if (en[EL_WPN]) OutlinedText(dl, font, fs, lr[EL_WPN].Min, v.weapon_color.Resolve(time), weapon);
    if (en[EL_ICON]) {
        const WiRect& q = WI_RECTS[wi];
        const ImRect ir = lr[EL_ICON];
        const ImVec2 uv0(static_cast<float>(q.x) / WI_AW, static_cast<float>(q.y) / WI_AH);
        const ImVec2 uv1(static_cast<float>(q.x + q.w) / WI_AW, static_cast<float>(q.y + q.h) / WI_AH);
        const float o = ImMax(1.0f, std::floor(P(1.0f)));
        const ImVec2 dd[4] = { ImVec2(-o, 0), ImVec2(o, 0), ImVec2(0, -o), ImVec2(0, o) };
        for (const ImVec2& d : dd)
            dl->AddImage(r.icons->GetTexRef(), ImVec2(ir.Min.x + d.x, ir.Min.y + d.y), ImVec2(ir.Max.x + d.x, ir.Max.y + d.y), uv0, uv1,
                         draw::Col(ImVec4(0, 0, 0, 0.6f)));
        dl->AddImage(r.icons->GetTexRef(), ir.Min, ir.Max, uv0, uv1, draw::Col(v.weapon_icon_color.Resolve(time)));
    }
    if (en[EL_DIST]) OutlinedText(dl, font, fs, lr[EL_DIST].Min, v.distance_color.Resolve(time), "12m");
    if (en[EL_FLAGS]) {
        const ImRect fr = lr[EL_FLAGS];
        const int sd = s.side[EL_FLAGS];
        float fy = fr.Min.y;
        for (int i = 0; i < nfl; ++i) {
            const float w = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, fl[i]).x;
            const float x = sd == SIDE_LEFT ? fr.Max.x - w : (sd == SIDE_RIGHT ? fr.Min.x : std::floor(fr.GetCenter().x - w * 0.5f));
            OutlinedText(dl, font, fs, ImVec2(x, fy), v.flags_color.Resolve(time), fl[i]);
            fy += line_h;
        }
    }
}

} // namespace astral
