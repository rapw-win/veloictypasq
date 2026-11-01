// lust - Android overlay host for the menu (GLSurfaceView inside a TYPE_APPLICATION_OVERLAY window).
// Based on the velocitypasq Android host: touches are queued from the UI thread and replayed on the GL thread as
// ImGui touch-screen mouse events, one button transition per frame.
#include <jni.h>
#include <GLES3/gl3.h>

#include <cfloat>
#include <cmath>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>

#include "astral/menu.h"
#include "astral/persist.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"

namespace {

astral::Menu* g_menu = nullptr;
bool g_ctx_made = false, g_inited = false;
int g_w = 0, g_h = 0;
double g_last = 0.0;
float g_scale = 0.0f;
std::string g_files_dir;

struct Ev {
    int type; // 0 pos, 1 down, 2 up, 3 leave
    float x, y;
};
std::deque<Ev> g_ev;
std::mutex g_evm;
std::mutex g_cmdm;
int g_pending_toggles = 0;

double Now()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) * 1e-9;
}

// EGL context was recreated: every ImGui texture (font atlas, icon atlas, 3D model) must be uploaded again.
void InvalidateTextures()
{
    for (ImTextureData* td : ImGui::GetPlatformIO().Textures)
        if (td->Status == ImTextureStatus_OK || td->Status == ImTextureStatus_WantUpdates) {
            td->SetTexID(ImTextureID_Invalid);
            td->SetStatus(ImTextureStatus_WantCreate);
        }
}

// The 600 x 620 menu fitted on the phone screen (portrait or landscape), in 0.05 steps.
float FitScale(int w, int h)
{
    float s = std::fmin(static_cast<float>(h) * 0.90f / 620.0f, static_cast<float>(w) * 0.94f / 600.0f);
    s = std::floor(s * 20.0f) / 20.0f;
    return s < 0.75f ? 0.75f : (s > 4.0f ? 4.0f : s);
}

} // namespace

extern "C" JNIEXPORT void JNICALL Java_com_lust_menu_Native_setFilesDir(JNIEnv* env, jclass, jstring dir)
{
    const char* s = env->GetStringUTFChars(dir, nullptr);
    g_files_dir = s != nullptr ? s : "";
    env->ReleaseStringUTFChars(dir, s);
    astral::persist::SetBaseDir(g_files_dir.c_str());
}

extern "C" JNIEXPORT void JNICALL Java_com_lust_menu_Native_init(JNIEnv*, jclass)
{
    if (!g_ctx_made) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_IsTouchScreen;
        ImGui_ImplOpenGL3_Init("#version 300 es");
        g_menu = new astral::Menu();
        g_menu->Init(g_scale > 0.0f ? g_scale : 1.5f);
        g_ctx_made = true;
    } else {
        ImGui_ImplOpenGL3_Shutdown();
        InvalidateTextures();
        ImGui_ImplOpenGL3_Init("#version 300 es");
    }
    g_inited = true;
    g_last = Now();
}

extern "C" JNIEXPORT void JNICALL Java_com_lust_menu_Native_resize(JNIEnv*, jclass, jint w, jint h)
{
    g_w = w;
    g_h = h;
    glViewport(0, 0, w, h);
    if (w > 0 && h > 0) {
        g_scale = FitScale(w, h);
        if (g_menu != nullptr) g_menu->SetSystemDpi(g_scale);
    }
}

extern "C" JNIEXPORT void JNICALL Java_com_lust_menu_Native_touch(JNIEnv*, jclass, jint a, jfloat x, jfloat y)
{
    std::lock_guard<std::mutex> lk(g_evm);
    g_ev.push_back({ 0, x, y });
    if (a == 0) g_ev.push_back({ 1, x, y });
    else if (a == 2) {
        g_ev.push_back({ 2, x, y });
        g_ev.push_back({ 3, x, y });
    }
}

// 1: show / hide the menu (the white line at the bottom of the screen)
extern "C" JNIEXPORT void JNICALL Java_com_lust_menu_Native_command(JNIEnv*, jclass, jint c)
{
    std::lock_guard<std::mutex> lk(g_cmdm);
    if (c == 1) ++g_pending_toggles;
}

// returns bit0: menu open or animating (the overlay takes touches)
extern "C" JNIEXPORT jint JNICALL Java_com_lust_menu_Native_frame(JNIEnv*, jclass)
{
    if (!g_inited || g_menu == nullptr || g_w <= 0) return 0;
    {
        std::lock_guard<std::mutex> lk(g_cmdm);
        for (; g_pending_toggles > 0; --g_pending_toggles) g_menu->Toggle();
    }
    const double now = Now();
    const float dt = static_cast<float>(now - g_last);
    g_last = now;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(g_w), static_cast<float>(g_h));
    io.DeltaTime = dt > 0.0f && dt < 0.5f ? dt : 1.0f / 60.0f;
    { // one button transition per frame: hover is resolved before the press, the press before the release
        std::lock_guard<std::mutex> lk(g_evm);
        while (!g_ev.empty()) {
            const Ev e = g_ev.front();
            g_ev.pop_front();
            if (e.type == 0) {
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(e.x, e.y);
                continue;
            }
            if (e.type == 1) {
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMouseButtonEvent(0, true);
                break;
            }
            if (e.type == 2) {
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMouseButtonEvent(0, false);
                break;
            }
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            break;
        }
    }

    g_menu->PreFrame();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    g_menu->Frame();
    ImGui::Render();
    glViewport(0, 0, g_w, g_h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return g_menu->Showing() ? 1 : 0;
}
