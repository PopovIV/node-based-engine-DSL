#include <application.h>
#include "editor_modules_list.h"
#include "utilities/builders.h"
#include "utilities/widgets.h"
#include "structs/pin.h"
#include "structs/node.h"
#include "structs/link.h"

#include <imgui_node_editor.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <utility>
#include <iostream>

#include <fstream>
#include "thirdparty/json.hpp"
using json = nlohmann::json;

#include "ImGuiFileBrowser.h"
imgui_addons::ImGuiFileBrowser file_dialog;

#define ZERO_CODE 0xFFFFFFFF

static inline ImRect ImGui_GetItemRect() {
    return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

static inline ImRect ImRect_Expanded(const ImRect& rect, float x, float y) {
    auto result = rect;
    result.Min.x -= x;
    result.Min.y -= y;
    result.Max.x += x;
    result.Max.y += y;
    return result;
}

namespace ed = ax::NodeEditor;
namespace util = ax::NodeEditor::Utilities;

using namespace ax;

using ax::Widgets::IconType;

static ed::EditorContext* m_Editor = nullptr;

std::vector<std::string> InsertOrder;
std::map<std::string, std::vector<BlueprintLibraryNode>> BlueprintLibrary;
std::map<EditorArgsTypes, ImColor> PinColorChoser;

static bool Splitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size = -1.0f) {
    using namespace ImGui;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID("##Splitter");
    ImRect bb;
    bb.Min = window->DC.CursorPos + (split_vertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
    bb.Max = bb.Min + CalcItemSize(split_vertically ? ImVec2(thickness, splitter_long_axis_size) : ImVec2(splitter_long_axis_size, thickness), 0.0f, 0.0f);
    return SplitterBehavior(bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 0.0f);
}

// Main editor class
struct Editor: public Application {
    using Application::Application;

    int GetNextId() {
        return m_NextId++;
    }

    void TouchNode(ed::NodeId id) {
        m_NodeTouchTime[id] = m_TouchTime;
    }

    float GetTouchProgress(ed::NodeId id) {
        auto it = m_NodeTouchTime.find(id);
        if (it != m_NodeTouchTime.end() && it->second > 0.0f) {
            return (m_TouchTime - it->second) / m_TouchTime;
        }
        else {
            return 0.0f;
        }
    }

    void UpdateTouch() {
        const auto deltaTime = ImGui::GetIO().DeltaTime;
        for (auto& entry : m_NodeTouchTime) {
            if (entry.second > 0.0f) {
                entry.second -= deltaTime;
            }
        }
    }

    Node* FindNode(ed::NodeId id) {
        for (auto& node : m_Nodes) {
            if (node.ID == id) {
                return &node;
            }
        }

        return nullptr;
    }

    Link* FindLink(ed::LinkId id) {
        for (auto& link : m_Links) {
            if (link.ID == id) {
                return &link;
            }
        }

        return nullptr;
    }

    Pin* FindPin(ed::PinId id) {
        if (!id) {
            return nullptr;
        }

        for (auto& node : m_Nodes) {
            for (auto& pin : node.Inputs) {
                if (pin.ID == id) {
                    return &pin;
                }
            }

            for (auto& pin : node.Outputs) {
                if (pin.ID == id) {
                    return &pin;
                }
            }
        }

        return nullptr;
    }

    bool IsPinLinked(ed::PinId id) {
        if (!id) {
            return false;
        }

        for (auto& link : m_Links) {
            if (link.StartPinID == id || link.EndPinID == id) {
                return true;
            }
        }

        return false;
    }

    Node* FindEndNode(ed::PinId id) {
        if (!id) {
            return nullptr;
        }

        for (auto& link : m_Links) {
            if (link.StartPinID == id) {
                return FindPin(link.EndPinID)->Node;
            }
        }

        return nullptr;
    }

    bool CanCreateLink(Pin* a, Pin* b) {
        if (!a || !b || a == b || a->Kind == b->Kind || a->Type != b->Type || a->Node == b->Node) {
            return false;
        }

        return true;
    }

    void BuildNode(Node* node) {
        for (auto& input : node->Inputs) {
            input.Node = node;
            input.Kind = PinKind::Input;
        }

        for (auto& output : node->Outputs) {
            output.Node = node;
            output.Kind = PinKind::Output;
        }
    }

    void BuildNodes() {
        for (auto& node : m_Nodes) {
            BuildNode(&node);
        }
    }

    void OnStart() override {
        ed::Config config;

        config.SettingsFile = "Editor.json";
        config.UserPointer = this;

        m_Editor = ed::CreateEditor(&config);
        ed::SetCurrentEditor(m_Editor);

        // Create 4 event nodes 
        float x = 0.0f;
        float y = -300.0f;
        for (auto& f : BlueprintLibrary["Event"]) {
            int nodeId = GetNextId();
            m_Nodes.emplace_back(nodeId, f, GetNodeColor(f.EngineType));
            m_Nodes.back().Outputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
            auto pos = ImVec2(x, y);
            ed::SetNodePosition(nodeId, pos);
            y += 100.0f;
        }
        BuildNodes();

        ed::NavigateToContent();

        m_HeaderBackground = LoadTexture("data/BlueprintBackground.png");
        m_SaveIcon         = LoadTexture("data/ic_save_white_24dp.png");
        m_RestoreIcon      = LoadTexture("data/ic_restore_white_24dp.png");
    }

    void OnStop() override {
        auto releaseTexture = [this](ImTextureID& id) {
            if (id) {
                DestroyTexture(id);
                id = nullptr;
            }
        };

        releaseTexture(m_RestoreIcon);
        releaseTexture(m_SaveIcon);
        releaseTexture(m_HeaderBackground);

        if (m_Editor) {
            ed::DestroyEditor(m_Editor);
            m_Editor = nullptr;
        }
    }

    // Function to get Node color based on Node type
    ImColor GetNodeColor(EditorNodeTypes type) {
        switch (type) {
            default:                                      return ImColor(255, 255, 255);
            case EditorNodeTypes::editor_node_event:      return ImColor(220, 48, 48);
            case EditorNodeTypes::editor_node_function:   return ImColor(68, 201, 156);
            case EditorNodeTypes::editor_node_none:       return ImColor(147, 226, 74);
            case EditorNodeTypes::editor_node_workflow:   return ImColor(124, 21, 153);
        }
    };

    // Function to select Pin icon based on args type
    void DrawPinIcon(const Pin& pin, bool connected, int alpha) {
        IconType iconType;
        ImColor color = PinColorChoser[pin.Type];
        color.Value.w = alpha / 255.0f;
        switch (pin.Type) {
            case EditorArgsTypes::editor_arg_float:       iconType = IconType::Square;   break;
            case EditorArgsTypes::editor_arg_float2:      iconType = IconType::Square;   break;
            case EditorArgsTypes::editor_arg_float3:      iconType = IconType::Square;   break;
            case EditorArgsTypes::editor_arg_float4:      iconType = IconType::Square;   break;
            case EditorArgsTypes::editor_arg_gdr_index:   iconType = IconType::Circle; break;
            case EditorArgsTypes::editor_arg_matr:        iconType = IconType::RoundSquare; break;
            case EditorArgsTypes::editor_arg_none:        iconType = IconType::Flow;   break;
            case EditorArgsTypes::editor_arg_string:      iconType = IconType::Diamond; break;
            default:
                iconType = IconType::Square; break;
        }

        ax::Widgets::Icon(ImVec2(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize)), iconType, connected, color, ImColor(32, 32, 32, alpha));
    };

    void ShowStyleEditor(bool* show = nullptr) {
        if (!ImGui::Begin("Style", show)) {
            ImGui::End();
            return;
        }

        auto paneWidth = ImGui::GetContentRegionAvail().x;

        auto& editorStyle = ed::GetStyle();
        ImGui::BeginHorizontal("Style buttons", ImVec2(paneWidth, 0), 1.0f);
        ImGui::TextUnformatted("Values");
        ImGui::Spring();
        if (ImGui::Button("Reset to defaults")) {
            editorStyle = ed::Style();
        }
        ImGui::EndHorizontal();
        ImGui::Spacing();
        ImGui::DragFloat4("Node Padding", &editorStyle.NodePadding.x, 0.1f, 0.0f, 40.0f);
        ImGui::DragFloat("Node Rounding", &editorStyle.NodeRounding, 0.1f, 0.0f, 40.0f);
        ImGui::DragFloat("Node Border Width", &editorStyle.NodeBorderWidth, 0.1f, 0.0f, 15.0f);
        ImGui::DragFloat("Hovered Node Border Width", &editorStyle.HoveredNodeBorderWidth, 0.1f, 0.0f, 15.0f);
        ImGui::DragFloat("Selected Node Border Width", &editorStyle.SelectedNodeBorderWidth, 0.1f, 0.0f, 15.0f);
        ImGui::DragFloat("Pin Rounding", &editorStyle.PinRounding, 0.1f, 0.0f, 40.0f);
        ImGui::DragFloat("Pin Border Width", &editorStyle.PinBorderWidth, 0.1f, 0.0f, 15.0f);
        ImGui::DragFloat("Link Strength", &editorStyle.LinkStrength, 1.0f, 0.0f, 500.0f);
        ImGui::DragFloat("Scroll Duration", &editorStyle.ScrollDuration, 0.001f, 0.0f, 2.0f);
        ImGui::DragFloat("Flow Marker Distance", &editorStyle.FlowMarkerDistance, 1.0f, 1.0f, 200.0f);
        ImGui::DragFloat("Flow Speed", &editorStyle.FlowSpeed, 1.0f, 1.0f, 2000.0f);
        ImGui::DragFloat("Flow Duration", &editorStyle.FlowDuration, 0.001f, 0.0f, 5.0f);
        ImGui::DragFloat("Group Rounding", &editorStyle.GroupRounding, 0.1f, 0.0f, 40.0f);
        ImGui::DragFloat("Group Border Width", &editorStyle.GroupBorderWidth, 0.1f, 0.0f, 15.0f);

        ImGui::Separator();

        static ImGuiColorEditFlags edit_mode = ImGuiColorEditFlags_DisplayRGB;
        ImGui::BeginHorizontal("Color Mode", ImVec2(paneWidth, 0), 1.0f);
        ImGui::TextUnformatted("Filter Colors");
        ImGui::Spring();
        ImGui::RadioButton("RGB", &edit_mode, ImGuiColorEditFlags_DisplayRGB);
        ImGui::Spring(0);
        ImGui::RadioButton("HSV", &edit_mode, ImGuiColorEditFlags_DisplayHSV);
        ImGui::Spring(0);
        ImGui::RadioButton("HEX", &edit_mode, ImGuiColorEditFlags_DisplayHex);
        ImGui::EndHorizontal();

        static ImGuiTextFilter filter;
        filter.Draw("", paneWidth);

        ImGui::Spacing();

        ImGui::PushItemWidth(-160);
        for (int i = 0; i < ed::StyleColor_Count; ++i) {
            auto name = ed::GetStyleColorName((ed::StyleColor)i);
            if (!filter.PassFilter(name)) {
                continue;
            }

            ImGui::ColorEdit4(name, &editorStyle.Colors[i].x, edit_mode);
        }
        ImGui::PopItemWidth();

        ImGui::End();
    }

    void ShowLeftPane(float paneWidth) {
        auto& io = ImGui::GetIO();

        ImGui::BeginChild("Selection", ImVec2(paneWidth, 0));

        paneWidth = ImGui::GetContentRegionAvail().x;

        ImGui::BeginHorizontal("Scene name", ImVec2(paneWidth, 0));
        ImGui::InputText("Scene name", sceneName, IM_ARRAYSIZE(sceneName));
        ImGui::EndHorizontal();

        static bool showStyleEditor = false;
        ImGui::BeginHorizontal("Style Editor", ImVec2(paneWidth, 0));
        ImGui::Spring(0.0f, 0.0f);
        if (ImGui::Button("Zoom to Content")) {
            ed::NavigateToContent();
        }
        ImGui::Spring(0.0f);
        if (ImGui::Button("Show Flow")) {
            for (auto& link : m_Links) {
                if (FindPin(link.StartPinID)->Type == EditorArgsTypes::editor_arg_none) {
                    ed::Flow(link.ID);
                }
            }
        }
        ImGui::Spring();
#if not NDEBUG
        if (ImGui::Button("Edit Style")) {
            showStyleEditor = true;
        }
#endif
        ImGui::EndHorizontal();

        ImGui::BeginHorizontal("Json loader", ImVec2(paneWidth, 0));
        ImGui::Spring(0.0f, 0.0f);
        if (ImGui::Button("Save")) {
            ImGui::OpenPopup("Save File");
        }
        ImGui::Spring(0.0f);
        if (ImGui::Button("Load")) {
            ImGui::OpenPopup("Open File");
        }
        if (file_dialog.showFileDialog("Open File", imgui_addons::ImGuiFileBrowser::DialogMode::OPEN, ImVec2(700, 310), ".json"))
        {
            ParseJson(file_dialog.selected_path);
        }
        if (file_dialog.showFileDialog("Save File", imgui_addons::ImGuiFileBrowser::DialogMode::SAVE, ImVec2(700, 310), ".json"))
        {
            SceneToJSon(file_dialog.selected_path);
        }

        if (ImGui::BeginPopupModal("Json file error"))
        {
            ImGui::Text("Could not open json file");
            if (ImGui::Button("Ok", ImVec2(120, 0))) { 
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Json version error"))
        {
            ImGui::Text("Incorrect json version");
            if (ImGui::Button("Ok", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::EndHorizontal();

        if (showStyleEditor) {
            ShowStyleEditor(&showStyleEditor);
        }
        std::vector<ed::NodeId> selectedNodes;
        std::vector<ed::LinkId> selectedLinks;
        selectedNodes.resize(ed::GetSelectedObjectCount());
        selectedLinks.resize(ed::GetSelectedObjectCount());

        int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
        int linkCount = ed::GetSelectedLinks(selectedLinks.data(), static_cast<int>(selectedLinks.size()));

        selectedNodes.resize(nodeCount);
        selectedLinks.resize(linkCount);

        int saveIconWidth     = GetTextureWidth(m_SaveIcon);
        int saveIconHeight    = GetTextureWidth(m_SaveIcon);
        int restoreIconWidth  = GetTextureWidth(m_RestoreIcon);
        int restoreIconHeight = GetTextureWidth(m_RestoreIcon);

        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImGui::GetCursorScreenPos() + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
            ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]), ImGui::GetTextLineHeight() * 0.25f);
        ImGui::Spacing(); ImGui::SameLine();
        ImGui::TextUnformatted("Nodes");
        ImGui::Indent();
        for (auto& node : m_Nodes) {
            ImGui::PushID(node.ID.AsPointer());
            auto start = ImGui::GetCursorScreenPos();

            if (const auto progress = GetTouchProgress(node.ID)) {
                ImGui::GetWindowDrawList()->AddLine(
                    start + ImVec2(-8, 0),
                    start + ImVec2(-8, ImGui::GetTextLineHeight()),
                    IM_COL32(255, 0, 0, 255 - (int)(255 * progress)), 4.0f);
            }

            bool isSelected = std::find(selectedNodes.begin(), selectedNodes.end(), node.ID) != selectedNodes.end();
            if (ImGui::Selectable((node.Name + "##" + std::to_string(reinterpret_cast<uintptr_t>(node.ID.AsPointer()))).c_str(), &isSelected)) {
                if (io.KeyCtrl) {
                    if (isSelected) {
                        ed::SelectNode(node.ID, true);
                    }
                    else {
                        ed::DeselectNode(node.ID);
                    }
                }
                else {
                    ed::SelectNode(node.ID, false);
                }

                ed::NavigateToSelection();
            }

            auto id = std::string("(") + std::to_string(reinterpret_cast<uintptr_t>(node.ID.AsPointer())) + ")";
            auto textSize = ImGui::CalcTextSize(id.c_str(), nullptr);
            auto iconPanelPos = start + ImVec2(
                paneWidth - ImGui::GetStyle().FramePadding.x - ImGui::GetStyle().IndentSpacing - saveIconWidth - restoreIconWidth - ImGui::GetStyle().ItemInnerSpacing.x * 1,
                (ImGui::GetTextLineHeight() - saveIconHeight) / 2);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(iconPanelPos.x - textSize.x - ImGui::GetStyle().ItemInnerSpacing.x, start.y),
                IM_COL32(255, 255, 255, 255), id.c_str(), nullptr);

            auto drawList = ImGui::GetWindowDrawList();
            ImGui::SetCursorScreenPos(iconPanelPos);
            ImGui::SetItemAllowOverlap();

            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::SetItemAllowOverlap();

            ImGui::SameLine(0, 0);
            ImGui::SetItemAllowOverlap();
            ImGui::Dummy(ImVec2(0, (float)restoreIconHeight));

            ImGui::PopID();
        }
        ImGui::Unindent();

        static int changeCount = 0;

        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImGui::GetCursorScreenPos() + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
            ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]), ImGui::GetTextLineHeight() * 0.25f);
        ImGui::Spacing(); ImGui::SameLine();
        ImGui::TextUnformatted("Selection");

        ImGui::BeginHorizontal("Selection Stats", ImVec2(paneWidth, 0));
        ImGui::Text("Changed %d time%s", changeCount, changeCount > 1 ? "s" : "");
        ImGui::Spring();
        if (ImGui::Button("Deselect All")) {
            ed::ClearSelection();
        }
        ImGui::EndHorizontal();
        ImGui::Indent();
        for (int i = 0; i < nodeCount; ++i) ImGui::Text("Node (%p)", selectedNodes[i].AsPointer());
        for (int i = 0; i < linkCount; ++i) ImGui::Text("Link (%p)", selectedLinks[i].AsPointer());
        ImGui::Unindent();

        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z))) {
            for (auto& link : m_Links) {
                ed::Flow(link.ID);
            }
        }

        if (ed::HasSelectionChanged()) {
            ++changeCount;
        }

        ImGui::EndChild();
    }

    void OnFrame(float deltaTime) override {
        UpdateTouch();

        auto& io = ImGui::GetIO();

        ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);

        ed::SetCurrentEditor(m_Editor);

        static ed::NodeId contextNodeId      = 0;
        static ed::LinkId contextLinkId      = 0;
        static ed::PinId  contextPinId       = 0;
        static bool createNewNode  = false;
        static Pin* newNodeLinkPin = nullptr;
        static Pin* newLinkPin     = nullptr;

        static float leftPaneWidth  = 400.0f;
        static float rightPaneWidth = 800.0f;
        Splitter(true, 4.0f, &leftPaneWidth, &rightPaneWidth, 50.0f, 50.0f);

        ShowLeftPane(leftPaneWidth - 4.0f);

        ImGui::SameLine(0.0f, 12.0f);

        ed::Begin("Node editor"); {
            auto cursorTopLeft = ImGui::GetCursorScreenPos();

            util::BlueprintNodeBuilder builder(m_HeaderBackground, GetTextureWidth(m_HeaderBackground), GetTextureHeight(m_HeaderBackground));

            for (auto& node : m_Nodes) {
                builder.Begin(node.ID);

                builder.Header(node.Color);
                        ImGui::Spring(0);
                        ImGui::TextUnformatted(node.Name.c_str());
                        ImGui::Spring(1);
                        ImGui::Dummy(ImVec2(0, 28));
                ImGui::Spring(0);
                builder.EndHeader();


                for (auto& input : node.Inputs) {
                    auto alpha = ImGui::GetStyle().Alpha;
                    if (newLinkPin && !CanCreateLink(newLinkPin, &input) && &input != newLinkPin) {
                        alpha = alpha * (48.0f / 255.0f);
                    }

                    builder.Input(input.ID);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                    DrawPinIcon(input, IsPinLinked(input.ID), (int)(alpha * 255));
                    ImGui::Spring(0);
                    if (!input.Name.empty()) {
                         ImGui::TextUnformatted(input.Name.c_str());
                         ImGui::Spring(0);
                    }

                    static char str1[128] = "";
                    static int i0 = 0;
                    static float f1 = 0.0f;
                    static float vec4f[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

                    static float matrRow0[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    static float matrRow1[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    static float matrRow2[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    static float matrRow3[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    ImGui::AlignTextToFramePadding();
                    if (!IsPinLinked(input.ID)) {
                        float width = 100.0f;
                        ImGui::PushItemWidth(width);
                        switch (input.Type) {
                        case EditorArgsTypes::editor_arg_string:
                            strcpy_s(str1, input.ConstantValue.Get<std::string>().c_str());
                            ImGui::InputText("", str1, IM_ARRAYSIZE(str1));
                            input.ConstantValue.Set(str1);
                            break;
                        case EditorArgsTypes::editor_arg_float:
                            f1 = input.ConstantValue.Get<float>();
                            ImGui::InputFloat("", &f1, 0.0f, 0.0f);
                            input.ConstantValue.Set(f1);
                            break;
                        case EditorArgsTypes::editor_arg_float2:
                            vec4f[0] = input.ConstantValue.Get<mth::vec2<float>>()[0];
                            vec4f[1] = input.ConstantValue.Get<mth::vec2<float>>()[1];
                            ImGui::PushItemWidth(width * 2);
                            ImGui::InputFloat2("", vec4f);
                            input.ConstantValue.Set(mth::vec2<float>{vec4f[0], vec4f[1]});
                            break;
                        case EditorArgsTypes::editor_arg_float3:
                            vec4f[0] = input.ConstantValue.Get<mth::vec3<float>>()[0];
                            vec4f[1] = input.ConstantValue.Get<mth::vec3<float>>()[1];
                            vec4f[2] = input.ConstantValue.Get<mth::vec3<float>>()[2];
                            ImGui::PushItemWidth(width * 3);
                            ImGui::InputFloat3("", vec4f);
                            input.ConstantValue.Set(mth::vec3<float>{vec4f[0], vec4f[1], vec4f[2]});
                            break;
                        case EditorArgsTypes::editor_arg_float4:
                            vec4f[0] = input.ConstantValue.Get<mth::vec4<float>>()[0];
                            vec4f[1] = input.ConstantValue.Get<mth::vec4<float>>()[1];
                            vec4f[2] = input.ConstantValue.Get<mth::vec4<float>>()[2];
                            vec4f[3] = input.ConstantValue.Get<mth::vec4<float>>()[3];
                            ImGui::PushItemWidth(width * 4);
                            ImGui::InputFloat4("", vec4f);
                            input.ConstantValue.Set(mth::vec4<float>{vec4f[0], vec4f[1], vec4f[2], vec4f[3]});
                            break;
                        case EditorArgsTypes::editor_arg_matr:
                            matrRow0[0] = input.ConstantValue.Get<mth::matr4<float>>()[0][0];
                            matrRow0[1] = input.ConstantValue.Get<mth::matr4<float>>()[0][1];
                            matrRow0[2] = input.ConstantValue.Get<mth::matr4<float>>()[0][2];
                            matrRow0[3] = input.ConstantValue.Get<mth::matr4<float>>()[0][3];
                            matrRow1[0] = input.ConstantValue.Get<mth::matr4<float>>()[1][0];
                            matrRow1[1] = input.ConstantValue.Get<mth::matr4<float>>()[1][1];
                            matrRow1[2] = input.ConstantValue.Get<mth::matr4<float>>()[1][2];
                            matrRow1[3] = input.ConstantValue.Get<mth::matr4<float>>()[1][3];
                            matrRow2[0] = input.ConstantValue.Get<mth::matr4<float>>()[2][0];
                            matrRow2[1] = input.ConstantValue.Get<mth::matr4<float>>()[2][1];
                            matrRow2[2] = input.ConstantValue.Get<mth::matr4<float>>()[2][2];
                            matrRow2[3] = input.ConstantValue.Get<mth::matr4<float>>()[2][3];
                            matrRow3[0] = input.ConstantValue.Get<mth::matr4<float>>()[3][0];
                            matrRow3[1] = input.ConstantValue.Get<mth::matr4<float>>()[3][1];
                            matrRow3[2] = input.ConstantValue.Get<mth::matr4<float>>()[3][2];
                            matrRow3[3] = input.ConstantValue.Get<mth::matr4<float>>()[3][3];
                            ImGui::PushItemWidth(width * 4);
                            ImGui::InputFloat4("", matrRow0);
                            ImGui::InputFloat4("", matrRow1);
                            ImGui::InputFloat4("", matrRow2);
                            ImGui::InputFloat4("", matrRow3);
                            input.ConstantValue.Set(mth::matr4<float>{
                               matrRow0[0], matrRow0[1], matrRow0[2], matrRow0[3],
                               matrRow1[0], matrRow1[1], matrRow1[2], matrRow1[3],
                               matrRow2[0], matrRow2[1], matrRow2[2], matrRow2[3],
                               matrRow3[0], matrRow3[1], matrRow3[2], matrRow3[3] });
                            break;
                        case EditorArgsTypes::editor_arg_none:
                            break;
                        case EditorArgsTypes::editor_arg_gdr_index:
                        default:
                            i0 = input.ConstantValue.Get<gdr_index>();
                            ImGui::InputInt("", &i0);
                            if (i0 < 0) {
                                i0 = 0;
                            }
                            input.ConstantValue.Set((unsigned int)i0);
                            break;
                        }
                        ImGui::PopItemWidth();
                    }

                    ImGui::PopStyleVar();
                    builder.EndInput();
                }

                for (auto& output : node.Outputs) {
                    auto alpha = ImGui::GetStyle().Alpha;
                    if (newLinkPin && !CanCreateLink(newLinkPin, &output) && &output != newLinkPin) {
                        alpha = alpha * (48.0f / 255.0f);
                    }

                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                    builder.Output(output.ID);
                    if (!output.Name.empty()) {
                        ImGui::Spring(0);
                        ImGui::TextUnformatted(output.Name.c_str());
                    }
                    ImGui::Spring(0);
                    DrawPinIcon(output, IsPinLinked(output.ID), (int)(alpha * 255));
                    ImGui::PopStyleVar();
                    builder.EndOutput();
                }

                builder.End();
            }

            for (auto& link : m_Links) {
                ed::Link(link.ID, link.StartPinID, link.EndPinID, link.Color, 2.0f);
            }

            if (!createNewNode) {
                if (ed::BeginCreate(ImColor(255, 255, 255), 2.0f)) {
                    auto showLabel = [](const char* label, ImColor color) {
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
                        auto size = ImGui::CalcTextSize(label);

                        auto padding = ImGui::GetStyle().FramePadding;
                        auto spacing = ImGui::GetStyle().ItemSpacing;

                        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(spacing.x, -spacing.y));

                        auto rectMin = ImGui::GetCursorScreenPos() - padding;
                        auto rectMax = ImGui::GetCursorScreenPos() + size + padding;

                        auto drawList = ImGui::GetWindowDrawList();
                        drawList->AddRectFilled(rectMin, rectMax, color, size.y * 0.15f);
                        ImGui::TextUnformatted(label);
                    };

                    ed::PinId startPinId = 0, endPinId = 0;
                    if (ed::QueryNewLink(&startPinId, &endPinId)) {
                        auto startPin = FindPin(startPinId);
                        auto endPin   = FindPin(endPinId);

                        newLinkPin = startPin ? startPin : endPin;

                        if (startPin->Kind == PinKind::Input) {
                            std::swap(startPin, endPin);
                            std::swap(startPinId, endPinId);
                        }

                        if (startPin && endPin) {
                            if (endPin == startPin) {
                                ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                            }
                            else if (endPin->Kind == startPin->Kind) {
                                showLabel("x Incompatible Pin Kind", ImColor(45, 32, 32, 180));
                                ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                            }
                            else if (endPin->Node == startPin->Node)
                            {
                                showLabel("x Cannot connect to self", ImColor(45, 32, 32, 180));
                                ed::RejectNewItem(ImColor(255, 0, 0), 1.0f);
                            }
                            else if (endPin->Type != startPin->Type) {
                                showLabel("x Incompatible Pin Type", ImColor(45, 32, 32, 180));
                                ed::RejectNewItem(ImColor(255, 128, 128), 1.0f);
                            }
                            else {
                                showLabel("+ Create Link", ImColor(32, 45, 32, 180));
                                if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {
                                    m_Links.emplace_back(Link(GetNextId(), startPinId, endPinId));
                                    m_Links.back().Color = PinColorChoser[startPin->Type];
                                }
                            }
                        }
                    }

                    ed::PinId pinId = 0;
                    if (ed::QueryNewNode(&pinId)) {
                        newLinkPin = FindPin(pinId);
                        if (newLinkPin->Type == EditorArgsTypes::editor_arg_none) {
                            if (newLinkPin) {
                                showLabel("+ Create Node", ImColor(32, 45, 32, 180));
                            }

                            if (ed::AcceptNewItem()) {
                                createNewNode = true;
                                newNodeLinkPin = FindPin(pinId);
                                newLinkPin = nullptr;
                                ed::Suspend();
                                ImGui::OpenPopup("Create New Node");
                                ed::Resume();
 
                            }
                        }
                    }
                }
                else {
                    newLinkPin = nullptr;
                }

                ed::EndCreate();

                if (ed::BeginDelete()) {

                    ed::NodeId nodeId = 0;
                    while (ed::QueryDeletedNode(&nodeId)) {
                        if (ed::AcceptDeletedItem()) {
                            auto id = std::find_if(m_Nodes.begin(), m_Nodes.end(), [nodeId](auto& node) { return node.ID == nodeId; });
                            if ((*id).EngineNode.EngineType == EditorNodeTypes::editor_node_event) {
                                continue;
                            }
                            if (id != m_Nodes.end()) {
                                for (auto& f : (*id).Inputs) {
                                    for (auto& link : m_Links) {
                                        if (link.EndPinID == f.ID || link.StartPinID == f.ID) {
                                            ed::DeleteLink(link.ID);
                                        }
                                    }
                                }
                                for (auto& f : (*id).Outputs) {
                                    for (auto& link : m_Links) {
                                        if (link.EndPinID == f.ID || link.StartPinID == f.ID) {
                                            ed::DeleteLink(link.ID);
                                        }
                                    }
                                }
                                m_Nodes.erase(id);
                            }
                        }
                    }

                    ed::LinkId linkId = 0;
                    while (ed::QueryDeletedLink(&linkId)) {
                        if (ed::AcceptDeletedItem()) {
                            auto id = std::find_if(m_Links.begin(), m_Links.end(), [linkId](auto& link) { return link.ID == linkId; });
                            if (id != m_Links.end()) {
                                m_Links.erase(id);
                            }
                        }
                    }
                }
                ed::EndDelete();
            }

            ImGui::SetCursorScreenPos(cursorTopLeft);
        }

        auto openPopupPosition = ImGui::GetMousePos();
        ed::Suspend();
        if (ed::ShowNodeContextMenu(&contextNodeId)) {
            ImGui::OpenPopup("Node Context Menu");
        }
        else if (ed::ShowPinContextMenu(&contextPinId)) {
            ImGui::OpenPopup("Pin Context Menu");
        }
        else if (ed::ShowLinkContextMenu(&contextLinkId)) {
            ImGui::OpenPopup("Link Context Menu");
        }
        else if (ed::ShowBackgroundContextMenu()) {
            ImGui::OpenPopup("Create New Node");
            newNodeLinkPin = nullptr;
        }
        ed::Resume();

        ed::Suspend();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        if (ImGui::BeginPopup("Node Context Menu")) {
            auto node = FindNode(contextNodeId);

            ImGui::TextUnformatted("Node Context Menu");
            ImGui::Separator();
            if (node) {
                ImGui::Text("ID: %p", node->ID.AsPointer());
                ImGui::Text("Type: %s", "Blueprint");
                ImGui::Text("Inputs: %d", (int)node->Inputs.size());
                ImGui::Text("Outputs: %d", (int)node->Outputs.size());
            }
            else {
                ImGui::Text("Unknown node: %p", contextNodeId.AsPointer());
            }
            ImGui::Separator();
            if (node->EngineNode.EngineType != EditorNodeTypes::editor_node_event) {
                if (ImGui::MenuItem("Delete")) {
                    ed::DeleteNode(contextNodeId);
                }
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("Pin Context Menu")) {
            auto pin = FindPin(contextPinId);

            ImGui::TextUnformatted("Pin Context Menu");
            ImGui::Separator();
            if (pin) {
                ImGui::Text("ID: %p", pin->ID.AsPointer());
                if (pin->Node) {
                    ImGui::Text("Node: %p", pin->Node->ID.AsPointer());
                }
                else {
                    ImGui::Text("Node: %s", "<none>");
                }
            }
            else {
                ImGui::Text("Unknown pin: %p", contextPinId.AsPointer());
            }

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("Link Context Menu"))
        {
            auto link = FindLink(contextLinkId);

            ImGui::TextUnformatted("Link Context Menu");
            ImGui::Separator();
            if (link) {
                ImGui::Text("ID: %p", link->ID.AsPointer());
                ImGui::Text("From: %p", link->StartPinID.AsPointer());
                ImGui::Text("To: %p", link->EndPinID.AsPointer());
            }
            else {
                ImGui::Text("Unknown link: %p", contextLinkId.AsPointer());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete")) {
                ed::DeleteLink(contextLinkId);
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("Create New Node")) {
            auto newNodePostion = openPopupPosition;

            Node* node = nullptr;
            for (auto& v : BlueprintLibrary) {
                if (v.first == "Event") {
                    continue;
                }
                if (ImGui::BeginMenu(v.first.c_str())) {
                    std::string key = v.first;
                    for (auto& f : BlueprintLibrary[key]) {
                        if (ImGui::MenuItem((f.Name).c_str())) {
                            int nodeId = GetNextId();
                            SpawnNodeFromLibrary(f, nodeId);
                            if (newNodeLinkPin != nullptr) {
                                m_Links.emplace_back(Link(GetNextId(), newNodeLinkPin->ID, m_Nodes.back().Inputs[0].ID));
                                m_Links.back().Color = PinColorChoser[newNodeLinkPin->Type];
                            }
                            //BuildNode(&m_Nodes.back());
                            ed::SetNodePosition(nodeId, newNodePostion);
                            BuildNodes();
                        }
                    }
                    ImGui::EndMenu();
                }
            }

            if (node) {
                BuildNodes();

                createNewNode = false;

                ed::SetNodePosition(node->ID, newNodePostion);

                if (auto startPin = newNodeLinkPin) {
                    auto& pins = startPin->Kind == PinKind::Input ? node->Outputs : node->Inputs;

                    for (auto& pin : pins) {
                        if (CanCreateLink(startPin, &pin)) {
                            auto endPin = &pin;
                            if (startPin->Kind == PinKind::Input) {
                                std::swap(startPin, endPin);
                            }

                            m_Links.emplace_back(Link(GetNextId(), startPin->ID, endPin->ID));
                            m_Links.back().Color = PinColorChoser[startPin->Type];

                            break;
                        }
                    }
                }
            }

            ImGui::EndPopup();
        }
        else {
            createNewNode = false;
        }
        ImGui::PopStyleVar();
        ed::Resume();

        ed::End();
    }

    // Function to create new node with correct input/outputs based on node from editor_modules_list.h
    // Used in json loading and basic editor node creation
    void SpawnNodeFromLibrary(BlueprintLibraryNode node, unsigned int id) {
        m_Nodes.emplace_back(id, node, GetNodeColor(node.EngineType));
        if (node.EngineType == EditorNodeTypes::editor_node_event) {
            m_Nodes.back().Outputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
        }
        else if (node.EngineType == EditorNodeTypes::editor_node_function) {
            m_Nodes.back().Inputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
            m_Nodes.back().Outputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
        }
        else if (node.EngineType == EditorNodeTypes::editor_node_workflow) {
            m_Nodes.back().Inputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
            m_Nodes.back().Outputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
            m_Nodes.back().Outputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
        }
        for (int i = 0; i < node.InputArgsNames.size(); i++) {
            m_Nodes.back().Inputs.emplace_back(GetNextId(), node.InputArgsNames[i].c_str(), node.InputArgsTypes[i]);
        }
        for (int i = 0; i < node.OutputArgsNames.size(); i++) {
            m_Nodes.back().Outputs.emplace_back(GetNextId(), node.OutputArgsNames[i].c_str(), node.OutputArgsTypes[i]);
        }
    }

    // Function to save current file to json file
    void SceneToJSon(std::string fileName) {
        std::ofstream file(fileName);
        if (!file.is_open()) {
            ImGui::OpenPopup("Json file error");
            return;
        }

        for (auto& node : m_Nodes) {
            node.CommandIndex = 0;
            node.IsVisited = false;
        }

        json data;

        data["version"] = MAX_SUPPORTED_VERSION;
        data["name"] = sceneName;

        // Fill commands array here
        json commands = json(m_Nodes.size(), nullptr);
        // Push all event nodes in stack
        std::vector<Node*> stack;

        int commandIndex = 0;
        for (int i = 0; i < m_Nodes.size() && m_Nodes[i].EngineNode.EngineType == EditorNodeTypes::editor_node_event; i++) {
            m_Nodes[i].CommandIndex = commandIndex++;
            m_Nodes[i].IsVisited = true;
            stack.push_back(&m_Nodes[i]);
        }
        std::reverse(stack.begin(), stack.end());
        // While have nodes in stack
        while (!stack.empty()) {
            json command;
            Node* node = stack.back();
            stack.pop_back();

            command["library_func_index"] = node->EngineNode.LibraryIndex;
            ImVec2 pos = ed::GetNodePosition(node->ID);
            command["pos"] = { pos.x, pos.y };
            command["output_argument"] = std::vector<int>();
            for (auto& output : node->Outputs) {
                if (output.Type != EditorArgsTypes::editor_arg_none) {
                    command["output_argument"].emplace_back(output.ID.Get());
                    continue;
                }
                if (!IsPinLinked(output.ID)) {
                    continue;
                }
                Node* newNode = FindEndNode(output.ID);
                if (!newNode->IsVisited) {
                    command["next_nodes"].emplace_back(commandIndex);
                    newNode->CommandIndex = commandIndex++;
                    newNode->IsVisited = true;
                    stack.push_back(newNode);
                }
                else {
                    command["next_nodes"].emplace_back(newNode->CommandIndex);
                }
            }
            if (command["next_nodes"].empty()) {
                command["next_nodes"] = { ZERO_CODE };
            }
            commands[node->CommandIndex] = command;
        }
        for (auto& node : m_Nodes) {
            json inputs = std::vector<int>();
            for (auto& input : node.Inputs) {
                if (input.Type == EditorArgsTypes::editor_arg_none) {
                    continue;
                }
                json j_input;
                j_input["type"] = input.Type;
                j_input["value"] = "";
                j_input["slots"] = std::vector<int>();
                if (!IsPinLinked(input.ID)) {
                    j_input["value"] = input.ConstantValue.Get<std::string>();
                } 
                else {
                    for (auto& link : m_Links) {
                        if (link.StartPinID == input.ID) {
                            j_input["slots"].emplace_back(link.EndPinID.Get());
                        }
                    }
                }
                inputs.emplace_back(j_input);
            }
            commands[node.CommandIndex]["input_argument"] = inputs;
        }

        data["commands"] = commands;

        // dump json to file
        file << std::setw(4) << data << std::endl;
    }

    // Function to parse json file into nodes
    void ParseJson(std::string fileName) {
        std::ifstream file(fileName);
        if (!file.is_open()) {
            ImGui::OpenPopup("Json file error");
            return;
        }

        json data = json::parse(file);

        if (data["version"] != MAX_SUPPORTED_VERSION) {
            ImGui::OpenPopup("Json version error");
            return;
        }

        m_Nodes.clear();
        m_Links.clear();

        std::string str = data["name"];
        strcpy_s(sceneName, str.c_str());

        json commands = data["commands"];
        // Spawn nodes
        std::vector<Node> commandNodeMap;
        std::map<int, ed::PinId> outputPinsMap;
        for (int i = 0; i < commands.size(); i++) {
            auto node = SpawnNodeById(commands[i]["library_func_index"]);
            int nodeId = GetNextId();
            SpawnNodeFromLibrary(*node, nodeId);
            commandNodeMap.push_back(m_Nodes.back());
            int outputIndex = 0;
            for (auto& output : m_Nodes.back().Outputs) {
                if (output.Type == EditorArgsTypes::editor_arg_none) {
                    continue;
                }
                outputPinsMap.insert({ commands[i]["output_argument"][outputIndex++], output.ID });
            }
            auto pos = commands[i]["pos"];
            ed::SetNodePosition(nodeId, ImVec2(pos[0], pos[1]));
        }
        // Connect lines
        for (int i = 0; i < commands.size(); i++) {
            json nextNodes = commands[i]["next_nodes"];
            if (nextNodes[0] != ZERO_CODE) {
                int pinIndex = 0;
                for (auto& nodeId : nextNodes) {
                    m_Links.emplace_back(Link(GetNextId(), commandNodeMap[i].Outputs[pinIndex].ID, commandNodeMap[nodeId].Inputs[0].ID));
                    m_Links.back().Color = PinColorChoser[commandNodeMap[i].Outputs[pinIndex++].Type];
                }
            }

            json inputArgs = commands[i]["input_argument"];
            int inputIndex = 0;
            for (auto& input : commandNodeMap[i].Inputs) {
                if (input.Type == EditorArgsTypes::editor_arg_none) {
                    continue;
                }
                json arg = inputArgs[inputIndex++];
                if (arg["slots"].empty()) {
                    // I hate C++ for this, maybe fix later...
                    Pin* pin = FindPin(input.ID);
                    std::string str = arg["value"];
                    pin->ConstantValue.Set(str);
                }
                else {
                    for (auto& slot : arg["slots"]) {
                        Pin* output = FindPin(outputPinsMap[slot]);
                        m_Links.emplace_back(Link(GetNextId(), input.ID, output->ID));
                        m_Links.back().Color = PinColorChoser[input.Type];
                    }
                }
            }

        }
        BuildNodes();
    }

    // Function to spawn Node by index in editor_modules_list.h
    // Used in json loading
    BlueprintLibraryNode* SpawnNodeById(int id) {
        for (auto& f : InsertOrder) {
            if ((id -(int)BlueprintLibrary[f].size()) >= 0) {
                id -= (int)BlueprintLibrary[f].size();
            }
            else {
                return &BlueprintLibrary[f][id];
            }

        }
        return nullptr;
    }

    int                  m_NextId = 1;                        // Variable to keep track of ids
    const int            m_PinIconSize = 24;                  // Size of pin's size
    std::vector<Node>    m_Nodes;                             // Vector of all Nodes
    std::vector<Link>    m_Links;                             // Vector of all Links
    ImTextureID          m_HeaderBackground = nullptr;        // XZ
    ImTextureID          m_SaveIcon = nullptr;                // XZ
    ImTextureID          m_RestoreIcon = nullptr;             // XZ
    const float          m_TouchTime = 1.0f;                  // XZ
    std::map<ed::NodeId, float, NodeIdLess> m_NodeTouchTime;  // XZ
    char sceneName[128] = "scene";                            // Name of the scene
};

int Main(int argc, char** argv)
{
    Editor editor("Editor", argc, argv);

    // init library
    {
#define GDR_BLUEPRINT_NODE(type, filter, name, number_of_input_args, number_of_output_args, input_args_types, output_args_types, input_args_names, output_args_names) \
        { \
          BlueprintLibraryNode newNode;\
          newNode.EngineType = type;\
          newNode.LibraryIndex = nodeCount++;\
          newNode.Filter = filter;\
          newNode.Name = name;\
          newNode.InputArgsNames = input_args_names;\
          newNode.OutputArgsNames = output_args_names;\
          EditorArgsTypes input_args[std::max(number_of_input_args, 1)] = input_args_types;\
          EditorArgsTypes output_args[std::max(number_of_output_args, 1)] = output_args_types;\
          for (int i = 0; i < number_of_input_args; i++)\
              newNode.InputArgsTypes.push_back(input_args[i]); \
          for (int i = 0; i < number_of_output_args; i++)\
              newNode.OutputArgsTypes.push_back(output_args[i]); \
          BlueprintLibrary[newNode.Filter].push_back(newNode); \
          InsertOrder.push_back(newNode.Filter);\
        };

        static int nodeCount = 0;
        GDR_BLUEPRINT_LIST

        std::vector<std::string>::iterator last;
        std::vector<std::string>::iterator it;
        std::vector<std::string> tmp;
        last = std::unique(InsertOrder.begin(), InsertOrder.end());
        for (it = InsertOrder.begin(); it != last; ++it) {
            tmp.push_back(*it);
        }
        InsertOrder = tmp;

#undef GDR_BLUEPRINT_NODE
    }

    // init maps for color choosing
    srand(1);
    PinColorChoser[EditorArgsTypes(0)] = ImColor(255, 255, 255);
    for (int i = 1; i < int(EditorArgsTypes::editor_arg_count); i++) {
        PinColorChoser[EditorArgsTypes(i)] = ImColor(128 + rand() % 128, 128 + rand() % 128, 128 + rand() % 128);
    }

    if (editor.Create()) {
        return editor.Run();
    }

    return 0;
}
