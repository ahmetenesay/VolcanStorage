#include "volcanstorage/volcanstorage.h"

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <system_error>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #ifdef DeviceCapabilities
        #undef DeviceCapabilities
    #endif
#else
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

#if defined(VOLCANSTORAGE_HAS_GDEFLATE)
    #include "GDeflate.h"
#endif

namespace volcanstorage
{

// -----------------------------------------------------------------------------
// File Implementation
// -----------------------------------------------------------------------------
class VolcanStorageFileImpl : public IVolcanStorageFile
{
public:
#if defined(_WIN32)
    HANDLE m_fileHandle{ INVALID_HANDLE_VALUE };
#else
    int m_fileDesc{ -1 };
#endif
    FileInformation m_info{};

    ~VolcanStorageFileImpl() override
    {
        Close();
    }

    FileInformation GetInformation() const override
    {
        return m_info;
    }

    void Close() override
    {
#if defined(_WIN32)
        if (m_fileHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_fileHandle);
            m_fileHandle = INVALID_HANDLE_VALUE;
        }
#else
        if (m_fileDesc != -1)
        {
            close(m_fileDesc);
            m_fileDesc = -1;
        }
#endif
    }

    bool ReadAsync(uint64_t offset, uint32_t size, void* destinationBuffer)
    {
#if defined(_WIN32)
        if (m_fileHandle == INVALID_HANDLE_VALUE)
            return false;

        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        overlapped.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFF);

        DWORD bytesRead = 0;
        BOOL result = ReadFile(m_fileHandle, destinationBuffer, size, &bytesRead, &overlapped);
        if (!result && GetLastError() == ERROR_IO_PENDING)
        {
            result = GetOverlappedResult(m_fileHandle, &overlapped, &bytesRead, TRUE);
        }
        return (result != FALSE) && (bytesRead == size);
#else
        if (m_fileDesc == -1)
            return false;

        ssize_t res = pread(m_fileDesc, destinationBuffer, size, static_cast<off_t>(offset));
        return (res == static_cast<ssize_t>(size));
#endif
    }
};

// -----------------------------------------------------------------------------
// Vulkan Staging Memory Helper
// -----------------------------------------------------------------------------
static uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
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

// -----------------------------------------------------------------------------
// Queue Implementation
// -----------------------------------------------------------------------------
class VolcanStorageQueueImpl : public IVolcanStorageQueue
{
public:
    QueueDesc m_desc;
    VkCommandPool m_commandPool{ VK_NULL_HANDLE };

    struct QueuedTask
    {
        Request req;
        VkFence signalFence{ VK_NULL_HANDLE };
        VkSemaphore signalSemaphore{ VK_NULL_HANDLE };
        VkSemaphore timelineSemaphore{ VK_NULL_HANDLE };
        uint64_t timelineValue{ 0 };
        bool isSignalOnly{ false };
    };

    VolcanDeviceCapabilities m_caps{};
    std::queue<QueuedTask> m_pendingTasks;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::thread m_workerThread;
    std::atomic<bool> m_running{ true };

    // Persistent Staging Ring-Buffer Pool (eliminates per-request allocation latency)
    VkBuffer m_stagingPoolBuffer{ VK_NULL_HANDLE };
    VkDeviceMemory m_stagingPoolMemory{ VK_NULL_HANDLE };
    void* m_stagingPoolMapped{ nullptr };
    uint32_t m_stagingPoolSize{ 0 };
    uint32_t m_stagingPoolHead{ 0 };

    VolcanStorageQueueImpl(const QueueDesc& desc)
        : m_desc(desc)
    {
        ProbeCapabilities();

        VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.queueFamilyIndex = m_desc.TransferQueueFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkCreateCommandPool(m_desc.Device, &poolInfo, nullptr, &m_commandPool);

        // Initialize persistent staging ring-buffer pool
        m_stagingPoolSize = (m_desc.StagingBufferSize > 0) ? m_desc.StagingBufferSize : (64 * 1024 * 1024);
        VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = m_stagingPoolSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_desc.Device, &bufferInfo, nullptr, &m_stagingPoolBuffer) == VK_SUCCESS)
        {
            VkMemoryRequirements memReqs;
            vkGetBufferMemoryRequirements(m_desc.Device, m_stagingPoolBuffer, &memReqs);

            VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocInfo.allocationSize = memReqs.size;
            allocInfo.memoryTypeIndex = FindMemoryType(
                m_desc.PhysicalDevice,
                memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            if (vkAllocateMemory(m_desc.Device, &allocInfo, nullptr, &m_stagingPoolMemory) == VK_SUCCESS)
            {
                vkBindBufferMemory(m_desc.Device, m_stagingPoolBuffer, m_stagingPoolMemory, 0);
                vkMapMemory(m_desc.Device, m_stagingPoolMemory, 0, m_stagingPoolSize, 0, &m_stagingPoolMapped);
            }
        }

        m_workerThread = std::thread(&VolcanStorageQueueImpl::WorkerLoop, this);
    }

    ~VolcanStorageQueueImpl() override
    {
        m_running = false;
        m_cv.notify_all();
        if (m_workerThread.joinable())
            m_workerThread.join();

        if (m_stagingPoolMapped)
        {
            vkUnmapMemory(m_desc.Device, m_stagingPoolMemory);
            m_stagingPoolMapped = nullptr;
        }

        if (m_stagingPoolBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_desc.Device, m_stagingPoolBuffer, nullptr);
            m_stagingPoolBuffer = VK_NULL_HANDLE;
        }

        if (m_stagingPoolMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_desc.Device, m_stagingPoolMemory, nullptr);
            m_stagingPoolMemory = VK_NULL_HANDLE;
        }

        if (m_commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_desc.Device, m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
        }
    }

    VolcanDeviceCapabilities GetCapabilities() const override
    {
        return m_caps;
    }

    void EnqueueRequest(const Request& request) override
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        QueuedTask task{};
        task.req = request;
        task.isSignalOnly = false;
        m_pendingTasks.push(task);
    }

    void EnqueueSignal(VkFence fence, VkSemaphore semaphore) override
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        QueuedTask task{};
        task.signalFence = fence;
        task.signalSemaphore = semaphore;
        task.isSignalOnly = true;
        m_pendingTasks.push(task);
    }

    void EnqueueSignalTimeline(VkSemaphore timelineSemaphore, uint64_t signalValue) override
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        QueuedTask task{};
        task.timelineSemaphore = timelineSemaphore;
        task.timelineValue = signalValue;
        task.isSignalOnly = true;
        m_pendingTasks.push(task);
    }

    void Submit() override
    {
        m_cv.notify_one();
    }

    void WaitIdle() override
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_cv.wait(lock, [this]() {
            return m_pendingTasks.empty();
        });
        vkQueueWaitIdle(m_desc.TransferQueue);
    }

