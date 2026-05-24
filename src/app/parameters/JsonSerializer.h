#pragma once

#include <string>
#include "ParameterRegistry.h"

class JsonSerializer {
public:
    static bool save(const std::string& path, const ParameterRegistry& registry);
    static bool load(const std::string& path, ParameterRegistry& registry);
};
