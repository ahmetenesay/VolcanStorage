// SPDX-License-Identifier: MIT
/**
 * @file volcanstorage.h
 * @brief VolcanStorage Public C and C++ API Specification.
 *
 * VolcanStorage is a high-performance, asynchronous GPU asset streaming and
 * hardware-accelerated decompression runtime designed specifically for Vulkan.
 *
 * @details
 * Inspired by Microsoft DirectStorage but built natively for the cross-platform
 * Vulkan ecosystem, VolcanStorage establishes an end-to-end data pipeline from
 * persistent NVMe storage directly into GPU Device-Local memory (VRAM).
 *
 * Key Architectural Highlights:
 * 1. **Direct I/O 4KB Alignment**: All archive entries and read operations are
 *    strictly aligned to 4096-byte boundaries. This enables OS file cache
 *    bypassing (`FILE_FLAG_NO_BUFFERING` on Windows, `O_DIRECT` on Linux),
 *    eliminating CPU memory copy hops and double-buffering cache pollution.
 * 2. **In-Flight Ring Pipelining**: Uses an asynchronous 4-slot ring buffer of
 *    Vulkan command buffers and fences. CPU disk streaming and GPU DMA transfers
 *    run concurrently in hardware, eliminating synchronization stalls.
 * 3. **Hardware-Accelerated GDeflate**: High-throughput parallel Huffman and
 *    LZ77 decompression executed directly within compute shaders on the GPU.
 * 4. **Graceful Vulkan Downgrade / Tiering**: Seamlessly adapts from legacy
 *    Vulkan 1.1 environments (Tier 1) up to modern Vulkan 1.3 / 1.4 runtimes with
 *    Timeline Semaphores and Synchronization2 (Tier 3).
 * 5. **Universal Compression Standards**: Supports GDeflate levels 1 to 12.
 *    Because GPU decompression speed is independent of the compression level,
 *    developers are strongly advised to always use Level 12 for production
 *    assets to minimize disk footprint and maximize PCIe/NVMe throughput.
 *
 * @author VolcanStorage Team & Ahmet Enes Ay
 * @version 1.0.0
 * @date 2026
 */
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

/* ========================================================================= */
/* Opaque Handles (Vulkan Specification Convention)                         */
/* ========================================================================= */

/**
 * @struct VolcanStorageFactory_T
 * @brief Opaque handle representing the VolcanStorage runtime instance.
 *
 * The factory acts as the central coordinator for hardware probing, file
 * handle instantiation, and storage queue allocation.
 */
VK_DEFINE_HANDLE(VolcanStorageFactory)

/**
 * @struct VolcanStorageQueue_T
 * @brief Opaque handle representing an asynchronous storage request queue.
 *
 * Manages background I/O worker threads, staging buffer ring pools, DMA transfer
 * command submissions, and GPU synchronization primitives.
 */
VK_DEFINE_HANDLE(VolcanStorageQueue)

/**
 * @struct VolcanStorageFile_T
 * @brief Opaque handle representing an opened file descriptor configured for Direct I/O.
 */
VK_DEFINE_HANDLE(VolcanStorageFile)

/* ========================================================================= */
/* Enumerations                                                              */
/* ========================================================================= */

/**
 * @enum VolcanCompressionFormat
 * @brief Specifies the compression algorithm applied to an asset payload.
 */
typedef enum VolcanCompressionFormat {
    /** @brief Uncompressed raw binary data. Transferred directly to GPU without compute dispatch. */
    VOLCAN_COMPRESSION_FORMAT_NONE = 0,

    /**
     * @brief GDeflate compression format (standardized parallel deflate).
     * Decompressed in parallel directly on the GPU using Vulkan compute shaders.
     */
    VOLCAN_COMPRESSION_FORMAT_GDEFLATE = 1,

    /** @brief Internal sentinel to enforce 32-bit enum sizing across all compilers. */
    VOLCAN_COMPRESSION_FORMAT_MAX_ENUM = 0x7FFFFFFF
} VolcanCompressionFormat;

/**
 * @enum VolcanPriority
 * @brief Execution scheduling priority for storage queues and request batches.
 */