private:
    void ProbeCapabilities()
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_desc.PhysicalDevice, &props);
        m_caps.ApiVersion = props.apiVersion;

        // Probe UMA vs Resizable BAR for Zero-Copy NVMe IO
        bool hasHostVisibleDeviceLocal = false;
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(m_desc.PhysicalDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            const auto flags = memProps.memoryTypes[i].propertyFlags;
            if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                hasHostVisibleDeviceLocal = true;
                break;
            }
        }

        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            m_caps.IsUnifiedMemoryArchitecture = true;
            m_caps.SupportsDirectGpuZeroCopy = true;
        }
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && hasHostVisibleDeviceLocal)
        {
            m_caps.HasResizableBAR = true;
            m_caps.SupportsDirectGpuZeroCopy = true;
        }
        else if (hasHostVisibleDeviceLocal)
        {
            m_caps.SupportsDirectGpuZeroCopy = true;
        }

        // Top-down feature probe and graceful degradation / downgrade
        if (props.apiVersion >= VK_API_VERSION_1_3)
        {
            VkPhysicalDeviceVulkan13Features v13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
            VkPhysicalDeviceVulkan12Features v12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
            v13.pNext = &v12;

            VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
            features2.pNext = &v13;

            vkGetPhysicalDeviceFeatures2(m_desc.PhysicalDevice, &features2);

            m_caps.HasSynchronization2 = (v13.synchronization2 == VK_TRUE);
            m_caps.HasTimelineSemaphores = (v12.timelineSemaphore == VK_TRUE);

            if (m_caps.HasSynchronization2 && m_caps.HasTimelineSemaphores)
            {
                m_caps.ActiveTier = FeatureTier::Tier3_Modern;
            }
            else if (m_caps.HasTimelineSemaphores)
            {
                m_caps.ActiveTier = FeatureTier::Tier2_Standard;
            }
            else
            {
                m_caps.ActiveTier = FeatureTier::Tier1_Legacy;
            }
        }
        else if (props.apiVersion >= VK_API_VERSION_1_2)
        {
            VkPhysicalDeviceVulkan12Features v12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
            VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
            features2.pNext = &v12;

            vkGetPhysicalDeviceFeatures2(m_desc.PhysicalDevice, &features2);

            m_caps.HasTimelineSemaphores = (v12.timelineSemaphore == VK_TRUE);
            m_caps.ActiveTier = m_caps.HasTimelineSemaphores ? FeatureTier::Tier2_Standard : FeatureTier::Tier1_Legacy;
        }
        else
        {
            m_caps.ActiveTier = FeatureTier::Tier1_Legacy;
        }

        m_caps.HasComputeDecompression = (m_desc.ComputeQueue != VK_NULL_HANDLE);
    }

