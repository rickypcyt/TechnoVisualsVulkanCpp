#include "JsonSerializer.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

using json = nlohmann::json;

nlohmann::json JsonSerializer::toJson(const ParameterRegistry& registry) {
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
    return j;
}

void JsonSerializer::fromJson(const nlohmann::json& j, ParameterRegistry& registry) {
    if (!j.is_object()) {
        std::cerr << "[JsonSerializer] Expected a JSON object, nothing loaded\n";
        return;
    }

    // One bad value must never abort the whole load (it used to throw and leave a half-applied state).
    int rejected = 0;
    std::vector<std::string> rejectedNames;
    auto reject = [&](const std::string& name) { ++rejected; rejectedNames.push_back(name); };

    for (auto& p : registry.all()) {
        auto it = j.find(p.name);
        if (it == j.end())
            continue;
        const json& v = *it;

        try {
            switch (p.type) {
                case ParameterType::Float: {
                    if (!v.is_number()) { reject(p.name); break; }
                    float f = v.get<float>();
                    if (!std::isfinite(f)) { reject(p.name); break; }
                    *(float*)p.ptr = std::clamp(f, p.minFloat, p.maxFloat);
                    break;
                }
                case ParameterType::Int: {
                    if (!v.is_number()) { reject(p.name); break; }
                    *(int*)p.ptr = std::clamp(v.get<int>(), p.minInt, p.maxInt);
                    break;
                }
                case ParameterType::Bool: {
                    if (!v.is_boolean()) { reject(p.name); break; }
                    *(bool*)p.ptr = v.get<bool>();
                    break;
                }
                case ParameterType::String: {
                    if (!v.is_string()) { reject(p.name); break; }
                    *(std::string*)p.ptr = v.get<std::string>();
                    break;
                }
                case ParameterType::Vec3: {
                    if (!v.is_array() || v.size() < 3) { reject(p.name); break; }
                    auto& out = *(glm::vec3*)p.ptr;
                    out.x = v[0].get<float>();
                    out.y = v[1].get<float>();
                    out.z = v[2].get<float>();
                    break;
                }
                case ParameterType::Vec4: {
                    if (!v.is_array() || v.size() < 4) { reject(p.name); break; }
                    auto& out = *(glm::vec4*)p.ptr;
                    out.x = v[0].get<float>();
                    out.y = v[1].get<float>();
                    out.z = v[2].get<float>();
                    out.w = v[3].get<float>();
                    break;
                }
                default:
                    break;
            }
        } catch (const std::exception&) {
            reject(p.name);
        }
    }

    // Keys present in the file that this build does not know (renamed/removed parameters)
    std::vector<std::string> unknown;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key() == "version") continue;
        if (!registry.get(it.key())) unknown.push_back(it.key());
    }

    if (rejected > 0 || !unknown.empty()) {
        std::cerr << "[JsonSerializer] Loaded with " << rejected << " invalid value(s) and "
                  << unknown.size() << " unknown key(s)";
        auto dump = [](const char* label, const std::vector<std::string>& v) {
            if (v.empty()) return;
            std::cerr << "\n   " << label << ":";
            for (size_t i = 0; i < v.size() && i < 12; ++i) std::cerr << ' ' << v[i];
            if (v.size() > 12) std::cerr << " ...";
        };
        dump("invalid", rejectedNames);
        dump("unknown", unknown);
        std::cerr << std::endl;
    }
}

bool JsonSerializer::save(const std::string& path, const ParameterRegistry& registry) {
    json j = toJson(registry);

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
        std::cerr << "[JsonSerializer] File is not valid JSON, skipping: " << path << std::endl;
        return false;
    }

    fromJson(j, registry);
    return true;
}