typedef enum VolcanPriority {
    /** @brief Background asset prefetching (level streaming ahead of time, LOD loads). */
    VOLCAN_PRIORITY_LOW = -1,

    /** @brief Standard asset loading during typical gameplay streaming. */
    VOLCAN_PRIORITY_NORMAL = 0,

    /** @brief High-priority assets needed immediately (critical shaders, UI textures). */
    VOLCAN_PRIORITY_HIGH = 1,

    /** @brief Blocking synchronous loads (e.g. initial game boot or load-screen barrier). */
    VOLCAN_PRIORITY_REALTIME = 2,

    /** @brief Internal sentinel to enforce 32-bit enum sizing across all compilers. */
    VOLCAN_PRIORITY_MAX_ENUM = 0x7FFFFFFF
} VolcanPriority;

/**
 * @enum VolcanDestinationType
 * @brief Target destination resource type on the Vulkan device or host.
 */
typedef enum VolcanDestinationType {
    /** @brief Destination is a VkBuffer (e.g. index/vertex buffers, uniform buffers, raw storage). */
    VOLCAN_DESTINATION_TYPE_BUFFER = 0,

    /** @brief Destination is a VkImage (e.g. 2D/3D textures, cubemaps, normal maps, BC/ASTC/KTX2). */
    VOLCAN_DESTINATION_TYPE_IMAGE = 1,

    /** @brief Destination is host memory (mapped CPU pointer, staging scratchpad). */
    VOLCAN_DESTINATION_TYPE_MEMORY = 2,

    /** @brief Internal sentinel to enforce 32-bit enum sizing across all compilers. */
    VOLCAN_DESTINATION_TYPE_MAX_ENUM = 0x7FFFFFFF
} VolcanDestinationType;

/**
 * @enum VolcanFeatureTier
 * @brief Hardware and Vulkan runtime capability tier detected at runtime.
 */
typedef enum VolcanFeatureTier {
    /**
     * @brief Tier 1 (Vulkan 1.1 Legacy):
     * Binary VkFence / VkSemaphore synchronization, CPU staging fallback if needed.
     */
    VOLCAN_FEATURE_TIER_1_LEGACY = 1,

    /**
     * @brief Tier 2 (Vulkan 1.2 Standard):
     * 64-bit Timeline Semaphores (`VK_KHR_timeline_semaphore`) and UMA zero-copy paths.
     */
    VOLCAN_FEATURE_TIER_2_STANDARD = 2,

    /**
     * @brief Tier 3 (Vulkan 1.3+ Modern):
     * Vulkan Synchronization2, Resizable BAR direct host-visible VRAM, and GPU compute decompression.
     */
    VOLCAN_FEATURE_TIER_3_MODERN = 3,

    /** @brief Internal sentinel to enforce 32-bit enum sizing across all compilers. */
    VOLCAN_FEATURE_TIER_MAX_ENUM = 0x7FFFFFFF
} VolcanFeatureTier;

/* ========================================================================= */
/* Structures                                                                */
/* ========================================================================= */

/**
 * @struct VolcanFileInformation
 * @brief Metadata query result for an opened VolcanStorage file.
 */
typedef struct VolcanFileInformation {
    /** @brief Total size of the file on disk in bytes. */
    uint64_t fileSize;

    /** @brief Hardware sector size required for unbuffered Direct I/O (typically 4096 bytes). */
    uint32_t sectorSize;
} VolcanFileInformation;

/**
 * @struct VolcanStorageCapabilities
 * @brief Hardware and driver capabilities discovered during device probing.
 */
typedef struct VolcanStorageCapabilities {
    /** @brief Core Vulkan API version reported by the physical device (e.g., VK_API_VERSION_1_3). */
    uint32_t apiVersion;

    /** @brief Highest operational feature tier supported by the device. */
    VolcanFeatureTier activeTier;

    /** @brief True if 64-bit timeline semaphores are supported and enabled. */
    VkBool32 hasTimelineSemaphores;

    /** @brief True if Vulkan 1.3 Synchronization2 (`VK_KHR_synchronization2`) is active. */
    VkBool32 hasSynchronization2;

    /** @brief True if the device is a Unified Memory Architecture (Apple Silicon, ARM SoC, AMD APU). */
    VkBool32 isUnifiedMemoryArchitecture;

    /** @brief True if Resizable BAR (Full host-visible Device-Local VRAM) is detected. */
    VkBool32 hasResizableBAR;

    /** @brief True if direct zero-copy NVMe-to-VRAM transfers can occur without host staging hops. */
    VkBool32 supportsDirectGpuZeroCopy;

    /** @brief True if the hardware compute queue supports GDeflate compute decompression. */
    VkBool32 hasComputeDecompression;
} VolcanStorageCapabilities;

