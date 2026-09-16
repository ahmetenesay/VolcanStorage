// SPDX-License-Identifier: Apache-2.0
// Ported for VolcanStorage (Vulkan Compute GLSL)

#ifndef TILESTREAM_GLSL
#define TILESTREAM_GLSL

const uint kDefaultTileSize = 64 * 1024;
const uint kStreamHeaderSize = 8;

struct TileParams
{
    uint inPos;
    uint inSize;
    uint outPos;
    uint outSize;
};

uint TileStream_GetField(uint value, uint bitsOffset, uint bitsLength)
{
    uint bitMask = (1u << bitsLength) - 1u;
    return (value >> bitsOffset) & bitMask;
}

struct TileStream
{
    uint m_word1;
    uint m_word2;
    uint m_numTiles;
};

TileStream TileStream_Construct(uint streamInPos)
{
    TileStream ts;
    // Input is word-aligned (4 bytes per uint)
    ts.m_word1 = inputBuffer.data[streamInPos >> 2];
    ts.m_word2 = inputBuffer.data[(streamInPos + 4) >> 2];
    ts.m_numTiles = TileStream_GetField(ts.m_word1, 16, 16);
    return ts;
}

uint TileStream_GetNumTiles(TileStream ts)
{
    return ts.m_numTiles;
}

uint TileStream_GetLastTileSizeField(TileStream ts)
{
    return TileStream_GetField(ts.m_word2, 2, 18);
}

uint TileStream_GetLastTileSize(TileStream ts)
{
    uint lastTileSize = TileStream_GetLastTileSizeField(ts);
    return (lastTileSize > 0) ? lastTileSize : kDefaultTileSize;
}

TileParams TileStream_GetTileParams(TileStream ts, uint streamInPos, uint streamOutPos, uint tileIdx)
{
    TileParams params;
    uint tileTablePos = streamInPos + kStreamHeaderSize;

    params.inPos = (tileIdx > 0) ? inputBuffer.data[(tileTablePos + tileIdx * 4) >> 2] : 0;

    if (tileIdx == ts.m_numTiles - 1)
    {
        params.inSize = inputBuffer.data[tileTablePos >> 2];
    }
    else
    {
        params.inSize = inputBuffer.data[(tileTablePos + (tileIdx + 1) * 4) >> 2] - params.inPos;
    }

    params.outPos = streamOutPos + tileIdx * kDefaultTileSize;
    params.outSize = (tileIdx < ts.m_numTiles - 1) ? kDefaultTileSize : TileStream_GetLastTileSize(ts);

    uint streamDataStartPos = tileTablePos + ts.m_numTiles * 4;
    params.inPos += streamDataStartPos;

    return params;
}

#endif // TILESTREAM_GLSL
