#include "ShaderCompiler.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

std::vector<char> ShaderCompiler::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    std::cout << "[ShaderCompiler] Reading file: " << filename << std::endl;

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    file.close();

    return buffer;
}

std::string ShaderCompiler::resolveIncludes(const std::string& source, const std::string& basePath) {
    std::istringstream stream(source);
    std::string line;
    std::string result;

    while (std::getline(stream, line)) {
        size_t includePos = line.find("#include");
        if (includePos != std::string::npos) {
            size_t start = line.find('"', includePos);
            size_t end = line.find('"', start + 1);
            if (start != std::string::npos && end != std::string::npos && end > start) {
                std::string includeFile = line.substr(start + 1, end - start - 1);
                std::string includePath = basePath + "/" + includeFile;
                auto includedRaw = readFile(includePath);
                std::string includedSource(includedRaw.begin(), includedRaw.end());
                result += resolveIncludes(includedSource, basePath);
                continue;
            }
        }

        result += line + "\n";
    }

    return result;
}

std::vector<char> ShaderCompiler::loadFromFile(const std::string& path) {
    std::cout << "[ShaderCompiler] Loading shader: " << path << std::endl;
    // Check if it's a compiled SPIR-V file (.spv extension)
    if (path.find(".spv") != std::string::npos) {
        auto code = readFile(path);
        std::cout << "[ShaderCompiler] Loaded SPIR-V shader: " << path << " (" << code.size() << " bytes)" << std::endl;
        return code;
    }
    
    // Otherwise treat as text shader with includes
    auto raw = readFile(path);
    std::string source(raw.begin(), raw.end());
    std::string basePath = ".";
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
        basePath = path.substr(0, slash);
    }
    std::string resolved = resolveIncludes(source, basePath);
    auto result = std::vector<char>(resolved.begin(), resolved.end());
    std::cout << "[ShaderCompiler] Resolved includes for: " << path << " (" << result.size() << " bytes)" << std::endl;
    return result;
}

VkShaderModule ShaderCompiler::createModule(VkDevice device, const std::vector<char>& code) {
    std::cout << "[ShaderCompiler] Creating shader module (" << code.size() << " bytes)..." << std::endl;
    if (code.empty()) {
        throw std::runtime_error("shader code is empty");
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module");
    }

    std::cout << "[ShaderCompiler] Shader module created successfully" << std::endl;
    return shaderModule;
}
