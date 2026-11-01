// Desktop test host for the lust menu (X11 + GLX, desktop OpenGL 3). Used to take screenshots of the exact
// code that ships in the APK: run it under Xvfb, it renders N frames and writes the last one as a PPM.
//   host_linux --w 1600 --h 900 --scale 1.5 --tab 2 --target 0 --frames 120 --out shot.ppm [--click x,y@frame ...]
#include "astral/menu.h"
#include "astral/persist.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

extern "C" {
typedef struct __GLXcontextRec* GLXContext;
XVisualInfo* glXChooseVisual(Display*, int, int*);
GLXContext glXCreateContext(Display*, XVisualInfo*, GLXContext, Bool);
Bool glXMakeCurrent(Display*, XID, GLXContext);
void glXSwapBuffers(Display*, XID);
void glViewport(int, int, int, int);
void glClearColor(float, float, float, float);
void glClear(unsigned);
void glReadPixels(int, int, int, int, unsigned, unsigned, void*);
void glFinish();
}

struct Click { float x, y; int frame; bool drag; float dx, dy; int len; };

int main(int argc, char** argv)
{
    int W = 1600, H = 900, frames = 90, tab = 4, target = -1, rage_cat = 0;
    float scale = 1.0f;
    std::string out = "shot.ppm";
    std::vector<Click> clicks;
    bool hidden = false;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string a = argv[i], v = argv[i + 1];
        if (a == "--w") W = atoi(v.c_str());
        else if (a == "--h") H = atoi(v.c_str());
        else if (a == "--frames") frames = atoi(v.c_str());
        else if (a == "--tab") tab = atoi(v.c_str());
        else if (a == "--target") target = atoi(v.c_str());
        else if (a == "--rage") rage_cat = atoi(v.c_str());
        else if (a == "--scale") scale = (float)atof(v.c_str());
        else if (a == "--out") out = v;
        else if (a == "--hidden") hidden = v == "1";
        else if (a == "--click" || a == "--drag") {
            Click c{};
            if (a == "--click") sscanf(v.c_str(), "%f,%f@%d", &c.x, &c.y, &c.frame);
            else { c.drag = true; sscanf(v.c_str(), "%f,%f,%f,%f,%d@%d", &c.x, &c.y, &c.dx, &c.dy, &c.len, &c.frame); }
            clicks.push_back(c);
        }
    }
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) { fprintf(stderr, "no display\n"); return 1; }
    int attr[] = { 4 /*GLX_RGBA*/, 5 /*GLX_DOUBLEBUFFER*/, 8, 8, 9, 8, 10, 8, 11, 8, 0 };
    // GLX_RED_SIZE=8 GLX_GREEN_SIZE=9 GLX_BLUE_SIZE=10 GLX_ALPHA_SIZE=11
    int attr2[] = { 4, 5, 8, 8, 9, 8, 10, 8, 0 };
    XVisualInfo* vi = glXChooseVisual(dpy, DefaultScreen(dpy), attr);
    if (!vi) vi = glXChooseVisual(dpy, DefaultScreen(dpy), attr2);
    if (!vi) { fprintf(stderr, "no visual\n"); return 1; }
    Window root = RootWindow(dpy, vi->screen);
    XSetWindowAttributes swa{};
    swa.colormap = XCreateColormap(dpy, root, vi->visual, AllocNone);
    Window win = XCreateWindow(dpy, root, 0, 0, W, H, 0, vi->depth, InputOutput, vi->visual, CWColormap, &swa);
    XMapWindow(dpy, win);
    GLXContext ctx = glXCreateContext(dpy, vi, nullptr, True);
    glXMakeCurrent(dpy, win, ctx);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui_ImplOpenGL3_Init("#version 130");
    astral::persist::SetBaseDir("/tmp");
    static astral::Menu menu;
    if (!menu.Init(scale)) { fprintf(stderr, "menu init failed\n"); return 1; }
    menu.SetTab(static_cast<astral::Tab>(tab));
    if (target >= 0) menu.config().visuals.target = target;
    menu.config().rage.category = rage_cat;
    if (hidden) menu.SetVisible(false);

    std::vector<unsigned char> px((size_t)W * H * 4);
    for (int f = 0; f < frames; ++f) {
        menu.PreFrame();
        ImGui_ImplOpenGL3_NewFrame();
        io.DisplaySize = ImVec2((float)W, (float)H);
        io.DeltaTime = 1.0f / 60.0f;
        const ImVec2 o = menu.Placed() ? menu.WindowPos() : ImVec2(0, 0);
        for (const Click& c : clicks) {
            if (!c.drag) {
                if (f == c.frame) { io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen); io.AddMousePosEvent(o.x + c.x, o.y + c.y); }
                if (f == c.frame + 1) io.AddMouseButtonEvent(0, true);
                if (f == c.frame + 3) io.AddMouseButtonEvent(0, false);
            } else {
                if (f == c.frame) io.AddMousePosEvent(o.x + c.x, o.y + c.y);
                if (f == c.frame + 1) io.AddMouseButtonEvent(0, true);
                if (f > c.frame + 1 && f <= c.frame + 1 + c.len) {
                    float t = (float)(f - c.frame - 1) / c.len;
                    io.AddMousePosEvent(o.x + c.x + c.dx * t, o.y + c.y + c.dy * t);
                }
                if (f == c.frame + 2 + c.len) io.AddMouseButtonEvent(0, false);
            }
        }
        ImGui::NewFrame();
        ImGui::GetBackgroundDrawList()->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2((float)W, (float)H), IM_COL32(40, 52, 60, 255),
                                                                IM_COL32(70, 80, 70, 255), IM_COL32(30, 30, 34, 255), IM_COL32(20, 28, 36, 255));
        menu.Frame();
        ImGui::Render();
        glViewport(0, 0, W, H);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(0x4000);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glFinish();
        if (f == frames - 1) glReadPixels(0, 0, W, H, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, px.data());
        glXSwapBuffers(dpy, win);
        std::this_thread::sleep_for(std::chrono::milliseconds(8)); // let the model worker deliver frames
    }
    FILE* fp = fopen(out.c_str(), "wb");
    fprintf(fp, "P6\n%d %d\n255\n", W, H);
    for (int y = H - 1; y >= 0; --y)
        for (int x = 0; x < W; ++x) fwrite(&px[((size_t)y * W + x) * 4], 1, 3, fp);
    fclose(fp);
    printf("wrote %s\n", out.c_str());
    return 0;
}
