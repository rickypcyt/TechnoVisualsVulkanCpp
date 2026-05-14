#include "MultiPassPipeline.h"
#include "../gfx/ResourceSystem.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <cstring>
#include <chrono>

MultiPassPipeline::MultiPassPipeline() = default;

MultiPassPipeline::~MultiPassPipeline() {
    cleanup();
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
    this->uniformBuffers = uniformBuffers;
    this->uniformBufferSize = uniformBufferSize;

    if (!createOffscreenRenderPass()) {
        std::cerr << "[MultiPass] Failed to create offscreen render pass" << std::endl;
        return false;
    }

    if (!createIntermediateFramebuffers()) {
        std::cerr << "[MultiPass] Failed to create intermediate framebuffers" << std::endl;
        return false;
    }

    if (!createFullscreenQuad()) {
        std::cerr << "[MultiPass] Failed to create fullscreen quad" << std::endl;
        return false;
    }

    if (!createPipelines()) {
        std::cerr << "[MultiPass] Failed to create pipelines" << std::endl;
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
        videoSamplerPrev
    );

    std::cout << "[MultiPass] Initialized successfully with " << NUM_PASSES << " passes" << std::endl;
    return true;
}

void MultiPassPipeline::cleanup() {
    cleanupDescriptorSets();
    cleanupPipelines();
    cleanupIntermediateFramebuffers();
    cleanupFullscreenQuad();

    if (offscreenRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, offscreenRenderPass, nullptr);
        offscreenRenderPass = VK_NULL_HANDLE;
    }
}

bool MultiPassPipeline::createOffscreenRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &offscreenRenderPass) != VK_SUCCESS) {
        return false;
    }

    std::cout << "[MultiPass] Offscreen render pass created" << std::endl;
    return true;
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
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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

        // Create framebuffer
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = offscreenRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &intermediate[i].imageView;
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &intermediate[i].framebuffer) != VK_SUCCESS) {
            return false;
        }
    }

    std::cout << "[MultiPass] Intermediate framebuffers created" << std::endl;
    return true;
}
bool MultiPassPipeline::createFullscreenQuad() {
    // Fullscreen quad vertices (position, texcoord)
    struct Vertex {
        float pos[2];
        float texCoord[2];
    };

    const std::vector<Vertex> vertices = {
        {{-1.0f, -1.0f}, {0.0f, 0.0f}},
        {{ 1.0f, -1.0f}, {1.0f, 0.0f}},
        {{-1.0f,  1.0f}, {0.0f, 1.0f}},
        {{ 1.0f,  1.0f}, {1.0f, 1.0f}}
    };

    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    // Create vertex buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &vertexBuffer) != VK_SUCCESS) {
        return false;
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, vertexBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &vertexBufferMemory) != VK_SUCCESS) {
        return false;
    }

    vkBindBufferMemory(device, vertexBuffer, vertexBufferMemory, 0);

    // Copy data (simplified - should use staging buffer)
    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), bufferSize);
    vkUnmapMemory(device, vertexBufferMemory);

    std::cout << "[MultiPass] Fullscreen quad created" << std::endl;
    return true;
}

