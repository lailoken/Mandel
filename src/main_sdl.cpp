#include "imgui.h"

#include <iostream>
#include <SDL2/SDL.h>
#include <GL/gl.h>

#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "mandel.hpp"
#include "mandel_render.hpp"

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

    // Create Mandelbrot renderer with long double precision
    mandel::MandelbrotRenderer<long double> mandelbrot(800, 800);
    
    // Create and set up ImGui renderer (platform-agnostic, uses ImGui canvas/rendering)
    // Platform-specific texture operations are handled via callbacks
    mandel::ImGuiRenderer<long double> renderer(&mandelbrot, update_texture_opengl, delete_texture_opengl);
    mandelbrot.set_render_callback(&renderer);

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

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // run imgui sample:
        // ImGui::ShowDemoWindow();

        renderer.draw();

        // Rendering
        ImGui::Render();
        
        // Clear the background
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
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
