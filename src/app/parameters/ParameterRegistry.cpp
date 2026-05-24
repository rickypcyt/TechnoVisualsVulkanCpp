#include "ParameterRegistry.h"

void ParameterRegistry::registerFloat(const std::string& name, float* ptr, float minValue, float maxValue) {
    ParameterDescriptor p;
    p.name = name;
    p.type = ParameterType::Float;
    p.ptr = ptr;
    p.minFloat = minValue;
    p.maxFloat = maxValue;
    lookup[name] = params.size();
    params.push_back(p);
}

void ParameterRegistry::registerInt(const std::string& name, int* ptr, int minValue, int maxValue) {
    ParameterDescriptor p;
    p.name = name;
    p.type = ParameterType::Int;
    p.ptr = ptr;
    p.minInt = minValue;
    p.maxInt = maxValue;
    lookup[name] = params.size();
    params.push_back(p);
}

void ParameterRegistry::registerBool(const std::string& name, bool* ptr) {
    ParameterDescriptor p;
    p.name = name;
    p.type = ParameterType::Bool;
    p.ptr = ptr;
    lookup[name] = params.size();
    params.push_back(p);
}

void ParameterRegistry::registerVec3(const std::string& name, glm::vec3* ptr) {
    ParameterDescriptor p;
    p.name = name;
    p.type = ParameterType::Vec3;
    p.ptr = ptr;
    lookup[name] = params.size();
    params.push_back(p);
}

void ParameterRegistry::registerVec4(const std::string& name, glm::vec4* ptr) {
    ParameterDescriptor p;
    p.name = name;
    p.type = ParameterType::Vec4;
    p.ptr = ptr;
    lookup[name] = params.size();
    params.push_back(p);
}

void ParameterRegistry::registerString(const std::string& name, std::string* ptr) {
    ParameterDescriptor p;
    p.name = name;
    p.type = ParameterType::String;
    p.ptr = ptr;
    lookup[name] = params.size();
    params.push_back(p);
}

ParameterDescriptor* ParameterRegistry::get(const std::string& name) {
    auto it = lookup.find(name);
    if (it == lookup.end())
        return nullptr;
    return &params[it->second];
}

const std::vector<ParameterDescriptor>& ParameterRegistry::all() const {
    return params;
}
