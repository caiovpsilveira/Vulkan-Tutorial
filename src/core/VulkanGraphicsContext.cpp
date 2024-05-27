// Dynamic loader/dispatcher settings for vulkan.hpp and VMA
#define VK_NO_PROTOTYPES

#define VULKAN_HPP_NO_SMART_HANDLE
#define VULKAN_HPP_NO_CONSTRUCTORS

#define VMA_STATIC_VULKAN_FUNCTIONS 0
// CHECKME why? Vulkan HPP default dispatcher should be able to load all extensions?
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION

#include "VulkanGraphicsContext.hpp"

#include "Logger.hpp"

// libs
#include <SDL.h>
#include <SDL_video.h>
#include <SDL_vulkan.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

// std
#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace core
{

namespace
{

VkBool32 debugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                VkDebugUtilsMessageTypeFlagsEXT messageType,
                                const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                void* pUserData) noexcept
{
    (void) messageType;
    (void) pUserData;

    switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            ERROR_FMT("[VALIDATION LAYER]: {}\n", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            WARN_FMT("[VALIDATION LAYER]: {}\n", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            INFO_FMT("[VALIDATION LAYER]: {}\n", pCallbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        default: TRACE_FMT("[VALIDATION LAYER]: {}\n", pCallbackData->pMessage); break;
    }

    return VK_FALSE;
}

bool isValidationLayerSupported()
{
    auto instanceLayerProperties = vk::enumerateInstanceLayerProperties();

    auto match = [](const vk::LayerProperties& p) {
        return std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0;
    };

    bool supportsValidationLayers = false;
    if (auto result = std::ranges::find_if(instanceLayerProperties, match); result != instanceLayerProperties.end()) {
        supportsValidationLayers = true;
    }
    return supportsValidationLayers;
}

bool isExtensionSupported(std::span<const vk::ExtensionProperties> availableExtensions,
                          const char* extensionName) noexcept
{
    auto match = [extensionName](const vk::ExtensionProperties& p) {
        return std::strcmp(p.extensionName, extensionName) == 0;
    };

    auto result = std::ranges::find_if(availableExtensions, match);
    return result != availableExtensions.end();
}

}   // namespace

VulkanGraphicsContext::VulkanGraphicsContext(std::span<const char* const> requiredInstanceExtensions,
                                             bool enableValidationLayersIfSupported,
                                             bool enableDebugMessengerIfSupported,
                                             SDL_Window* window)
{
    try {
        createInstanceAndDebug(requiredInstanceExtensions,
                               enableValidationLayersIfSupported,
                               enableDebugMessengerIfSupported);

        SDL_bool SDLRes = SDL_Vulkan_CreateSurface(window, m_instance, reinterpret_cast<VkSurfaceKHR*>(&m_surface));
        if (SDLRes != SDL_TRUE) {
            // To be consistent with vulkan.hpp, throw a vk::SystemError
            throw vk::UnknownError(SDL_GetError());
        }
        DEBUG("Successfullly created surface\n");

        searchPhysicalDevice();

        createLogicalDevice();

        createAllocator();

        createSwapchain(window);

        assert(m_instance);
        // assert(m_debugMessenger); // Allowed, can be nullptr if debug is disabled / not supported
        assert(m_surface);
        assert(m_physicalDevice);
        assert(m_device);
        assert(m_graphicsQueue);
        assert(m_presentQueue);
        assert(m_allocator);
        assert(m_swapchain);
    } catch (...) {
        cleanup();
        FATAL("VulkanGraphicsContext creation failed\n");
        throw;
    }
}

VulkanGraphicsContext::VulkanGraphicsContext(VulkanGraphicsContext&& rhs) noexcept
    : m_instance(std::exchange(rhs.m_instance, nullptr))
    , m_debugMessenger(std::exchange(rhs.m_debugMessenger, nullptr))
    , m_surface(std::exchange(rhs.m_surface, nullptr))
    , m_physicalDevice(std::exchange(rhs.m_physicalDevice, nullptr))
    , m_queueFamiliesIndices(rhs.m_queueFamiliesIndices)
    , m_device(std::exchange(rhs.m_device, nullptr))
    , m_graphicsQueue(std::exchange(rhs.m_graphicsQueue, nullptr))
    , m_presentQueue(std::exchange(rhs.m_presentQueue, nullptr))
    , m_allocator(std::exchange(rhs.m_allocator, nullptr))
    , m_swapchain(std::exchange(rhs.m_swapchain, nullptr))
{}

VulkanGraphicsContext& VulkanGraphicsContext::operator=(VulkanGraphicsContext&& rhs) noexcept
{
    if (this != &rhs) {
        std::swap(m_instance, rhs.m_instance);
        std::swap(m_debugMessenger, rhs.m_debugMessenger);
        std::swap(m_surface, rhs.m_surface);
        std::swap(m_physicalDevice, rhs.m_physicalDevice);
        m_queueFamiliesIndices = rhs.m_queueFamiliesIndices;
        std::swap(m_device, rhs.m_device);
        std::swap(m_graphicsQueue, rhs.m_graphicsQueue);
        std::swap(m_presentQueue, rhs.m_presentQueue);
        std::swap(m_allocator, rhs.m_allocator);
        std::swap(m_swapchain, rhs.m_swapchain);
    }
    return *this;
}

VulkanGraphicsContext::~VulkanGraphicsContext() noexcept
{
    cleanup();
}

void VulkanGraphicsContext::createInstanceAndDebug(std::span<const char* const> requiredInstanceExtensions,
                                                   bool enableValidationLayersIfSupported,
                                                   bool enableDebugMessengerIfSupported)
{
    VULKAN_HPP_DEFAULT_DISPATCHER.init();

    vk::ApplicationInfo applicationInfo {.sType = vk::StructureType::eApplicationInfo,
                                         .pNext = nullptr,
                                         .pApplicationName = "Voxel game",
                                         .applicationVersion = vk::makeApiVersion(0, 0, 1, 0),
                                         .pEngineName = "No engine",
                                         .engineVersion = vk::ApiVersion10,
                                         .apiVersion = vk::ApiVersion13};

    bool useValidationLayers = enableValidationLayersIfSupported && isValidationLayerSupported();
    if (enableValidationLayersIfSupported && !useValidationLayers) {
        INFO("Validation layer requested, but not supported\n");
    }

    auto availableInstanceExtensions = vk::enumerateInstanceExtensionProperties();
    bool useDebugMessenger = enableDebugMessengerIfSupported &&
                             isExtensionSupported(availableInstanceExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (enableDebugMessengerIfSupported && !useDebugMessenger) {
        INFO("Debug messenger util extension requested, but not supported\n");
    }

    uint32_t layerCount = useValidationLayers ? 1 : 0;
    const char* pLayer = useValidationLayers ? "VK_LAYER_KHRONOS_validation" : nullptr;

    std::vector<const char*> extensions(requiredInstanceExtensions.begin(), requiredInstanceExtensions.end());
    if (useDebugMessenger) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    vk::DebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo {
        .sType = vk::StructureType::eDebugUtilsMessengerCreateInfoEXT,
        .pNext = nullptr,
        .flags = {},
        .messageSeverity =
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = debugMessengerCallback,
        .pUserData = nullptr};

    void* instancePNext = useDebugMessenger ? &debugMessengerCreateInfo : nullptr;

    vk::InstanceCreateInfo instanceCreateInfo {.sType = vk::StructureType::eInstanceCreateInfo,
                                               .pNext = instancePNext,
                                               .flags = {},
                                               .pApplicationInfo = &applicationInfo,
                                               .enabledLayerCount = layerCount,
                                               .ppEnabledLayerNames = &pLayer,
                                               .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
                                               .ppEnabledExtensionNames = extensions.data()};

    m_instance = vk::createInstance(instanceCreateInfo);
    DEBUG("Successfullly created instance\n");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);

    if (useDebugMessenger) {
        m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(debugMessengerCreateInfo);
        DEBUG("Successfullly created debug messenger\n");
    }
}

void VulkanGraphicsContext::searchPhysicalDevice()
{
    // TODO: improve physical device selection.
    // Currently selecting the first one with graphicsQueueFamily + presentQueueFamily + swapchainSupport

    auto physicalDevices = m_instance.enumeratePhysicalDevices();

    for (const auto& pd : physicalDevices) {
        auto pdProperties = pd.getProperties();
        DEBUG_FMT("Encountered physical device: {}\n", static_cast<const char*>(pdProperties.deviceName));
    }

    for (const auto& pd : physicalDevices) {
        auto queueFamiliesProperties = pd.getQueueFamilyProperties();
        std::optional<uint32_t> graphicsFamilyIndex;
        std::optional<uint32_t> presentFamilyIndex;
        for (size_t i = 0; i < queueFamiliesProperties.size(); ++i) {
            const auto& qp = queueFamiliesProperties[i];
            if (qp.queueFlags & vk::QueueFlagBits::eGraphics) {
                graphicsFamilyIndex = i;
            }
            if (pd.getSurfaceSupportKHR(i, m_surface)) {
                presentFamilyIndex = i;
            }
            if (graphicsFamilyIndex && presentFamilyIndex) {
                break;
            }
        }

        // Query swapchain support
        // The Vulkan specs requires VK_KHR_surface extension to support at least VK_PRESENT_MODE_FIFO_KHR present mode
        // (ref VkPresentModeKHR(3) Manual Page), and requires at least one VkSurfaceFormatKHR to be supported, with
        // format != undefined (ref vkGetPhysicalDeviceSurfaceFormatsKHR(3) Manual Page).
        // Just checking if the VK_KHR_swapchain is supported is enough.
        auto availablePhysicalDeviceExtensions = pd.enumerateDeviceExtensionProperties();

        bool swapchainSupport =
            isExtensionSupported(availablePhysicalDeviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        if (graphicsFamilyIndex && presentFamilyIndex && swapchainSupport) {
            m_physicalDevice = pd;
            m_queueFamiliesIndices.graphicsFamilyIndex = *graphicsFamilyIndex;
            m_queueFamiliesIndices.presentFamilyIndex = *presentFamilyIndex;

            auto pdProperties = pd.getProperties();
            DEBUG_FMT(
                "Successfully encountered a suitable physical device.\nName: {}\nApi version: p{} {}.{}.{}\nDriver "
                "version: {}\n",
                static_cast<const char*>(pdProperties.deviceName),
                vk::apiVersionVariant(pdProperties.apiVersion),
                vk::apiVersionMajor(pdProperties.apiVersion),
                vk::apiVersionMinor(pdProperties.apiVersion),
                vk::apiVersionPatch(pdProperties.apiVersion),
                pdProperties.driverVersion);

            return;
        }
    }
    // To be consistent with vulkan.hpp, throw a vk::SystemError
    throw vk::UnknownError("No physical device matched the application requirements");
}

void VulkanGraphicsContext::createLogicalDevice()
{
    std::vector<uint32_t> uniqueQueueFamiliesIndices {m_queueFamiliesIndices.graphicsFamilyIndex,
                                                      m_queueFamiliesIndices.presentFamilyIndex};
    // remove duplicate elements
    uniqueQueueFamiliesIndices.erase(std::unique(uniqueQueueFamiliesIndices.begin(), uniqueQueueFamiliesIndices.end()),
                                     uniqueQueueFamiliesIndices.end());

    float queuePriorities[] {1.f};
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    queueCreateInfos.reserve(uniqueQueueFamiliesIndices.size());

    std::transform(uniqueQueueFamiliesIndices.begin(),
                   uniqueQueueFamiliesIndices.end(),
                   std::back_inserter(queueCreateInfos),
                   [&queuePriorities](uint32_t queueFamilyIndex) {
                       return vk::DeviceQueueCreateInfo {.sType = vk::StructureType::eDeviceQueueCreateInfo,
                                                         .pNext = nullptr,
                                                         .flags = {},
                                                         .queueFamilyIndex = queueFamilyIndex,
                                                         .queueCount = std::size(queuePriorities),
                                                         .pQueuePriorities = queuePriorities};
                   });

    std::vector<const char*> enabledExtensions {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    vk::DeviceCreateInfo deviceCreateInfo {.sType = vk::StructureType::eDeviceCreateInfo,
                                           .pNext = nullptr,
                                           .flags = {},
                                           .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
                                           .pQueueCreateInfos = queueCreateInfos.data(),
                                           .enabledLayerCount = 0,
                                           .ppEnabledLayerNames = nullptr,
                                           .enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size()),
                                           .ppEnabledExtensionNames = enabledExtensions.data(),
                                           .pEnabledFeatures = nullptr};

    m_device = m_physicalDevice.createDevice(deviceCreateInfo);
    DEBUG("Successfully created logical device\n");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);

    m_graphicsQueue = m_device.getQueue(m_queueFamiliesIndices.graphicsFamilyIndex, 0);
    m_presentQueue = m_device.getQueue(m_queueFamiliesIndices.presentFamilyIndex, 0);
}

void VulkanGraphicsContext::createAllocator()
{
    const auto& d = VULKAN_HPP_DEFAULT_DISPATCHER;
    VmaVulkanFunctions vulkanFunctions {};
    vulkanFunctions.vkGetInstanceProcAddr = d.vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = d.vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorCreateInfo {};
    allocatorCreateInfo.physicalDevice = m_physicalDevice, allocatorCreateInfo.device = m_device,
    allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
    allocatorCreateInfo.instance = m_instance, allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;

    VkResult res = vmaCreateAllocator(&allocatorCreateInfo, &m_allocator);
    if (res != VK_SUCCESS) {
        // To be consistent with vulkan.hpp, throw a vk::SystemError
        throw vk::UnknownError(std::string("VulkanMemoryAllocator initialization failed with code ") +
                               std::to_string(res));
    }
    DEBUG("Successfully created vmaAllocator\n");
}

void VulkanGraphicsContext::createSwapchain(SDL_Window* window)
{
    // Select the default values for the present mode and SurfaceFormatKHR
    auto presentModes = m_physicalDevice.getSurfacePresentModesKHR(m_surface);
    m_currentSwapchainPresentMode = std::ranges::find(presentModes, vk::PresentModeKHR::eMailbox) != presentModes.end()
                                        ? vk::PresentModeKHR::eMailbox
                                        : vk::PresentModeKHR::eFifo;

    auto surfaceFormats = m_physicalDevice.getSurfaceFormatsKHR(m_surface);
    // Vulkan.hpp provides trivial operator==
    m_currentSwapchainSurfaceFormat =
        std::ranges::find(surfaceFormats,
                          vk::SurfaceFormatKHR {vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear}) !=
                surfaceFormats.end()
            ? vk::SurfaceFormatKHR {vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear}
            : surfaceFormats[0];

    recreateSwapchain(window);
}

void VulkanGraphicsContext::recreateSwapchain(SDL_Window* window)
{
    auto surfaceCapabilities = m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface);

    if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        m_currentSwapchainExtent = surfaceCapabilities.currentExtent;
    } else {
        int w, h;
        SDL_Vulkan_GetDrawableSize(window, &w, &h);
        vk::Extent2D actualExtent(static_cast<uint32_t>(w), static_cast<uint32_t>(h));

        actualExtent.width = std::clamp(actualExtent.width,
                                        surfaceCapabilities.minImageExtent.width,
                                        surfaceCapabilities.maxImageExtent.width);

        actualExtent.height = std::clamp(actualExtent.height,
                                         surfaceCapabilities.minImageExtent.height,
                                         surfaceCapabilities.maxImageExtent.height);
        m_currentSwapchainExtent = actualExtent;
    }

    uint32_t imageCount = surfaceCapabilities.maxImageCount == 0   // unlimited
                              ? surfaceCapabilities.minImageCount + 1
                              : std::min(surfaceCapabilities.maxImageCount, surfaceCapabilities.minImageCount + 1);

    vk::SharingMode imageSharingMode;
    uint32_t queueFamilyIndexCount;
    uint32_t* pQueueFamilyIndices;
    uint32_t queueFamilyIndices[] = {m_queueFamiliesIndices.graphicsFamilyIndex,
                                     m_queueFamiliesIndices.presentFamilyIndex};

    // TODO: these probably can be stored and be immutable, but is it worth it?
    if (m_queueFamiliesIndices.graphicsFamilyIndex != m_queueFamiliesIndices.presentFamilyIndex) {
        imageSharingMode = vk::SharingMode::eConcurrent;
        queueFamilyIndexCount = std::size(queueFamilyIndices);
        pQueueFamilyIndices = queueFamilyIndices;
    } else {
        imageSharingMode = vk::SharingMode::eExclusive;
        queueFamilyIndexCount = 0;
        pQueueFamilyIndices = nullptr;
    }

    // on the first call, this will create the swapchain, as m_swapchain is initialized as nullptr
    // otherwise pass it as old swapchain
    vk::SwapchainCreateInfoKHR swapchainCreateInfo {.sType = vk::StructureType::eSwapchainCreateInfoKHR,
                                                    .pNext = nullptr,
                                                    .flags = {},
                                                    .surface = m_surface,
                                                    .minImageCount = imageCount,
                                                    .imageFormat = m_currentSwapchainSurfaceFormat.format,
                                                    .imageColorSpace = m_currentSwapchainSurfaceFormat.colorSpace,
                                                    .imageExtent = m_currentSwapchainExtent,
                                                    .imageArrayLayers = 1,
                                                    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
                                                    .imageSharingMode = imageSharingMode,
                                                    .queueFamilyIndexCount = queueFamilyIndexCount,
                                                    .pQueueFamilyIndices = pQueueFamilyIndices,
                                                    .preTransform = surfaceCapabilities.currentTransform,
                                                    .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                                    .presentMode = m_currentSwapchainPresentMode,
                                                    .clipped = vk::True,
                                                    .oldSwapchain = m_swapchain};

    bool creating = m_swapchain == nullptr;
    m_swapchain = m_device.createSwapchainKHR(swapchainCreateInfo);
    DEBUG_FMT("Successfully {} swapchain\n", creating ? "created" : "re-created");
}

void VulkanGraphicsContext::cleanup() noexcept
{
    // This is reused both on the destructor and to cleanup resouce aquired during the constructor, if something failed.
    // For the destructor, all members must be in a valid state, otherwise the constructor must have thrown

    if (m_device) {
        m_device.waitIdle();
    }
    if (m_swapchain) {
        assert(m_device);
        m_device.destroySwapchainKHR(m_swapchain);
    }
    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
    }
    // Queues are owned by device
    if (m_device) {
        m_device.destroy();
    }
    // PhysicalDevice is owned by the instance
    if (m_surface) {
        assert(m_instance);
        m_instance.destroySurfaceKHR(m_surface);
    }
    if (m_debugMessenger) {
        assert(m_instance);
        m_instance.destroyDebugUtilsMessengerEXT(m_debugMessenger);
    }
    if (m_instance) {
        m_instance.destroy();
    }
}

}   // namespace core