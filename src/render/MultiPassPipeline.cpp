#include "MultiPassPipeline.h"
#include "../gfx/ResourceSystem.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <cstring>
#include <chrono>

namespace {
VkPipelineStageFlags stageForLayout(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_PIPELINE_STAGE_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_UNDEFINED:
        default:
            return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
}
}

MultiPassPipeline::MultiPassPipeline() = default;

MultiPassPipeline::~MultiPassPipeline() {
    cleanup();
}

void MultiPassPipeline::setPassEnabled(int pass, bool enabled) {
    if (pass >= 0 && pass < NUM_PASSES) {
        passEnabled[pass] = enabled;
    }
}

bool MultiPassPipeline::isPassEnabled(int pass) const {
    if (pass >= 0 && pass < NUM_PASSES) {
        return passEnabled[pass];
    }
    return false;
}

void MultiPassPipeline::initProfiling(VkDevice device) {
    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = TOTAL_QUERIES;

    if (vkCreateQueryPool(device, &poolInfo, nullptr, &queryPool) != VK_SUCCESS) {
        std::cerr << "[MultiPass] Failed to create timestamp query pool" << std::endl;
        queryPool = VK_NULL_HANDLE;
    }
}

void MultiPassPipeline::cleanupProfiling(VkDevice device) {
    if (queryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device, queryPool, nullptr);
        queryPool = VK_NULL_HANDLE;
    }
}

void MultiPassPipeline::printProfilingResults(VkDevice device) {
    if (queryPool == VK_NULL_HANDLE) return;

    // Readback query results from the PREVIOUS frame (already completed)
    std::vector<uint64_t> queryResults(TOTAL_QUERIES);
    VkResult result = vkGetQueryPoolResults(
        device, queryPool,
        0, TOTAL_QUERIES,
        sizeof(uint64_t) * queryResults.size(), queryResults.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
    );

    if (result != VK_SUCCESS) {
        // Results not ready yet, skip this frame
        return;
    }

    static const char* PASS_NAMES[] = {
        "Pass A (Base)",
        "Pass B (Spatial)",
        "Pass C (Detail)",
        "Pass D (Temporal)",
        "Pass E (Degradation)",
        "Pass F (Color)",
        "Pass G (Output)",
        "Swapchain Final"
    };

    // Get timestamp period
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    float timestampPeriod = props.limits.timestampPeriod; // nanoseconds per tick

    lastGpuTotalTime = 0.0f;
    for (int i = 0; i < PROFILED_PASS_COUNT; ++i) {
        uint64_t start = queryResults[i * QUERIES_PER_PASS + 0];
        uint64_t end   = queryResults[i * QUERIES_PER_PASS + 1];
        if (end > start) {
            float ms = (end - start) * timestampPeriod / 1'000'000.0f;
            lastGpuPassTimes[i] = ms;
            lastGpuTotalTime += ms;
        } else {
            lastGpuPassTimes[i] = 0.0f;
        }
    }
}

bool MultiPassPipeline::createTemporalHistoryImage() {
    destroyTemporalHistoryImage();

    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = colorFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &temporalHistory.image) != VK_SUCCESS) {
        temporalHistory.image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device, temporalHistory.image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &temporalHistory.memory) != VK_SUCCESS) {
        destroyTemporalHistoryImage();
        return false;
    }

    vkBindImageMemory(device, temporalHistory.image, temporalHistory.memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = temporalHistory.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = colorFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &temporalHistory.imageView) != VK_SUCCESS) {
        destroyTemporalHistoryImage();
        return false;
    }

    temporalHistoryInitialized = false;
    temporalHistoryLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

