
# Sample Code
GDeflate is a new compression stream format that closely matches the DEFLATE format. The key difference lies in the way the bits in the compressed bitstream are stored. The GDeflate stream is essentially a reformatted version of any DEFLATE stream where the data is ordered in a particular way to efficiently extract 32 way parallelism without increasing the size of the input stream. This means that GDeflate can get very high decompression throughput on the GPU while still maintaining the exact same compression ratio of DEFLATE (with some small caveats about end effects).

Details on the bitstream can be found in [GDeflate Reference Implementation](GDeflate/README.md)

## 3rdParty\libdeflate
Builds a static library using an updated libdeflate implementation that supports GDeflate.

## GDeflate
Builds a static library for a GDeflate CPU compressor/decompressor used by VolcanStorage.


# Build

1. Install [Visual Studio](http://www.visualstudio.com/downloads) 2019 or higher.
2. Launch a Developer Command Prompt
3. Navigate into the GDeflate subdirectory
4. Configure CMake using the command line, VSCode, or Visual Studio

Note: This code can be built using linux as long as you have CMake 3.19 and ninja-build installed.

## Command line
```
cmake --preset Debug
cmake --build --preset Debug
```

## VSCode
Launch VSCode in the GDeflate directory root.  CMake generation will happen automatically.

## Visual Studio
Launch Visual Studio and choose 'Open a local folder' and select the GDeflate directory root.