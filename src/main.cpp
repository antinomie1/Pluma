#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <stdio.h>

#include "../default_font.c"
//#include "style.h"

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int, char**)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    const char* glsl_version = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    GLFWwindow* window = glfwCreateWindow((int)(800 * main_scale), (int)(450 * main_scale), "launcher", nullptr, nullptr);
    if (window == nullptr)
        return 1;

    // 限定窗口最小尺寸
    glfwSetWindowSizeLimits(window, 800, 450, GLFW_DONT_CARE, GLFW_DONT_CARE);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    ImGuiStyle& style = ImGui::GetStyle(); // 获取当前样式
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)
    
    style.Colors[ImGuiCol_Button] = ImVec4(95.0/256.0, 158.0/256.0, 160/256.0, 0.75f); // 按钮背景颜色
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(95.0/256.0, 158.0/256.0, 160/256.0, 0.85f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(95.0/256.0, 158.0/256.0, 160/256.0, 0.9f);
    style.FrameRounding = 3.0f; // 设置圆角半径，数值越大越圆
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f,0.0f,0.0f,0.75f));

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImFont* Font = io.Fonts->AddFontFromMemoryCompressedBase85TTF(defFont_compressed_data_base85, 24.0f * main_scale, nullptr, io.Fonts->GetGlyphRangesDefault());
    ImFont* bigFont = io.Fonts->AddFontFromMemoryCompressedBase85TTF(defFont_compressed_data_base85, 36.0f * main_scale, nullptr, io.Fonts->GetGlyphRangesDefault());

    ImVec4 clear_color = ImVec4(1.00f, 1.00f, 1.00f, 0.85f);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 主窗口全屏
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                        ImGuiWindowFlags_NoBackground;

        ImGui::Begin("Main", nullptr, window_flags);

        // 上半部分自适应高度
        float bottom_height = 60.0f;
        float top_height = ImGui::GetWindowHeight() - bottom_height -20.0f;
        if (top_height < 0) top_height = 0;

        ImGui::BeginChild("TopRegion", ImVec2(0, top_height), true, ImGuiWindowFlags_NoBackground);
        ImGui::PushFont(bigFont);
        ImGui::Text("Hello");
        ImGui::PopFont();
        ImGui::EndChild();

        // 下半部分固定高度
        ImGui::BeginChild(
            "BottomRegion",
            ImVec2(0, bottom_height),
            true,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        );
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255,255,255,255));
        ImGui::Button("主页", ImVec2(80.0f, 50.0f));
        ImGui::SameLine();
        ImGui::Button("下载", ImVec2(80.0f, 50.0f));
        ImGui::SameLine();
        ImGui::Button("设置", ImVec2(80.0f, 50.0f));
        ImGui::PopStyleColor();
        ImGui::EndChild();

        ImGui::End();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