bool MultiPassPipeline::initialize(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkQueue graphicsQueue,
    uint32_t graphicsQueueFamilyIndex,
    VkExtent2D extent,
    VkFormat colorFormat,
    VkSampler videoSampler,
    VkSampler videoSamplerPrev,
    VkImageView videoImageView,
    VkImageView videoPrevImageView,
    VkSampler video2Sampler,
    VkImageView video2ImageView,
    VkSampler video3Sampler,
    VkImageView video3ImageView,
    const std::vector<VkBuffer>& uniformBuffers,
    size_t uniformBufferSize
) {
    this->physicalDevice = physicalDevice;
    this->device = device;
    this->graphicsQueue = graphicsQueue;
    this->graphicsQueueFamilyIndex = graphicsQueueFamilyIndex;
    this->extent = extent;
    this->colorFormat = colorFormat;
    this->videoSampler = videoSampler;
    this->videoSamplerPrev = videoSamplerPrev;
    this->videoImageView = videoImageView;
    this->videoPrevImageView = videoPrevImageView;
    this->video2Sampler = video2Sampler;
    this->video2ImageView = video2ImageView;
    this->video3Sampler = video3Sampler;
    this->video3ImageView = video3ImageView;
    this->uniformBuffers = uniformBuffers;
    this->uniformBufferSize = uniformBufferSize;

    if (!createIntermediateFramebuffers()) {
        std::cerr << "[MultiPass] Failed to create intermediate framebuffers" << std::endl;
        return false;
    }

    if (!createTemporalHistoryImage()) {
        std::cerr << "[MultiPass] Failed to create temporal history image" << std::endl;
        return false;
    }

    if (!createComputePipelines()) {
        std::cerr << "[MultiPass] Failed to create compute pipelines" << std::endl;
        return false;
    }

    if (!createDescriptorSets()) {
        std::cerr << "[MultiPass] Failed to create descriptor sets" << std::endl;
        return false;
    }

    // CRITICAL: Update descriptor sets with texture bindings during initialization
    // Otherwise passes B-G will have uninitialized descriptors
    updateDescriptorSets(
        uniformBuffers,
        videoImageView,
        videoPrevImageView,
        videoSampler,
        videoSamplerPrev,
        video2ImageView,
        video2Sampler,
        video3ImageView,
        video3Sampler
    );

    initProfiling(device);

    std::cout << "[MultiPass] Initialized successfully with " << NUM_PASSES << " passes" << std::endl;
    return true;
}

void MultiPassPipeline::cleanup() {
    cleanupProfiling(device);
    cleanupDescriptorSets();
    cleanupPipelines();
    cleanupIntermediateFramebuffers();
    destroyTemporalHistoryImage();

    intermediateLayouts = {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED};
}

