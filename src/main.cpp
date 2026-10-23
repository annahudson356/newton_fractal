//--------------------------------------------------------------------------------------//
// PROJECT1: COMPUTE ARTIST
//--------------------------------------------------------------------------------------//
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h> //if we want to print out enum names
#define GLFW_INCLUDE_NONE //"no, do not include OpenGL"
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <iostream>
#include <chrono>
#include <cmath>
#include <fstream>
#include <filesystem>

//macro function to check the return value of Vulkan API calls. Print an error message and aborts the program if it fails
#define VK_CHECK(call)                                                  \
    do {                                                                \
        VkResult vk_check_result = (call);                              \
        if (vk_check_result != VK_SUCCESS) {                            \
            std::fprintf(stderr, "%s:%d: %s failed with VkResult %d\n", \
                        __FILE__, __LINE__, #call, vk_check_result);    \
            std::abort();                                               \
        }                                                               \
    } while (0)

static const uint32_t WINDOW_WIDTH = 800;
static const uint32_t WINDOW_HEIGHT = 800;
static const uint32_t FRAME_WIDTH = WINDOW_WIDTH;
static const uint32_t FRAME_HEIGHT = WINDOW_HEIGHT;
static const uint32_t FRAMES_IN_FLIGHT = 3;
static const char* COMPUTE_SHADER_PATH = SHADER_DIR "/myshader.comp.spv";

struct PushConstants {
    int   width;
    int   height;
    double center_x;
    double center_y;
    double scale;
    double root_x;
    double root_y;
    int   max_iter;
};

static double g_scroll_y = 0.0;
static void scroll_callback(GLFWwindow*, double, double yoffset) { g_scroll_y += yoffset; }

//validation layers pass debug messages to this function
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
                                           VkDebugUtilsMessageTypeFlagsEXT message_types,
                                           const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                                           void* user_data)
{
    (void)message_severity;
    (void)message_types;
    (void)user_data;

    std::fprintf(stderr, "[validation] %s\n", callback_data->pMessage);

    static std::ofstream log_file("validation.txt", std::ios::out | std::ios::trunc);

    if (log_file.is_open()) {
        log_file << "[validation] " << callback_data->pMessage << "\n";
        log_file << std::flush;
    }

    return VK_FALSE;
}

//set up our validation.txt file
static std::ofstream& ValidationLog()
{
    static std::ofstream log_file("validation.txt", std::ios::out | std::ios::trunc);
    return log_file;
}

//check if our physical device supports a certain device extension
static bool DeviceSupportsExtension(VkPhysicalDevice physical_device, const char* extension_name)
{
    uint32_t extension_count = 0;
    VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr));

    std::vector<VkExtensionProperties> extensions(extension_count);
    VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, extensions.data()));

    for (uint32_t i = 0; i < extension_count; ++i) {
        if (std::strcmp(extensions[i].extensionName, extension_name) == 0) {
            return true;
        }
    }

    return false;
}

//check if our Vulkan instance supports a certain instance extension
static bool InstanceSupportsExtension(const char* extension_name)
{
    uint32_t extension_count = 0;
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr));

    std::vector<VkExtensionProperties> extensions(extension_count);
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data()));

    for (uint32_t i = 0; i < extension_count; ++i) {
        if (std::strcmp(extensions[i].extensionName, extension_name) == 0) {
            return true;
        }
    }

    return false;
}

//check if a certain bit index is set in a mask
static bool BitIsSet(uint32_t mask, uint32_t bit_index)
{
    return (mask & (1u << bit_index)) != 0;
}

//helper function to interpret shader code as a uint32 vec
static std::vector<uint32_t> ReadSpirv(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        std::fprintf(stderr, "failed to open shader file: %s\n", std::filesystem::absolute(path).string().c_str());
        std::abort();
    }

    size_t size_in_bytes = (size_t)file.tellg();
    std::vector<uint32_t> code(size_in_bytes / sizeof(uint32_t));

    file.seekg(0);
    file.read((char*)code.data(), size_in_bytes);

    return code;
}

