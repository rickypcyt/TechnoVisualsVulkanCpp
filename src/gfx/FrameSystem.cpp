// gfx/FrameSystem.cpp
#include "gfx/FrameSystem.h"
#include <stdexcept>
#include <iostream>
#include <cassert>

// ─── RAII ────────────────────────────────────────────────────────────────────

FrameSystem::~FrameSystem() {
    cleanup();
}

FrameSystem::FrameSystem(FrameSystem&& other) noexcept
    : m_device(other.m_device)
    , m_maxFrames(other.m_maxFrames)
    , m_currentFrame(other.m_currentFrame)
    , m_frameContexts(std::move(other.m_frameContexts))
    , m_renderFinishedSemaphores(std::move(other.m_renderFinishedSemaphores))
    , m_imagesInFlight(std::move(other.m_imagesInFlight))
{
    other.m_device       = VK_NULL_HANDLE;
    other.m_maxFrames    = 0;
    other.m_currentFrame = 0;
}

FrameSystem& FrameSystem::operator=(FrameSystem&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_device                   = other.m_device;
        m_maxFrames                = other.m_maxFrames;
        m_currentFrame             = other.m_currentFrame;
        m_frameContexts            = std::move(other.m_frameContexts);
        m_renderFinishedSemaphores = std::move(other.m_renderFinishedSemaphores);
        m_imagesInFlight           = std::move(other.m_imagesInFlight);
        other.m_device             = VK_NULL_HANDLE;
        other.m_maxFrames          = 0;
        other.m_currentFrame       = 0;
    }
    return *this;
}

// ─── Init / Cleanup ──────────────────────────────────────────────────────────

void FrameSystem::init(VkDevice device, uint32_t frameCount, uint32_t swapchainImageCount) {
    if (device == VK_NULL_HANDLE)
        throw std::invalid_argument("FrameSystem::init — null device handle");
    if (frameCount == 0)
        throw std::invalid_argument("FrameSystem::init — frameCount must be > 0");
    if (swapchainImageCount == 0)
        throw std::invalid_argument("FrameSystem::init — swapchainImageCount must be > 0");

    std::lock_guard lock(m_mutex);

    if (m_device != VK_NULL_HANDLE)
        throw std::logic_error("FrameSystem::init — already initialized; call cleanup() first");

    m_device       = device;
    m_maxFrames    = frameCount;
    m_currentFrame = 0;

    createFrameSyncObjects(frameCount);
    createSwapchainSemaphores(swapchainImageCount);
    m_imagesInFlight.assign(swapchainImageCount, VK_NULL_HANDLE);

    std::cout << "[FrameSystem] Initialized — frames: " << std::dec << m_maxFrames
              << ", swapchain images: " << swapchainImageCount << '\n';
}

void FrameSystem::cleanup() {
    std::lock_guard lock(m_mutex);

    if (m_device == VK_NULL_HANDLE)
        return;

    destroyFrameSyncObjects();
    destroySwapchainSemaphores();

    m_imagesInFlight.clear();
    m_device       = VK_NULL_HANDLE;
    m_maxFrames    = 0;
    m_currentFrame = 0;
}

// ─── Frame lifecycle ─────────────────────────────────────────────────────────

