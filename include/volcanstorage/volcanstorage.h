// SPDX-License-Identifier: MIT
// VolcanStorage: Asynchronous GPU Storage Subsystem & Hardware Decompression Runtime for Vulkan
#pragma once

#ifndef VOLCANSTORAGE_H
#define VOLCANSTORAGE_H

#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stddef.h>

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

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Opaque Handles (Vulkan Specification Convention)
// -----------------------------------------------------------------------------
VK_DEFINE_HANDLE(VolcanStorageFactory)
VK_DEFINE_HANDLE(VolcanStorageQueue)
VK_DEFINE_HANDLE(VolcanStorageFile)

// -----------------------------------------------------------------------------
// Enumerations
// -----------------------------------------------------------------------------
typedef enum VolcanCompressionFormat {
    VOLCAN_COMPRESSION_FORMAT_NONE = 0,
    VOLCAN_COMPRESSION_FORMAT_GDEFLATE = 1,
    VOLCAN_COMPRESSION_FORMAT_MAX_ENUM = 0x7FFFFFFF
} VolcanCompressionFormat;

typedef enum VolcanPriority {
    VOLCAN_PRIORITY_LOW = -1,
    VOLCAN_PRIORITY_NORMAL = 0,
    VOLCAN_PRIORITY_HIGH = 1,
    VOLCAN_PRIORITY_REALTIME = 2,
    VOLCAN_PRIORITY_MAX_ENUM = 0x7FFFFFFF
} VolcanPriority;

typedef enum VolcanDestinationType {
    VOLCAN_DESTINATION_TYPE_BUFFER = 0,
    VOLCAN_DESTINATION_TYPE_IMAGE = 1,
    VOLCAN_DESTINATION_TYPE_MEMORY = 2,
    VOLCAN_DESTINATION_TYPE_MAX_ENUM = 0x7FFFFFFF
} VolcanDestinationType;

typedef enum VolcanFeatureTier {
    VOLCAN_FEATURE_TIER_1_LEGACY = 1,   // Vulkan 1.1: Binary Fences, staging copy, CPU decompression fallback
    VOLCAN_FEATURE_TIER_2_STANDARD = 2, // Vulkan 1.2: 64-bit Timeline Semaphores, UMA Zero-Copy
    VOLCAN_FEATURE_TIER_3_MODERN = 3,   // Vulkan 1.3 / 1.4: Synchronization2, ReBAR DMA, GPU Compute
    VOLCAN_FEATURE_TIER_MAX_ENUM = 0x7FFFFFFF
} VolcanFeatureTier;

// -----------------------------------------------------------------------------
// Structures
// -----------------------------------------------------------------------------
typedef struct VolcanFileInformation {
    uint64_t fileSize;
    uint32_t sectorSize;
} VolcanFileInformation;

typedef struct VolcanStorageCapabilities {
    uint32_t apiVersion;
    VolcanFeatureTier activeTier;
    VkBool32 hasTimelineSemaphores;
    VkBool32 hasSynchronization2;
    VkBool32 isUnifiedMemoryArchitecture; // True UMA (Apple Silicon, ARM SoC, APU)
    VkBool32 hasResizableBAR;              // Discrete GPU with ReBAR (Direct host-visible VRAM)
    VkBool32 supportsDirectGpuZeroCopy;   // True if either UMA or ReBAR is present
    VkBool32 hasComputeDecompression;
} VolcanStorageCapabilities;

typedef struct VolcanImageDestinationInfo {
    VkImage image;
    VkOffset3D imageOffset;
    VkExtent3D imageExtent;
    VkImageSubresourceLayers subresource;
    VkImageLayout finalLayout;
} VolcanImageDestinationInfo;

typedef struct VolcanRequest {
    VolcanDestinationType destType;
    VolcanStorageFile sourceFile;
    uint64_t sourceOffset;
    uint32_t sourceSize;

    // Buffer Destination (used when destType == VOLCAN_DESTINATION_TYPE_BUFFER)
    VkBuffer destinationBuffer;
    uint64_t destinationBufferOffset;

    // Image Destination (used when destType == VOLCAN_DESTINATION_TYPE_IMAGE)
    VolcanImageDestinationInfo destinationImage;

    // Memory Destination (used when destType == VOLCAN_DESTINATION_TYPE_MEMORY)
    void* destinationMemory;

    uint32_t destinationSize; // Uncompressed destination size
    VolcanCompressionFormat compression;
} VolcanRequest;

typedef struct VolcanQueueCreateInfo {
    uint32_t capacity;
    VolcanPriority queuePriority;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkQueue transferQueue;
    uint32_t transferQueueFamilyIndex;
    VkQueue computeQueue; // Optional, used for GPU GDeflate compute decompression
    uint32_t computeQueueFamilyIndex;
    uint32_t stagingBufferSize; // Persistent staging ring buffer size (e.g. 64 MB)
} VolcanQueueCreateInfo;

typedef struct VolcanFactoryCreateInfo {
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    uint32_t defaultStagingBufferSize;
} VolcanFactoryCreateInfo;

