#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "ParameterDescriptor.h"

class ParameterRegistry {
public:
    void registerFloat(const std::string& name, float* ptr, float minValue, float maxValue);
    void registerInt(const std::string& name, int* ptr, int minValue, int maxValue);
    void registerBool(const std::string& name, bool* ptr);
    void registerVec3(const std::string& name, glm::vec3* ptr);
    void registerVec4(const std::string& name, glm::vec4* ptr);
    void registerString(const std::string& name, std::string* ptr);

    ParameterDescriptor* get(const std::string& name);
    const std::vector<ParameterDescriptor>& all() const;

private:
    std::vector<ParameterDescriptor> params;
    std::unordered_map<std::string, size_t> lookup;
};
