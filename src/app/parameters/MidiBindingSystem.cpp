#include "MidiBindingSystem.h"
#include <algorithm>

void MidiBindingSystem::bind(int cc, ParameterDescriptor* target, float minValue, float maxValue, bool invert) {
    bindings[cc] = {cc, target, minValue, maxValue, invert};
}

void MidiBindingSystem::processCC(int cc, int value) {
    auto it = bindings.find(cc);
    if (it == bindings.end())
        return;

    auto& b = it->second;
    float t = value / 127.0f;
    if (b.invert)
        t = 1.0f - t;

    float v = b.minValue + (b.maxValue - b.minValue) * t;

    switch (b.target->type) {
        case ParameterType::Float:
            *(float*)b.target->ptr = v;
            break;
        case ParameterType::Bool:
            *(bool*)b.target->ptr = v > 0.5f;
            break;
        case ParameterType::Int: {
            int iv = static_cast<int>(v);
            iv = std::clamp(iv, b.target->minInt, b.target->maxInt);
            *(int*)b.target->ptr = iv;
            break;
        }
        default:
            break;
    }
}
