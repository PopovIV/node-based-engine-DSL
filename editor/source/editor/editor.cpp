#include <application.h>
#include "editor_modules_list.h"
#include "utilities/builders.h"
#include "utilities/widgets.h"

#include <imgui_node_editor.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <utility>
#include <iostream>

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

enum class PinType {
    Flow,
    Bool,
    Int,
    Float,
    String,
    Object,
    Function,
    Delegate,
};

enum class PinKind {
    Output,
    Input
};

struct Node;

struct Pin {
    ed::PinId   ID;
    ::Node*     Node;
    std::string Name;
    EditorArgsTypes     Type;
    PinKind     Kind;

    Pin(int id, const char* name, EditorArgsTypes type):
        ID(id), Node(nullptr), Name(name), Type(type), Kind(PinKind::Input)
    {
    }
};

struct BlueprintLibraryNode {
    EditorNodeTypes EngineType;
    std::string Filter;
    std::string Name;
    std::vector<EditorArgsTypes> InputArgsTypes;
    std::vector<EditorArgsTypes> OutputArgsTypes;
    std::vector<std::string> InputArgsNames;
    std::vector<std::string> OutputArgsNames;
};

static std::map<std::string, std::vector<BlueprintLibraryNode>> BlueprintLibrary;
static std::map<EditorArgsTypes, ImColor> PinColorChoser;

struct Node {
    ed::NodeId ID;
    BlueprintLibraryNode EngineNode;
    std::string Name;
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    ImColor Color;
    ImVec2 Size;

    std::string State;
    std::string SavedState;

    Node(int id, BlueprintLibraryNode engineNode, ImColor color = ImColor(255, 255, 255)):
        ID(id), EngineNode(engineNode), Color(color), Size(0, 0), Name(engineNode.Filter + ": " + engineNode.Name)
    {
    }
};

struct Link {
    ed::LinkId ID;

    ed::PinId StartPinID;
    ed::PinId EndPinID;

    ImColor Color;

    Link(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId):
        ID(id), StartPinID(startPinId), EndPinID(endPinId), Color(255, 255, 255)
    {
    }
};

struct NodeIdLess {
    bool operator()(const ed::NodeId& lhs, const ed::NodeId& rhs) const {
        return lhs.AsPointer() < rhs.AsPointer();
    }
};

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

struct Editor: public Application {
    using Application::Application;

    int GetNextId() {
        return m_NextId++;
    }

