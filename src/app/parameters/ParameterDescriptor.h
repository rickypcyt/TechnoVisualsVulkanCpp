#pragma once

#include <string>
#include <glm/glm.hpp>
#include "ParameterType.h"

struct ParameterDescriptor {
    std::string name;
    ParameterType type;
    void* ptr;

    float minFloat = 0.0f;
    float maxFloat = 1.0f;

    int minInt = 0;
    int maxInt = 100;

    bool automatable = true;
    bool audioReactive = true;
    bool midiBindable = true;
    bool oscBindable = true;
};