bool MultiPassPipeline::createIntermediateFramebuffers() {
    // Create 2 intermediate framebuffers for ping-pong rendering
    for (int i = 0; i < 2; ++i) {
        // Create image
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = extent.width;
        imageInfo.extent.height = extent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = colorFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device, &imageInfo, nullptr, &intermediate[i].image) != VK_SUCCESS) {
            return false;
        }

        // Allocate memory
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, intermediate[i].image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &intermediate[i].memory) != VK_SUCCESS) {
            return false;
        }

        vkBindImageMemory(device, intermediate[i].image, intermediate[i].memory, 0);

        // Create image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = intermediate[i].image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = colorFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &intermediate[i].imageView) != VK_SUCCESS) {
            return false;
        }

        intermediateLayouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    std::cout << "[MultiPass] Intermediate storage images created" << std::endl;
    return true;
}
bool MultiPassPipeline::createComputePipelines() {
    // Compute shader file names for each pass (one SPIR-V per pass)
    static const char* PASS_COMPUTE_SHADERS[] = {
        "shaders/pass_a_base.comp.spv",
        "shaders/pass_b_spatial.comp.spv",
        "shaders/pass_c_detail.comp.spv",
        "shaders/pass_d_temporal.comp.spv",
        "shaders/pass_e_degradation.comp.spv",
        "shaders/pass_f_color.comp.spv",
        "shaders/pass_g_output.comp.spv"
    };

    // Output storage image binding for each pass (after all sampled inputs)
    static const int PASS_OUTPUT_BINDING[] = {4, 1, 1, 2, 1, 1, 2};

    for (int i = 0; i < NUM_PASSES; ++i) {
        std::cout << "[MultiPass] Creating compute pipeline for pass " << i << " (" << PASS_COMPUTE_SHADERS[i] << ")" << std::endl;
        auto compShaderCode = readFile(PASS_COMPUTE_SHADERS[i]);
        if (compShaderCode.empty()) {
            std::cerr << "[MultiPass] Failed to read compute shader for pass " << i << std::endl;
            return false;
        }

        VkShaderModule compShaderModule = createShaderModule(compShaderCode);

        VkPipelineShaderStageCreateInfo compStage{};
        compStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        compStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        compStage.module = compShaderModule;
        compStage.pName = "main";

        // Set 0: UBO (used in both graphics and compute paths)
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo set0LayoutInfo{};
        set0LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set0LayoutInfo.bindingCount = 1;
        set0LayoutInfo.pBindings = &uboBinding;

        if (vkCreateDescriptorSetLayout(device, &set0LayoutInfo, nullptr, &passes[i].descriptorSetLayouts[0]) != VK_SUCCESS) {
            std::cerr << "[MultiPass] Failed to create descriptor set layout 0 for pass " << i << std::endl;
            return false;
        }

        // Set 1: sampled input textures + storage output image
        std::vector<VkDescriptorSetLayoutBinding> textureBindings;

        auto addSamplerBinding = [&](uint32_t binding) {
            VkDescriptorSetLayoutBinding b{};
            b.binding = binding;
            b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b.descriptorCount = 1;
            b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            textureBindings.push_back(b);
        };

        if (i == 0) {
            addSamplerBinding(0); // videoTex
            addSamplerBinding(1); // videoTexPrev
            addSamplerBinding(2); // video2Tex
            addSamplerBinding(3); // video3Tex
        } else if (i == 3) {
            addSamplerBinding(0); // inputTex
            addSamplerBinding(1); // prevFrameTex
        } else if (i == 6) {
            addSamplerBinding(0); // inputTex
            addSamplerBinding(1); // proceduralTex
        } else {
            addSamplerBinding(0); // inputTex
        }

        // Output storage image binding
        VkDescriptorSetLayoutBinding outputBinding{};
        outputBinding.binding = PASS_OUTPUT_BINDING[i];
        outputBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        outputBinding.descriptorCount = 1;
        outputBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        textureBindings.push_back(outputBinding);

        VkDescriptorSetLayoutCreateInfo set1LayoutInfo{};
        set1LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set1LayoutInfo.bindingCount = static_cast<uint32_t>(textureBindings.size());
        set1LayoutInfo.pBindings = textureBindings.data();

        if (vkCreateDescriptorSetLayout(device, &set1LayoutInfo, nullptr, &passes[i].descriptorSetLayouts[1]) != VK_SUCCESS) {
            std::cerr << "[MultiPass] Failed to create descriptor set layout 1 for pass " << i << std::endl;
            return false;
        }

        // Pipeline layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 2;
        pipelineLayoutInfo.pSetLayouts = passes[i].descriptorSetLayouts;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &passes[i].pipelineLayout) != VK_SUCCESS) {
            std::cerr << "[MultiPass] Failed to create pipeline layout for pass " << i << std::endl;
            return false;
        }

        // Compute pipeline
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = compStage;
        pipelineInfo.layout = passes[i].pipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &passes[i].computePipeline) != VK_SUCCESS) {
            std::cerr << "[MultiPass] Failed to create compute pipeline for pass " << i << std::endl;
            return false;
        }

        vkDestroyShaderModule(device, compShaderModule, nullptr);
    }

    std::cout << "[MultiPass] Compute pipelines created for all " << NUM_PASSES << " passes" << std::endl;
    return true;
}

