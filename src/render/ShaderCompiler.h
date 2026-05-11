#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

class ShaderCompiler {
public:
    // Load shader code from file (with include resolution)
    static std::vector<char> loadFromFile(const std::string& path);
    
    // Create Vulkan shader module from bytecode
    static VkShaderModule createModule(VkDevice device, const std::vector<char>& code);
    
    // Simple file read (no include resolution)
    static std::vector<char> readFile(const std::string& filename);

private:
    // Resolve #include directives in shader source
    static std::string resolveIncludes(const std::string& source, const std::string& basePath);
};
