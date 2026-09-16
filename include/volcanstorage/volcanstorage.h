#pragma once

#ifndef VOLCANSTORAGE_H
#define VOLCANSTORAGE_H

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstddef>
#include <memory>

#if defined(_WIN32)
    #if defined(VOLCANSTORAGE_BUILD_SHARED)
        #define VOLCANSTORAGE_API __declspec(dllexport)
    #elif defined(VOLCANSTORAGE_USE_SHARED)
        #define VOLCANSTORAGE_API __declspec(dllimport)
    #else
        #define VOLCANSTORAGE_API
    #endif
#else
    #define VOLCANSTORAGE_API __attribute__((visibility("default")))
#endif

// -----------------------------------------------------------------------------
// Platform & Feature Detection Macros
// -----------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
    #define VOLCANSTORAGE_PLATFORM_WINDOWS 1
#elif defined(__APPLE__) && defined(__MACH__)
    #define VOLCANSTORAGE_PLATFORM_APPLE 1
    #define VOLCANSTORAGE_PLATFORM_POSIX 1
#elif defined(__linux__)
    #define VOLCANSTORAGE_PLATFORM_LINUX 1
    #define VOLCANSTORAGE_PLATFORM_POSIX 1
#elif defined(__ANDROID__)
    #define VOLCANSTORAGE_PLATFORM_ANDROID 1
    #define VOLCANSTORAGE_PLATFORM_POSIX 1
#else
    #define VOLCANSTORAGE_PLATFORM_GENERIC 1
#endif

namespace volcanstorage
{

enum class CompressionFormat : uint32_t
{
    None = 0,
    GDeflate = 1
};

enum class Priority : int32_t
{
    Low = -1,
    Normal = 0,
    High = 1,
    Realtime = 2
};

enum class DestinationType : uint32_t
{
    Buffer = 0,
    Image = 1,
    Memory = 2
};

struct FileInformation
{
    uint64_t FileSize{ 0 };
    uint32_t SectorSize{ 4096 };
};

class IVolcanStorageFile
{
public:
    virtual ~IVolcanStorageFile() = default;
    virtual FileInformation GetInformation() const = 0;
    virtual void Close() = 0;
};

struct QueueDesc
{
    uint32_t Capacity{ 1024 };
    Priority QueuePriority{ Priority::Normal };
    VkDevice Device{ VK_NULL_HANDLE };
    VkPhysicalDevice PhysicalDevice{ VK_NULL_HANDLE };
    VkQueue TransferQueue{ VK_NULL_HANDLE };
    uint32_t TransferQueueFamilyIndex{ 0 };
    VkQueue ComputeQueue{ VK_NULL_HANDLE }; // Optional, used for GPU GDeflate decompression
    uint32_t ComputeQueueFamilyIndex{ 0 };
    uint32_t StagingBufferSize{ 64 * 1024 * 1024 }; // 64 MB persistent staging ring-buffer pool
};

enum class FeatureTier : uint32_t
{
    Tier1_Legacy = 1,   // Vulkan 1.1: Binary Fences, staging copy, CPU decompression fallback
    Tier2_Standard = 2, // Vulkan 1.2: 64-bit Timeline Semaphores (Monotonic GPU sync), staging pool
    Tier3_Modern = 3    // Vulkan 1.3 / 1.4: Synchronization2 (vkCmdPipelineBarrier2), UMA Zero-Copy, GPU Compute
};

struct VolcanDeviceCapabilities
{
    uint32_t ApiVersion{ 0 };
    FeatureTier ActiveTier{ FeatureTier::Tier1_Legacy };
    bool HasTimelineSemaphores{ false };
    bool HasSynchronization2{ false };
    bool IsUnifiedMemoryArchitecture{ false }; // True UMA (Apple Silicon, ARM SoC, APU)
    bool HasResizableBAR{ false };              // Discrete GPU with ReBAR (Direct host-visible VRAM)
    bool SupportsDirectGpuZeroCopy{ false };   // True if either UMA or ReBAR is present
    bool HasComputeDecompression{ false };
};

struct ImageDestinationInfo
{
    VkImage Image{ VK_NULL_HANDLE };
    VkOffset3D ImageOffset{ 0, 0, 0 };
    VkExtent3D ImageExtent{ 0, 0, 1 };
    VkImageSubresourceLayers Subresource{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    VkImageLayout FinalLayout{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
};

struct Request
{
    DestinationType DestType{ DestinationType::Buffer };

    IVolcanStorageFile* SourceFile{ nullptr };
    uint64_t SourceOffset{ 0 };
    uint32_t SourceSize{ 0 };

    // Buffer Destination (used when DestinationType == Buffer)
    VkBuffer DestinationBuffer{ VK_NULL_HANDLE };
    uint64_t DestinationBufferOffset{ 0 };

    // Image Destination (used when DestinationType == Image)
    ImageDestinationInfo DestinationImage{};

    // Memory Destination (used when DestinationType == Memory)
    void* DestinationMemory{ nullptr };

    uint32_t DestinationSize{ 0 }; // Uncompressed destination size
    CompressionFormat Compression{ CompressionFormat::None };
};

class IVolcanStorageQueue
{
public:
    virtual ~IVolcanStorageQueue() = default;
    virtual VolcanDeviceCapabilities GetCapabilities() const = 0;
    virtual void EnqueueRequest(const Request& request) = 0;
    virtual void EnqueueSignal(VkFence fence, VkSemaphore binarySemaphore = VK_NULL_HANDLE) = 0;
    virtual void EnqueueSignalTimeline(VkSemaphore timelineSemaphore, uint64_t signalValue) = 0;
    virtual void Submit() = 0;
    virtual void WaitIdle() = 0;
};

class IVolcanStorageFactory
{
public:
    virtual ~IVolcanStorageFactory() = default;
    virtual VkResult OpenFile(const char* utf8Path, IVolcanStorageFile** ppFile) = 0;
    virtual VkResult OpenFileW(const wchar_t* widePath, IVolcanStorageFile** ppFile) = 0;
    virtual VkResult CreateQueue(const QueueDesc& desc, IVolcanStorageQueue** ppQueue) = 0;
};

// Entry point to obtain the singleton VolcanStorage Factory
extern "C" VOLCANSTORAGE_API VkResult VolcanStorageGetFactory(IVolcanStorageFactory** ppFactory);

} // namespace volcanstorage

#endif // VOLCANSTORAGE_H
