#include "ui.h"

namespace UI {
    int Index=0;
    void RenderUI() {
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

        float bottom_height = 50.0f;
        float top_height = 40.0f;
        float middle_height = ImGui::GetWindowHeight() - bottom_height - top_height -25.0f;

        ImGui::BeginChild("TopRegion", ImVec2(0, top_height), true, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        TitleBar();

        ImGui::EndChild();

        ImGui::BeginChild("MiddleRegion", ImVec2(0, middle_height), true, ImGuiWindowFlags_NoBackground);

        ImGui::EndChild();

        // 下半部分固定高度
        ImGui::BeginChild("BottomRegion",ImVec2(0, bottom_height),true,ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        BottomBtn();
        
        ImGui::EndChild();

        ImGui::End();
    }

    void TitleBar() {
        ImGui::PushFont(Font::boldFont);
        ImGui::Text("Launcher");
        ImGui::PopFont();
        ImGui::SameLine(ImGui::GetWindowWidth() - 40.0f);
        if(ImGui::Button("X", ImVec2(30.0f, 30.0f)))
            exit(0);
    }
    void SetStyle() {
        ImGuiStyle& style = ImGui::GetStyle();

        style.Colors[ImGuiCol_Button] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.0f, 0.0f, 0.15f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.0f, 0.0f, 0.2f);
        style.FrameRounding = 2.0f;

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.75f));
    }
    void BottomBtn(){
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0,0,0,250));

        BtnStyle(Index==0);
        if (ImGui::Button("主页", ImVec2(70.0f, 40.0f)))
            Index=0;
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.0f,20.0f);

        //BtnStyle(Index==1);
        //if (ImGui::Button("游戏", ImVec2(70.0f, 40.0f)))
        //    Index=1;
        //ImGui::PopStyleColor(3);

        //ImGui::SameLine(0.0f,20.0f);

        BtnStyle(Index==2);
        if (ImGui::Button("下载", ImVec2(70.0f, 40.0f)))
            Index=2;
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.0f,20.0f);

        BtnStyle(Index==3);
        if(ImGui::Button("档案管理", ImVec2(110.0f, 40.0f)))
            Index=3;
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.0f,20.0f);

        BtnStyle(Index==4);
        if(ImGui::Button("设置", ImVec2(70.0f, 40.0f)))
            Index=4;
        ImGui::PopStyleColor(3);

        ImGui::PopStyleColor();
    }
    void BtnStyle(bool Actived){
        ImGui::PushStyleColor(ImGuiCol_Button, Actived ? ImVec4(95.0f/256.0f, 158.0f/256.0f, 160.0f/256.0f, 0.9f) : ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Actived ? ImVec4(95.0f/256.0f, 158.0f/256.0f, 160.0f/256.0f, 0.85f) : ImVec4(0.0f, 0.0f, 0.0f, 0.15f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Actived ? ImVec4(95.0f/256.0f, 158.0f/256.0f, 160.0f/256.0f, 0.9f) : ImVec4(0.0f, 0.0f, 0.0f, 0.2f));
    }

}