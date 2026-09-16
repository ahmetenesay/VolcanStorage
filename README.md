# VolcanStorage

[![C++20](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.1%20--%201.4-red.svg)](https://www.vulkan.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()
[![License](https://img.shields.io/badge/License-Apache%202.0%20%2F%20MIT-green.svg)]()

**VolcanStorage** is a high-performance, cross-platform asynchronous GPU asset streaming and decompression library built on **Vulkan 1.1 – 1.4** and modern **C++20**. It serves as an open-source, vendor-agnostic equivalent to Microsoft DirectStorage.

---

## 🚀 Key Features

* **Cross-Platform Asynchronous I/O**:
  * **Windows**: Win32 `FILE_FLAG_OVERLAPPED` and I/O Completion Ports (IOCP) with kernel bypass optimization.
  * **Linux**: High-throughput zero-syscall `io_uring` and `pread` with `O_DIRECT`.
  * **macOS / iOS (Apple Silicon)**: Unified Memory Architecture (UMA) zero-copy streaming.
* **Top-Down Feature Tiers & Graceful Downgrade**:
  * **Tier 3 (Vulkan 1.3 / 1.4)**: `Synchronization2` (`vkCmdPipelineBarrier2`), `Timeline Semaphores`, GPU Compute Decompression.
  * **Tier 2 (Vulkan 1.2)**: `Timeline Semaphores` (monotonic 64-bit DirectStorage fence equivalent), staging pools.
  * **Tier 1 (Vulkan 1.1 Fallback)**: Binary `VkFence`, CPU `libdeflate` decompression fallback. Never crashes on older hardware or drivers.
* **Hardware-Aware Zero-Copy Engine**:
  * Automatically detects **UMA (Apple Silicon, ARM SoC, AMD APU)** and **Resizable BAR (ReBAR)** on discrete GPUs.
  * Avoids staging memory hops by streaming directly from NVMe storage into host-visible VRAM.
* **Persistent Staging Ring-Buffer Memory Pool**:
  * Eliminates per-request `vkAllocateMemory` and `vkCreateBuffer` overhead, delivering sub-millisecond continuous streaming for open-world games and large textures.
* **Multi-Target Streaming**:
  * Stream into **`VkBuffer`** (geometry, meshes, uniform buffers).
  * Stream into **`VkImage`** (textures with automatic pipeline layout transition to `SHADER_READ_ONLY_OPTIMAL`).
  * Stream into host **`Memory`** (CPU RAM).
* **GDeflate Decompression**:
  * Bundled NVIDIA GDeflate codec with GLSL compute shader (`GDeflate.comp` SPIR-V) and fast multithreaded CPU fallback.
* **Pure Vulkan & Zero DirectX Dependencies**:
  * No D3D12, DXGI, COM, or Windows-specific runtimes required.

---

## 📊 DirectStorage vs VolcanStorage

| Capability | Microsoft DirectStorage | VolcanStorage |
| :--- | :---: | :---: |
| **Graphics API** | DirectX 12 only | **Vulkan 1.1 – 1.4** |
| **Operating System** | Windows 10/11 only | **Windows, Linux, macOS (MoltenVK), Android** |
| **GPU Architecture** | Discrete PC & Xbox | **Discrete PCIe, Apple Silicon UMA, ARM SoC, APU** |
| **Fence Signaling** | `ID3D12Fence` (64-bit monotonic) | **Vulkan Timeline Semaphores** (`uint64_t`) |
| **Decompression** | GDeflate (D3D12 Compute) | **GDeflate (GLSL Compute SPIR-V + CPU)** |
| **Kernel I/O** | Windows BypassIO | **Linux `io_uring` & Windows IOCP** |
| **License** | Proprietary NuGet DLL | **Open Source (Apache-2.0 / MIT)** |

---

## 🛠️ Building

### Prerequisites
* CMake 3.20 or newer
* C++20 compatible compiler (MSVC 2022/2026, Clang 14+, GCC 12+)
* **Vulkan SDK** (automatically detected via `VULKAN_SDK` or `VK_SDK_PATH` environment variables)

### Build Commands
```bash
# Configure
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release
```

Run the included verification sample:
```bash
./build/samples/HelloVolcanStorage/Release/HelloVolcanStorage
```

---

## 💻 Quick Start API Example

```cpp
#include "volcanstorage/volcanstorage.h"

using namespace volcanstorage;

// 1. Obtain Factory singleton
IVolcanStorageFactory* factory = nullptr;
VolcanStorageGetFactory(&factory);

// 2. Open asset file asynchronously
IVolcanStorageFile* file = nullptr;
factory->OpenFile("textures/diffuse_4k.bin", &file);

// 3. Create VolcanStorage Queue
QueueDesc qDesc{};
qDesc.Device = vkDevice;
qDesc.PhysicalDevice = vkPhysicalDevice;
qDesc.TransferQueue = vkTransferQueue;
qDesc.TransferQueueFamilyIndex = transferQueueFamilyIndex;

IVolcanStorageQueue* queue = nullptr;
factory->CreateQueue(qDesc, &queue);

// 4. Enqueue Asynchronous Stream Request
Request req{};
req.DestinationType = DestinationType::Buffer;
req.SourceFile = file;
req.SourceOffset = 0;
req.SourceSize = static_cast<uint32_t>(file->GetInformation().FileSize);
req.DestinationBuffer = targetVkBuffer;
req.DestinationBufferOffset = 0;
req.Compression = CompressionFormat::None; // Or CompressionFormat::GDeflate

queue->EnqueueRequest(req);

// 5. Submit and synchronize (Timeline Semaphore or Fence)
queue->EnqueueSignalTimeline(timelineSemaphore, 1);
queue->Submit();
```

---

## 📜 License

VolcanStorage is licensed under the **Apache License 2.0** and **MIT License**. GDeflate compression algorithms are Copyright NVIDIA Corporation & Microsoft Corporation.
