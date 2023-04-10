#pragma once

#include "../math/my_any.h"
#include "../utilities/builders.h"

using namespace ax;

struct Node;

// Enum class for pin type
enum class PinKind {
    Output,
    Input
};

// Pin in Node is that little thing where/from all links go through
struct Pin {
    ax::NodeEditor::PinId  ID;    // Pin id
    my_any ConstantValue;         // Value that input pin stores if there is no link to it
    ::Node* Node;                 // Pointer to Node that have this pin
    std::string Name;             // Name of pin (float, string, key, flag, etc.)
    EditorArgsTypes Type;         // Pin input/output argument type from editor_modules_list.h
    PinKind Kind;                 // Pin type from (Output/Input)

    Pin(int id, const char* name, EditorArgsTypes type) :
        ID(id), Node(nullptr), Name(name), Type(type), Kind(PinKind::Input)
    {
    }
};