// -----------------------------------------------------------------------------
// Core Vulkan C API Function Signatures
// -----------------------------------------------------------------------------

VOLCANSTORAGE_API VkResult volcanCreateFactory(
    const VolcanFactoryCreateInfo* pCreateInfo,
    VolcanStorageFactory* pFactory);

VOLCANSTORAGE_API void volcanDestroyFactory(
    VolcanStorageFactory factory);

VOLCANSTORAGE_API VkResult volcanOpenFile(
    VolcanStorageFactory factory,
    const char* pUtf8Path,
    VolcanStorageFile* pFile);

VOLCANSTORAGE_API VkResult volcanOpenFileW(
    VolcanStorageFactory factory,
    const wchar_t* pWidePath,
    VolcanStorageFile* pFile);

VOLCANSTORAGE_API void volcanCloseFile(
    VolcanStorageFile file);

VOLCANSTORAGE_API VkResult volcanGetFileInformation(
    VolcanStorageFile file,
    VolcanFileInformation* pInformation);

VOLCANSTORAGE_API VkResult volcanCreateQueue(
    VolcanStorageFactory factory,
    const VolcanQueueCreateInfo* pCreateInfo,
    VolcanStorageQueue* pQueue);

VOLCANSTORAGE_API void volcanDestroyQueue(
    VolcanStorageQueue queue);

VOLCANSTORAGE_API VkResult volcanGetQueueCapabilities(
    VolcanStorageQueue queue,
    VolcanStorageCapabilities* pCapabilities);

VOLCANSTORAGE_API VkResult volcanEnqueueRequest(
    VolcanStorageQueue queue,
    const VolcanRequest* pRequest);

VOLCANSTORAGE_API VkResult volcanEnqueueSignal(
    VolcanStorageQueue queue,
    VkFence fence,
    VkSemaphore binarySemaphore);

VOLCANSTORAGE_API VkResult volcanEnqueueSignalTimeline(
    VolcanStorageQueue queue,
    VkSemaphore timelineSemaphore,
    uint64_t signalValue);

VOLCANSTORAGE_API VkResult volcanSubmitQueue(
    VolcanStorageQueue queue);

VOLCANSTORAGE_API VkResult volcanWaitQueueIdle(
    VolcanStorageQueue queue);

#ifdef __cplusplus
} // extern "C"

// -----------------------------------------------------------------------------
// C++ OOP Interface Bindings (Backward Compatible for Existing Engines)
// -----------------------------------------------------------------------------
namespace volcanstorage
{

enum class CompressionFormat : uint32_t
{
    None = VOLCAN_COMPRESSION_FORMAT_NONE,
    GDeflate = VOLCAN_COMPRESSION_FORMAT_GDEFLATE
};

enum class Priority : int32_t
{
    Low = VOLCAN_PRIORITY_LOW,
    Normal = VOLCAN_PRIORITY_NORMAL,
    High = VOLCAN_PRIORITY_HIGH,
    Realtime = VOLCAN_PRIORITY_REALTIME
};

enum class DestinationType : uint32_t
{
    Buffer = VOLCAN_DESTINATION_TYPE_BUFFER,
    Image = VOLCAN_DESTINATION_TYPE_IMAGE,
    Memory = VOLCAN_DESTINATION_TYPE_MEMORY
};

enum class FeatureTier : uint32_t
{
    Tier1_Legacy = VOLCAN_FEATURE_TIER_1_LEGACY,
    Tier2_Standard = VOLCAN_FEATURE_TIER_2_STANDARD,
    Tier3_Modern = VOLCAN_FEATURE_TIER_3_MODERN
};

struct FileInformation
{
    uint64_t FileSize{ 0 };
    uint32_t SectorSize{ 4096 };
};

struct VolcanDeviceCapabilities
{
    uint32_t ApiVersion{ 0 };
    FeatureTier ActiveTier{ FeatureTier::Tier1_Legacy };
    bool HasTimelineSemaphores{ false };
    bool HasSynchronization2{ false };
    bool IsUnifiedMemoryArchitecture{ false };
    bool HasResizableBAR{ false };
    bool SupportsDirectGpuZeroCopy{ false };
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
    VkQueue ComputeQueue{ VK_NULL_HANDLE };
    uint32_t ComputeQueueFamilyIndex{ 0 };
    uint32_t StagingBufferSize{ 64 * 1024 * 1024 };
};

struct Request
{
    DestinationType DestType{ DestinationType::Buffer };
    IVolcanStorageFile* SourceFile{ nullptr };
    uint64_t SourceOffset{ 0 };
    uint32_t SourceSize{ 0 };
    VkBuffer DestinationBuffer{ VK_NULL_HANDLE };
    uint64_t DestinationBufferOffset{ 0 };
    ImageDestinationInfo DestinationImage{};
    void* DestinationMemory{ nullptr };
    uint32_t DestinationSize{ 0 };
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

extern "C" VOLCANSTORAGE_API VkResult VolcanStorageGetFactory(IVolcanStorageFactory** ppFactory);

} // namespace volcanstorage
#endif // __cplusplus

#endif // VOLCANSTORAGE_H
