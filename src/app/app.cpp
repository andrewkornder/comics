#define GL_SILENCE_DEPRECATION
#include <GL/glew.h>

#include <GLFW/glfw3.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <imgui/imgui_internal.h>

#include <stb/stb_image.h>

#include <sstream>
#include <string_view>
#include <filesystem>
#include <iostream>

#include "app.h"



void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}
void GLAPIENTRY gl_error_callback(GLenum /* source */, GLenum type, GLuint id, GLenum severity, 
                                  GLsizei /* length */, const GLchar* message, const void* /* userParam */) {
    if (id == 131185) return;

    auto cout = std::ostringstream{};
    auto endl = '\n';
    cout << "---------------------opengl-callback-start------------" << endl;
    cout << "message: "<< message << '\n';
    cout << "type: ";
    switch (type) {
        case GL_DEBUG_TYPE_ERROR: cout << "ERROR"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: cout << "DEPRECATED_BEHAVIOR"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: cout << "UNDEFINED_BEHAVIOR"; break;
        case GL_DEBUG_TYPE_PORTABILITY: cout << "PORTABILITY"; break;
        case GL_DEBUG_TYPE_PERFORMANCE: cout << "PERFORMANCE"; break;
        case GL_DEBUG_TYPE_OTHER: cout << "OTHER"; break;
    }
    cout << '\n';

    cout << "id: " << id << '\n';
    cout << "severity: ";
    switch (severity){
        case GL_DEBUG_SEVERITY_LOW: cout << "LOW"; break;
        case GL_DEBUG_SEVERITY_MEDIUM: cout << "MEDIUM"; break;
        case GL_DEBUG_SEVERITY_HIGH: cout << "HIGH"; break;
    }
    cout << "\n---------------------opengl-callback-end--------------\n";

    std::cout << std::move(cout).str();
}

AppRoot::AppRoot(std::string_view here): here(here), database(here) {
}


int AppRoot::init() {
    const auto get_path_of = [this](const std::string_view path) {
        return (std::filesystem::path(here) / path).string();
    };

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    window = glfwCreateWindow(INITIAL_W, INITIAL_H, TITLE.data(), nullptr, nullptr);
    if (window == nullptr)
        return 1;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(VSYNC_INTERVAL);

    glfwSetWindowSizeLimits(window, MIN_WINDOW_W, MIN_WINDOW_H, GLFW_DONT_CARE, GLFW_DONT_CARE);

    {
        dbg("loading icon file at {}", get_path_of(ICON_FILE));
        GLFWimage icon;
        icon.pixels = stbi_load(
            get_path_of(ICON_FILE).c_str(),
            &icon.width, &icon.height, NULL, 4
        );
        glfwSetWindowIcon(window, 1, &icon);
        stbi_image_free(icon.pixels);
    }

    glewInit();

    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(gl_error_callback, 0);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigDockingWithShift = true;

    ImFontConfig font_config;
    font_config.MergeMode = true;
    for (std::size_t i = 0; i < FONTS.size(); ++i) {
        io.Fonts->AddFontFromFileTTF(get_path_of(FONTS[i]).c_str(), 0.0f, i ? &font_config : nullptr);
    }
    
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    {
        state = {
            .x = 0, .y = 0,
            .w = 0, .h = 0,
            .maximized = false,
            .minimized = false,
        };
        glfwGetWindowPos(window, &state.x, &state.y);
        glfwGetWindowSize(window, &state.w, &state.h);
    }

    {
        ImGuiSettingsHandler handler;
        handler.TypeName = "PersistentAppSettings";
        handler.TypeHash = ImHashStr(handler.TypeName);
        handler.UserData = (void*) &settings;
        handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char*) {
            return (void*) (std::intptr_t) -1;
        };
        handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, void*, const char* line) {
            ((PersistentAppSettings*) handler->UserData)->read_line_ini(line);
        }; 
        handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {
            const std::string dump = ((PersistentAppSettings*) handler->UserData)->dump_to_ini();
            buf->append(dump.c_str(), dump.c_str() + dump.size());
            buf->append("\n\n");
        };
        ImGui::AddSettingsHandler(&handler);
    }
    return 0;
}


void AppRoot::mainloop() {
    dbg("entering app mainloop");

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        PendingAppUpdates up {
            .close = false, .maximize = false, .minimize = false,
            .w {}, .h {}, .x {}, .y {},
            .is_dragging = state.is_dragging,
        };
        glfwGetWindowPos(window, &up.x, &up.y);
        glfwGetWindowSize(window, &up.w, &up.h);
        render(up);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(window);

        apply_updates(up);
    }

    dbg("exited main loop");

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    dbg("shutdown");
}

void AppRoot::apply_updates(const PendingAppUpdates& up) {
    if (up.close) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    if (up.maximize && !up.minimize) {
        if (state.maximized) glfwRestoreWindow(window);
        else glfwMaximizeWindow(window);

        state.maximized = !state.maximized;
    }
    if (up.minimize) {
        if (state.maximized) {
            glfwRestoreWindow(window);
            state.maximized = false;
        }
        glfwIconifyWindow(window);
        state.minimized = true;
    } else {
        state.minimized = !!glfwGetWindowAttrib(window, GLFW_ICONIFIED);
    }

    if (state.x != up.x || state.y != up.y) {
        glfwSetWindowPos(window, up.x, up.y);
        state.x = up.x;
        state.y = up.y;
    }
    if (state.w != up.w || state.h != up.h) {
        glfwSetWindowSize(window, up.w, up.h);
        state.w = up.w;
        state.h = up.h;
    }
    
    state.is_dragging = up.is_dragging;
}

AppRoot::~AppRoot() {
    json_exp.clear();
    errors.clear();
    inspect.clear();
    reader.clear();
    manga_list.reset();
    images.reset();
    file_index.reset();
}
