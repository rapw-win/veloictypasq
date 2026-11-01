// lust web host: the exact menu code compiled to WebAssembly (wasm32-wasi). JavaScript feeds input and time, calls
// lust_frame() and draws the ImGui draw data with WebGL2 (see shell.html). Textures (font atlas, icon atlas,
// 3D model, weapon icons) are created / updated through three imported JS functions.
#include "astral/menu.h"
#include "astral/persist.h"
#include "imgui.h"
#include "model3d.h"

#include <cstdint>
#include <cstring>
#include <vector>

#define WASM_EXPORT(name) extern "C" __attribute__((export_name(#name)))
#define WASM_IMPORT(name) __attribute__((import_module("env"), import_name(#name)))

extern "C" {
WASM_IMPORT(tex_create) void js_tex_create(int id, int w, int h, const void* rgba);
WASM_IMPORT(tex_update) void js_tex_update(int id, int x, int y, int w, int h, int pitch, const void* rgba);
WASM_IMPORT(tex_destroy) void js_tex_destroy(int id);
}

namespace {

astral::Menu* g_menu = nullptr;
int g_next_tex = 1;
std::vector<std::uint32_t> g_out;   // frame description for JS
int g_tz_minutes = 0;

void UpdateTexture(ImTextureData* tex)
{
    if (tex->Status == ImTextureStatus_WantCreate) {
        const int id = g_next_tex++;
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
        js_tex_create(id, tex->Width, tex->Height, tex->GetPixels());
        tex->SetTexID(static_cast<ImTextureID>(id));
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
        const int id = static_cast<int>(tex->TexID);
        for (const ImTextureRect& r : tex->Updates)
            js_tex_update(id, r.x, r.y, r.w, r.h, tex->Width, tex->GetPixelsAt(r.x, r.y));
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        js_tex_destroy(static_cast<int>(tex->TexID));
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

} // namespace

int lust_web_tz_minutes() { return g_tz_minutes; }

WASM_EXPORT(lust_init) void lust_init(float scale, int tz_minutes)
{
    g_tz_minutes = tz_minutes;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendRendererName = "lust_webgl2";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    astral::persist::SetBaseDir("/tmp");
    g_menu = new astral::Menu();
    g_menu->Init(scale);
}

WASM_EXPORT(lust_set_scale) void lust_set_scale(float scale)
{
    if (g_menu != nullptr) g_menu->SetSystemDpi(scale);
}

// kind: 0 move, 1 down, 2 up, 3 leave; touch: 1 for touch screens
WASM_EXPORT(lust_pointer) void lust_pointer(int kind, float x, float y, int touch)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(touch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
    if (kind == 3) {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        return;
    }
    io.AddMousePosEvent(x, y);
    if (kind == 1) io.AddMouseButtonEvent(0, true);
    if (kind == 2) io.AddMouseButtonEvent(0, false);
}

WASM_EXPORT(lust_wheel) void lust_wheel(float dy) { ImGui::GetIO().AddMouseWheelEvent(0.0f, dy); }
WASM_EXPORT(lust_char) void lust_char(unsigned c) { ImGui::GetIO().AddInputCharacter(c); }
// 1 backspace, 2 enter, 3 escape, 4 left, 5 right, 6 delete, 7 ctrl
WASM_EXPORT(lust_key) void lust_key(int k, int down)
{
    static const ImGuiKey map[] = { ImGuiKey_None, ImGuiKey_Backspace, ImGuiKey_Enter, ImGuiKey_Escape, ImGuiKey_LeftArrow,
                                    ImGuiKey_RightArrow, ImGuiKey_Delete, ImGuiMod_Ctrl, ImGuiKey_F };
    if (k > 0 && k < IM_ARRAYSIZE(map)) ImGui::GetIO().AddKeyEvent(map[k], down != 0);
}
WASM_EXPORT(lust_toggle) void lust_toggle()
{
    if (g_menu != nullptr) g_menu->Toggle();
}
WASM_EXPORT(lust_showing) int lust_showing() { return g_menu != nullptr && g_menu->Showing() ? 1 : 0; }

// Runs one frame and returns a pointer to: [count, then per draw list: vtx_ptr, vtx_count, idx_ptr, idx_count,
// cmd_count, then per command: elem_count, idx_offset, tex_id, clip x0 y0 x1 y1 (float bits)].
WASM_EXPORT(lust_frame) const std::uint32_t* lust_frame(float w, float h, float dt)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(w, h);
    io.DeltaTime = dt > 0.0f && dt < 0.5f ? dt : 1.0f / 60.0f;
    g_menu->PreFrame();
    ImGui::NewFrame();
    // backdrop standing in for the game under the overlay
    ImGui::GetBackgroundDrawList()->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, h), IM_COL32(40, 52, 60, 255), IM_COL32(70, 80, 70, 255),
                                                            IM_COL32(30, 30, 34, 255), IM_COL32(20, 28, 36, 255));
    g_menu->Frame();
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd->Textures != nullptr)
        for (ImTextureData* tex : *dd->Textures)
            if (tex->Status != ImTextureStatus_OK) UpdateTexture(tex);

    g_out.clear();
    g_out.push_back(static_cast<std::uint32_t>(dd->CmdListsCount));
    auto f = [](float v) {
        std::uint32_t u;
        std::memcpy(&u, &v, 4);
        return u;
    };
    for (const ImDrawList* dl : dd->CmdLists) {
        g_out.push_back(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(dl->VtxBuffer.Data)));
        g_out.push_back(static_cast<std::uint32_t>(dl->VtxBuffer.Size));
        g_out.push_back(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(dl->IdxBuffer.Data)));
        g_out.push_back(static_cast<std::uint32_t>(dl->IdxBuffer.Size));
        int n = 0;
        for (const ImDrawCmd& c : dl->CmdBuffer)
            if (c.UserCallback == nullptr && c.ElemCount > 0) ++n;
        g_out.push_back(static_cast<std::uint32_t>(n));
        for (const ImDrawCmd& c : dl->CmdBuffer) {
            if (c.UserCallback != nullptr || c.ElemCount == 0) continue;
            g_out.push_back(c.ElemCount);
            g_out.push_back(c.IdxOffset);
            g_out.push_back(static_cast<std::uint32_t>(c.GetTexID()));
            g_out.push_back(f(c.ClipRect.x));
            g_out.push_back(f(c.ClipRect.y));
            g_out.push_back(f(c.ClipRect.z));
            g_out.push_back(f(c.ClipRect.w));
        }
    }
    return g_out.data();
}

// ---- Web Worker side (a second instance of this module): renders the 3D agent for the main instance ----
namespace {
M3DSettings g_ws;
M3DOut g_wo{};
std::vector<std::uint8_t> g_wbuf;
} // namespace

WASM_EXPORT(lust_worker_settings) M3DSettings* lust_worker_settings() { return &g_ws; }
WASM_EXPORT(lust_worker_out) M3DOut* lust_worker_out() { return &g_wo; }
WASM_EXPORT(lust_worker_render) std::uint8_t* lust_worker_render(int w, int h)
{
    M3D_Init();
    g_wbuf.resize(static_cast<size_t>(w) * h * 4);
    M3D_Render(g_wbuf.data(), w, h, g_ws, &g_wo);
    return g_wbuf.data();
}