/**
 * @struct VolcanImageDestinationInfo
 * @brief Destination descriptor for streaming data into a Vulkan image.
 */
typedef struct VolcanImageDestinationInfo {
    /** @brief Target Vulkan image handle. */
    VkImage image;

    /** @brief Subregion offset within the destination image. */
    VkOffset3D imageOffset;

    /** @brief 3D extent (width, height, depth) of the subregion to update. */
    VkExtent3D imageExtent;

    /** @brief Subresource layers (mip level, array layer, aspect mask). */
    VkImageSubresourceLayers subresource;

    /** @brief Desired layout to transition the image into after the transfer completes. */
    VkImageLayout finalLayout;
} VolcanImageDestinationInfo;

/**
 * @struct VolcanRequest
 * @brief Primary work descriptor representing an asynchronous read, decompress, and upload job.
 */
typedef struct VolcanRequest {
    /** @brief Destination target type (Buffer, Image, or Host Memory). */
    VolcanDestinationType destType;

    /** @brief Source file handle opened via volcanOpenFile or volcanOpenFileW. */
    VolcanStorageFile sourceFile;

    /**
     * @brief Byte offset within the source file.
     * @note Must be aligned to 4096-byte boundaries when Direct I/O is active.
     */
    uint64_t sourceOffset;

    /** @brief Number of compressed or uncompressed bytes to read from disk. */
    uint32_t sourceSize;

    /** @brief Target buffer handle (used when destType == VOLCAN_DESTINATION_TYPE_BUFFER). */
    VkBuffer destinationBuffer;

    /** @brief Byte offset within the destination buffer. */
    uint64_t destinationBufferOffset;

    /** @brief Image destination parameters (used when destType == VOLCAN_DESTINATION_TYPE_IMAGE). */
    VolcanImageDestinationInfo destinationImage;

    /** @brief Raw host memory pointer (used when destType == VOLCAN_DESTINATION_TYPE_MEMORY). */
    void* destinationMemory;

    /**
     * @brief Expected size of the uncompressed data after decompression.
     * For uncompressed payloads, this must equal sourceSize.
     */
    uint32_t destinationSize;

    /** @brief Compression format used by the source asset payload. */
    VolcanCompressionFormat compression;
} VolcanRequest;

/**
 * @struct VolcanQueueCreateInfo
 * @brief Configuration parameters for initializing an asynchronous storage queue.
 */
typedef struct VolcanQueueCreateInfo {
    /** @brief Maximum queue capacity in tasks (default: 1024). */
    uint32_t capacity;

    /** @brief Priority level for thread scheduling. */
    VolcanPriority queuePriority;

    /** @brief Logical Vulkan device. */
    VkDevice device;

    /** @brief Physical Vulkan device. */
    VkPhysicalDevice physicalDevice;

    /** @brief Dedicated hardware transfer queue handle. */
    VkQueue transferQueue;

    /** @brief Queue family index of the transfer queue. */
    uint32_t transferQueueFamilyIndex;

    /** @brief Optional compute queue handle for GPU GDeflate decompression (can be VK_NULL_HANDLE). */
    VkQueue computeQueue;

    /** @brief Queue family index of the compute queue. */
    uint32_t computeQueueFamilyIndex;

    /**
     * @brief Size in bytes of the persistent host-visible staging ring buffer.
     * Default: 64 MB (67,108,864 bytes).
     */
    uint32_t stagingBufferSize;
} VolcanQueueCreateInfo;

/**
 * @struct VolcanFactoryCreateInfo
 * @brief Initialization parameters for the VolcanStorage runtime factory.
 */
typedef struct VolcanFactoryCreateInfo {
    /** @brief Logical Vulkan device handle. */
    VkDevice device;

    /** @brief Physical Vulkan device handle. */
    VkPhysicalDevice physicalDevice;

    /** @brief Default staging ring buffer size for created queues. */
    uint32_t defaultStagingBufferSize;
} VolcanFactoryCreateInfo;

