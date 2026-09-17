// SPDX-License-Identifier: MIT
// VolcanStorage Archiver & GDeflate Compression Utility
#include "volcanstorage/volcanstorage.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <algorithm>
#include <cstring>

using namespace volcanstorage;

static void PrintUsage(const char* prog)
{
    std::cout << "========================================================================\n"
              << "   VolcanStorage Archiver: Universal GDeflate Asset Packaging Tool     \n"
              << "========================================================================\n"
              << "Usage:\n"
              << "  Pack archive with ANY custom extension:\n"
              << "    " << prog << " -o <output_archive> [-l <1-12>] <file1> [file2 ...]\n"
              << "    Example: " << prog << " -o example.assets.gdfl texture.ktx2 mesh.bin\n"
              << "    Example: " << prog << " -o example.assets.tar data1.raw data2.raw\n\n"
              << "  Compress single file:\n"
              << "    " << prog << " -c <source_file> -o <dest_file> [-l <1-12>]\n"
              << "    Example: " << prog << " -c nano_banana.raw -o nano_banana.gdfl\n\n"
              << "  Inspect archive contents:\n"
              << "    " << prog << " -i <archive_file>\n"
              << "    Example: " << prog << " -i example.assets.gdfl\n"
              << "========================================================================\n";
}

int main(int argc, char* argv[])
{
    std::string outputPath;
    std::string singleCompressSource;
    std::string inspectPath;
    std::vector<std::string> inputFiles;
    uint32_t compressionLevel = 9;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")
        {
            PrintUsage(argv[0]);
            return 0;
        }
        else if ((arg == "-o" || arg == "--output") && i + 1 < argc)
        {
            outputPath = argv[++i];
        }
        else if ((arg == "-c" || arg == "--compress") && i + 1 < argc)
        {
            singleCompressSource = argv[++i];
        }
        else if ((arg == "-i" || arg == "--info" || arg == "--inspect") && i + 1 < argc)
        {
            inspectPath = argv[++i];
        }
        else if ((arg == "-l" || arg == "--level") && i + 1 < argc)
        {
            compressionLevel = static_cast<uint32_t>(std::clamp(std::stoi(argv[++i]), 1, 12));
        }
        else if (arg[0] != '-')
        {
            inputFiles.push_back(arg);
        }
    }

    // Mode 1: Inspect Archive
    if (!inspectPath.empty())
    {
        std::cout << "\n[VolcanStorage] Inspecting archive: " << inspectPath << std::endl;
        VolcanArchiveHeader header{};
        std::vector<VolcanArchiveEntry> entries;

        VkResult res = InspectArchive(inspectPath, header, entries);
        if (res != VK_SUCCESS)
        {
            std::cerr << "[Error] Failed to inspect archive (invalid magic or corrupted format): " << inspectPath << std::endl;
            return -1;
        }

        std::cout << "  Archive Magic:  " << std::string(header.magic, 4) << "\n"
                  << "  Format Version: " << header.version << "\n"
                  << "  Entry Count:    " << header.entryCount << "\n\n";

        std::cout << std::left << std::setw(32) << "Entry Name"
                  << std::right << std::setw(14) << "Offset"
                  << std::setw(16) << "Comp Size"
                  << std::setw(16) << "Uncomp Size"
                  << std::setw(12) << "Ratio" << "\n";
        std::cout << std::string(90, '-') << "\n";

        uint64_t totalComp = 0;
        uint64_t totalUncomp = 0;

        for (const auto& e : entries)
        {
            double ratio = (e.uncompressedSize > 0)
                ? (1.0 - (static_cast<double>(e.compressedSize) / e.uncompressedSize)) * 100.0
                : 0.0;

            std::cout << std::left << std::setw(32) << e.fileName
                      << std::right << std::setw(14) << e.offset
                      << std::setw(16) << e.compressedSize
                      << std::setw(16) << e.uncompressedSize
                      << std::fixed << std::setprecision(1) << std::setw(10) << ratio << "%\n";

            totalComp += e.compressedSize;
            totalUncomp += e.uncompressedSize;
        }

        std::cout << std::string(90, '-') << "\n";
        double totalRatio = (totalUncomp > 0)
            ? (1.0 - (static_cast<double>(totalComp) / totalUncomp)) * 100.0
            : 0.0;
        std::cout << "Total: " << totalComp << " bytes compressed vs " << totalUncomp 
                  << " bytes uncompressed (" << std::fixed << std::setprecision(1) << totalRatio << "% reduction)\n\n";
        return 0;
    }

    // Mode 2: Single File Compression
    if (!singleCompressSource.empty())
    {
        if (outputPath.empty())
        {
            outputPath = singleCompressSource + ".gdfl";
        }

        std::cout << "[VolcanStorage] Compressing single file: " << singleCompressSource 
                  << " -> " << outputPath << " (Level " << compressionLevel << ")..." << std::endl;

        VkResult res = CompressFile(singleCompressSource, outputPath, CompressionFormat::GDeflate, compressionLevel);
        if (res != VK_SUCCESS)
        {
            std::cerr << "[Error] Failed to compress file: " << singleCompressSource << std::endl;
            return -1;
        }

        std::cout << "\033[1;32m[Success] Created compressed file: " << outputPath << "\033[0m\n";
        return 0;
    }

    // Mode 3: Archive Packaging
    if (!inputFiles.empty() || !outputPath.empty())
    {
        if (outputPath.empty())
        {
            outputPath = "assets.gdfl"; // Modern default with custom extension!
        }

        if (inputFiles.empty())
        {
            std::cerr << "[Error] No input files specified to pack into: " << outputPath << std::endl;
            PrintUsage(argv[0]);
            return -1;
        }

        std::cout << "[VolcanStorage] Packaging " << inputFiles.size() << " files into archive: " 
                  << outputPath << " (Level " << compressionLevel << ")..." << std::endl;

        VkResult res = PackArchive(inputFiles, outputPath, CompressionFormat::GDeflate, compressionLevel);
        if (res != VK_SUCCESS)
        {
            std::cerr << "[Error] Failed to package archive: " << outputPath << std::endl;
            return -1;
        }

        std::cout << "\033[1;32m[Success] Successfully created VolcanStorage container archive: " 
                  << outputPath << "\033[0m\n";
        return 0;
    }

    // Default Fallback: If no CLI options, package standard sample files if found
    std::vector<std::string> defaultFiles = {
        "nano_banana.raw",
        "nano_banana_bc7.ktx2",
        "nano_banana_astc.ktx2"
    };

    std::vector<std::string> existingFiles;
    for (const auto& f : defaultFiles)
    {
        std::ifstream test(f, std::ios::binary);
        if (test.is_open())
            existingFiles.push_back(f);
    }

    if (!existingFiles.empty())
    {
        std::string defaultArchive = "nano_banana_archive.volcan";
        std::cout << "[VolcanStorage] No arguments provided. Packing detected sample assets into " << defaultArchive << "...\n";
        PackArchive(existingFiles, defaultArchive, CompressionFormat::GDeflate, 9);
        std::cout << "\033[1;32m[Success] Packaged default sample archive: " << defaultArchive << "\033[0m\n";
        std::cout << "\nTip: Run '" << argv[0] << " -h' to see full custom extension options (e.g. -o example.assets.gdfl).\n";
        return 0;
    }

    PrintUsage(argv[0]);
    return 0;
}