bool MultiPassPipeline::createPipelines() {
    // Shader file names for each pass
    static const char* PASS_SHADERS[][2] = {
        {"shaders/pass_shared.vert.spv", "shaders/pass_a_base.frag.spv"},      // Pass A
        {"shaders/pass_shared.vert.spv", "shaders/pass_b_spatial.frag.spv"},   // Pass B
        {"shaders/pass_shared.vert.spv", "shaders/pass_c_detail.frag.spv"},    // Pass C
        {"shaders/pass_shared.vert.spv", "shaders/pass_d_temporal.frag.spv"},  // Pass D
        {"shaders/pass_shared.vert.spv", "shaders/pass_e_degradation.frag.spv"},// Pass E
        {"shaders/pass_shared.vert.spv", "shaders/pass_f_color.frag.spv"},     // Pass F
        {"shaders/pass_shared.vert.spv", "shaders/pass_g_output.frag.spv"}     // Pass G
    };

    // For each pass, create a pipeline
    for (int i = 0; i < NUM_PASSES; ++i) {
        auto vertShaderCode = readFile(PASS_SHADERS[i][0]);
        auto fragShaderCode = readFile(PASS_SHADERS[i][1]);

        if (vertShaderCode.empty() || fragShaderCode.empty()) {
            std::cerr << "[MultiPass] Failed to read shader files for pass " << i << std::endl;
            return false;
        }

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShaderModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShaderModule;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertStage, fragStage};

        // No vertex input (fullscreen quad)
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)extent.width;
        viewport.height = (float)extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = extent;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // Create descriptor set layout for set 0 (UBOs) - single binding for GlobalParamsUBO
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo set0LayoutInfo{};
        set0LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set0LayoutInfo.bindingCount = 1;
        set0LayoutInfo.pBindings = &uboBinding;

        if (vkCreateDescriptorSetLayout(device, &set0LayoutInfo, nullptr, &passes[i].descriptorSetLayouts[0]) != VK_SUCCESS) {
            std::cerr << "[MultiPass] Failed to create descriptor set layout 0 for pass " << i << std::endl;
            return false;
        }

        // Create descriptor set layout for set 1 (textures) - pass-specific texture bindings
        std::vector<VkDescriptorSetLayoutBinding> textureBindings;

        // Pass A needs video textures at bindings 0 and 1
        if (i == 0) {
            VkDescriptorSetLayoutBinding videoTexBinding{};
            videoTexBinding.binding = 0;
            videoTexBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            videoTexBinding.descriptorCount = 1;
            videoTexBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            textureBindings.push_back(videoTexBinding);

            VkDescriptorSetLayoutBinding videoTexPrevBinding{};
            videoTexPrevBinding.binding = 1;
            videoTexPrevBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            videoTexPrevBinding.descriptorCount = 1;
            videoTexPrevBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            textureBindings.push_back(videoTexPrevBinding);
        }
        // Pass D needs inputTex and prevFrameTex
        else if (i == 3) {
            VkDescriptorSetLayoutBinding inputTexBinding{};
            inputTexBinding.binding = 0;
            inputTexBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            inputTexBinding.descriptorCount = 1;
            inputTexBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            textureBindings.push_back(inputTexBinding);

            VkDescriptorSetLayoutBinding prevFrameBinding{};
            prevFrameBinding.binding = 1;
            prevFrameBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            prevFrameBinding.descriptorCount = 1;
            prevFrameBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            textureBindings.push_back(prevFrameBinding);
        }
        // Pass G needs inputTex and proceduralTex
        else if (i == 6) {
            VkDescriptorSetLayoutBinding inputTexBinding{};
            inputTexBinding.binding = 0;
            inputTexBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            inputTexBinding.descriptorCount = 1;
            inputTexBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            textureBindings.push_back(inputTexBinding);

            VkDescriptorSetLayoutBinding proceduralBinding{};
            proceduralBinding.binding = 1;
            proceduralBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            proceduralBinding.descriptorCount = 1;
            proceduralBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            textureBindings.push_back(proceduralBinding);
        }
        // Other passes need just inputTex
        else {
            VkDescriptorSetLayoutBinding inputTexBinding{};
            inputTexBinding.binding = 0;
            inputTexBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            inputTexBinding.descriptorCount = 1;
            inputTexBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            textureBindings.push_back(inputTexBinding);
        }

        VkDescriptorSetLayoutCreateInfo set1LayoutInfo{};
        set1LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set1LayoutInfo.bindingCount = static_cast<uint32_t>(textureBindings.size());
        set1LayoutInfo.pBindings = textureBindings.data();

        if (vkCreateDescriptorSetLayout(device, &set1LayoutInfo, nullptr, &passes[i].descriptorSetLayouts[1]) != VK_SUCCESS) {
            std::cerr << "[MultiPass] Failed to create descriptor set layout 1 for pass " << i << std::endl;
            return false;
        }

        // Create pipeline layout with both descriptor set layouts
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 2;
        pipelineLayoutInfo.pSetLayouts = passes[i].descriptorSetLayouts;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &passes[i].pipelineLayout) != VK_SUCCESS) {
            return false;
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = passes[i].pipelineLayout;
        pipelineInfo.renderPass = offscreenRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &passes[i].pipeline) != VK_SUCCESS) {
            std::cerr << "[MultiPass] Failed to create pipeline for pass " << i << std::endl;
            return false;
        }

        printf("[MultiPass] Pipeline created for pass %d: pipeline=%p\n", i, (void*)passes[i].pipeline);

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);
    }

    std::cout << "[MultiPass] Pipelines created for all " << NUM_PASSES << " passes" << std::endl;
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
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, NUM_PASSES * numFrames * 3} // Set 1: up to 3 textures per pass per frame
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