#pragma pack(push, 1)
/**
 * @struct VolcanArchiveHeader
 * @brief 4KB sector-aligned binary header for VolcanStorage archives.
 */
typedef struct VolcanArchiveHeader {
    /** @brief Magic identifier: must be "VOST". */
    char magic[4];

    /** @brief Archive format specification version (currently 1). */
    uint32_t version;

    /** @brief Total number of asset entries indexed in this archive. */
    uint32_t entryCount;

    /**
     * @brief Memory alignment boundary in bytes (strictly 4096 for zero-copy Direct I/O).
     */
    uint32_t alignment;
} VolcanArchiveHeader;

/**
 * @struct VolcanArchiveEntry
 * @brief Metadata entry describing an individual compressed asset stored within an archive.
 */
typedef struct VolcanArchiveEntry {
    /** @brief Relative file path or asset identifier (null-terminated UTF-8 string). */
    char fileName[64];

    /**
     * @brief Absolute byte offset in the archive file where payload data begins.
     * Guaranteed to be a multiple of 4096 bytes for direct NVMe DMA reads.
     */
    uint64_t offset;

    /** @brief Size of the payload as stored on disk in bytes. */
    uint32_t compressedSize;

    /** @brief Size of the uncompressed raw asset data in bytes. */
    uint32_t uncompressedSize;

    /** @brief Compression format: 1 = GDeflate, 0 = None. */
    uint32_t compressionFormat;

    /** @brief IEEE 802.3 32-bit CRC checksum of uncompressed payload for integrity verification. */
    uint32_t crc32;
} VolcanArchiveEntry;
#pragma pack(pop)

/* ========================================================================= */
/* Core Vulkan C API Function Signatures                                     */
/* ========================================================================= */

/**
 * @brief Creates a VolcanStorage factory instance.
 *
 * @param[in]  pCreateInfo Pointer to factory initialization structure.
 * @param[out] pFactory    Address where the created factory handle is stored.
 * @return VK_SUCCESS on success, or an appropriate Vulkan error code.
 */
VOLCANSTORAGE_API VkResult volcanCreateFactory(
    const VolcanFactoryCreateInfo* pCreateInfo,
    VolcanStorageFactory* pFactory);

/**
 * @brief Destroys a VolcanStorage factory and releases associated resources.
 *
 * @param[in] factory Handle to the factory to destroy.
 */
VOLCANSTORAGE_API void volcanDestroyFactory(
    VolcanStorageFactory factory);

/**
 * @brief Opens a file from disk using a UTF-8 path, configured for Direct I/O.
 *
 * @param[in]  factory  VolcanStorage factory handle.
 * @param[in]  pUtf8Path Null-terminated UTF-8 path to the target file.
 * @param[out] pFile     Address where the created file handle is stored.
 * @return VK_SUCCESS on success, or VK_ERROR_INITIALIZATION_FAILED if file cannot be opened.
 */
VOLCANSTORAGE_API VkResult volcanOpenFile(
    VolcanStorageFactory factory,
    const char* pUtf8Path,
    VolcanStorageFile* pFile);

/**
 * @brief Opens a file from disk using a wide character path (Windows native UTF-16).
 *
 * @param[in]  factory   VolcanStorage factory handle.
 * @param[in]  pWidePath Null-terminated wchar_t path to the target file.
 * @param[out] pFile      Address where the created file handle is stored.
 * @return VK_SUCCESS on success, or VK_ERROR_INITIALIZATION_FAILED if file cannot be opened.
 */
VOLCANSTORAGE_API VkResult volcanOpenFileW(
    VolcanStorageFactory factory,
    const wchar_t* pWidePath,
    VolcanStorageFile* pFile);

/**
 * @brief Closes an opened VolcanStorage file handle.
 *
 * @param[in] file Handle to the file to close.
 */
VOLCANSTORAGE_API void volcanCloseFile(
    VolcanStorageFile file);

/**
 * @brief Retrieves metadata and sector alignment parameters for an opened file.
 *
 * @param[in]  file         Handle to the opened file.
 * @param[out] pInformation Structure filled with file size and sector alignment info.
 * @return VK_SUCCESS on success, or VK_ERROR_INITIALIZATION_FAILED on error.
 */
