/*
* This file is part of Project SkyFire https://www.projectskyfire.org. 
* See LICENSE.md file for Copyright information
*/

#define _CRT_SECURE_NO_DEPRECATE

#include "loadlib.h"
#include <cstdio>

u_map_fcc MverMagic = { {'R','E','V','M'} };

ChunkedFile::ChunkedFile()
{
    data = 0;
    data_size = 0;
}

ChunkedFile::~ChunkedFile()
{
    free();
}

bool ChunkedFile::loadFile(HANDLE mpq, char* filename, bool log)
{
    free();
    HANDLE file;
    if (!SFileOpenFileEx(mpq, filename, SFILE_OPEN_FROM_MPQ, &file))
    {
        if (log)
            printf("No such file %s\n", filename);
        return false;
    }

    data_size = SFileGetFileSize(file, NULL);
    data = new uint8[data_size];
    SFileReadFile(file, data, data_size, NULL/*bytesRead*/, NULL);
    parseChunks();
    if (prepareLoadedData())
    {
        SFileCloseFile(file);
        return true;
    }

    printf("Error loading %s\n", filename);
    SFileCloseFile(file);
    free();
    return false;
}

bool ChunkedFile::prepareLoadedData()
{
    FileChunk* chunk = GetChunk("MVER");
    if (!chunk)
        return false;

    // Check version
    file_MVER* version = chunk->As<file_MVER>();
    if (version->fcc != MverMagic.fcc)
        return false;
    if (version->ver != FILE_FORMAT_VERSION)
        return false;
    return true;
}

void ChunkedFile::free()
{
    for (auto chunk : chunks)
        delete chunk.second;

    chunks.clear();

    delete[] data;
    data = 0;
    data_size = 0;
}

u_map_fcc InterestingChunks[] = {
    { 'R', 'E', 'V', 'M' },
    { 'N', 'I', 'A', 'M' },
    { 'O', '2', 'H', 'M' },
    { 'K', 'N', 'C', 'M' },
    { 'T', 'V', 'C', 'M' },
    { 'Q', 'L', 'C', 'M' },
    { 'X', 'E', 'T', 'M' },
    { 'K', 'N', 'C', 'M' }
};

bool IsInterestingChunk(u_map_fcc const& fcc)
{
    for (u_map_fcc const& f : InterestingChunks)
        if (f.fcc == fcc.fcc)
            return true;

    return false;
}

void ChunkedFile::parseChunks()
{
    uint8* ptr = GetData();// Prevent infinite loops or crashes on bad files
    uint8* endPtr = GetData() + GetDataSize();
    while (ptr < endPtr)
    {
        // 1. Read Header
        if (ptr + 8 > endPtr) break; // Safety check
        u_map_fcc header = *(u_map_fcc*)ptr;

        // 2. Read Size
        uint32 size = *(uint32*)(ptr + 4);

        // --- DEBUG: Print Every Chunk Found ---
        // Print as characters to see the name
       /* printf("[DEBUG] SCANNER: Found Chunk '%c%c%c%c' (Hex: %08X, Size: %u)\n",
            header.fcc_txt[0], header.fcc_txt[1], header.fcc_txt[2], header.fcc_txt[3],
            header.fcc, size);*/
        // --------------------------------------

        if (IsInterestingChunk(header))
        {
            if (size <= data_size)
            {
                // Swap bytes for internal storage (if needed)
                std::swap(header.fcc_txt[0], header.fcc_txt[3]);
                std::swap(header.fcc_txt[1], header.fcc_txt[2]);

                /*printf("[DEBUG] -> Capturing Interesting Chunk: %c%c%c%c\n",
                    header.fcc_txt[0], header.fcc_txt[1], header.fcc_txt[2], header.fcc_txt[3]);*/

                FileChunk* chunk = new FileChunk{ ptr, size };
                chunk->parseSubChunks();
                chunks.insert({ std::string(header.fcc_txt, 4), chunk });
            }
        }

        // move to next chunk
        ptr += size + 8;
    }
}

FileChunk* ChunkedFile::GetChunk(std::string const& name)
{
    auto range = chunks.equal_range(name);
    if (std::distance(range.first, range.second) == 1)
        return range.first->second;

    return NULL;
}

FileChunk::~FileChunk()
{
    for (auto subchunk : subchunks)
        delete subchunk.second;

    subchunks.clear();
}

void FileChunk::parseSubChunks()
{
    uint8* ptr = data + 8; // skip self
    while (ptr < data + size)
    {
        u_map_fcc header = *(u_map_fcc*)ptr;
        uint32 subsize = 0;
        if (IsInterestingChunk(header))
        {
            subsize = *(uint32*)(ptr + 4);
            if (subsize < size)
            {
                std::swap(header.fcc_txt[0], header.fcc_txt[3]);
                std::swap(header.fcc_txt[1], header.fcc_txt[2]);

                FileChunk* chunk = new FileChunk{ ptr, subsize };
                chunk->parseSubChunks();
                subchunks.insert({ std::string(header.fcc_txt, 4), chunk });
            }
        }

        // move to next chunk
        ptr += subsize + 8;
    }
}

FileChunk* FileChunk::GetSubChunk(std::string const& name)
{
    auto range = subchunks.equal_range(name);
    if (std::distance(range.first, range.second) == 1)
        return range.first->second;

    return NULL;
}
