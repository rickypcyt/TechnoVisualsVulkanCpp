#pragma once

#include "ParameterRegistry.h"

struct VisualControls;

class VisualControlsRegistry {
public:
    static void build(ParameterRegistry& registry, VisualControls& c);
};
