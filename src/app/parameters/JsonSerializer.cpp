#include "JsonSerializer.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

using json = nlohmann::json;

bool JsonSerializer::save(const std::string& path, const ParameterRegistry& registry) {
    json j;
    j["version"] = 1;

    for (auto& p : registry.all()) {
        switch (p.type) {
            case ParameterType::Float:
                j[p.name] = *(float*)p.ptr;
                break;
            case ParameterType::Int:
                j[p.name] = *(int*)p.ptr;
                break;
            case ParameterType::Bool:
                j[p.name] = *(bool*)p.ptr;
                break;
            case ParameterType::String:
                j[p.name] = *(std::string*)p.ptr;
                break;
            case ParameterType::Vec3: {
                auto& v = *(glm::vec3*)p.ptr;
                j[p.name] = {v.x, v.y, v.z};
                break;
            }
            case ParameterType::Vec4: {
                auto& v = *(glm::vec4*)p.ptr;
                j[p.name] = {v.x, v.y, v.z, v.w};
                break;
            }
            default:
                break;
        }
    }

    std::ofstream file(path);
    if (!file.is_open())
        return false;

    file << j.dump(4);
    return true;
}

bool JsonSerializer::load(const std::string& path, ParameterRegistry& registry) {
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        // File exists but is not valid JSON (likely old format)
        std::cerr << "[JsonSerializer] File is not valid JSON, skipping: " << path << std::endl;
        return false;
    }

    for (auto& p : registry.all()) {
        if (!j.contains(p.name))
            continue;

        switch (p.type) {
            case ParameterType::Float: {
                float v = j[p.name];
                v = std::clamp(v, p.minFloat, p.maxFloat);
                *(float*)p.ptr = v;
                break;
            }
            case ParameterType::Int: {
                int v = j[p.name];
                v = std::clamp(v, p.minInt, p.maxInt);
                *(int*)p.ptr = v;
                break;
            }
            case ParameterType::Bool:
                *(bool*)p.ptr = j[p.name];
                break;
            case ParameterType::String:
                *(std::string*)p.ptr = j[p.name];
                break;
            case ParameterType::Vec3: {
                auto arr = j[p.name];
                auto& v = *(glm::vec3*)p.ptr;
                v.x = arr[0];
                v.y = arr[1];
                v.z = arr[2];
                break;
            }
            case ParameterType::Vec4: {
                auto arr = j[p.name];
                auto& v = *(glm::vec4*)p.ptr;
                v.x = arr[0];
                v.y = arr[1];
                v.z = arr[2];
                v.w = arr[3];
                break;
            }
            default:
                break;
        }
    }

    return true;
}