VOLCANSTORAGE_API VkResult volcanGetFileInformation(
    VolcanStorageFile file,
    VolcanFileInformation* pInformation);

/**
 * @brief Creates an asynchronous storage queue with dedicated staging memory and worker threads.
 *
 * @param[in]  factory     VolcanStorage factory handle.
 * @param[in]  pCreateInfo Queue creation parameters including Vulkan queues and buffers.
 * @param[out] pQueue       Address where the created queue handle is stored.
 * @return VK_SUCCESS on success, or an appropriate Vulkan error code.
 */
VOLCANSTORAGE_API VkResult volcanCreateQueue(
    VolcanStorageFactory factory,
    const VolcanQueueCreateInfo* pCreateInfo,
    VolcanStorageQueue* pQueue);

/**
 * @brief Destroys a storage queue, stopping background workers and freeing staging resources.
 *
 * @param[in] queue Handle to the queue to destroy.
 */
VOLCANSTORAGE_API void volcanDestroyQueue(
    VolcanStorageQueue queue);

/**
 * @brief Queries hardware capabilities and Vulkan feature tiers detected on the queue.
 *
 * @param[in]  queue         Storage queue handle.
 * @param[out] pCapabilities Structure populated with detected capabilities.
 * @return VK_SUCCESS on success.
 */
VOLCANSTORAGE_API VkResult volcanGetQueueCapabilities(
    VolcanStorageQueue queue,
    VolcanStorageCapabilities* pCapabilities);

/**
 * @brief Enqueues an asynchronous storage request (read, decompress, and upload to GPU).
 *
 * @param[in] queue    Target storage queue handle.
 * @param[in] pRequest Pointer to request parameters describing source and destination.
 * @return VK_SUCCESS on success, or VK_ERROR_INITIALIZATION_FAILED if parameters are invalid.
 */
VOLCANSTORAGE_API VkResult volcanEnqueueRequest(
    VolcanStorageQueue queue,
    const VolcanRequest* pRequest);

/**
 * @brief Enqueues a binary Vulkan fence and/or binary semaphore signal upon batch completion.
 *
 * @param[in] queue           Target storage queue handle.
 * @param[in] fence           Vulkan fence to signal when preceding requests finish (can be VK_NULL_HANDLE).
 * @param[in] binarySemaphore Vulkan binary semaphore to signal (can be VK_NULL_HANDLE).
 * @return VK_SUCCESS on success.
 */
VOLCANSTORAGE_API VkResult volcanEnqueueSignal(
    VolcanStorageQueue queue,
    VkFence fence,
    VkSemaphore binarySemaphore);

/**
 * @brief Enqueues a 64-bit Vulkan timeline semaphore signal upon batch completion.
 *
 * @param[in] queue             Target storage queue handle.
 * @param[in] timelineSemaphore Vulkan timeline semaphore handle.
 * @param[in] signalValue       64-bit integer value to signal when preceding work completes.
 * @return VK_SUCCESS on success, or VK_ERROR_FEATURE_NOT_PRESENT if timeline semaphores are unsupported.
 */
VOLCANSTORAGE_API VkResult volcanEnqueueSignalTimeline(
    VolcanStorageQueue queue,
    VkSemaphore timelineSemaphore,
    uint64_t signalValue);

/**
 * @brief Submits all currently enqueued requests to the background I/O pipeline for processing.
 *
 * @param[in] queue Target storage queue handle.
 * @return VK_SUCCESS on success.
 */
VOLCANSTORAGE_API VkResult volcanSubmitQueue(
    VolcanStorageQueue queue);

/**
 * @brief Blocks the calling thread until all pending requests on the queue have completely executed.
 *
 * @param[in] queue Target storage queue handle.
 * @return VK_SUCCESS on success.
 */
VOLCANSTORAGE_API VkResult volcanWaitQueueIdle(
    VolcanStorageQueue queue);

/* ========================================================================= */
/* Compression, Decompression & Archive Packaging (Any custom extension)    */
/* ========================================================================= */

/**
 * @brief Calculates the upper-bound buffer size required to store compressed output.
 *
 * @param[in] uncompressedSize Size of the uncompressed data in bytes.
 * @param[in] format           Compression format to calculate bounds for.
 * @return Maximum required buffer size in bytes.
 */
