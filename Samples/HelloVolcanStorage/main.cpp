#include "volcanstorage/volcanstorage.h"

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <cassert>

using namespace volcanstorage;

// Helper to select a memory type on Vulkan physical device
uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    return 0;
}

int main(int argc, char* argv[])
{
    std::cout << "=================================================" << std::endl;
    std::cout << "      HelloVolcanStorage (Vulkan DirectStorage)   " << std::endl;
    std::cout << "=================================================" << std::endl;

    // 1. Prepare sample test asset file
    std::string testFilePath = "volcan_sample_asset.bin";
    if (argc >= 2)
    {
        testFilePath = argv[1];
    }
    else
    {
        std::ofstream out(testFilePath, std::ios::binary);
        const std::string sampleText = "Hello from VolcanStorage! High-performance asynchronous GPU streaming using Vulkan.";
        for (int i = 0; i < 256; ++i)
        {
            out.write(sampleText.c_str(), sampleText.size());
            out.put('\n');
        }
        out.close();
        std::cout << "[Info] Created demo asset file: " << testFilePath << std::endl;
    }

    // 2. Initialize Vulkan Instance
    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "HelloVolcanStorage";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VolcanStorage";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
    {
        std::cerr << "[Error] Failed to create Vulkan Instance!" << std::endl;
        return -1;
    }
    std::cout << "[Vulkan] Instance initialized successfully." << std::endl;

    // 3. Pick Physical Device
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        std::cerr << "[Error] No Vulkan physical devices found!" << std::endl;
        vkDestroyInstance(instance, nullptr);
        return -1;
    }

    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());
    VkPhysicalDevice physicalDevice = physicalDevices[0];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    std::cout << "[Vulkan] Using GPU: " << props.deviceName << std::endl;

    // 4. Find Queue Family
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    uint32_t transferQueueIndex = 0;
    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        if (queueFamilies[i].queueFlags & (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_GRAPHICS_BIT))
        {
            transferQueueIndex = i;
            break;
        }
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueCreateInfo.queueFamilyIndex = transferQueueIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS)
    {
        std::cerr << "[Error] Failed to create Vulkan logical device!" << std::endl;
        vkDestroyInstance(instance, nullptr);
        return -1;
    }

    VkQueue transferQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, transferQueueIndex, 0, &transferQueue);

    // 5. Initialize VolcanStorage
    IVolcanStorageFactory* factory = nullptr;
    if (VolcanStorageGetFactory(&factory) != VK_SUCCESS || !factory)
    {
        std::cerr << "[Error] Failed to obtain VolcanStorage Factory!" << std::endl;
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return -1;
    }

    IVolcanStorageFile* file = nullptr;
    if (factory->OpenFile(testFilePath.c_str(), &file) != VK_SUCCESS || !file)
    {
        std::cerr << "[Error] Failed to open file via VolcanStorage: " << testFilePath << std::endl;
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return -1;
    }

    FileInformation info = file->GetInformation();
    std::cout << "[VolcanStorage] Opened file: " << testFilePath << " (Size: " << info.FileSize << " bytes)" << std::endl;

    // 6. Create Destination VkBuffer (Host-visible destination to verify bytes)
    VkBuffer destBuffer = VK_NULL_HANDLE;
    VkDeviceMemory destMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo bufInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufInfo.size = info.FileSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device, &bufInfo, nullptr, &destBuffer);

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, destBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(
        physicalDevice,
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    vkAllocateMemory(device, &allocInfo, nullptr, &destMemory);
    vkBindBufferMemory(device, destBuffer, destMemory, 0);

    // 7. Create VolcanStorage Queue
    QueueDesc qDesc{};
    qDesc.Capacity = 64;
    qDesc.QueuePriority = Priority::Normal;
    qDesc.Device = device;
    qDesc.PhysicalDevice = physicalDevice;
    qDesc.TransferQueue = transferQueue;
    qDesc.TransferQueueFamilyIndex = transferQueueIndex;

    IVolcanStorageQueue* queue = nullptr;
    if (factory->CreateQueue(qDesc, &queue) != VK_SUCCESS || !queue)
    {
        std::cerr << "[Error] Failed to create VolcanStorage Queue!" << std::endl;
        file->Close();
        vkDestroyBuffer(device, destBuffer, nullptr);
        vkFreeMemory(device, destMemory, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        return -1;
    }

    // Display Hardware Capability Probe & Graceful Downgrade Tier
    VolcanDeviceCapabilities caps = queue->GetCapabilities();
    std::cout << "[VolcanStorage Hardware Probe]" << std::endl;
    std::cout << "  - Vulkan API: " << VK_VERSION_MAJOR(caps.ApiVersion) << "." << VK_VERSION_MINOR(caps.ApiVersion) << "." << VK_VERSION_PATCH(caps.ApiVersion) << std::endl;
    std::cout << "  - Active Feature Tier: " << (caps.ActiveTier == FeatureTier::Tier3_Modern ? "Tier 3 (Modern Vulkan 1.3/1.4 - Sync2)" : (caps.ActiveTier == FeatureTier::Tier2_Standard ? "Tier 2 (Standard Vulkan 1.2 - Timeline)" : "Tier 1 (Legacy Vulkan 1.1 - Fallback)")) << std::endl;
    std::cout << "  - Timeline Semaphores (DirectStorage Fence): " << (caps.HasTimelineSemaphores ? "Supported" : "Fallback (Binary Fence)") << std::endl;
    std::cout << "  - Synchronization2 (vkCmdPipelineBarrier2): " << (caps.HasSynchronization2 ? "Supported" : "Fallback (PipelineBarrier1)") << std::endl;
    std::cout << "  - GPU Architecture: " << (caps.IsUnifiedMemoryArchitecture ? "UMA (Apple Silicon / ARM SoC / APU)" : "Discrete PCIe GPU") << std::endl;
    std::cout << "  - Resizable BAR (ReBAR): " << (caps.HasResizableBAR ? "Enabled (Host-Visible VRAM)" : "Disabled / Standard VRAM") << std::endl;
    std::cout << "  - Direct Zero-Copy Streaming: " << (caps.SupportsDirectGpuZeroCopy ? "Supported" : "Staging Buffer Pipeline") << std::endl;

    // 8. Enqueue Async Streaming Request
    Request req{};
    req.DestType = DestinationType::Buffer;
    req.SourceFile = file;
    req.SourceOffset = 0;
    req.SourceSize = static_cast<uint32_t>(info.FileSize);
    req.DestinationBuffer = destBuffer;
    req.DestinationBufferOffset = 0;
    req.DestinationSize = static_cast<uint32_t>(info.FileSize);
    req.Compression = CompressionFormat::None;

    std::cout << "[VolcanStorage] Enqueuing request..." << std::endl;
    queue->EnqueueRequest(req);

    std::cout << "[VolcanStorage] Submitting queue and waiting for completion..." << std::endl;
    queue->Submit();
    queue->WaitIdle();

    std::cout << "[VolcanStorage] Asynchronous transfer to Vulkan buffer completed successfully!" << std::endl;

    // 9. Verify Loaded Content from Destination Buffer
    void* mappedData = nullptr;
    vkMapMemory(device, destMemory, 0, info.FileSize, 0, &mappedData);

    char preview[65] = { 0 };
    std::memcpy(preview, mappedData, std::min<size_t>(64, static_cast<size_t>(info.FileSize)));
    std::cout << "[Verification] First 64 bytes in destination VkBuffer: \"" << preview << "\"" << std::endl;

    vkUnmapMemory(device, destMemory);

    // 10. Clean up
    delete queue;
    file->Close();
    delete file;

    vkDestroyBuffer(device, destBuffer, nullptr);
    vkFreeMemory(device, destMemory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    std::cout << "[Success] VolcanStorage demonstrated successfully!" << std::endl;
    return 0;
}