int main()
{
    //--------------------------------------------------------------------------------------//
    // WINDOW CREATION
    //--------------------------------------------------------------------------------------//

    //initialize glfw
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE; //EXIT_FAILURE resolves to 1
    }

    //disables OpenGL context creation
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    const char* window_title = "P3DG-PROJECT1";

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, window_title, nullptr, nullptr);

    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        return EXIT_FAILURE;
    }
    glfwSetScrollCallback(window, scroll_callback);

    ValidationLog() << "=== VALIDATION LOG: " << window_title << " ===\n" << std::flush;
    std::fprintf(stderr, "[info] validation log location: %s\n", std::filesystem::absolute("validation.txt").string().c_str());

    //--------------------------------------------------------------------------------------//
    // INSTANCE CREATION
    //--------------------------------------------------------------------------------------//

    //glfw needs certain vulkan instance extensions to be enabled to perform core functions like creating a window surface. we query for them here
    uint32_t glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    if (!glfw_extensions) {
        std::fprintf(stderr, "glfwGetRequiredInstanceExtensions failed (Vulkan not supported by GLFW on this platform)\n");
        return EXIT_FAILURE;
    }

    std::vector<const char*> instance_extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

    //let's add the debug extension so that our debug messanger works
    instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    //enable validation layers that create debug messages
    const char* enabled_layers[] = { "VK_LAYER_KHRONOS_validation" };

    //describe our debug messanger
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = NULL,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
        .pUserData = nullptr,
    };

    //let's enable synchronization validation to make sure our stage mask targets are correct
    instance_extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);

    VkBool32 validate_sync_enabled = VK_TRUE;

    VkLayerSettingEXT layer_settings[] = {
        {
            .pLayerName = "VK_LAYER_KHRONOS_validation",
            .pSettingName = "validate_sync",
            .type = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
            .valueCount = 1,
            .pValues = &validate_sync_enabled,
        },
    };

    VkLayerSettingsCreateInfoEXT layer_settings_create_info = {
        .sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
        .pNext = &debug_messenger_create_info,
        .settingCount = sizeof(layer_settings) / sizeof(layer_settings[0]),
        .pSettings = layer_settings,
    };

    VkInstanceCreateFlags instance_create_flags = 0;
    //mac support
    if (InstanceSupportsExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instance_create_flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    //describe our application
    VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = window_title,
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .pEngineName = "none",
        .engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),

        .apiVersion = VK_API_VERSION_1_3,
    };

    //describe our Vulkan instance
    VkInstanceCreateInfo instance_create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = &layer_settings_create_info,
        .flags = instance_create_flags,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = sizeof(enabled_layers) / sizeof(enabled_layers[0]),
        .ppEnabledLayerNames = enabled_layers,
        .enabledExtensionCount = (uint32_t)instance_extensions.size(),
        .ppEnabledExtensionNames = instance_extensions.data(),
    };

    //create our Vulkan instance
    VkInstance instance = VK_NULL_HANDLE;
    VkResult instance_creation_result = vkCreateInstance(&instance_create_info, nullptr, &instance);
    VK_CHECK(instance_creation_result);

    //since extension functions are not loaded in by default (after all, they are extensions), we have to query the function as a pointer in order to use it
    PFN_vkCreateDebugUtilsMessengerEXT pfn_create_debug_utils_messengerEXT =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT");

    //establish the debug messanger callback
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VK_CHECK(pfn_create_debug_utils_messengerEXT(instance, &debug_messenger_create_info, nullptr, &debug_messenger));

    //--------------------------------------------------------------------------------------//
    // SURFACE CREATION
    //--------------------------------------------------------------------------------------//
    
    //create the surface
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));

    //--------------------------------------------------------------------------------------//
    // PHYSICAL DEVICE SELECTION
    //--------------------------------------------------------------------------------------//

    //simply check the number of available physical devices (GPUs), don't get their data yet
    uint32_t physical_device_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr));
    if (physical_device_count == 0) {
        std::fprintf(stderr, "vkEnumeratePhysicalDevices failed (no Vulkan-capable devices found)\n");
        return EXIT_FAILURE;
    }
    
    //properly get the physical device data, look through them and find the best* one
    //"best" meaning preferably a dedicated GPU (as opposed to an integrated one on the CPU), and minimally must support version 1.3 of Vulkan 
    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices.data()));

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    uint32_t queue_family_index = UINT32_MAX;

    for (uint32_t i = 0; i < physical_device_count; ++i) {
        VkPhysicalDeviceProperties physical_device_properties = {};
        vkGetPhysicalDeviceProperties(physical_devices[i], &physical_device_properties);

        if (physical_device_properties.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }

        //devices have one or more queue families that support different pipelines
        //to keep it simple, let's grab a family that supports both the graphics pipeline and glfw surfaces 
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, queue_families.data());

        for (uint32_t j = 0; j < queue_family_count; ++j) {
            if (!(queue_families[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                continue;
            }

            VkBool32 present_surface_support = VK_FALSE;
            VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, surface, &present_surface_support));

            if (present_surface_support == VK_TRUE) {
                queue_family_index = j;
                break;
            }
        }

        if (queue_family_index == UINT32_MAX) {
            continue;
        }

        if (physical_device != VK_NULL_HANDLE && physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
            continue;
        }

        physical_device = physical_devices[i];

        if (physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            break;
        }
    }

    if (physical_device == VK_NULL_HANDLE) {
        std::fprintf(stderr, "No device supporting Vulkan 1.3 + present\n");
        return EXIT_FAILURE;
    }

    //--------------------------------------------------------------------------------------//
    // LOGICAL DEVICE & QUEUE CREATION
    //--------------------------------------------------------------------------------------//

    //queue priority is useful for > 1 queue but still necessary for == 1, ranges from 0.0-1.0
    //(it's used for prioritizing queues when scheduling)
    float queue_priority = 1.f;

    //describe our queue
    VkDeviceQueueCreateInfo device_queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    //note that simply being on vulkan 1.3 doesn't automatically turn 1.3 features on.
    //they default to VK_FALSE, so we enable them here and pass it to the logical device creation info
    VkPhysicalDeviceVulkan13Features vulkan_1_3_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = NULL,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE,
    };

    //presenting a surface (and by extension, using a swapchain) is not core Vulkan but rather a device extension
    std::vector<const char*> device_extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    //mac support
    if (DeviceSupportsExtension(physical_device, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }

    //describe our logical device that serves as the interface between our Vulkan instance and our physical device (GPU)
    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan_1_3_features,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &device_queue_create_info,
        .enabledExtensionCount = (uint32_t)device_extensions.size(),
        .ppEnabledExtensionNames = device_extensions.data(),
    };

    //create our logical device and its queues 
    VkDevice device = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDevice(physical_device, &device_create_info, nullptr, &device));

    //retrieve the handle of the queue generated with the logical device
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_index = 0;
    vkGetDeviceQueue(device, queue_family_index, queue_index, &queue);

    //--------------------------------------------------------------------------------------//
    // SWAPCHAIN CREATION
    //--------------------------------------------------------------------------------------//

    //let's check if our physical device supports the transfer op we're going to use later
    //(don't worry, it practically always is)
    VkSurfaceCapabilitiesKHR surface_capabilities = {};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities));

    //VK_IMAGE_USAGE_TRANSFER_DST_BIT is the flag that signifies the image can be used as the destination for a transfer op
    if (!(surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        std::fprintf(stderr, "Surface does not support VK_IMAGE_USAGE_TRANSFER_DST_BIT\n");
        return EXIT_FAILURE;
    }

    //query the number of supported surface (image) formats first before fetching them
    uint32_t surface_format_count = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &surface_format_count, nullptr));

    //...then fetch them
    std::vector<VkSurfaceFormatKHR> surface_formats(surface_format_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &surface_format_count, surface_formats.data()));

    //for our purposes, this is the format and color space we want. in case it isn't there, we grab the first one
    //at least one format is guaranteed to exist
    VkSurfaceFormatKHR surface_format = surface_formats[0];
    for (uint32_t i = 0; i < surface_format_count; ++i) {
        if (surface_formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            surface_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format = surface_formats[i];
            break;
        }
    }

    //device will support up to a specific surface size, or any size (UINT32_MAX). 
    //since we presumably get to choose, let's just use the framebuffer dimensions
    VkExtent2D swapchain_extent = surface_capabilities.currentExtent;
    if (swapchain_extent.width == UINT32_MAX) {
        int framebuffer_width = 0;
        int framebuffer_height = 0;
        glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
        swapchain_extent.width = (uint32_t)framebuffer_width;
        swapchain_extent.height = (uint32_t)framebuffer_height;
    }

    //minImageCount can be as low as 1...
    //let's make sure there are at least two images to use in our swapchain to "swap" betweeen
    uint32_t swapchain_min_image_count = surface_capabilities.minImageCount + 1;
    if (surface_capabilities.maxImageCount > 0 && swapchain_min_image_count > surface_capabilities.maxImageCount) {
        swapchain_min_image_count = surface_capabilities.maxImageCount;
    }

    //describe our swapchain
    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = surface,
        .minImageCount = swapchain_min_image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = swapchain_extent,
        //more than 1 only used for stereoscopic rendering
        .imageArrayLayers = 1,
        //transfer dst is here bc we clear via a transfer command
        //color attachment won't be used until we have a fully-fledged pipeline
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        //FIFO is guaranteed to be supported
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, 
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    //create our swapchain
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_create_info, nullptr, &swapchain));

    //query number of images in the swapchain we just made
    uint32_t swapchain_image_count = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr));
    
    //grab the images the swapchain made - we do not generate them ourselves
    std::vector<VkImage> swapchain_images(swapchain_image_count);
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images.data()));

    //--------------------------------------------------------------------------------------//
    // COMMAND POOL CREATION & COMMAND BUFFER ALLOCATION 
    //--------------------------------------------------------------------------------------//

    //describe our command pool
    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        //this flag enables vkResetCommandBuffer which we need because we are re-recording the command buffers on each frame
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        //a command pool is bound to (& effectively owned by) one queue family.
        //its buffers can only be submitted to queues in that family
        .queueFamilyIndex = queue_family_index,
    };

    //create our command pool
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(device, &command_pool_create_info, nullptr, &command_pool));

    //describe the buffer(s) we are going to create
    VkCommandBufferAllocateInfo command_buffer_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = FRAMES_IN_FLIGHT,
    };

    //allocate (create) command buffers for the pool we've made
    std::vector<VkCommandBuffer> command_buffers(FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_allocate_info, command_buffers.data()));

    //--------------------------------------------------------------------------------------//
    // IMAGE ALLOCATION
    //--------------------------------------------------------------------------------------//

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {
            .width = FRAME_WIDTH,
            .height = FRAME_HEIGHT,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,//SRC -> blit, STORAGE -> img view
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    std::vector<VkImage> frame(FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateImage(device, &image_info, nullptr, &frame[i])); //creates the VkImage handle (equivalent to VkBuffer)
    }

    VkPhysicalDeviceMemoryProperties memory_properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    VkMemoryRequirements memory_requirements = {};
    vkGetImageMemoryRequirements(device, frame[0], &memory_requirements);
    VkMemoryPropertyFlags property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    uint32_t memory_type_index = UINT32_MAX;
    uint32_t allowed_memory_type_bits = memory_requirements.memoryTypeBits;

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        bool type_allowed = BitIsSet(allowed_memory_type_bits, i);
        bool has_properties = (memory_properties.memoryTypes[i].propertyFlags & property_flags) == property_flags;

        if (type_allowed && has_properties) {
            memory_type_index = i;
            break;
        }
    }

    if (memory_type_index == UINT32_MAX) {
        std::fprintf(stderr, "No host-visible coherent memory type available\n");
        return EXIT_FAILURE;
    }

    VkDeviceSize stride = ((memory_requirements.size + memory_requirements.alignment - 1) / memory_requirements.alignment) * memory_requirements.alignment;

    VkDeviceSize frame_size = stride * FRAMES_IN_FLIGHT;

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = frame_size,
        .memoryTypeIndex = memory_type_index,
    };

    VkDeviceMemory frame_memory = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateMemory(device, &alloc_info, nullptr, &frame_memory));

    std::vector<VkImageView> frame_view(FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VkDeviceSize bind_offset = i * stride;
        VK_CHECK(vkBindImageMemory(device, frame[i], frame_memory, bind_offset)); //binds VkImage handle to VkMemory block

        //to create the image view...
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = frame[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &frame_view[i])); //creates the VkImageView handle
    }

    VkDescriptorPoolSize descriptor_pool_size = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = FRAMES_IN_FLIGHT,
    };

    VkDescriptorPoolCreateInfo descriptor_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .maxSets = FRAMES_IN_FLIGHT,
        .poolSizeCount = 1,
        .pPoolSizes = &descriptor_pool_size,
    };

    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device, &descriptor_pool_info, NULL, &descriptor_pool));

    VkDescriptorSetLayoutBinding descriptor_set_layout_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .bindingCount = 1,
        .pBindings = &descriptor_set_layout_binding,
    };

    std::vector<VkDescriptorSetLayout> descriptor_set_layout(FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {  
        VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptor_set_layout_info, NULL, &descriptor_set_layout[i]));
    }

    VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = NULL,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = FRAMES_IN_FLIGHT,
        .pSetLayouts = descriptor_set_layout.data(),
    };

    std::vector<VkDescriptorSet> descriptor_set(FRAMES_IN_FLIGHT);
    VK_CHECK(vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, descriptor_set.data()));

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorImageInfo descriptor_image_info = {
            .sampler = VK_NULL_HANDLE,
            .imageView = frame_view[i],
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };

        VkWriteDescriptorSet write_descriptor_set = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = NULL,
            .dstSet = descriptor_set[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &descriptor_image_info,
        };

        vkUpdateDescriptorSets(device, 1, &write_descriptor_set, 0, nullptr);
    }

    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .setLayoutCount = FRAMES_IN_FLIGHT,
        .pSetLayouts = descriptor_set_layout.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline_layout));

    std::vector<uint32_t> compute_code = ReadSpirv(COMPUTE_SHADER_PATH);

    VkShaderModuleCreateInfo shader_module_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .codeSize = compute_code.size() * sizeof(uint32_t),
        .pCode = compute_code.data(),
    };

    VkShaderModule compute_shader_module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &shader_module_info, nullptr, &compute_shader_module));

    VkPipelineShaderStageCreateInfo pipeline_shader_stage_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = compute_shader_module,
        .pName = "main",
    };

    VkComputePipelineCreateInfo compute_pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stage = pipeline_shader_stage_info,
        .layout = pipeline_layout,
    };

    VkPipeline compute_pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute_pipeline_info, NULL, &compute_pipeline));

    //--------------------------------------------------------------------------------------//
    // SYNCHRONIZATION OBJECT CREATION
    //--------------------------------------------------------------------------------------//

    VkSemaphoreCreateInfo semaphore_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_SEMAPHORE_TYPE_BINARY,
    };

    VkFenceCreateInfo command_buffer_fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        // changed 
        .flags = 0,
    };

    std::vector<VkSemaphore> image_available_semaphore(FRAMES_IN_FLIGHT);
    std::vector<VkSemaphore> render_finished_semaphore(swapchain_image_count);
    std::vector<VkFence> in_flight_fence(FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(device, &semaphore_create_info, nullptr, &image_available_semaphore[i]));
        VK_CHECK(vkCreateFence(device, &command_buffer_fence_info, nullptr, &in_flight_fence[i]));
    }

    for (uint32_t i = 0; i < swapchain_image_count; ++i) {
        VK_CHECK(vkCreateSemaphore(device, &semaphore_create_info, nullptr, &render_finished_semaphore[i]));
    }

    //--------------------------------------------------------------------------------------//
    // FRAME LOOP
    //--------------------------------------------------------------------------------------//

    //--------------------------------------------------------------------------------------//
    // PART A: MOVE UNDEFINED -> GENERAL OUTSIDE THE LOOP
    //--------------------------------------------------------------------------------------//
    // Last assignment I stated that it was redundant to have the initial UNDEFINED -> GENERAL
    // image barrier for every frame, since we are only ever in UNDEFINED at the very start of
    // the program. We ought to now move it out of the main loop, to occur once right before
    // the loop kicks off. If you recall, I stated in the A3 review that stating your image
    // format is UNDEFINED tells Vulkan it can freely discard any data within it. If you
    // choose to implement Feature1, where reading from a previous frame is required, this
    // image barrier existing in the main loop suddenly becomes detrimental, because we
    // absolutely do not want to discard previous frame data.
    //
    // Relevant structures: VkCommandBufferBeginInfo, VkImageSubresourceRange,
    //                      VkImageMemoryBarrier2, VkDependencyInfo,
    //                      VkCommandBufferSubmitInfo, VkSubmitInfo2
    //
    // Relevant functions:  vkResetCommandBuffer, vkBeginCommandBuffer, vkCmdPipelineBarrier2,
    //                      vkEndCommandBuffer, vkQueueSubmit2
    //
    // Hints:   To record and submit the image barrier, you must go through the whole process
    //          involved with recording to a command buffer, and submitting that buffer.
    //
    //          Make sure this image transition happens for all FRAMES_IN_FLIGHT images.
    //
    //          We don't need to wait on any semaphores for this operation. VkDependencyInfo
    //          should reflect that.
    //
    //          We don't need to signal any semaphores for this operation. Fences, on the
    //          other hand... In which case, should our fences still initialize as signaled?

    //--------------------------------------------------------------------------------------//
    // FEATURE1: TEMPORAL ACCUMULATION
    //--------------------------------------------------------------------------------------//
    // If you would like to use the previous frame's data to inform your current frame, there
    // may be several roadblocks you run into. The primary concern is already somewhat handled
    // by our FRAMES_IN_FLIGHT setup - that being, we cannot write to the same image we are
    // reading from. Typically, if a solution wasn't already present, you would be pointed
    // towards the concept of ping-pong buffers. The idea is that you have two images A and B,
    // and on frame N you read data from image A and write your output to image B, then on
    // frame N+1 you alternate to read from image B and write over image A. Currently we have
    // images A B and C since FRAMES_IN_FLIGHT = 3, but I would recommend you lower it to 2,
    // in order to easily replicate this described model.
    //
    // Even though we have this largely implemented already, you are currently sending in one
    // image to your shader. This is reflected in your descriptor set mapping, and our image
    // memory barriers. Both of these will have to change to accommodate the new setup. If we
    // pass in both images every frame, then we need to perform layout transitions for both
    // images every frame. Additionally, they can be easily slotted into one descriptor set
    // as two descriptors of the same type.
    //
    // You ought to pass in current_frame as a push constant so that your shader knows which
    // array index to read from and which to write to.

    //--------------------------------------------------------------------------------------//
    // FEATURE2: CURSOR IMAGE TRANSFORMATION
    //--------------------------------------------------------------------------------------//
    // A novel way to create interactivity with your shader art would be to have it transform
    // in some way based on mouse information. Specifically, this feature necessitates you use
    // cursor position to inform the final image, but optionally, utilizing mouse clicks may
    // greatly widen the possibilities of your implementation.
    //
    // Using the mouse/cursor to zoom into your image or pan around a zoomed image does NOT
    // qualify for this feature. Your art actually has to change based on mouse behavior. In
    // the demo video, I made holding the cursor enable my fern to sway with the wind, while
    // the position of my cursor influenced how the direction of the wind. While admittedly
    // playing it a little loose with the cursor position implementation, I still consider
    // this sufficient for the requirement. The key thing is that the position of the cursor
    // actually mapped to something positional about my art. Like, there is a
    // logical/intuitive connection to be made between moving the cursor around and seeing
    // the fern react and sway differently. What would not suffice is to map some arbitrary
    // non-positional value to the cursor position and call it a day. Unless it looks REALLY
    // cool. Just don't be lazy.
    //
    // To get cursor position (and more generally, input data) you need to use glfw. For
    // cursor position in particular, use glfwGetCursorPos which takes a pointer to the glfw
    // window (called "window") and by reference two floats, since it provides subpixel
    // precision. You can easily cast these to ints (or keep them as floats if you please) and
    // send them to your shader as push constants.
    //
    // I'm feeling generous so I will also give you an instance of keystroke checking using
    // glfw. It's for the space bar. The logic checks if the space bar was pressed down on
    // that particular frame, and does not signal multiple times when held. You are not forced
    // to use it.

    //--------------------------------------------------------------------------------------//
    // FEATURE3: DEPTH OF DETAIL
    //--------------------------------------------------------------------------------------//
    // The premise for this feature is simple - create practically infinite detail in your
    // image generation, such that a camera can continuously zoom into your generation and not
    // lose resolution or image clarity. This will work if your compute generation is
    // mathematically formulaic, e.g., a fractal, but it can also work for other project types
    // if you're creative enough.
    //
    // The primary mechanism to achieve this would be by having the image be a function of
    // coordinates rather than discrete pixels, continuously evaluated based on the position
    // of a viewing camera. Your viewing camera can simply be a center point and a scale, and
    // your shader can map from pixel coordinates to worldspace coordinates, and vice versa.
    // You would have to do your calculations, in general, in that worldspace coordinate
    // plane, then translate your results to pixel coordinates. You can still dispatch one
    // thread per pixel, as is currently done, since your frame dimensions aren't changing.
    //
    // This feature may be particularly hard to pair with Feature1 for a few reasons, but
    // chiefly because the data we read from the prior image is in pixels. If you also store
    // the camera data associated with that image, you could reverse the calculation back
    // into worldspace coordinates. If you are zooming out or panning, then you cannot sample
    // from a part of the image that did not previously exist, so a solution would have to be
    // devised for that as well. If it doesn't break your intended result, simply resetting
    // the image or choosing not to do temporal accumulation during camera movement could be
    // an easy solution.
    //
    // To be clear, the zoom seen in my fern demo would NOT suffice for this feature. That did
    // not resolve new detail and was just a simple crop of the blit. I imagine your
    // implementation of this feature would leave the blit dimensions alone.

    VkImageSubresourceRange subresource_range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    // One-time UNDEFINED -> GENERAL transition for every frame image
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkResetCommandBuffer(command_buffers[i], 0));

        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        VK_CHECK(vkBeginCommandBuffer(command_buffers[i], &begin_info));

        VkImageMemoryBarrier2 init_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = frame[i],
            .subresourceRange = subresource_range,
        };

        VkDependencyInfo init_dependency = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &init_barrier,
        };
        vkCmdPipelineBarrier2(command_buffers[i], &init_dependency);

        VK_CHECK(vkEndCommandBuffer(command_buffers[i]));

        VkCommandBufferSubmitInfo init_cb_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = command_buffers[i],
        };
        VkSubmitInfo2 init_submit = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &init_cb_info,
        };
        VK_CHECK(vkQueueSubmit2(queue, 1, &init_submit, in_flight_fence[i]));
    }

    bool space_pressed = false;

    const float DEFAULT_SCALE = 4.0f / FRAME_HEIGHT; // view is 4 units tall
    float center_x = 0.f, center_y = 0.f, scale = DEFAULT_SCALE;
    float root_x = 1.f, root_y = 0.f;

    uint32_t current_frame = 0;
    uint64_t total_frames = 0;
    std::chrono::milliseconds start_ms = duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    std::chrono::milliseconds curr_ms, end_ms;
    std::chrono::milliseconds last_frame_ms = start_ms;
    std::chrono::milliseconds delta_ms = start_ms;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        vkWaitForFences(device, 1, &in_flight_fence[current_frame], VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &in_flight_fence[current_frame]);

        uint32_t image_index = 0;
        VkResult acquire_result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available_semaphore[current_frame], VK_NULL_HANDLE, &image_index);

        if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
            std::fprintf(stderr, "vkAcquireNextImageKHR failed with VkResult %d\n", acquire_result);
            return EXIT_FAILURE;
        }

        VK_CHECK(vkResetCommandBuffer(command_buffers[current_frame], 0));

        VkCommandBufferBeginInfo command_buffer_begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };

        VK_CHECK(vkBeginCommandBuffer(command_buffers[current_frame], &command_buffer_begin_info));


        // delta_ms starts out as start_ms (huge), so clamp for the first frame
        float dt = fminf((float)delta_ms.count() / 1000.f, 0.1f);

        double cur_x, cur_y;
        glfwGetCursorPos(window, &cur_x, &cur_y);
        // window coords -> frame pixels (safe on high-DPI too)
        float px = (float)(cur_x / WINDOW_WIDTH)  * FRAME_WIDTH;
        float py = (float)(cur_y / WINDOW_HEIGHT) * FRAME_HEIGHT;
        float cursor_wx = center_x + (px - FRAME_WIDTH  * 0.5f) * scale;
        float cursor_wy = center_y - (py - FRAME_HEIGHT * 0.5f) * scale;

        // zoom toward the cursor
        if (g_scroll_y != 0.0) {
            float new_scale = scale * powf(0.85f, (float)g_scroll_y);
            float ratio = new_scale / scale;
            center_x = cursor_wx + (center_x - cursor_wx) * ratio;
            center_y = cursor_wy + (center_y - cursor_wy) * ratio;
            scale = new_scale;
            g_scroll_y = 0.0;
        }

        // pan
        float pan = 300.f * scale * dt;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) center_y += pan;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) center_y -= pan;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) center_x -= pan;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) center_x += pan;

        // Feature 2: drag root r0 with the cursor
        double root_x = 1e9, root_y = 0.0;

        bool cursor_inside = cur_x >= 0.0 && cur_x < WINDOW_WIDTH &&
                     cur_y >= 0.0 && cur_y < WINDOW_HEIGHT;

        if (cursor_inside) {
            double nx =  (cur_x / WINDOW_WIDTH)  * 2.0 - 1.0;
            double ny = -((cur_y / WINDOW_HEIGHT) * 2.0 - 1.0);
            root_x = 1.0 + 0.6 * nx;
            root_y = 0.8 * ny;
        }else{
            root_x = 1.0; root_y = 0.0;
        }

        // space: reset
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && !space_pressed) {
            center_x = center_y = 0.f;
            scale = DEFAULT_SCALE;
            root_x = 1.0f; root_y = 0.f;
        }
        space_pressed = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);

        int max_iter = 100 + (int)(30.f * log2f(DEFAULT_SCALE / scale));
        if (max_iter < 100) max_iter = 100;
        if (max_iter > 1000) max_iter = 1000;

        PushConstants pc = {
            (int)FRAME_WIDTH, (int)FRAME_HEIGHT,
            center_x, center_y, scale,
            root_x, root_y,
            max_iter
        };

        vkCmdBindPipeline(command_buffers[current_frame], VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);
        vkCmdBindDescriptorSets(command_buffers[current_frame], VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set[current_frame], 0, nullptr);
        vkCmdPushConstants(command_buffers[current_frame], pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(command_buffers[current_frame], (FRAME_WIDTH + 15) / 16, (FRAME_HEIGHT + 15) / 16, 1);

        VkImageMemoryBarrier2 barrier_storage_to_transfer_src = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,

            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,

            .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,

            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,

            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

            .image = frame[current_frame],
            .subresourceRange = subresource_range,
        };

        VkImageMemoryBarrier2 barrier_to_swapchain_transfer_dst = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,

            .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,

            .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,

            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            
            .image = swapchain_images[image_index],
            .subresourceRange = subresource_range,
        };

        VkImageMemoryBarrier2 barriers_before_blit[2] = {barrier_storage_to_transfer_src, barrier_to_swapchain_transfer_dst};

        VkDependencyInfo dependency_info_before_blit = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 2,
            .pImageMemoryBarriers = barriers_before_blit,
        };

        vkCmdPipelineBarrier2(command_buffers[current_frame], &dependency_info_before_blit);

        VkImageBlit image_blit = {
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .srcOffsets = { {0, 0, 0}, {FRAME_WIDTH, FRAME_HEIGHT, 1} },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .dstOffsets = { {0, 0, 0}, {(int32_t)swapchain_extent.width, (int32_t)swapchain_extent.height, 1} },
        };

        vkCmdBlitImage(command_buffers[current_frame], frame[current_frame], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchain_images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_blit, VK_FILTER_LINEAR);

        VkImageMemoryBarrier2 barrier_to_present = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,

            .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,

            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
            .dstAccessMask = VK_ACCESS_2_NONE,

            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,

            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

            .image = swapchain_images[image_index],
            .subresourceRange = subresource_range,
        };

        VkImageMemoryBarrier2 barrier_to_general = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,

            .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,

            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,

            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,

            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
 
            .image = frame[current_frame],
            .subresourceRange = subresource_range,
        };

        VkImageMemoryBarrier2 barriers_after_blit[2] = {barrier_to_present, barrier_to_general};

        VkDependencyInfo dependency_info_to_present = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 2,
            .pImageMemoryBarriers = barriers_after_blit,
        };

        vkCmdPipelineBarrier2(command_buffers[current_frame], &dependency_info_to_present);

        VK_CHECK(vkEndCommandBuffer(command_buffers[current_frame]));

        VkSemaphoreSubmitInfo wait_semaphore_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = NULL,
            .semaphore = image_available_semaphore[current_frame],
            .value = 0,
            .stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            .deviceIndex = 0,
        };

        VkCommandBufferSubmitInfo command_buffer_submit_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext = NULL,
            .commandBuffer = command_buffers[current_frame],
            .deviceMask = 0,
        };

        VkSemaphoreSubmitInfo signal_semaphore_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = NULL,
            .semaphore = render_finished_semaphore[image_index],
            .value = 0,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0,
        };

        VkSubmitInfo2 submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = NULL,
            .flags = 0,
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &wait_semaphore_info,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &command_buffer_submit_info,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signal_semaphore_info,
        };

        VK_CHECK(vkQueueSubmit2(queue, 1, &submit_info, in_flight_fence[current_frame]));

        VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = NULL,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &render_finished_semaphore[image_index],
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &image_index,
            .pResults = nullptr,
        };

        VkResult present_result = vkQueuePresentKHR(queue, &present_info);
        if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR) {
            std::fprintf(stderr, "vkQueuePresentKHR failed with VkResult %d\n", present_result);
            return EXIT_FAILURE;
        }

        curr_ms = duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
        delta_ms = curr_ms - last_frame_ms;
        last_frame_ms = curr_ms;
        current_frame = (current_frame + 1) % FRAMES_IN_FLIGHT;
        total_frames++;
    }

    //let's wait for everything to finish on the host side before cleaning up or finalizing timers
    VK_CHECK(vkDeviceWaitIdle(device));

    end_ms = duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    delta_ms = end_ms - start_ms;

    double fps = (double)total_frames / (delta_ms.count() / 1000.0);
    std::printf("Avg FPS: %i\n", int(fps));

    //destruction generally goes in reverse order of creation

    for (uint32_t i = 0; i < swapchain_image_count; ++i) {
        vkDestroySemaphore(device, render_finished_semaphore[i], nullptr);
    }

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(device, image_available_semaphore[i], nullptr);
        vkDestroyFence(device, in_flight_fence[i], nullptr);
    }

    vkDestroyPipeline(device, compute_pipeline, nullptr);
    
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);

    vkDestroyShaderModule(device, compute_shader_module, nullptr);

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        vkDestroyDescriptorSetLayout(device, descriptor_set_layout[i], nullptr);
    }

    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);

    vkDestroyCommandPool(device, command_pool, nullptr);

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        vkDestroyImageView(device, frame_view[i], nullptr);
        vkDestroyImage(device, frame[i], nullptr);
    }

    vkFreeMemory(device, frame_memory, nullptr);

    vkDestroySwapchainKHR(device, swapchain, nullptr);

    vkDestroyDevice(device, nullptr);

    vkDestroySurfaceKHR(instance, surface, nullptr);

    PFN_vkDestroyDebugUtilsMessengerEXT pfn_destroy_debug_utils_messengerEXT =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
    pfn_destroy_debug_utils_messengerEXT(instance, debug_messenger, nullptr);

    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