    ed::LinkId GetNextLinkId() {
        return ed::LinkId(GetNextId());
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
        config.LoadNodeSettings = [](ed::NodeId nodeId, char* data, void* userPointer) -> size_t {
            auto self = static_cast<Editor*>(userPointer);
            auto node = self->FindNode(nodeId);
            if (!node) {
                return 0;
            }

            if (data != nullptr) {
                memcpy(data, node->State.data(), node->State.size());
            }
            return node->State.size();
        };

        config.SaveNodeSettings = [](ed::NodeId nodeId, const char* data, size_t size, ed::SaveReasonFlags reason, void* userPointer) -> bool {
            auto self = static_cast<Editor*>(userPointer);
            auto node = self->FindNode(nodeId);
            if (!node) {
                return false;
            }

            node->State.assign(data, size);
            self->TouchNode(nodeId);

            return true;
        };

        m_Editor = ed::CreateEditor(&config);
        ed::SetCurrentEditor(m_Editor);

        // Create 4 event nodes 
        float x = 0.0f;
        float y = -300.0f;
        for (auto& f : BlueprintLibrary["Event"]) {
            int nodeId = GetNextId();
            m_Nodes.emplace_back(nodeId, f, GetNodeColor(f.EngineType));
            m_Nodes.back().Outputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
            BuildNode(&m_Nodes.back());
            auto pos = ImVec2(x, y);
            ed::SetNodePosition(nodeId, pos);
            y += 100.0f;
        }

        BlueprintLibrary.erase("Event");

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

    ImColor GetNodeColor(EditorNodeTypes type) {
        switch (type) {
            default:                                      return ImColor(255, 255, 255);
            case EditorNodeTypes::editor_node_event:      return ImColor(220, 48, 48);
            case EditorNodeTypes::editor_node_function:   return ImColor(68, 201, 156);
            case EditorNodeTypes::editor_node_none:       return ImColor(147, 226, 74);
            case EditorNodeTypes::editor_node_workflow:   return ImColor(124, 21, 153);
        }
    };

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
        if (ImGui::Button("Edit Style")) {
            showStyleEditor = true;
        }
        ImGui::EndHorizontal();
        ImGui::Checkbox("Show Ordinals", &m_ShowOrdinals);

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
            if (ImGui::IsItemHovered() && !node.State.empty()) {
                ImGui::SetTooltip("State: %s", node.State.c_str());
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
            if (node.SavedState.empty()) {
                if (ImGui::InvisibleButton("save", ImVec2((float)saveIconWidth, (float)saveIconHeight))) {
                    node.SavedState = node.State;
                }

                if (ImGui::IsItemActive()) {
                    drawList->AddImage(m_SaveIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 96));
                }
                else if (ImGui::IsItemHovered()) {
                    drawList->AddImage(m_SaveIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
                }
                else {
                    drawList->AddImage(m_SaveIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 160));
                }
            }
            else {
                ImGui::Dummy(ImVec2((float)saveIconWidth, (float)saveIconHeight));
                drawList->AddImage(m_SaveIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 32));
            }

            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::SetItemAllowOverlap();
            if (!node.SavedState.empty()) {
                if (ImGui::InvisibleButton("restore", ImVec2((float)restoreIconWidth, (float)restoreIconHeight))) {
                    node.State = node.SavedState;
                    ed::RestoreNodeState(node.ID);
                    node.SavedState.clear();
                }

                if (ImGui::IsItemActive()) {
                    drawList->AddImage(m_RestoreIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 96));
                }
                else if (ImGui::IsItemHovered()) {
                    drawList->AddImage(m_RestoreIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
                }
                else {
                    drawList->AddImage(m_RestoreIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 160));
                }
            }
            else {
                ImGui::Dummy(ImVec2((float)restoreIconWidth, (float)restoreIconHeight));
                drawList->AddImage(m_RestoreIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 32));
            }

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

    # if 0
        {
            for (auto x = -io.DisplaySize.y; x < io.DisplaySize.x; x += 10.0f)
            {
                ImGui::GetWindowDrawList()->AddLine(ImVec2(x, 0), ImVec2(x + io.DisplaySize.y, io.DisplaySize.y),
                    IM_COL32(255, 255, 0, 255));
            }
        }
    # endif

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
                    //
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
                            ImGui::InputText("", str1, IM_ARRAYSIZE(str1));
                            break;
                        case EditorArgsTypes::editor_arg_float:
                            ImGui::InputFloat("", &f1, 0.0f, 0.0f, "%.3e");
                            break;
                        case EditorArgsTypes::editor_arg_float2:
                            ImGui::PushItemWidth(width * 2);
                            ImGui::InputFloat2("", vec4f);
                            break;
                        case EditorArgsTypes::editor_arg_float3:
                            ImGui::PushItemWidth(width * 3);
                            ImGui::InputFloat3("", vec4f);
                            break;
                        case EditorArgsTypes::editor_arg_float4:
                            ImGui::PushItemWidth(width * 4);
                            ImGui::InputFloat4("", vec4f);
                            break;
                        case EditorArgsTypes::editor_arg_matr:
                            ImGui::PushItemWidth(width * 4);
                            ImGui::InputFloat4("", matrRow0);
                            ImGui::InputFloat4("", matrRow1);
                            ImGui::InputFloat4("", matrRow2);
                            ImGui::InputFloat4("", matrRow3);
                            break;
                        case EditorArgsTypes::editor_arg_none:
                            break;
                        case EditorArgsTypes::editor_arg_gdr_index:
                        default:
                            ImGui::InputInt("", &i0);
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
                    ed::LinkId linkId = 0;
                    while (ed::QueryDeletedLink(&linkId)) {
                        if (ed::AcceptDeletedItem()) {
                            auto id = std::find_if(m_Links.begin(), m_Links.end(), [linkId](auto& link) { return link.ID == linkId; });
                            if (id != m_Links.end()) {
                                m_Links.erase(id);
                            }
                        }
                    }

                    ed::NodeId nodeId = 0;
                    while (ed::QueryDeletedNode(&nodeId)) {
                        if (ed::AcceptDeletedItem()) {
                            auto id = std::find_if(m_Nodes.begin(), m_Nodes.end(), [nodeId](auto& node) { return node.ID == nodeId; });
                            if (id != m_Nodes.end()) {
                                m_Nodes.erase(id);
                            }
                        }
                    }
                }
                ed::EndDelete();
            }

            ImGui::SetCursorScreenPos(cursorTopLeft);
        }

    # if 1
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
                if (ImGui::BeginMenu(v.first.c_str())) {
                    std::string key = v.first;
                    for (auto& f : BlueprintLibrary[key]) {
                        if (ImGui::MenuItem((f.Name).c_str())) {
                            int nodeId = GetNextId();
                            m_Nodes.emplace_back(nodeId, f, GetNodeColor(f.EngineType));
                            if (f.EngineType == EditorNodeTypes::editor_node_function) {
                                m_Nodes.back().Inputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
                                m_Nodes.back().Outputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
                            }
                            else if (f.EngineType == EditorNodeTypes::editor_node_workflow) {
                                m_Nodes.back().Inputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
                                m_Nodes.back().Outputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
                                m_Nodes.back().Outputs.emplace_back(GetNextId(), "", EditorArgsTypes::editor_arg_none);
                            }
                            for (int i = 0; i < f.InputArgsNames.size(); i++) {
                                m_Nodes.back().Inputs.emplace_back(GetNextId(), f.InputArgsNames[i].c_str(), f.InputArgsTypes[i]);
                            }
                            for (int i = 0; i < f.OutputArgsNames.size(); i++) {
                                m_Nodes.back().Outputs.emplace_back(GetNextId(), f.OutputArgsNames[i].c_str(), f.OutputArgsTypes[i]);
                            }

                            if (newNodeLinkPin != nullptr) {
                                m_Links.emplace_back(Link(GetNextId(), newNodeLinkPin->ID, m_Nodes.back().Inputs[0].ID));
                                m_Links.back().Color = PinColorChoser[newNodeLinkPin->Type];
                            }

                            BuildNode(&m_Nodes.back());
                            ed::SetNodePosition(nodeId, openPopupPosition);
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
    # endif

        ed::End();

        auto editorMin = ImGui::GetItemRectMin();
        auto editorMax = ImGui::GetItemRectMax();

        if (m_ShowOrdinals) {
            int nodeCount = ed::GetNodeCount();
            std::vector<ed::NodeId> orderedNodeIds;
            orderedNodeIds.resize(static_cast<size_t>(nodeCount));
            ed::GetOrderedNodeIds(orderedNodeIds.data(), nodeCount);


            auto drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(editorMin, editorMax);

            int ordinal = 0;
            for (auto& nodeId : orderedNodeIds) {
                auto p0 = ed::GetNodePosition(nodeId);
                auto p1 = p0 + ed::GetNodeSize(nodeId);
                p0 = ed::CanvasToScreen(p0);
                p1 = ed::CanvasToScreen(p1);


                ImGuiTextBuffer builder;
                builder.appendf("#%d", ordinal++);

                auto textSize   = ImGui::CalcTextSize(builder.c_str());
                auto padding    = ImVec2(2.0f, 2.0f);
                auto widgetSize = textSize + padding * 2;

                auto widgetPosition = ImVec2(p1.x, p0.y) + ImVec2(0.0f, -widgetSize.y);

                drawList->AddRectFilled(widgetPosition, widgetPosition + widgetSize, IM_COL32(100, 80, 80, 190), 3.0f, ImDrawFlags_RoundCornersAll);
                drawList->AddRect(widgetPosition, widgetPosition + widgetSize, IM_COL32(200, 160, 160, 190), 3.0f, ImDrawFlags_RoundCornersAll);
                drawList->AddText(widgetPosition + padding, IM_COL32(255, 255, 255, 255), builder.c_str());
            }

            drawList->PopClipRect();
        }
    }

    int                  m_NextId = 1;
    const int            m_PinIconSize = 24;
    std::vector<Node>    m_Nodes;
    std::vector<Link>    m_Links;
    ImTextureID          m_HeaderBackground = nullptr;
    ImTextureID          m_SaveIcon = nullptr;
    ImTextureID          m_RestoreIcon = nullptr;
    const float          m_TouchTime = 1.0f;
    std::map<ed::NodeId, float, NodeIdLess> m_NodeTouchTime;
    bool                 m_ShowOrdinals = false;
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
        };

        GDR_BLUEPRINT_LIST

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