VOLCANSTORAGE_API size_t volcanCompressBound(
    size_t uncompressedSize,
    VolcanCompressionFormat format);

/**
 * @brief Compresses a block of memory using the specified algorithm and compression level.
 *
 * @details
 * Supported compression levels range from 1 to 12.
 * - Levels 1–3: Fast iteration and greedy parsing. Smallest CPU bake time.
 * - Levels 4–7: Balanced compression (Level 6 is default balanced).
 * - Levels 8–12: Optimal parsing for maximum compression ratio.
 *
 * @note **Production Recommendation**:
 * Unless CPU baking time is constrained during local development iteration,
 * ALWAYS use **Level 12** for shipping release assets.
 * GPU decompression speed is independent of the compression level, and smaller
 * compressed footprints reduce NVMe read latency and PCIe transfer overhead.
 *
 * @param[in]      pSourceData         Pointer to raw source data.
 * @param[in]      sourceSize          Size of raw source data in bytes.
 * @param[out]     pDestinationBuffer  Pointer to buffer receiving compressed output.
 * @param[in,out]  pDestinationSize   In: available buffer capacity. Out: actual compressed bytes.
 * @param[in]      format              Compression format (e.g. VOLCAN_COMPRESSION_FORMAT_GDEFLATE).
 * @param[in]      compressionLevel    Compression level (1 to 12, recommended: 12).
 * @return VK_SUCCESS on success, or an error code on failure.
 */
VOLCANSTORAGE_API VkResult volcanCompressBuffer(
    const void* pSourceData,
    size_t sourceSize,
    void* pDestinationBuffer,
    size_t* pDestinationSize,
    VolcanCompressionFormat format,
    uint32_t compressionLevel);

/**
 * @brief Decompresses a block of compressed memory back into raw uncompressed bytes.
 *
 * @param[in]  pSourceCompressedData Pointer to compressed input data.
 * @param[in]  sourceCompressedSize  Size of compressed input data in bytes.
 * @param[out] pDestinationBuffer    Pointer to buffer receiving uncompressed output.
 * @param[in]  destinationSize       Exact expected size of uncompressed output.
 * @param[in]  format                Compression format.
 * @return VK_SUCCESS on success, or VK_ERROR_INITIALIZATION_FAILED if decompression fails.
 */
VOLCANSTORAGE_API VkResult volcanDecompressBuffer(
    const void* pSourceCompressedData,
    size_t sourceCompressedSize,
    void* pDestinationBuffer,
    size_t destinationSize,
    VolcanCompressionFormat format);

/**
 * @brief Compresses a single file on disk and writes the compressed stream to a destination file.
 *
 * @param[in] pSourceFilePath      Path to source file.
 * @param[in] pDestinationFilePath Path to destination compressed file.
 * @param[in] format               Compression format.
 * @param[in] compressionLevel     Compression level (1 to 12, recommended: 12).
 * @return VK_SUCCESS on success.
 */
VOLCANSTORAGE_API VkResult volcanCompressFile(
    const char* pSourceFilePath,
    const char* pDestinationFilePath,
    VolcanCompressionFormat format,
    uint32_t compressionLevel);

/**
 * @brief Packs multiple files into a single unified 4KB sector-aligned VolcanStorage archive.
 *
 * @details
 * Any custom file extension is permitted (e.g. `.assets.gdfl`, `.archive.bin`, `.volcan`, `.tar`).
 * The output archive guarantees:
 * - 4096-byte Direct I/O alignment for all asset payloads and the metadata table.
 * - Embedded IEEE 802.3 CRC32 checksums for uncompressed data verification.
 * - Zero-padding between entries to eliminate unaligned disk sector split-reads.
 *
 * @param[in] ppSourceFilePaths       Array of null-terminated UTF-8 file paths to package.
 * @param[in] sourceFileCount         Number of files in ppSourceFilePaths.
 * @param[in] pDestinationArchivePath Destination archive file path on disk.
 * @param[in] format                  Compression format to apply to all entries.
 * @param[in] compressionLevel        Compression level (1 to 12, recommended: 12).
 * @return VK_SUCCESS on success, or an error code if packaging fails.
 */