bool MultiPassPipeline::createDescriptorSets() {
    printf("[MultiPass] Creating descriptor sets...\n");

    // Use the actual number of frames from uniformBuffers
    uint32_t numFrames = static_cast<uint32_t>(uniformBuffers.size());
    if (numFrames == 0) {
        std::cerr << "[MultiPass] ERROR: uniformBuffers is empty!" << std::endl;
        return false;
    }

    // Calculate total descriptor sets needed: 2 sets per pass per frame (set 0 for UBOs, set 1 for textures)
    uint32_t maxSets = NUM_PASSES * numFrames * 2;

    // Calculate pool sizes
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NUM_PASSES * numFrames}, // Set 0: one UBO per pass per frame
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, NUM_PASSES * numFrames * 4}, // Set 1: up to 4 sampled textures per pass per frame
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, NUM_PASSES * numFrames} // Set 1: one storage output per pass per frame
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        std::cerr << "[MultiPass] Failed to create descriptor pool" << std::endl;
        return false;
    }

    for (int pass = 0; pass < NUM_PASSES; ++pass) {
        passes[pass].descriptorSets[0].resize(numFrames); // Set 0: UBOs
        passes[pass].descriptorSets[1].resize(numFrames); // Set 1: Textures

        for (uint32_t frame = 0; frame < numFrames; ++frame) {
            // Allocate set 0 (UBOs)
            VkDescriptorSetAllocateInfo allocInfo0{};
            allocInfo0.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo0.descriptorPool = descriptorPool;
            allocInfo0.descriptorSetCount = 1;
            allocInfo0.pSetLayouts = &passes[pass].descriptorSetLayouts[0];

            if (vkAllocateDescriptorSets(device, &allocInfo0, &passes[pass].descriptorSets[0][frame]) != VK_SUCCESS) {
                std::cerr << "[MultiPass] Failed to allocate descriptor set 0 for pass " << pass << " frame " << frame << std::endl;
                return false;
            }

            // Allocate set 1 (textures)
            VkDescriptorSetAllocateInfo allocInfo1{};
            allocInfo1.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo1.descriptorPool = descriptorPool;
            allocInfo1.descriptorSetCount = 1;
            allocInfo1.pSetLayouts = &passes[pass].descriptorSetLayouts[1];

            if (vkAllocateDescriptorSets(device, &allocInfo1, &passes[pass].descriptorSets[1][frame]) != VK_SUCCESS) {
                std::cerr << "[MultiPass] Failed to allocate descriptor set 1 for pass " << pass << " frame " << frame << std::endl;
                return false;
            }
        }
    }

    // Update UBO descriptor sets (set 0)
    for (int pass = 0; pass < NUM_PASSES; ++pass) {
        for (uint32_t frame = 0; frame < numFrames; ++frame) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers[frame];
            bufferInfo.offset = 0;
            bufferInfo.range = uniformBufferSize;

            VkWriteDescriptorSet uboWrite{};
            uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            uboWrite.dstSet = passes[pass].descriptorSets[0][frame];
            uboWrite.dstBinding = 0;
            uboWrite.dstArrayElement = 0;
            uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboWrite.descriptorCount = 1;
            uboWrite.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(device, 1, &uboWrite, 0, nullptr);
        }
    }
    
    return true;
}

uint32_t MultiPassPipeline::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    throw std::runtime_error("failed to find suitable memory type");
}

