#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include "GDeflate.h"

#pragma pack(push, 1)
struct ArchiveHeader
{
    char Magic[4];        // "VOST"
    uint32_t Version;     // 1
    uint32_t EntryCount;  // 2
};

struct ArchiveEntry
{
    char FileName[64];
    uint64_t Offset;
    uint32_t CompressedSize;
    uint32_t UncompressedSize;
    uint32_t CompressionFormat; // 1 = GDeflate
};
#pragma pack(pop)

int main(int argc, char* argv[])
{
    std::cout << "=================================================" << std::endl;
    std::cout << "  VolcanStorage Archiver: GDeflate KTX2 Packager " << std::endl;
    std::cout << "=================================================" << std::endl;

    std::vector<std::string> inputFiles = {
        "nano_banana.raw",
        "nano_banana_bc7.ktx2",
        "nano_banana_astc.ktx2"
    };

    std::string archivePath = "nano_banana_archive.volcan";

    std::vector<ArchiveEntry> entries;
    std::vector<std::vector<uint8_t>> compressedPayloads;

    uint64_t currentDataOffset = sizeof(ArchiveHeader) + (inputFiles.size() * sizeof(ArchiveEntry));

    for (const auto& filePath : inputFiles)
    {
        std::ifstream in(filePath, std::ios::binary | std::ios::ate);
        if (!in.is_open())
        {
            std::cerr << "[Error] Cannot open input file: " << filePath << std::endl;
            return -1;
        }

        size_t fileSize = static_cast<size_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        std::vector<uint8_t> uncompressedData(fileSize);
        in.read(reinterpret_cast<char*>(uncompressedData.data()), fileSize);
        in.close();

        size_t maxCompressedSize = GDeflate::CompressBound(fileSize);
        std::vector<uint8_t> compressedData(maxCompressedSize);

        size_t actualCompressedSize = maxCompressedSize;
        bool ok = GDeflate::Compress(
            compressedData.data(),
            &actualCompressedSize,
            uncompressedData.data(),
            fileSize,
            9, // High compression level
            0
        );

        if (!ok)
        {
            std::cerr << "[Error] GDeflate compression failed for: " << filePath << std::endl;
            return -1;
        }

        compressedData.resize(actualCompressedSize);

        ArchiveEntry entry{};
        std::strncpy(entry.FileName, filePath.c_str(), sizeof(entry.FileName) - 1);
        entry.Offset = currentDataOffset;
        entry.CompressedSize = static_cast<uint32_t>(actualCompressedSize);
        entry.UncompressedSize = static_cast<uint32_t>(fileSize);
        entry.CompressionFormat = 1; // GDeflate

        entries.push_back(entry);
        compressedPayloads.push_back(std::move(compressedData));

        currentDataOffset += actualCompressedSize;

        double ratio = (1.0 - ((double)actualCompressedSize / fileSize)) * 100.0;
        std::cout << "[Pack] " << filePath << ": " << fileSize << " bytes -> " 
                  << actualCompressedSize << " bytes (" << ratio << "% reduction)" << std::endl;
    }

    std::ofstream out(archivePath, std::ios::binary);
    if (!out.is_open())
    {
        std::cerr << "[Error] Cannot create archive: " << archivePath << std::endl;
        return -1;
    }

    ArchiveHeader header{};
    std::memcpy(header.Magic, "VOST", 4);
    header.Version = 1;
    header.EntryCount = static_cast<uint32_t>(entries.size());

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

    std::cout << "\n\033[1;32m[Success] Created GDeflate VolcanStorage Archive: " 
              << archivePath << " (" << currentDataOffset << " bytes total)\033[0m" << std::endl;

    return 0;
}
