#pragma once

#include "../utilities/builders.h"

using namespace ax;
namespace ed = ax::NodeEditor;

// Link is abstraction that stores two connected Pins => two connected Nodes
struct Link {
    ed::LinkId ID;         // Link id
    ed::PinId StartPinID;  // Id of first(output) Pin
    ed::PinId EndPinID;    // Id of secont(input) Pin
    ImColor Color;         // Color of Link

    Link(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId) :
        ID(id), StartPinID(startPinId), EndPinID(endPinId), Color(255, 255, 255)
    {
    }
};