private:
    void WorkerLoop()
    {
        while (m_running)
        {
            QueuedTask task{};
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_cv.wait(lock, [this]() {
                    return !m_running || !m_pendingTasks.empty();
                });

                if (!m_running && m_pendingTasks.empty())
                    break;

                task = m_pendingTasks.front();
                m_pendingTasks.pop();
            }

            ProcessTask(task);
            m_cv.notify_all();
        }
    }

    void ProcessTask(const QueuedTask& task)
    {
        if (task.isSignalOnly)
        {
            if (task.timelineSemaphore != VK_NULL_HANDLE)
            {
                VkTimelineSemaphoreSubmitInfo timelineInfo{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
                timelineInfo.signalSemaphoreValueCount = 1;
                timelineInfo.pSignalSemaphoreValues = &task.timelineValue;

                VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
                submitInfo.pNext = &timelineInfo;
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pSignalSemaphores = &task.timelineSemaphore;

                vkQueueSubmit(m_desc.TransferQueue, 1, &submitInfo, VK_NULL_HANDLE);
            }
            else if (task.signalFence != VK_NULL_HANDLE || task.signalSemaphore != VK_NULL_HANDLE)
            {
                // Signal fence via empty queue submit
                VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
                if (task.signalSemaphore != VK_NULL_HANDLE)
                {
                    submitInfo.signalSemaphoreCount = 1;
                    submitInfo.pSignalSemaphores = &task.signalSemaphore;
                }
                vkQueueSubmit(m_desc.TransferQueue, 1, &submitInfo, task.signalFence);
            }
            return;
        }

        auto fileImpl = dynamic_cast<VolcanStorageFileImpl*>(task.req.SourceFile);
        if (!fileImpl)
            return;

        // Security & Boundary Validation: Prevent integer underflow and OOB reads
        if (task.req.SourceOffset >= fileImpl->m_info.FileSize)
            return;

        uint32_t maxAvailable = static_cast<uint32_t>(fileImpl->m_info.FileSize - task.req.SourceOffset);
        uint32_t readSize = task.req.SourceSize;
        if (readSize == 0 || readSize > maxAvailable)
            readSize = maxAvailable;

        if (readSize == 0)
            return;

        // Path 1: Direct CPU Memory Destination
        if (task.req.DestType == DestinationType::Memory)
        {
            if (task.req.DestinationMemory != nullptr)
            {
                fileImpl->ReadAsync(task.req.SourceOffset, readSize, task.req.DestinationMemory);
            }
            return;
        }

        // Path 2: GPU Destination (Buffer or Image) via Staging Ring-Buffer or Dedicated Buffer
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        uint64_t stagingSrcOffset = 0;
        void* mappedData = nullptr;
        bool isDedicatedBuffer = false;
        VkBuffer dedicatedBuffer = VK_NULL_HANDLE;
        VkDeviceMemory dedicatedMemory = VK_NULL_HANDLE;

        uint32_t neededStagingSize = (task.req.DestinationSize > 0) ? std::max(readSize, task.req.DestinationSize) : readSize;
        uint32_t alignedStagingSize = (neededStagingSize + 255) & ~255;
        if (m_stagingPoolMapped != nullptr && alignedStagingSize <= m_stagingPoolSize)
        {
            // Use persistent staging ring-buffer pool (Zero allocation overhead!)
            if (m_stagingPoolHead + alignedStagingSize > m_stagingPoolSize)
            {
                m_stagingPoolHead = 0;
            }
            stagingSrcOffset = m_stagingPoolHead;
            m_stagingPoolHead += alignedStagingSize;
            stagingBuffer = m_stagingPoolBuffer;
            mappedData = static_cast<uint8_t*>(m_stagingPoolMapped) + stagingSrcOffset;
        }
        else
        {
            // Dedicated fallback buffer for oversized requests
            isDedicatedBuffer = true;
            VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            bufferInfo.size = neededStagingSize;
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(m_desc.Device, &bufferInfo, nullptr, &dedicatedBuffer) != VK_SUCCESS)
                return;

            VkMemoryRequirements memReqs;
            vkGetBufferMemoryRequirements(m_desc.Device, dedicatedBuffer, &memReqs);

            VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocInfo.allocationSize = memReqs.size;
            allocInfo.memoryTypeIndex = FindMemoryType(
                m_desc.PhysicalDevice,
                memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            if (vkAllocateMemory(m_desc.Device, &allocInfo, nullptr, &dedicatedMemory) != VK_SUCCESS)
            {
                vkDestroyBuffer(m_desc.Device, dedicatedBuffer, nullptr);
                return;
            }

            vkBindBufferMemory(m_desc.Device, dedicatedBuffer, dedicatedMemory, 0);
            vkMapMemory(m_desc.Device, dedicatedMemory, 0, neededStagingSize, 0, &mappedData);
            stagingBuffer = dedicatedBuffer;
            stagingSrcOffset = 0;
        }

        // Read directly into Vulkan mapped staging memory
        bool readSuccess = fileImpl->ReadAsync(task.req.SourceOffset, readSize, mappedData);

        // GDeflate Decompression Hook
        uint32_t transferPayloadSize = (task.req.DestinationSize > 0) ? task.req.DestinationSize : readSize;
        if (readSuccess && task.req.Compression == CompressionFormat::GDeflate)
        {
#if defined(VOLCANSTORAGE_HAS_GDEFLATE)
            if (task.req.DestinationSize > 0)
            {
                std::vector<uint8_t> decompressed(task.req.DestinationSize);
                if (GDeflate::Decompress(decompressed.data(), decompressed.size(), static_cast<const uint8_t*>(mappedData), readSize, 1))
                {
                    std::memcpy(mappedData, decompressed.data(), decompressed.size());
                    transferPayloadSize = task.req.DestinationSize;
                }
            }
#endif
        }

        if (readSuccess)
        {
            VkCommandBufferAllocateInfo cmdAllocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAllocInfo.commandPool = m_commandPool;
            cmdAllocInfo.commandBufferCount = 1;

            VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
            vkAllocateCommandBuffers(m_desc.Device, &cmdAllocInfo, &cmdBuffer);

            VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmdBuffer, &beginInfo);

            if (task.req.DestType == DestinationType::Buffer && task.req.DestinationBuffer != VK_NULL_HANDLE)
            {
                VkBufferCopy copyRegion{};
                copyRegion.srcOffset = stagingSrcOffset;
                copyRegion.dstOffset = task.req.DestinationBufferOffset;
                copyRegion.size = transferPayloadSize;

                vkCmdCopyBuffer(cmdBuffer, stagingBuffer, task.req.DestinationBuffer, 1, &copyRegion);
            }
            else if (task.req.DestType == DestinationType::Image && task.req.DestinationImage.Image != VK_NULL_HANDLE)
            {
                // Transition image to TRANSFER_DST_OPTIMAL
                VkImageMemoryBarrier barrierToDst{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                barrierToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrierToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrierToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrierToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrierToDst.image = task.req.DestinationImage.Image;
                barrierToDst.subresourceRange.aspectMask = task.req.DestinationImage.Subresource.aspectMask;
                barrierToDst.subresourceRange.baseMipLevel = task.req.DestinationImage.Subresource.mipLevel;
                barrierToDst.subresourceRange.levelCount = 1;
                barrierToDst.subresourceRange.baseArrayLayer = task.req.DestinationImage.Subresource.baseArrayLayer;
                barrierToDst.subresourceRange.layerCount = task.req.DestinationImage.Subresource.layerCount;
                barrierToDst.srcAccessMask = 0;
                barrierToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                vkCmdPipelineBarrier(cmdBuffer,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrierToDst);

                // Copy buffer to image
                VkBufferImageCopy region{};
                region.bufferOffset = stagingSrcOffset;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource = task.req.DestinationImage.Subresource;
                region.imageOffset = task.req.DestinationImage.ImageOffset;
                region.imageExtent = task.req.DestinationImage.ImageExtent;

                vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, task.req.DestinationImage.Image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

                // Transition image to FinalLayout
                VkImageMemoryBarrier barrierToFinal{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                barrierToFinal.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrierToFinal.newLayout = task.req.DestinationImage.FinalLayout;
                barrierToFinal.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrierToFinal.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrierToFinal.image = task.req.DestinationImage.Image;
                barrierToFinal.subresourceRange = barrierToDst.subresourceRange;
                barrierToFinal.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrierToFinal.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(cmdBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrierToFinal);
            }

            vkEndCommandBuffer(cmdBuffer);

            VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmdBuffer;

            VkFence copyFence = VK_NULL_HANDLE;
            VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            vkCreateFence(m_desc.Device, &fenceInfo, nullptr, &copyFence);

            vkQueueSubmit(m_desc.TransferQueue, 1, &submitInfo, copyFence);
            vkWaitForFences(m_desc.Device, 1, &copyFence, VK_TRUE, UINT64_MAX);

            vkDestroyFence(m_desc.Device, copyFence, nullptr);
            vkFreeCommandBuffers(m_desc.Device, m_commandPool, 1, &cmdBuffer);
        }

        if (isDedicatedBuffer)
        {
            vkUnmapMemory(m_desc.Device, dedicatedMemory);
            vkDestroyBuffer(m_desc.Device, dedicatedBuffer, nullptr);
            vkFreeMemory(m_desc.Device, dedicatedMemory, nullptr);
        }
    }
};

// -----------------------------------------------------------------------------
// Factory Implementation
// -----------------------------------------------------------------------------
class VolcanStorageFactoryImpl : public IVolcanStorageFactory
{
public:
    VkResult OpenFile(const char* utf8Path, IVolcanStorageFile** ppFile) override
    {
        if (!utf8Path || !ppFile)
            return VK_ERROR_INITIALIZATION_FAILED;

#if defined(_WIN32)
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, nullptr, 0);
        if (wideLen <= 0)
            return VK_ERROR_INITIALIZATION_FAILED;

        std::vector<wchar_t> wideBuf(wideLen);
        MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, wideBuf.data(), wideLen);
        return OpenFileW(wideBuf.data(), ppFile);
#else
        int fd = open(utf8Path, O_RDONLY);
        if (fd < 0)
            return VK_ERROR_INITIALIZATION_FAILED;

        struct stat st{};
        fstat(fd, &st);

        auto file = new VolcanStorageFileImpl();
        file->m_fileDesc = fd;
        file->m_info.FileSize = static_cast<uint64_t>(st.st_size);
        file->m_info.SectorSize = 4096;

        *ppFile = file;
        return VK_SUCCESS;
#endif
    }

    VkResult OpenFileW(const wchar_t* widePath, IVolcanStorageFile** ppFile) override
    {
        if (!widePath || !ppFile)
            return VK_ERROR_INITIALIZATION_FAILED;

#if defined(_WIN32)
        HANDLE hFile = CreateFileW(
            widePath,
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr
        );

        if (hFile == INVALID_HANDLE_VALUE)
            return VK_ERROR_INITIALIZATION_FAILED;

        LARGE_INTEGER size{};
        GetFileSizeEx(hFile, &size);

        auto file = new VolcanStorageFileImpl();
        file->m_fileHandle = hFile;
        file->m_info.FileSize = static_cast<uint64_t>(size.QuadPart);
        file->m_info.SectorSize = 4096;

        *ppFile = file;
        return VK_SUCCESS;
#else
        // Wide to UTF-8 on POSIX
        return VK_ERROR_FEATURE_NOT_PRESENT;
#endif
    }

    VkResult CreateQueue(const QueueDesc& desc, IVolcanStorageQueue** ppQueue) override
    {
        if (!ppQueue || desc.Device == VK_NULL_HANDLE || desc.TransferQueue == VK_NULL_HANDLE)
            return VK_ERROR_INITIALIZATION_FAILED;

        *ppQueue = new VolcanStorageQueueImpl(desc);
        return VK_SUCCESS;
    }
};