void MultiPassPipeline::transitionImageLayout(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask) {
    if (image == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE) {
        return;
    }

    auto accessMaskForLayout = [](VkImageLayout layout, bool isDst) -> VkAccessFlags {
        switch (layout) {
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                return VK_ACCESS_TRANSFER_WRITE_BIT;
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                return VK_ACCESS_TRANSFER_READ_BIT;
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                return isDst ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                return VK_ACCESS_SHADER_READ_BIT;
            case VK_IMAGE_LAYOUT_GENERAL:
                return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            default:
                return 0;
        }
    };

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = accessMaskForLayout(oldLayout, false);
    barrier.dstAccessMask = accessMaskForLayout(newLayout, true);

    vkCmdPipelineBarrier(
        cmd,
        srcStageMask,
        dstStageMask,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

void MultiPassPipeline::execute(VkCommandBuffer cmd, uint32_t frameIndex, VkDescriptorSet uboDescriptorSet,
                                VkRenderPass swapchainRenderPass, const std::vector<VkFramebuffer>& swapchainFramebuffers,
                                uint32_t swapchainImageIndex, VkPipeline swapchainPipeline, VkPipelineLayout swapchainPipelineLayout,
                                VkDescriptorSet swapchainDescriptorSet, VkExtent2D swapchainExtent, VkSampler swapchainSampler,
                                int previewOverlay) {
    auto ensureLayout = [&](VkImage image, VkImageLayout& currentLayout, VkImageLayout newLayout) {
        if (image == VK_NULL_HANDLE || currentLayout == newLayout) {
            currentLayout = newLayout;
            return;
        }
        VkPipelineStageFlags srcStage = stageForLayout(currentLayout);
        VkPipelineStageFlags dstStage = stageForLayout(newLayout);
        transitionImageLayout(cmd, image, currentLayout, newLayout, srcStage, dstStage);
        currentLayout = newLayout;
    };

    if (!temporalHistoryInitialized && temporalHistory.image != VK_NULL_HANDLE) {
        ensureLayout(temporalHistory.image, temporalHistoryLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;
        vkCmdClearColorImage(cmd, temporalHistory.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

        ensureLayout(temporalHistory.image, temporalHistoryLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        temporalHistoryInitialized = true;
    }

    // Execute all passes in sequence to offscreen buffers
    int lastOutputBuffer = -1;  // -1 = no offscreen output yet (pass A reads external textures)

    if (queryPool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(cmd, queryPool, 0, TOTAL_QUERIES);
    }

    static const int PASS_OUTPUT_BINDING[] = {4, 1, 1, 2, 1, 1, 2};

    for (int pass = 0; pass < NUM_PASSES; ++pass) {
        if (!passEnabled[pass]) continue;

        int targetBuffer = (lastOutputBuffer == -1) ? 0 : (1 - lastOutputBuffer);

        // Validate descriptor sets before dispatch
        if (frameIndex >= passes[pass].descriptorSets[0].size() ||
            frameIndex >= passes[pass].descriptorSets[1].size() ||
            passes[pass].descriptorSets[0][frameIndex] == VK_NULL_HANDLE ||
            passes[pass].descriptorSets[1][frameIndex] == VK_NULL_HANDLE) {
            std::cerr << "[MultiPass] ERROR: Pass " << pass << " descriptor set is NULL for frame " << frameIndex << std::endl;
            return;
        }

        if (queryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, pass * QUERIES_PER_PASS);
        }

        // Transition output image to GENERAL for compute write
        ensureLayout(intermediate[targetBuffer].image, intermediateLayouts[targetBuffer], VK_IMAGE_LAYOUT_GENERAL);

        // Bind output storage image for this pass (dynamic because ping-pong depends on enabled passes)
        VkDescriptorImageInfo outputImageInfo{};
        outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        outputImageInfo.imageView = intermediate[targetBuffer].imageView;

        VkWriteDescriptorSet outputWrite{};
        outputWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        outputWrite.dstSet = passes[pass].descriptorSets[1][frameIndex];
        outputWrite.dstBinding = PASS_OUTPUT_BINDING[pass];
        outputWrite.dstArrayElement = 0;
        outputWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        outputWrite.descriptorCount = 1;
        outputWrite.pImageInfo = &outputImageInfo;

        vkUpdateDescriptorSets(device, 1, &outputWrite, 0, nullptr);

        // Bind compute pipeline and descriptor sets
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, passes[pass].computePipeline);

        VkDescriptorSet descriptorSetsToBind[2] = {
            passes[pass].descriptorSets[0][frameIndex],
            passes[pass].descriptorSets[1][frameIndex]
        };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, passes[pass].pipelineLayout,
                                0, 2, descriptorSetsToBind, 0, nullptr);

        // Dispatch fullscreen compute (8x8 local size)
        uint32_t groupX = (extent.width + 7) / 8;
        uint32_t groupY = (extent.height + 7) / 8;
        vkCmdDispatch(cmd, groupX, groupY, 1);

        // Transition output image to SHADER_READ_ONLY_OPTIMAL for the next pass
        ensureLayout(intermediate[targetBuffer].image, intermediateLayouts[targetBuffer], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (pass == 3 && temporalHistory.image != VK_NULL_HANDLE) {
            ensureLayout(intermediate[targetBuffer].image, intermediateLayouts[targetBuffer], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            ensureLayout(temporalHistory.image, temporalHistoryLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageCopy copyRegion{};
            copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.srcSubresource.baseArrayLayer = 0;
            copyRegion.srcSubresource.mipLevel = 0;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.dstSubresource = copyRegion.srcSubresource;
            copyRegion.extent = {extent.width, extent.height, 1};

            vkCmdCopyImage(cmd,
                          intermediate[targetBuffer].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          temporalHistory.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          1, &copyRegion);

            ensureLayout(temporalHistory.image, temporalHistoryLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            // Return intermediate back to shader-read after copy so next pass can sample it
            ensureLayout(intermediate[targetBuffer].image, intermediateLayouts[targetBuffer], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if (queryPool != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, pass * QUERIES_PER_PASS + 1);
        }

        lastOutputBuffer = targetBuffer;
    }

    // Final pass: render to swapchain
    // The final output is in lastOutputBuffer (dynamically tracked)
    int finalBuffer = (lastOutputBuffer >= 0) ? lastOutputBuffer : 0;

    // Update swapchain descriptor set to point to final multipass output
    VkDescriptorImageInfo finalOutputInfo{};
    finalOutputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    finalOutputInfo.imageView = intermediate[finalBuffer].imageView;
    finalOutputInfo.sampler = swapchainSampler;

    VkWriteDescriptorSet finalOutputWrite{};
    finalOutputWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    finalOutputWrite.dstSet = swapchainDescriptorSet;
    finalOutputWrite.dstBinding = 1;
    finalOutputWrite.dstArrayElement = 0;
    finalOutputWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    finalOutputWrite.descriptorCount = 1;
    finalOutputWrite.pImageInfo = &finalOutputInfo;

    vkUpdateDescriptorSets(device, 1, &finalOutputWrite, 0, nullptr);

    if (queryPool != VK_NULL_HANDLE) {
        int swapQuery = NUM_PASSES * QUERIES_PER_PASS;
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, swapQuery);
    }

    // Begin swapchain render pass
    VkRenderPassBeginInfo swapchainRenderPassInfo{};
    swapchainRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    swapchainRenderPassInfo.renderPass = swapchainRenderPass;
    swapchainRenderPassInfo.framebuffer = swapchainFramebuffers[swapchainImageIndex];

    VkClearValue swapchainClearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    swapchainRenderPassInfo.clearValueCount = 1;
    swapchainRenderPassInfo.pClearValues = &swapchainClearColor;
    swapchainRenderPassInfo.renderArea.offset = {0, 0};
    swapchainRenderPassInfo.renderArea.extent = swapchainExtent;

    vkCmdBeginRenderPass(cmd, &swapchainRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind swapchain pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, swapchainPipeline);

    // Bind swapchain descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, swapchainPipelineLayout,
                            0, 1, &swapchainDescriptorSet, 0, nullptr);

    // Push preview overlay flag
    vkCmdPushConstants(cmd, swapchainPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &previewOverlay);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainExtent.width);
    viewport.height = static_cast<float>(swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Draw fullscreen quad
    vkCmdDraw(cmd, 4, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

    if (queryPool != VK_NULL_HANDLE) {
        int swapQuery = NUM_PASSES * QUERIES_PER_PASS + 1;
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, swapQuery);
    }
}

void MultiPassPipeline::updateDescriptorSets(
    const std::vector<VkBuffer>& uniformBuffers,
    VkImageView videoImageView,
    VkImageView videoPrevImageView,
    VkSampler videoSampler,
    VkSampler videoSamplerPrev,
    VkImageView video2ImageView,
    VkSampler video2Sampler,
    VkImageView video3ImageView,
    VkSampler video3Sampler
) {
    if (videoImageView == VK_NULL_HANDLE || videoSampler == VK_NULL_HANDLE) {
        return;
    }

    // Update stored image views so recreate() uses correct values
    this->videoImageView = videoImageView;
    this->videoPrevImageView = videoPrevImageView;
    this->videoSampler = videoSampler;
    this->videoSamplerPrev = videoSamplerPrev;
    this->video2ImageView = video2ImageView;
    this->video2Sampler = video2Sampler;
    this->video3ImageView = video3ImageView;
    this->video3Sampler = video3Sampler;

    // Accumulate all writes into a single vector to issue one vkUpdateDescriptorSets call.
    // Note: Set 0 (UBOs) is already updated in createDescriptorSets, so we only update set 1 (textures) here.
    std::vector<VkWriteDescriptorSet> textureWrites;
    std::vector<VkDescriptorImageInfo> imageInfos;
    textureWrites.reserve(NUM_PASSES * uniformBuffers.size() * 4);
    imageInfos.reserve(NUM_PASSES * uniformBuffers.size() * 4);

    int currentBuffer = 0;  // Ping-pong buffer tracking for descriptor set updates
    for (int pass = 0; pass < NUM_PASSES; ++pass) {
        for (size_t frame = 0; frame < uniformBuffers.size(); ++frame) {
            auto addTextureWrite = [&](uint32_t binding, VkImageView view, VkSampler sampler) {
                if (view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
                    return;
                }

                VkDescriptorImageInfo& imageInfo = imageInfos.emplace_back();
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = view;
                imageInfo.sampler = sampler;

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = passes[pass].descriptorSets[1][frame];
                write.dstBinding = binding;
                write.dstArrayElement = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.descriptorCount = 1;
                write.pImageInfo = &imageInfo;

                textureWrites.push_back(write);
            };

            // Pass-specific texture bindings for set 1
            if (pass == 0) {  // Pass A needs video textures at bindings 0,1,2,3
                addTextureWrite(0, videoImageView, videoSampler);
                addTextureWrite(1, videoPrevImageView, videoSamplerPrev);

                if (video2ImageView != VK_NULL_HANDLE && video2Sampler != VK_NULL_HANDLE) {
                    addTextureWrite(2, video2ImageView, video2Sampler);
                }
                if (video3ImageView != VK_NULL_HANDLE && video3Sampler != VK_NULL_HANDLE) {
                    addTextureWrite(3, video3ImageView, video3Sampler);
                }
            } else if (pass >= 1 && pass <= 5) {  // Passes B-F need input texture at binding 0
                int prevBuffer = 1 - currentBuffer;
                addTextureWrite(0, intermediate[prevBuffer].imageView, videoSampler);

                if (pass == 3) {
                    VkImageView historyView = temporalHistory.imageView != VK_NULL_HANDLE
                        ? temporalHistory.imageView
                        : intermediate[prevBuffer].imageView;
                    addTextureWrite(1, historyView, videoSampler);
                }
            }
            // Pass G (pass 6) needs input texture and procedural texture
            else if (pass == 6) {
                int prevBuffer = 1 - currentBuffer;
                addTextureWrite(0, intermediate[prevBuffer].imageView, videoSampler);
                addTextureWrite(1, intermediate[prevBuffer].imageView, videoSampler);
            }
        }
        // Alternate ping-pong buffer after each pass
        currentBuffer = 1 - currentBuffer;
    }

    if (!textureWrites.empty()) {
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(textureWrites.size()),
                               textureWrites.data(), 0, nullptr);
    }
}

void MultiPassPipeline::recreate(VkExtent2D newExtent) {
    cleanupIntermediateFramebuffers();
    extent = newExtent;
    createIntermediateFramebuffers();
    createTemporalHistoryImage();

    // CRITICAL: Recreate compute pipelines after resize
    cleanupPipelines();
    createComputePipelines();

    // CRITICAL: Recreate descriptor sets since their layouts were destroyed
    cleanupDescriptorSets();
    createDescriptorSets();

    // CRITICAL: Update descriptor sets to point to new intermediate image views
    // Otherwise passes B-G will sample from destroyed image views
    updateDescriptorSets(
        uniformBuffers,
        videoImageView,
        videoPrevImageView,
        videoSampler,
        videoSamplerPrev,
        video2ImageView,
        video2Sampler,
        video3ImageView,
        video3Sampler
    );
}

void MultiPassPipeline::cleanupIntermediateFramebuffers() {
    for (int i = 0; i < 2; ++i) {
        if (intermediate[i].imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, intermediate[i].imageView, nullptr);
            intermediate[i].imageView = VK_NULL_HANDLE;
        }
        if (intermediate[i].image != VK_NULL_HANDLE) {
            vkDestroyImage(device, intermediate[i].image, nullptr);
            intermediate[i].image = VK_NULL_HANDLE;
        }
        if (intermediate[i].memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, intermediate[i].memory, nullptr);
            intermediate[i].memory = VK_NULL_HANDLE;
        }
    }
}

void MultiPassPipeline::destroyTemporalHistoryImage() {
    if (temporalHistory.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, temporalHistory.imageView, nullptr);
        temporalHistory.imageView = VK_NULL_HANDLE;
    }
    if (temporalHistory.image != VK_NULL_HANDLE) {
        vkDestroyImage(device, temporalHistory.image, nullptr);
        temporalHistory.image = VK_NULL_HANDLE;
    }
    if (temporalHistory.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, temporalHistory.memory, nullptr);
        temporalHistory.memory = VK_NULL_HANDLE;
    }
    temporalHistoryInitialized = false;
    temporalHistoryLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void MultiPassPipeline::cleanupPipelines() {
    for (int i = 0; i < NUM_PASSES; ++i) {
        if (passes[i].computePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, passes[i].computePipeline, nullptr);
            passes[i].computePipeline = VK_NULL_HANDLE;
        }
        if (passes[i].pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, passes[i].pipelineLayout, nullptr);
            passes[i].pipelineLayout = VK_NULL_HANDLE;
        }
        // Clean up both descriptor set layouts
        if (passes[i].descriptorSetLayouts[0] != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, passes[i].descriptorSetLayouts[0], nullptr);
            passes[i].descriptorSetLayouts[0] = VK_NULL_HANDLE;
        }
        if (passes[i].descriptorSetLayouts[1] != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, passes[i].descriptorSetLayouts[1], nullptr);
            passes[i].descriptorSetLayouts[1] = VK_NULL_HANDLE;
        }
    }
}

void MultiPassPipeline::cleanupDescriptorSets() {
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    // Clear descriptor set arrays
    for (int i = 0; i < NUM_PASSES; ++i) {
        passes[i].descriptorSets[0].clear();
        passes[i].descriptorSets[1].clear();
    }
}


std::vector<char> MultiPassPipeline::readFile(const std::string& filename) {
    std::cout << "[MultiPass] Reading shader file: " << filename << std::endl;
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[MultiPass] Failed to open file: " << filename << std::endl;
        return {};
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    std::cout << "[MultiPass] Read " << buffer.size() << " bytes from " << filename << std::endl;
    return buffer;
}

VkShaderModule MultiPassPipeline::createShaderModule(const std::vector<char>& code) {
    std::cout << "[MultiPass] Creating shader module (" << code.size() << " bytes)..." << std::endl;
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        std::cerr << "[MultiPass] Failed to create shader module" << std::endl;
        return VK_NULL_HANDLE;
    }
    std::cout << "[MultiPass] Shader module created successfully" << std::endl;
    return shaderModule;
}