void MultiPassPipeline::execute(VkCommandBuffer cmd, uint32_t frameIndex, VkDescriptorSet uboDescriptorSet,
                                VkRenderPass swapchainRenderPass, std::vector<VkFramebuffer>& swapchainFramebuffers,
                                uint32_t swapchainImageIndex, VkPipeline swapchainPipeline, VkPipelineLayout swapchainPipelineLayout,
                                VkDescriptorSet swapchainDescriptorSet, VkExtent2D swapchainExtent, VkSampler swapchainSampler) {
    // Update intermediate texture descriptors before each frame
    // This ensures passes B-F read from the correct intermediate textures
    updateIntermediateDescriptors(frameIndex);

    // Execute all 7 passes in sequence to offscreen buffers
    int currentBuffer = 0;

    for (int pass = 0; pass < NUM_PASSES; ++pass) {
        // Validate descriptor sets before drawing
        if (frameIndex >= passes[pass].descriptorSets[0].size() ||
            frameIndex >= passes[pass].descriptorSets[1].size() ||
            passes[pass].descriptorSets[0][frameIndex] == VK_NULL_HANDLE ||
            passes[pass].descriptorSets[1][frameIndex] == VK_NULL_HANDLE) {
            std::cerr << "[MultiPass] ERROR: Pass " << pass << " descriptor set is NULL for frame " << frameIndex << std::endl;
            return;
        }

        // DEBUG: Print descriptor set state before draw (once per second)
        static auto lastDebugTime = std::chrono::steady_clock::now();
        auto currentTime = std::chrono::steady_clock::now();
        if (pass == 0 && std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastDebugTime).count() > 1000) {
            int prevBuffer = (pass % 2 == 0) ? 1 : 0;
            // printf("[MultiPass DRAW] Pass=%d frameIndex=%u descriptorSet0=%p descriptorSet1=%p videoImageView=%p\n",
            //        pass, frameIndex, (void*)passes[pass].descriptorSets[0][frameIndex],
            //        (void*)passes[pass].descriptorSets[1][frameIndex], (void*)videoImageView);
            lastDebugTime = currentTime;
        }

        // Begin render pass
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = offscreenRenderPass;

        // For the last pass, we render to swapchain (not implemented here)
        // For now, all passes render to intermediate buffers
        renderPassInfo.framebuffer = intermediate[currentBuffer].framebuffer;

        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = extent;

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Bind pipeline
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, passes[pass].pipeline);

        // Bind both descriptor sets (set 0: UBOs, set 1: textures)
        VkDescriptorSet descriptorSetsToBind[2] = {
            passes[pass].descriptorSets[0][frameIndex],
            passes[pass].descriptorSets[1][frameIndex]
        };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, passes[pass].pipelineLayout,
                                0, 2, descriptorSetsToBind, 0, nullptr);

        // Draw fullscreen quad using vertex shader (no vertex buffer needed)
        vkCmdDraw(cmd, 4, 1, 0, 0);

        vkCmdEndRenderPass(cmd);

        // Ping-pong buffers
        currentBuffer = 1 - currentBuffer;
    }

    // Final pass: render to swapchain
    // The final output is in buffer 1 (since we ping-pong after each pass)
    int finalBuffer = 1;

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
}