FrameContext* FrameSystem::beginFrame(VkSwapchainKHR swapchain,
                                       uint32_t&      imageIndex,
                                       VkResult&      result)
{
    std::lock_guard lock(m_mutex);

    if (m_device == VK_NULL_HANDLE)
        throw std::logic_error("FrameSystem::beginFrame — not initialized");
    if (swapchain == VK_NULL_HANDLE)
        throw std::invalid_argument("FrameSystem::beginFrame — null swapchain");

    FrameContext& frame = m_frameContexts[m_currentFrame];

    // 1. Wait for previous use of this frame slot
    //    (frees imageAvailableSemaphore for reuse)
    vkWaitForFences(m_device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

    // 2. Acquire next swapchain image
    result = vkAcquireNextImageKHR(
        m_device,
        swapchain,
        UINT64_MAX,
        frame.imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR || result == VK_NOT_READY) {
        return nullptr;
    }

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("FrameSystem::beginFrame — vkAcquireNextImageKHR failed");

    // Grow tracking array if swapchain was recreated with more images
    if (imageIndex >= m_imagesInFlight.size())
        m_imagesInFlight.resize(imageIndex + 1, VK_NULL_HANDLE);

    // 3. Wait for any previous frame still using this swapchain image
    if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(m_device, 1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);

    // 4. Reset fence only when we're about to submit work
    vkResetFences(m_device, 1, &frame.inFlightFence);

    m_imagesInFlight[imageIndex] = frame.inFlightFence;
    frame.swapchainImageIndex    = imageIndex;

    return &frame;
}

void FrameSystem::endFrame() {
    std::lock_guard lock(m_mutex);
    if (m_maxFrames == 0) return;
    m_currentFrame = (m_currentFrame + 1) % m_maxFrames;
}

// ─── Resize / recreate ───────────────────────────────────────────────────────

void FrameSystem::resizeSwapchainImages(uint32_t count) {
    if (count == 0)
        throw std::invalid_argument("FrameSystem::resizeSwapchainImages — count must be > 0");

    std::lock_guard lock(m_mutex);

    destroySwapchainSemaphores();
    createSwapchainSemaphores(count);
    m_imagesInFlight.assign(count, VK_NULL_HANDLE);
}

void FrameSystem::waitForAllFences() {
    std::lock_guard lock(m_mutex);

    for (auto& frame : m_frameContexts) {
        if (frame.inFlightFence != VK_NULL_HANDLE)
            vkWaitForFences(m_device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
    }
}

void FrameSystem::recreateSemaphores() {
    // Wait idle before touching semaphores
    waitForAllFences();

    std::lock_guard lock(m_mutex);

    // Recreate per-frame imageAvailable semaphores
    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (auto& frame : m_frameContexts) {
        if (frame.imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, frame.imageAvailableSemaphore, nullptr);
            frame.imageAvailableSemaphore = VK_NULL_HANDLE;
        }
        if (vkCreateSemaphore(m_device, &si, nullptr, &frame.imageAvailableSemaphore) != VK_SUCCESS)
            throw std::runtime_error("FrameSystem::recreateSemaphores — imageAvailable failed");
    }

    // Recreate renderFinished semaphores
    const auto count = static_cast<uint32_t>(m_renderFinishedSemaphores.size());
    destroySwapchainSemaphores();
    createSwapchainSemaphores(count);
}

void FrameSystem::resetCurrentFrame() {
    std::lock_guard lock(m_mutex);
    m_currentFrame = 0;
}

// ─── Getters ─────────────────────────────────────────────────────────────────

std::optional<VkFence> FrameSystem::getFence(uint32_t frameIndex) const {
    std::lock_guard lock(m_mutex);
    if (frameIndex >= m_frameContexts.size())
        return std::nullopt;
    return m_frameContexts[frameIndex].inFlightFence;
}

std::optional<VkSemaphore> FrameSystem::getRenderFinishedSemaphore(uint32_t swapchainImageIndex) const {
    std::lock_guard lock(m_mutex);
    if (swapchainImageIndex >= m_renderFinishedSemaphores.size())
        return std::nullopt;
    return m_renderFinishedSemaphores[swapchainImageIndex];
}

// ─── Private helpers ─────────────────────────────────────────────────────────

void FrameSystem::createFrameSyncObjects(uint32_t frameCount) {
    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // Pre-signaled so first wait doesn't block

    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    m_frameContexts.resize(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i) {
        FrameContext& ctx = m_frameContexts[i];
        ctx.frameIndex = i;

        if (vkCreateFence(m_device, &fi, nullptr, &ctx.inFlightFence) != VK_SUCCESS)
            throw std::runtime_error("FrameSystem — failed to create inFlightFence");

        if (vkCreateSemaphore(m_device, &si, nullptr, &ctx.imageAvailableSemaphore) != VK_SUCCESS) {
            vkDestroyFence(m_device, ctx.inFlightFence, nullptr);
            ctx.inFlightFence = VK_NULL_HANDLE;
            throw std::runtime_error("FrameSystem — failed to create imageAvailableSemaphore");
        }
    }
}

void FrameSystem::createSwapchainSemaphores(uint32_t count) {
    VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    m_renderFinishedSemaphores.resize(count);

    for (uint32_t i = 0; i < count; ++i) {
        if (vkCreateSemaphore(m_device, &si, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
            throw std::runtime_error("FrameSystem — failed to create renderFinishedSemaphore");
    }
}

void FrameSystem::destroyFrameSyncObjects() {
    for (auto& frame : m_frameContexts) {
        if (frame.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, frame.inFlightFence, nullptr);
            frame.inFlightFence = VK_NULL_HANDLE;
        }
        if (frame.imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, frame.imageAvailableSemaphore, nullptr);
            frame.imageAvailableSemaphore = VK_NULL_HANDLE;
        }
    }
    m_frameContexts.clear();
}

void FrameSystem::destroySwapchainSemaphores() {
    for (auto& sem : m_renderFinishedSemaphores) {
        if (sem != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, sem, nullptr);
            sem = VK_NULL_HANDLE;
        }
    }
    m_renderFinishedSemaphores.clear();
}