extern "C" VOLCANSTORAGE_API VkResult VolcanStorageGetFactory(IVolcanStorageFactory** ppFactory)
{
    if (!ppFactory)
        return VK_ERROR_INITIALIZATION_FAILED;

    static VolcanStorageFactoryImpl s_factory;
    *ppFactory = &s_factory;
    return VK_SUCCESS;
}

} // namespace volcanstorage

// -----------------------------------------------------------------------------
// Pure Vulkan C API Implementations (Free Functions)
// -----------------------------------------------------------------------------
extern "C" {

VOLCANSTORAGE_API VkResult volcanCreateFactory(
    const VolcanFactoryCreateInfo* pCreateInfo,
    VolcanStorageFactory* pFactory)
{
    (void)pCreateInfo;
    if (!pFactory)
        return VK_ERROR_INITIALIZATION_FAILED;

    static volcanstorage::VolcanStorageFactoryImpl s_factory;
    *pFactory = reinterpret_cast<VolcanStorageFactory>(&s_factory);
    return VK_SUCCESS;
}

VOLCANSTORAGE_API void volcanDestroyFactory(
    VolcanStorageFactory factory)
{
    (void)factory;
}

VOLCANSTORAGE_API VkResult volcanOpenFile(
    VolcanStorageFactory factory,
    const char* pUtf8Path,
    VolcanStorageFile* pFile)
{
    if (!factory || !pFile || !pUtf8Path)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto* pFactoryImpl = reinterpret_cast<volcanstorage::IVolcanStorageFactory*>(factory);
    volcanstorage::IVolcanStorageFile* pInternalFile = nullptr;
    VkResult res = pFactoryImpl->OpenFile(pUtf8Path, &pInternalFile);
    if (res == VK_SUCCESS)
    {
        *pFile = reinterpret_cast<VolcanStorageFile>(pInternalFile);
    }
    return res;
}

VOLCANSTORAGE_API VkResult volcanOpenFileW(
    VolcanStorageFactory factory,
    const wchar_t* pWidePath,
    VolcanStorageFile* pFile)
{
    if (!factory || !pFile || !pWidePath)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto* pFactoryImpl = reinterpret_cast<volcanstorage::IVolcanStorageFactory*>(factory);
    volcanstorage::IVolcanStorageFile* pInternalFile = nullptr;
    VkResult res = pFactoryImpl->OpenFileW(pWidePath, &pInternalFile);
    if (res == VK_SUCCESS)
    {
        *pFile = reinterpret_cast<VolcanStorageFile>(pInternalFile);
    }
    return res;
}

VOLCANSTORAGE_API void volcanCloseFile(
    VolcanStorageFile file)
{
    if (file)
    {
        auto* pInternalFile = reinterpret_cast<volcanstorage::IVolcanStorageFile*>(file);
        pInternalFile->Close();
        delete pInternalFile;
    }
}

VOLCANSTORAGE_API VkResult volcanGetFileInformation(
    VolcanStorageFile file,
    VolcanFileInformation* pInformation)
{
    if (!file || !pInformation)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto* pInternalFile = reinterpret_cast<volcanstorage::IVolcanStorageFile*>(file);
    auto info = pInternalFile->GetInformation();
    pInformation->fileSize = info.FileSize;
    pInformation->sectorSize = info.SectorSize;
    return VK_SUCCESS;
}

VOLCANSTORAGE_API VkResult volcanCreateQueue(
    VolcanStorageFactory factory,
    const VolcanQueueCreateInfo* pCreateInfo,
    VolcanStorageQueue* pQueue)
{
    if (!factory || !pCreateInfo || !pQueue)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto* pFactoryImpl = reinterpret_cast<volcanstorage::IVolcanStorageFactory*>(factory);
    volcanstorage::QueueDesc desc{};
    desc.Capacity = pCreateInfo->capacity;
    desc.QueuePriority = static_cast<volcanstorage::Priority>(pCreateInfo->queuePriority);
    desc.Device = pCreateInfo->device;
    desc.PhysicalDevice = pCreateInfo->physicalDevice;
    desc.TransferQueue = pCreateInfo->transferQueue;
    desc.TransferQueueFamilyIndex = pCreateInfo->transferQueueFamilyIndex;
    desc.ComputeQueue = pCreateInfo->computeQueue;
    desc.ComputeQueueFamilyIndex = pCreateInfo->computeQueueFamilyIndex;
    desc.StagingBufferSize = pCreateInfo->stagingBufferSize;

    volcanstorage::IVolcanStorageQueue* pInternalQueue = nullptr;
    VkResult res = pFactoryImpl->CreateQueue(desc, &pInternalQueue);
    if (res == VK_SUCCESS)
    {
        *pQueue = reinterpret_cast<VolcanStorageQueue>(pInternalQueue);
    }
    return res;
}

VOLCANSTORAGE_API void volcanDestroyQueue(
    VolcanStorageQueue queue)
{
    if (queue)
    {
        auto* pInternalQueue = reinterpret_cast<volcanstorage::IVolcanStorageQueue*>(queue);
        delete pInternalQueue;
    }
}

VOLCANSTORAGE_API VkResult volcanGetQueueCapabilities(
    VolcanStorageQueue queue,
    VolcanStorageCapabilities* pCapabilities)
{
    if (!queue || !pCapabilities)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto* pInternalQueue = reinterpret_cast<volcanstorage::IVolcanStorageQueue*>(queue);
    auto caps = pInternalQueue->GetCapabilities();
    pCapabilities->apiVersion = caps.ApiVersion;
    pCapabilities->activeTier = static_cast<VolcanFeatureTier>(caps.ActiveTier);
    pCapabilities->hasTimelineSemaphores = caps.HasTimelineSemaphores ? VK_TRUE : VK_FALSE;
    pCapabilities->hasSynchronization2 = caps.HasSynchronization2 ? VK_TRUE : VK_FALSE;
    pCapabilities->isUnifiedMemoryArchitecture = caps.IsUnifiedMemoryArchitecture ? VK_TRUE : VK_FALSE;
    pCapabilities->hasResizableBAR = caps.HasResizableBAR ? VK_TRUE : VK_FALSE;
    pCapabilities->supportsDirectGpuZeroCopy = caps.SupportsDirectGpuZeroCopy ? VK_TRUE : VK_FALSE;
    pCapabilities->hasComputeDecompression = caps.HasComputeDecompression ? VK_TRUE : VK_FALSE;
    return VK_SUCCESS;
}

VOLCANSTORAGE_API VkResult volcanEnqueueRequest(
    VolcanStorageQueue queue,
    const VolcanRequest* pRequest)
{
    if (!queue || !pRequest)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto* pInternalQueue = reinterpret_cast<volcanstorage::IVolcanStorageQueue*>(queue);
    
    volcanstorage::Request req{};
    req.DestType = static_cast<volcanstorage::DestinationType>(pRequest->destType);
    req.SourceFile = reinterpret_cast<volcanstorage::IVolcanStorageFile*>(pRequest->sourceFile);
    req.SourceOffset = pRequest->sourceOffset;
    req.SourceSize = pRequest->sourceSize;
    req.DestinationBuffer = pRequest->destinationBuffer;
    req.DestinationBufferOffset = pRequest->destinationBufferOffset;
    req.DestinationImage.Image = pRequest->destinationImage.image;
    req.DestinationImage.ImageOffset = pRequest->destinationImage.imageOffset;
    req.DestinationImage.ImageExtent = pRequest->destinationImage.imageExtent;
    req.DestinationImage.Subresource = pRequest->destinationImage.subresource;
    req.DestinationImage.FinalLayout = pRequest->destinationImage.finalLayout;
    req.DestinationMemory = pRequest->destinationMemory;
    req.DestinationSize = pRequest->destinationSize;
    req.Compression = static_cast<volcanstorage::CompressionFormat>(pRequest->compression);

    pInternalQueue->EnqueueRequest(req);
    return VK_SUCCESS;
}

VOLCANSTORAGE_API VkResult volcanEnqueueSignal(
    VolcanStorageQueue queue,
    VkFence fence,
    VkSemaphore binarySemaphore)
{
    if (!queue)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto* pInternalQueue = reinterpret_cast<volcanstorage::IVolcanStorageQueue*>(queue);
    pInternalQueue->EnqueueSignal(fence, binarySemaphore);
    return VK_SUCCESS;
}

VOLCANSTORAGE_API VkResult volcanEnqueueSignalTimeline(
    VolcanStorageQueue queue,
    VkSemaphore timelineSemaphore,
    uint64_t signalValue)
{
    if (!queue)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto* pInternalQueue = reinterpret_cast<volcanstorage::IVolcanStorageQueue*>(queue);
    pInternalQueue->EnqueueSignalTimeline(timelineSemaphore, signalValue);
    return VK_SUCCESS;
}

VOLCANSTORAGE_API VkResult volcanSubmitQueue(
    VolcanStorageQueue queue)
{
    if (!queue)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto* pInternalQueue = reinterpret_cast<volcanstorage::IVolcanStorageQueue*>(queue);
    pInternalQueue->Submit();
    return VK_SUCCESS;
}

VOLCANSTORAGE_API VkResult volcanWaitQueueIdle(
    VolcanStorageQueue queue)
{
    if (!queue)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto* pInternalQueue = reinterpret_cast<volcanstorage::IVolcanStorageQueue*>(queue);
    pInternalQueue->WaitIdle();
    return VK_SUCCESS;
}

// -----------------------------------------------------------------------------
// Compression, Decompression & Archive Packaging Implementations
// -----------------------------------------------------------------------------

VOLCANSTORAGE_API size_t volcanCompressBound(
    size_t uncompressedSize,
    VolcanCompressionFormat format)
{
#if defined(VOLCANSTORAGE_HAS_GDEFLATE)
    if (format == VOLCAN_COMPRESSION_FORMAT_GDEFLATE)
    {
        return GDeflate::CompressBound(uncompressedSize);
    }
#else
    (void)format;
#endif
    return uncompressedSize;
}

VOLCANSTORAGE_API VkResult volcanCompressBuffer(
    const void* pSourceData,
    size_t sourceSize,
    void* pDestinationBuffer,
    size_t* pDestinationSize,
    VolcanCompressionFormat format,
    uint32_t compressionLevel)
{
    if (!pSourceData || !pDestinationBuffer || !pDestinationSize || sourceSize == 0)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (format == VOLCAN_COMPRESSION_FORMAT_NONE)
    {
        if (*pDestinationSize < sourceSize)
            return VK_ERROR_INITIALIZATION_FAILED;
        std::memcpy(pDestinationBuffer, pSourceData, sourceSize);
        *pDestinationSize = sourceSize;
        return VK_SUCCESS;
    }

#if defined(VOLCANSTORAGE_HAS_GDEFLATE)
    if (format == VOLCAN_COMPRESSION_FORMAT_GDEFLATE)
    {
        uint32_t level = std::clamp(compressionLevel, 1u, 12u);
        size_t actualSize = *pDestinationSize;
        bool ok = GDeflate::Compress(
            static_cast<uint8_t*>(pDestinationBuffer),
            &actualSize,
            static_cast<const uint8_t*>(pSourceData),
            sourceSize,
            level,
            0
        );
        if (!ok)
            return VK_ERROR_INITIALIZATION_FAILED;

        *pDestinationSize = actualSize;
        return VK_SUCCESS;
    }
#endif

    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VOLCANSTORAGE_API VkResult volcanDecompressBuffer(
    const void* pSourceCompressedData,
    size_t sourceCompressedSize,
    void* pDestinationBuffer,
    size_t destinationSize,
    VolcanCompressionFormat format)
{
    if (!pSourceCompressedData || !pDestinationBuffer || sourceCompressedSize == 0 || destinationSize == 0)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (format == VOLCAN_COMPRESSION_FORMAT_NONE)
    {
        size_t copyBytes = std::min(sourceCompressedSize, destinationSize);
        std::memcpy(pDestinationBuffer, pSourceCompressedData, copyBytes);
        return VK_SUCCESS;
    }

#if defined(VOLCANSTORAGE_HAS_GDEFLATE)
    if (format == VOLCAN_COMPRESSION_FORMAT_GDEFLATE)
    {
        bool ok = GDeflate::Decompress(
            static_cast<uint8_t*>(pDestinationBuffer),
            destinationSize,
            static_cast<const uint8_t*>(pSourceCompressedData),
            sourceCompressedSize,
            1
        );
        return ok ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
    }
#endif

    return VK_ERROR_FEATURE_NOT_PRESENT;
}

VOLCANSTORAGE_API VkResult volcanCompressFile(
    const char* pSourceFilePath,
    const char* pDestinationFilePath,
    VolcanCompressionFormat format,
    uint32_t compressionLevel)
{
    if (!pSourceFilePath || !pDestinationFilePath)
        return VK_ERROR_INITIALIZATION_FAILED;

    std::ifstream in(pSourceFilePath, std::ios::binary | std::ios::ate);
    if (!in.is_open())
        return VK_ERROR_INITIALIZATION_FAILED;

    size_t fileSize = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> uncompressedData(fileSize);
    in.read(reinterpret_cast<char*>(uncompressedData.data()), fileSize);
    in.close();

    size_t maxCompSize = volcanCompressBound(fileSize, format);
    std::vector<uint8_t> compressedData(maxCompSize);

    size_t actualCompSize = maxCompSize;
    VkResult res = volcanCompressBuffer(
        uncompressedData.data(),
        fileSize,
        compressedData.data(),
        &actualCompSize,
        format,
        compressionLevel
    );

    if (res != VK_SUCCESS)
        return res;

    std::ofstream out(pDestinationFilePath, std::ios::binary);
    if (!out.is_open())
        return VK_ERROR_INITIALIZATION_FAILED;

    out.write(reinterpret_cast<const char*>(compressedData.data()), actualCompSize);
    out.close();

    return VK_SUCCESS;
}

VOLCANSTORAGE_API VkResult volcanPackArchive(
    const char* const* ppSourceFilePaths,
    uint32_t sourceFileCount,
    const char* pDestinationArchivePath,
    VolcanCompressionFormat format,
    uint32_t compressionLevel)
{
    if (!ppSourceFilePaths || sourceFileCount == 0 || !pDestinationArchivePath)
        return VK_ERROR_INITIALIZATION_FAILED;

    std::vector<VolcanArchiveEntry> entries;
    entries.reserve(sourceFileCount);
    std::vector<std::vector<uint8_t>> compressedPayloads;
    compressedPayloads.reserve(sourceFileCount);

    uint64_t currentDataOffset = sizeof(VolcanArchiveHeader) + (sourceFileCount * sizeof(VolcanArchiveEntry));

    for (uint32_t i = 0; i < sourceFileCount; ++i)
    {
        const char* filePath = ppSourceFilePaths[i];
        std::ifstream in(filePath, std::ios::binary | std::ios::ate);
        if (!in.is_open())
            return VK_ERROR_INITIALIZATION_FAILED;

        size_t fileSize = static_cast<size_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        std::vector<uint8_t> uncompressedData(fileSize);
        in.read(reinterpret_cast<char*>(uncompressedData.data()), fileSize);
        in.close();

        size_t maxCompSize = volcanCompressBound(fileSize, format);
        std::vector<uint8_t> compressedData(maxCompSize);

        size_t actualCompSize = maxCompSize;
        VkResult res = volcanCompressBuffer(
            uncompressedData.data(),
            fileSize,
            compressedData.data(),
            &actualCompSize,
            format,
            compressionLevel
        );

        if (res != VK_SUCCESS)
            return res;

        compressedData.resize(actualCompSize);

        VolcanArchiveEntry entry{};
        std::memset(entry.fileName, 0, sizeof(entry.fileName));
#if defined(_WIN32)
        strncpy_s(entry.fileName, filePath, sizeof(entry.fileName) - 1);
#else
        std::strncpy(entry.fileName, filePath, sizeof(entry.fileName) - 1);
#endif
        entry.offset = currentDataOffset;
        entry.compressedSize = static_cast<uint32_t>(actualCompSize);
        entry.uncompressedSize = static_cast<uint32_t>(fileSize);
        entry.compressionFormat = static_cast<uint32_t>(format);

        entries.push_back(entry);
        compressedPayloads.push_back(std::move(compressedData));

        currentDataOffset += actualCompSize;
    }

    std::ofstream out(pDestinationArchivePath, std::ios::binary);
    if (!out.is_open())
        return VK_ERROR_INITIALIZATION_FAILED;

    VolcanArchiveHeader header{};
    std::memcpy(header.magic, "VOST", 4);
    header.version = 1;
    header.entryCount = sourceFileCount;

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    for (const auto& entry : entries)
    {
        out.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }
    for (const auto& payload : compressedPayloads)
    {
        out.write(reinterpret_cast<const char*>(payload.data()), payload.size());
    }
    out.close();

    return VK_SUCCESS;
}

VOLCANSTORAGE_API VkResult volcanInspectArchive(
    const char* pArchivePath,
    VolcanArchiveHeader* pOutHeader,
    VolcanArchiveEntry* pOutEntries,
    uint32_t maxEntries,
    uint32_t* pOutActualEntryCount)
{
    if (!pArchivePath || !pOutActualEntryCount)
        return VK_ERROR_INITIALIZATION_FAILED;

    std::ifstream in(pArchivePath, std::ios::binary);
    if (!in.is_open())
        return VK_ERROR_INITIALIZATION_FAILED;

    VolcanArchiveHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (in.gcount() != sizeof(header) || std::memcmp(header.magic, "VOST", 4) != 0)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (pOutHeader)
        *pOutHeader = header;

    *pOutActualEntryCount = header.entryCount;

    if (pOutEntries && maxEntries > 0)
    {
        uint32_t toRead = std::min(maxEntries, header.entryCount);
        in.read(reinterpret_cast<char*>(pOutEntries), toRead * sizeof(VolcanArchiveEntry));
    }

    return VK_SUCCESS;
}

} // extern "C"

namespace volcanstorage
{

size_t CompressBound(size_t uncompressedSize, CompressionFormat format)
{
    return volcanCompressBound(uncompressedSize, static_cast<VolcanCompressionFormat>(format));
}

VkResult CompressBuffer(
    const void* src, size_t srcSize,
    void* dst, size_t* dstSize,
    CompressionFormat format, uint32_t level)
{
    return volcanCompressBuffer(src, srcSize, dst, dstSize, static_cast<VolcanCompressionFormat>(format), level);
}

VkResult DecompressBuffer(
    const void* srcCompressed, size_t srcCompressedSize,
    void* dst, size_t dstSize,
    CompressionFormat format)
{
    return volcanDecompressBuffer(srcCompressed, srcCompressedSize, dst, dstSize, static_cast<VolcanCompressionFormat>(format));
}

VkResult CompressFile(
    const char* srcPath, const char* dstPath,
    CompressionFormat format, uint32_t level)
{
    return volcanCompressFile(srcPath, dstPath, static_cast<VolcanCompressionFormat>(format), level);
}

VkResult PackArchive(
    const char* const* srcPaths, uint32_t srcCount,
    const char* dstArchivePath,
    CompressionFormat format, uint32_t level)
{
    return volcanPackArchive(srcPaths, srcCount, dstArchivePath, static_cast<VolcanCompressionFormat>(format), level);
}

VkResult InspectArchive(
    const char* archivePath,
    VolcanArchiveHeader* outHeader,
    VolcanArchiveEntry* outEntries,
    uint32_t maxEntries,
    uint32_t* outActualEntryCount)
{
    return volcanInspectArchive(archivePath, outHeader, outEntries, maxEntries, outActualEntryCount);
}

} // namespace volcanstorage


