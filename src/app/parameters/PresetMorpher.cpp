#include "PresetMorpher.h"
#include <glm/glm.hpp>

void PresetMorpher::morph(ParameterRegistry& current, ParameterRegistry& a, ParameterRegistry& b, float t) {
    auto& currentParams = current.all();
    auto& aParams = a.all();
    auto& bParams = b.all();

    for (size_t i = 0; i < currentParams.size(); i++) {
        auto& c = currentParams[i];
        auto& pa = aParams[i];
        auto& pb = bParams[i];

        if (c.type != pa.type || c.type != pb.type)
            continue;

        switch (c.type) {
            case ParameterType::Float: {
                float va = *(float*)pa.ptr;
                float vb = *(float*)pb.ptr;
                *(float*)c.ptr = va + (vb - va) * t;
                break;
            }
            case ParameterType::Vec3: {
                auto va = *(glm::vec3*)pa.ptr;
                auto vb = *(glm::vec3*)pb.ptr;
                *(glm::vec3*)c.ptr = glm::mix(va, vb, t);
                break;
            }
            case ParameterType::Vec4: {
                auto va = *(glm::vec4*)pa.ptr;
                auto vb = *(glm::vec4*)pb.ptr;
                *(glm::vec4*)c.ptr = glm::mix(va, vb, t);
                break;
            }
            default:
                break;
        }
    }
}
