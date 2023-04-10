#pragma once

#include "pin.h"

using namespace ax;
namespace ed = ax::NodeEditor;

// Node with data from editor_modules_list.h
struct BlueprintLibraryNode {
    EditorNodeTypes EngineType;                   // Type of Node from editor_modules_list.h (Event, workflow, Function and etc.)
    std::string Filter;                           // String with Node type from editor_modules_list.h (Event, Animation, Time, Model and etc.)
    std::string Name;                             // Name of Node (Load, Delete, Set, Get and etc.)
    unsigned int LibraryIndex;                    // Index number of Node from editor_modules_list.h
    std::vector<EditorArgsTypes> InputArgsTypes;  // Vector with types of input arguments of node
    std::vector<EditorArgsTypes> OutputArgsTypes; // Vector with types of output arguments of node
    std::vector<std::string> InputArgsNames;      // Vector with names of input arguments
    std::vector<std::string> OutputArgsNames;     // Vector with names of output argumets
};

// Node struct that represents one node in editor
struct Node {
    ed::NodeId ID;                     // Node id
    bool IsVisited = false;            // Flag for traversal of nodes when creating output json
    BlueprintLibraryNode EngineNode;   // Specific node from editor_modules_list.h
    std::string Name;                  // Additional name for Node
    std::vector<Pin> Inputs;           // Vector of input Pins
    std::vector<Pin> Outputs;          // Vector of output Pins
    ImColor Color;                     // Color of Node
    ImVec2 Size;                       // Size of Node
    unsigned int CommandIndex;         // Index number of Node in command list when creating output json

    Node(int id, BlueprintLibraryNode engineNode, ImColor color = ImColor(255, 255, 255)) :
        ID(id), EngineNode(engineNode), Color(color), Size(0, 0), Name(engineNode.Filter + ": " + engineNode.Name)
    {
    }
};

// Dunno? Like node without id
// Legacy code lol
struct NodeIdLess {
    bool operator()(const ed::NodeId& lhs, const ed::NodeId& rhs) const {
        return lhs.AsPointer() < rhs.AsPointer();
    }
};