VOLCANSTORAGE_API VkResult volcanPackArchive(
    const char* const* ppSourceFilePaths,
    uint32_t sourceFileCount,
    const char* pDestinationArchivePath,
    VolcanCompressionFormat format,
    uint32_t compressionLevel);

/**
 * @brief Inspects and validates the metadata table and entries of a VolcanStorage archive.
 *
 * @param[in]  pArchivePath          Path to archive file on disk.
 * @param[out] pOutHeader            Pointer to receive archive header (can be NULL).
 * @param[out] pOutEntries           Array to receive archive entries (can be NULL to query count).
 * @param[in]  maxEntries            Maximum capacity of pOutEntries array.
 * @param[out] pOutActualEntryCount  Pointer receiving total number of entries in archive.
 * @return VK_SUCCESS on success, or VK_ERROR_INITIALIZATION_FAILED if corrupted or invalid.
 */
VOLCANSTORAGE_API VkResult volcanInspectArchive(
    const char* pArchivePath,
    VolcanArchiveHeader* pOutHeader,
    VolcanArchiveEntry* pOutEntries,
    uint32_t maxEntries,
    uint32_t* pOutActualEntryCount);

#ifdef __cplusplus
} // extern "C"
#endif

#if defined(__cplusplus)
#include <string>
#include <vector>

