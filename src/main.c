#define GLAD_GL_IMPLEMENTATION
#include <glad2/gl.h>

#define SOKOL_IMPL
#define SOKOL_GLCORE
#include <sokol/sokol_app.h>

#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg/nanovg.h>
#include <nanovg/nanovg_gl.h>

static NVGcontext* vg;

void init(void) 
{
    gladLoaderLoadGL();
    vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
}
void cleanup()
{

}

void frame(void) 
{
    int w = sapp_width(), h = sapp_height();
    float dpi = sapp_dpi_scale();

    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    nvgBeginFrame(vg, w / dpi, h / dpi, dpi);
    nvgBeginPath(vg);
    nvgRect(vg, 100, 100, 120, 30);
    nvgFillColor(vg, nvgRGBA(255, 192, 0, 255));
    nvgFill(vg);
    nvgEndFrame(vg);
}

sapp_desc sokol_main(int argc, char* argv[]) {
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = NULL,
        .width = 800,
        .height = 600,
        .window_title = "My Sokol Window",
    };
}