void MultiPassPipeline::updateDescriptorSets(
    const std::vector<VkBuffer>& uniformBuffers,
    VkImageView videoImageView,
    VkImageView videoPrevImageView,
    VkSampler videoSampler,
    VkSampler videoSamplerPrev
) {
    // CRITICAL: Validate input handles before proceeding
    printf("[MultiPass] updateDescriptorSets - videoImageView=%p, videoPrevImageView=%p, videoSampler=%p\n",
           (void*)videoImageView, (void*)videoPrevImageView, (void*)videoSampler);

    if (videoImageView == VK_NULL_HANDLE) {
        std::cerr << "[MultiPass] ERROR: videoImageView is VK_NULL_HANDLE!" << std::endl;
        return;
    }
    if (videoSampler == VK_NULL_HANDLE) {
        std::cerr << "[MultiPass] ERROR: videoSampler is VK_NULL_HANDLE!" << std::endl;
        return;
    }

    // CRITICAL: Update stored image views so recreate() uses correct values
    this->videoImageView = videoImageView;
    this->videoPrevImageView = videoPrevImageView;
    this->videoSampler = videoSampler;
    this->videoSamplerPrev = videoSamplerPrev;

    // Update descriptor sets for each pass and frame
    // Note: Set 0 (UBOs) is already updated in createDescriptorSets, so we only update set 1 (textures) here
    int currentBuffer = 0;  // Ping-pong buffer tracking for descriptor set updates
    for (int pass = 0; pass < NUM_PASSES; ++pass) {
        // printf("[MultiPass] Processing pass=%d\n", pass);
        for (size_t frame = 0; frame < uniformBuffers.size(); ++frame) {
            // printf("[MultiPass] Processing pass=%d frame=%zu\n", pass, frame);
            std::vector<VkWriteDescriptorSet> textureWrites;

            // Pass-specific texture bindings for set 1
            if (pass == 0) {  // Pass A needs video textures at bindings 0 and 1
                VkDescriptorImageInfo videoTexInfo{};
                videoTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                videoTexInfo.imageView = videoImageView;
                videoTexInfo.sampler = videoSampler;

                VkWriteDescriptorSet videoTexWrite{};
                videoTexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                videoTexWrite.dstSet = passes[pass].descriptorSets[1][frame];
                videoTexWrite.dstBinding = 0;  // Set 1, binding 0
                videoTexWrite.dstArrayElement = 0;
                videoTexWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                videoTexWrite.descriptorCount = 1;
                videoTexWrite.pImageInfo = &videoTexInfo;

                textureWrites.push_back(videoTexWrite);

                VkDescriptorImageInfo videoTexPrevInfo{};
                videoTexPrevInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                videoTexPrevInfo.imageView = videoPrevImageView;
                videoTexPrevInfo.sampler = videoSamplerPrev;

                VkWriteDescriptorSet videoTexPrevWrite{};
                videoTexPrevWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                videoTexPrevWrite.dstSet = passes[pass].descriptorSets[1][frame];
                videoTexPrevWrite.dstBinding = 1;  // Set 1, binding 1
                videoTexPrevWrite.dstArrayElement = 0;
                videoTexPrevWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                videoTexPrevWrite.descriptorCount = 1;
                videoTexPrevWrite.pImageInfo = &videoTexPrevInfo;

                textureWrites.push_back(videoTexPrevWrite);
            } else if (pass >= 1 && pass <= 5) {  // Passes B-F need input texture at binding 0
                // Use intermediate texture from previous pass as input
                int prevBuffer = 1 - currentBuffer;

                VkDescriptorImageInfo inputTexInfo{};
                inputTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                inputTexInfo.imageView = intermediate[prevBuffer].imageView;
                inputTexInfo.sampler = videoSampler;

                VkWriteDescriptorSet inputTexWrite{};
                inputTexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                inputTexWrite.dstSet = passes[pass].descriptorSets[1][frame];
                inputTexWrite.dstBinding = 0;  // Set 1, binding 0
                inputTexWrite.dstArrayElement = 0;
                inputTexWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                inputTexWrite.descriptorCount = 1;
                inputTexWrite.pImageInfo = &inputTexInfo;

                textureWrites.push_back(inputTexWrite);

                // Pass D (pass 3) also needs prevFrameTex at binding 1
                if (pass == 3) {
                    VkDescriptorImageInfo prevFrameTexInfo{};
                    prevFrameTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    prevFrameTexInfo.imageView = intermediate[prevBuffer].imageView;
                    prevFrameTexInfo.sampler = videoSampler;

                    VkWriteDescriptorSet prevFrameTexWrite{};
                    prevFrameTexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    prevFrameTexWrite.dstSet = passes[pass].descriptorSets[1][frame];
                    prevFrameTexWrite.dstBinding = 1;  // Set 1, binding 1
                    prevFrameTexWrite.dstArrayElement = 0;
                    prevFrameTexWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    prevFrameTexWrite.descriptorCount = 1;
                    prevFrameTexWrite.pImageInfo = &prevFrameTexInfo;

                    textureWrites.push_back(prevFrameTexWrite);
                }
            }
            // Pass G (pass 6) needs input texture and procedural texture
            else if (pass == 6) {  // Pass G needs inputTex + proceduralTex
                int prevBuffer = 1 - currentBuffer;

                VkDescriptorImageInfo inputTexInfo{};
                inputTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                inputTexInfo.imageView = intermediate[prevBuffer].imageView;
                inputTexInfo.sampler = videoSampler;

                VkWriteDescriptorSet inputTexWrite{};
                inputTexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                inputTexWrite.dstSet = passes[pass].descriptorSets[1][frame];
                inputTexWrite.dstBinding = 0;  // Set 1, binding 0
                inputTexWrite.dstArrayElement = 0;
                inputTexWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                inputTexWrite.descriptorCount = 1;
                inputTexWrite.pImageInfo = &inputTexInfo;

                textureWrites.push_back(inputTexWrite);

                // For now, use the same input texture as procedural texture (fallback)
                VkDescriptorImageInfo proceduralTexInfo{};
                proceduralTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                proceduralTexInfo.imageView = intermediate[prevBuffer].imageView;
                proceduralTexInfo.sampler = videoSampler;

                VkWriteDescriptorSet proceduralTexWrite{};
                proceduralTexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                proceduralTexWrite.dstSet = passes[pass].descriptorSets[1][frame];
                proceduralTexWrite.dstBinding = 1;  // Set 1, binding 1
                proceduralTexWrite.dstArrayElement = 0;
                proceduralTexWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                proceduralTexWrite.descriptorCount = 1;
                proceduralTexWrite.pImageInfo = &proceduralTexInfo;

                textureWrites.push_back(proceduralTexWrite);
            }

            if (!textureWrites.empty()) {
                vkUpdateDescriptorSets(device, static_cast<uint32_t>(textureWrites.size()),
                                       textureWrites.data(), 0, nullptr);
            }
        }
        // Alternate ping-pong buffer after each pass
        currentBuffer = 1 - currentBuffer;
    }
}

