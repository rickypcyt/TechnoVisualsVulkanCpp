#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "ParameterRegistry.h"

class JsonSerializer {
public:
    static bool save(const std::string& path, const ParameterRegistry& registry);
    static bool load(const std::string& path, ParameterRegistry& registry);

    static nlohmann::json toJson(const ParameterRegistry& registry);
    static void fromJson(const nlohmann::json& j, ParameterRegistry& registry);
};