/* ========================================================================= */
/* C++ Object-Oriented Interface Bindings (Backward Compatible)              */
/* ========================================================================= */
namespace volcanstorage
{

/** @brief C++ strongly-typed alias for VolcanCompressionFormat. */
enum class CompressionFormat : uint32_t
{
    None = VOLCAN_COMPRESSION_FORMAT_NONE,
    GDeflate = VOLCAN_COMPRESSION_FORMAT_GDEFLATE
};

/** @brief C++ strongly-typed alias for VolcanPriority. */
enum class Priority : int32_t
{
    Low = VOLCAN_PRIORITY_LOW,
    Normal = VOLCAN_PRIORITY_NORMAL,
    High = VOLCAN_PRIORITY_HIGH,
    Realtime = VOLCAN_PRIORITY_REALTIME
};

/** @brief C++ strongly-typed alias for VolcanDestinationType. */
enum class DestinationType : uint32_t
{
    Buffer = VOLCAN_DESTINATION_TYPE_BUFFER,
    Image = VOLCAN_DESTINATION_TYPE_IMAGE,
    Memory = VOLCAN_DESTINATION_TYPE_MEMORY
};

/** @brief C++ strongly-typed alias for VolcanFeatureTier. */
enum class FeatureTier : uint32_t
{
    Tier1_Legacy = VOLCAN_FEATURE_TIER_1_LEGACY,
    Tier2_Standard = VOLCAN_FEATURE_TIER_2_STANDARD,
    Tier3_Modern = VOLCAN_FEATURE_TIER_3_MODERN
};

/** @brief File metadata information structure. */
struct FileInformation
{
    uint64_t FileSize{ 0 };
    uint32_t SectorSize{ 4096 };
};

/** @brief Comprehensive device and driver capability structure. */
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

/** @brief Target image destination descriptor. */
struct ImageDestinationInfo
{
    VkImage Image{ VK_NULL_HANDLE };
    VkOffset3D ImageOffset{ 0, 0, 0 };
    VkExtent3D ImageExtent{ 0, 0, 1 };
    VkImageSubresourceLayers Subresource{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    VkImageLayout FinalLayout{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
};

/**
 * @class IVolcanStorageFile
 * @brief Pure virtual interface for an opened storage file descriptor.
 */
class IVolcanStorageFile
{
public:
    virtual ~IVolcanStorageFile() = default;
    virtual FileInformation GetInformation() const = 0;
    virtual void Close() = 0;
};

/** @brief Storage queue descriptor. */
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

/** @brief Storage job request descriptor. */
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

/**
 * @class IVolcanStorageQueue
 * @brief Pure virtual interface representing an asynchronous storage queue.
 */
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

/**
 * @class IVolcanStorageFactory
 * @brief Factory interface for file opening and queue creation.
 */
class IVolcanStorageFactory
{
public:
    virtual ~IVolcanStorageFactory() = default;
    virtual VkResult OpenFile(const char* utf8Path, IVolcanStorageFile** ppFile) = 0;
    virtual VkResult OpenFileW(const wchar_t* widePath, IVolcanStorageFile** ppFile) = 0;
    virtual VkResult CreateQueue(const QueueDesc& desc, IVolcanStorageQueue** ppQueue) = 0;
};

/** @brief Retrieves the singleton factory instance. */
extern "C" VOLCANSTORAGE_API VkResult VolcanStorageGetFactory(IVolcanStorageFactory** ppFactory);

// -----------------------------------------------------------------------------
// C++ Compression & Archive Packaging Helpers (Any custom file extension allowed)
// -----------------------------------------------------------------------------

/** @brief Calculates upper bound size for compressed output. */
VOLCANSTORAGE_API size_t CompressBound(
    size_t uncompressedSize,
    CompressionFormat format = CompressionFormat::GDeflate);

/**
 * @brief Compresses memory buffer using GDeflate.
 *
 * @param[in]      src     Pointer to raw uncompressed data.
 * @param[in]      srcSize Size of raw data.
 * @param[out]     dst     Output buffer.
 * @param[in,out]  dstSize Available capacity / compressed size output.
 * @param[in]      format  Compression format.
 * @param[in]      level   Compression level 1-12 (Recommended: 12 for production).
 */
VOLCANSTORAGE_API VkResult CompressBuffer(
    const void* src,
    size_t srcSize,
    void* dst,
    size_t* dstSize,
    CompressionFormat format = CompressionFormat::GDeflate,
    uint32_t level = 12);

/** @brief Decompresses buffer back to raw bytes. */
VOLCANSTORAGE_API VkResult DecompressBuffer(
    const void* srcCompressed,
    size_t srcCompressedSize,
    void* dst,
    size_t dstSize,
    CompressionFormat format = CompressionFormat::GDeflate);

/** @brief Compresses file on disk. */
VOLCANSTORAGE_API VkResult CompressFile(
    const char* srcPath,
    const char* dstPath,
    CompressionFormat format = CompressionFormat::GDeflate,
    uint32_t level = 12);

/** @brief Packages files into a 4KB-aligned archive. */
VOLCANSTORAGE_API VkResult PackArchive(
    const char* const* srcPaths,
    uint32_t srcCount,
    const char* dstArchivePath,
    CompressionFormat format = CompressionFormat::GDeflate,
    uint32_t level = 12);

/** @brief Inspects archive header and entry table. */
VOLCANSTORAGE_API VkResult InspectArchive(
    const char* archivePath,
    VolcanArchiveHeader* outHeader,
    VolcanArchiveEntry* outEntries,
    uint32_t maxEntries,
    uint32_t* outActualEntryCount);

/** @brief C++ std::string overload for file compression. */
inline VkResult CompressFile(
    const std::string& srcPath,
    const std::string& dstPath,
    CompressionFormat format = CompressionFormat::GDeflate,
    uint32_t level = 12)
{
    return CompressFile(srcPath.c_str(), dstPath.c_str(), format, level);
}

/** @brief C++ std::vector<std::string> overload for archive packing. */
inline VkResult PackArchive(
    const std::vector<std::string>& srcPaths,
    const std::string& dstArchivePath,
    CompressionFormat format = CompressionFormat::GDeflate,
    uint32_t level = 12)
{
    std::vector<const char*> cstrPaths;
    cstrPaths.reserve(srcPaths.size());
    for (const auto& p : srcPaths)
        cstrPaths.push_back(p.c_str());
    return PackArchive(cstrPaths.data(), static_cast<uint32_t>(cstrPaths.size()), dstArchivePath.c_str(), format, level);
}

/** @brief C++ std::vector overload for archive inspection. */
inline VkResult InspectArchive(
    const std::string& archivePath,
    VolcanArchiveHeader& outHeader,
    std::vector<VolcanArchiveEntry>& outEntries)
{
    uint32_t count = 0;
    VkResult res = InspectArchive(archivePath.c_str(), &outHeader, nullptr, 0, &count);
    if (res != VK_SUCCESS)
        return res;
    outEntries.resize(count);
    return InspectArchive(archivePath.c_str(), &outHeader, outEntries.data(), count, &count);
}
} // namespace volcanstorage
#endif

#endif // VOLCANSTORAGE_H
