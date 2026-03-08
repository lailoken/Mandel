#include "imgui.h"

#include <GL/gl.h>
#include <iostream>
#include <SDL2/SDL.h>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "mandel_ui.hpp"
#include "thread_pool.hpp"

// Platform-specific texture management functions (OpenGL/SDL)
// These handle texture creation/update/deletion for the SDL/OpenGL platform
static void update_texture_opengl(ImTextureID* texture_id, const unsigned char* pixels, int width, int height)
{
    GLuint gl_texture_id = 0;
    
    if (*texture_id == 0)
    {
        // Create new texture
        glGenTextures(1, &gl_texture_id);
        *texture_id = static_cast<ImTextureID>(gl_texture_id);
    }
    else
    {
        gl_texture_id = static_cast<GLuint>(*texture_id);
    }
    
    // Update texture data
    glBindTexture(GL_TEXTURE_2D, gl_texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

static void delete_texture_opengl(ImTextureID texture_id)
{
    if (texture_id != 0)
    {
        GLuint gl_texture_id = static_cast<GLuint>(texture_id);
        glDeleteTextures(1, &gl_texture_id);
    }
}

int main(int /* argc */, char* /* argv */[])
{
#ifdef _WIN32
    // Tell Windows we are DPI aware (see ImGui FAQ: How should I handle DPI in my application)
    ::SetProcessDPIAware();
#endif

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::cerr << "Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Mandelbrot Explorer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 1200, window_flags);
    if (window == nullptr)
    {
        std::cerr << "Error: SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);  // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Create thread pool (shared by all components)
    // Leave one core free for the UI thread to ensure smooth rendering/input handling
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads > 1)
    {
        num_threads -= 1;
    }
    ThreadPool thread_pool(num_threads);

    int window_w, window_h;
    SDL_GetWindowSize(window, &window_w, &window_h);

    // Create and set up UI (handles overscan, user input, texture management)
    mandel::MandelUI ui(update_texture_opengl, delete_texture_opengl, thread_pool, window_w, window_h);

    // Main loop
    bool done = false;
    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
            {
                done = true;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
            {
                done = true;
            }
        }

        // FPS Counter
        static Uint32 last_time = SDL_GetTicks();
        static int frame_count = 0;
        frame_count++;
        Uint32 current_time = SDL_GetTicks();
        if (current_time - last_time >= 1000)
        {
            char title[256];
            sprintf(title, "Mandelbrot Explorer - FPS: %d", frame_count);
            SDL_SetWindowTitle(window, title);
            frame_count = 0;
            last_time = current_time;
        }

        // Start the Dear ImGui frame (backend sets io.DisplaySize and io.DisplayFramebufferScale)
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // DPI scaling per https://github.com/ocornut/imgui/blob/master/docs/FAQ.md#q-how-should-i-handle-dpi-in-my-application
        float dpi_scale = (io.DisplayFramebufferScale.x + io.DisplayFramebufferScale.y) * 0.5f;
        if (dpi_scale > 0.0f)
        {
            ImGuiStyle& style = ImGui::GetStyle();
            style.FontScaleDpi = dpi_scale;  // scale all fonts (ImGui 1.92+)
            static bool dpi_style_scaled_once = false;
            if (!dpi_style_scaled_once)
            {
                style.ScaleAllSizes(dpi_scale);  // scale paddings, spacing, etc. — call once
                dpi_style_scaled_once = true;
            }
        }

        // run imgui sample:
        // ImGui::ShowDemoWindow();

        ui.draw();

        // Rendering
        ImGui::Render();

        // Viewport must use framebuffer pixel size (not logical DisplaySize) for correct high-DPI rendering
        int fb_w = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
        int fb_h = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);  // ImGui dark theme background color
        glClear(GL_COLOR_BUFFER_BIT);
        
        // ImGui backend handles viewport automatically through RenderDrawData
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