void MultiPassPipeline::recreate(VkExtent2D newExtent) {
    cleanupIntermediateFramebuffers();
    extent = newExtent;
    createIntermediateFramebuffers();

    // CRITICAL: Recreate pipelines since they depend on render pass
    cleanupPipelines();
    createPipelines();

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
        videoSamplerPrev
    );
}

void MultiPassPipeline::cleanupIntermediateFramebuffers() {
    for (int i = 0; i < 2; ++i) {
        if (intermediate[i].framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, intermediate[i].framebuffer, nullptr);
            intermediate[i].framebuffer = VK_NULL_HANDLE;
        }
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

void MultiPassPipeline::cleanupPipelines() {
    for (int i = 0; i < NUM_PASSES; ++i) {
        if (passes[i].pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, passes[i].pipeline, nullptr);
            passes[i].pipeline = VK_NULL_HANDLE;
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

void MultiPassPipeline::cleanupFullscreenQuad() {
    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexBufferMemory, nullptr);
        vertexBufferMemory = VK_NULL_HANDLE;
    }
}

std::vector<char> MultiPassPipeline::readFile(const std::string& filename) {
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

    return buffer;
}

VkShaderModule MultiPassPipeline::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

void MultiPassPipeline::updateIntermediateDescriptors(uint32_t frameIndex) {
    // Update intermediate texture descriptors for passes B-F (passes 1-5)
    // Pass B (pass 1) reads from intermediate[0] (output of pass A)
    // Pass C (pass 2) reads from intermediate[1] (output of pass B)
    // And so on with ping-pong

    for (int pass = 1; pass < NUM_PASSES - 1; ++pass) {  // Passes B-F (1-5)
        // Calculate which intermediate buffer this pass should read from
        // Pass 1 reads from buffer 0 (output of pass 0)
        // Pass 2 reads from buffer 1 (output of pass 1)
        // Pass 3 reads from buffer 0 (output of pass 2)
        // etc.
        int prevBuffer = (pass % 2 == 1) ? 0 : 1;

        VkDescriptorImageInfo inputTexInfo{};
        inputTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        inputTexInfo.imageView = intermediate[prevBuffer].imageView;
        inputTexInfo.sampler = videoSampler;

        VkWriteDescriptorSet inputTexWrite{};
        inputTexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        inputTexWrite.dstSet = passes[pass].descriptorSets[1][frameIndex];
        inputTexWrite.dstBinding = 0;  // Set 1, binding 0
        inputTexWrite.dstArrayElement = 0;
        inputTexWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        inputTexWrite.descriptorCount = 1;
        inputTexWrite.pImageInfo = &inputTexInfo;

        vkUpdateDescriptorSets(device, 1, &inputTexWrite, 0, nullptr);

        // Pass D (pass 3) also needs prevFrameTex at binding 1
        if (pass == 3) {
            VkDescriptorImageInfo prevFrameTexInfo{};
            prevFrameTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            prevFrameTexInfo.imageView = intermediate[prevBuffer].imageView;
            prevFrameTexInfo.sampler = videoSampler;

            VkWriteDescriptorSet prevFrameTexWrite{};
            prevFrameTexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            prevFrameTexWrite.dstSet = passes[pass].descriptorSets[1][frameIndex];
            prevFrameTexWrite.dstBinding = 1;  // Set 1, binding 1
            prevFrameTexWrite.dstArrayElement = 0;
            prevFrameTexWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            prevFrameTexWrite.descriptorCount = 1;
            prevFrameTexWrite.pImageInfo = &prevFrameTexInfo;

            vkUpdateDescriptorSets(device, 1, &prevFrameTexWrite, 0, nullptr);
        }
    }

    // Pass G (pass 6) needs inputTex at binding 0 and proceduralTex at binding 1
    // Pass G reads from buffer 1 (output of pass F)
    int passGBuffer = 1;
    VkDescriptorImageInfo inputTexInfoG{};
    inputTexInfoG.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputTexInfoG.imageView = intermediate[passGBuffer].imageView;
    inputTexInfoG.sampler = videoSampler;

    VkWriteDescriptorSet inputTexWriteG{};
    inputTexWriteG.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    inputTexWriteG.dstSet = passes[6].descriptorSets[1][frameIndex];
    inputTexWriteG.dstBinding = 0;  // Set 1, binding 0
    inputTexWriteG.dstArrayElement = 0;
    inputTexWriteG.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    inputTexWriteG.descriptorCount = 1;
    inputTexWriteG.pImageInfo = &inputTexInfoG;

    vkUpdateDescriptorSets(device, 1, &inputTexWriteG, 0, nullptr);
}
