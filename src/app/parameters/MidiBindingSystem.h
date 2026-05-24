#pragma once

#include <unordered_map>
#include "ParameterDescriptor.h"

struct MidiBinding {
    int cc;
    ParameterDescriptor* target;
    float minValue;
    float maxValue;
    bool invert = false;
};

class MidiBindingSystem {
public:
    void bind(int cc, ParameterDescriptor* target, float minValue, float maxValue, bool invert);
    void processCC(int cc, int value);

private:
    std::unordered_map<int, MidiBinding> bindings;
};
