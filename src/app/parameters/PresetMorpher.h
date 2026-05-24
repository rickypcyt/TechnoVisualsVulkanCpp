#pragma once

#include "ParameterRegistry.h"

class PresetMorpher {
public:
    static void morph(ParameterRegistry& current, ParameterRegistry& a, ParameterRegistry& b, float t);
};
