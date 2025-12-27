/*
* This file is part of Project SkyFire https://www.projectskyfire.org. 
* See LICENSE.md file for Copyright information
*/

#define _CRT_SECURE_NO_DEPRECATE

#include <stdio.h>
#include <deque>
#include <list>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <string>
#include <iostream>
#include <fstream>

#ifdef _WIN64
#include "direct.h"
#define chdir _chdir
#elif defined (_WIN32)
#include "direct.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#define ERROR_PATH_NOT_FOUND ERROR_FILE_NOT_FOUND
#endif

#include "StormLib.h"
#include "dbcfile.h"

#include "adt.h"
#include "wdt.h"
#include <fcntl.h>

#if defined( __GNUC__ )
    #define _open   open
    #define _close close
    #ifndef O_BINARY
        #define O_BINARY 0
    #endif
#else
    #include <io.h>
#endif

#ifdef O_LARGEFILE
    #define OPEN_FLAGS (O_RDONLY | O_BINARY | O_LARGEFILE)
#else
    #define OPEN_FLAGS (O_RDONLY | O_BINARY)
#endif

HANDLE WorldMpq = NULL;
HANDLE LocaleMpq = NULL;

typedef struct
{
    char name[64];
    uint32 id;
} map_id;

map_id *map_ids;
uint16 *areas;
uint16 *LiqType;
char output_path[128] = ".";
char input_path[128] = ".";
uint32 maxAreaId = 0;

// **************************************************
// Extractor options
// **************************************************
enum Extract
{
    EXTRACT_MAP = 1,
    EXTRACT_DBC = 2,
    EXTRACT_CAMERA = 4
};

// Select data for extract
int   CONF_extract = EXTRACT_MAP | EXTRACT_DBC | EXTRACT_CAMERA;

// This option allow limit minimum height to some value (Allow save some memory)
bool  CONF_allow_height_limit = true;
float CONF_use_minHeight = -500.0f;

// This option allow use float to int conversion
bool  CONF_allow_float_to_int   = false;
float CONF_float_to_int8_limit  = 2.0f;      // Max accuracy = val/256
float CONF_float_to_int16_limit = 2048.0f;   // Max accuracy = val/65536
float CONF_flat_height_delta_limit = 0.005f; // If max - min less this value - surface is flat
float CONF_flat_liquid_delta_limit = 0.001f; // If max - min less this value - liquid surface is flat

uint32 CONF_TargetBuild = 18273;              // 5.4.8 18273 -- current build is 18414, but Blizz didnt rename the MPQ files

// List MPQ for extract maps from
char const* CONF_mpq_list[] =
{
    "world.MPQ",
    "model.MPQ",
    "misc.MPQ",
    "expansion1.MPQ",
    "expansion2.MPQ",
    "expansion3.MPQ",
    "expansion4.MPQ"
};

uint32 const Builds[] = {16016, 16048, 16057, 16309, 16357, 16516, 16650, 16844, 16965, 17116, 17266, 17325, 17345, 17538, 17645, 17688, 17898, 18273};
//#define LAST_DBC_IN_DATA_BUILD 13623    // after this build mpqs with dbc are back to locale folder
#define NEW_BASE_SET_BUILD  15211
#define LOCALES_COUNT 15

char const* Locales[LOCALES_COUNT] =
{
    "enGB", "enUS",
    "deDE", "esES",
    "frFR", "koKR",
    "zhCN", "zhTW",
    "enCN", "enTW",
    "esMX", "ruRU",
    "ptBR", "ptPT",
    "itIT",
};

TCHAR const* LocalesT[LOCALES_COUNT] =
{
    _T("enGB"), _T("enUS"),
    _T("deDE"), _T("esES"),
    _T("frFR"), _T("koKR"),
    _T("zhCN"), _T("zhTW"),
    _T("enCN"), _T("enTW"),
    _T("esMX"), _T("ruRU"),
    _T("ptBR"), _T("ptPT"),
    _T("itIT"),
};

void CreateDir(std::string const& path)
{
    if (chdir(path.c_str()) == 0)
    {
            chdir("../");
            return;
    }

#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IRWXO); // 0777
#endif
}

bool FileExists(TCHAR const* fileName)
{
    int fp = _open(fileName, O_RDONLY | O_BINARY);
    if(fp != -1)
    {
        _close(fp);
        return true;
    }

    return false;
}

void Usage(char const* prg)
{
    printf(
        "Usage:\n"\
        "%s -[var] [value]\n"\
        "-i set input path\n"\
        "-o set output path\n"\
        "-e extract only MAP(1)/DBC(2)/Camera(4) - standard: all(7)\n"\
        "-f height stored as int (less map size but lost some accuracy) 1 by default\n"\
        "-b target build (default %u)\n"\
        "Example: %s -f 0 -i \"c:\\games\\game\"", prg, CONF_TargetBuild, prg);
    exit(1);
}

void HandleArgs(int argc, char* arg[])
{
    for (int c = 1; c < argc; ++c)
    {
        // i - input path
        // o - output path
        // e - extract only MAP(1)/DBC(2) - standard both(3)
        // f - use float to int conversion
        // h - limit minimum height
        // b - target client build
        if (arg[c][0] != '-')
            Usage(arg[0]);

        switch (arg[c][1])
        {
            case 'i':
                if (c + 1 < argc)                            // all ok
                    strcpy(input_path, arg[c++ + 1]);
                else
                    Usage(arg[0]);
                break;
            case 'o':
                if (c + 1 < argc)                            // all ok
                    strcpy(output_path, arg[c++ + 1]);
                else
                    Usage(arg[0]);
                break;
            case 'f':
                if (c + 1 < argc)                            // all ok
                    CONF_allow_float_to_int = atoi(arg[c++ + 1])!=0;
                else
                    Usage(arg[0]);
                break;
            case 'e':
                if (c + 1 < argc)                            // all ok
                {
                    CONF_extract = atoi(arg[c++ + 1]);
                    if (!(CONF_extract > 0 && CONF_extract < 8))
                        Usage(arg[0]);
                }
                else
                    Usage(arg[0]);
                break;
            case 'b':
                if (c + 1 < argc)                            // all ok
                    CONF_TargetBuild = atoi(arg[c++ + 1]);
                else
                    Usage(arg[0]);
                break;
            default:
                break;
        }
    }
}

uint32 ReadBuild(int locale)
{
    // include build info file also
    std::string filename  = std::string("component.wow-") + Locales[locale] + ".txt";
    //printf("Read %s file... ", filename.c_str());

    HANDLE dbcFile;
    if (!SFileOpenFileEx(LocaleMpq, filename.c_str(), SFILE_OPEN_FROM_MPQ, &dbcFile))
    {
        printf("Fatal error: Not found %s file!\n", filename.c_str());
        exit(1);
    }

    char buff[512] = { 0 };
    DWORD readBytes = 0;
    SFileReadFile(dbcFile, buff, 512, &readBytes, NULL);
    if (!readBytes)
    {
        printf("Fatal error: Not found %s file!\n", filename.c_str());
        exit(1);
    }

    std::string text = buff;
    SFileCloseFile(dbcFile);
    std::string version = "version=\"";

    size_t pos = text.find(version);
    size_t pos1 = pos + version.length();
    size_t pos2 = text.find("\"", pos1);
    if (pos == text.npos || pos2 == text.npos || pos1 >= pos2)
    {
        printf("Fatal error: Invalid  %s file format!\n", filename.c_str());
        exit(1);
    }

    std::string build_str = text.substr(pos1,pos2-pos1);

    int build = atoi(build_str.c_str());
    if (build <= 0)
    {
        printf("Fatal error: Invalid  %s file format!\n", filename.c_str());
        exit(1);
    }

    return build;
}

uint32 ReadMapDBC()
{
    printf("Read Map.dbc file... ");

    HANDLE dbcFile;
    if (!SFileOpenFileEx(LocaleMpq, "DBFilesClient\\Map.dbc", SFILE_OPEN_FROM_MPQ, &dbcFile))
    {
        printf("Fatal error: Cannot find Map.dbc in archive!\n");
        exit(1);
    }

    DBCFile dbc(dbcFile);
    if (!dbc.open())
    {
        printf("Fatal error: Invalid Map.dbc file format!\n");
        exit(1);
    }

    size_t map_count = dbc.getRecordCount();
    map_ids = new map_id[map_count];
    for(uint32 x = 0; x < map_count; ++x)
    {
        map_ids[x].id = dbc.getRecord(x).getUInt(0);
        strcpy(map_ids[x].name, dbc.getRecord(x).getString(1));
    }

    SFileCloseFile(dbcFile);
    printf("Done! (%u maps loaded)\n", uint32(map_count));
    return map_count;
}

void ReadAreaTableDBC()
{
    printf("Read AreaTable.dbc file...");
    HANDLE dbcFile;
    if (!SFileOpenFileEx(LocaleMpq, "DBFilesClient\\AreaTable.dbc", SFILE_OPEN_FROM_MPQ, &dbcFile))
    {
        printf("Fatal error: Cannot find AreaTable.dbc in archive!\n");
        exit(1);
    }

    DBCFile dbc(dbcFile);
    if(!dbc.open())
    {
        printf("Fatal error: Invalid AreaTable.dbc file format!\n");
        exit(1);
    }

    size_t area_count = dbc.getRecordCount();
    maxAreaId = dbc.getMaxId();
    areas = new uint16[maxAreaId + 1];

    for (uint32 x = 0; x < area_count; ++x)
        areas[dbc.getRecord(x).getUInt(0)] = dbc.getRecord(x).getUInt(3);

    SFileCloseFile(dbcFile);
    printf("Done! (%u areas loaded)\n", uint32(area_count));
}

void ReadLiquidTypeTableDBC()
{
    printf("Read LiquidType.dbc file...");
    HANDLE dbcFile;
    if (!SFileOpenFileEx(LocaleMpq, "DBFilesClient\\LiquidType.dbc", SFILE_OPEN_FROM_MPQ, &dbcFile))
    {
        printf("Fatal error: Cannot find LiquidType.dbc in archive!\n");
        exit(1);
    }

    DBCFile dbc(dbcFile);
    if (!dbc.open())
    {
        printf("Fatal error: Invalid LiquidType.dbc file format!\n");
        exit(1);
    }

    size_t liqTypeCount = dbc.getRecordCount();
    size_t liqTypeMaxId = dbc.getMaxId();
    LiqType = new uint16[liqTypeMaxId + 1];
    memset(LiqType, 0xff, (liqTypeMaxId + 1) * sizeof(uint16));

    for (uint32 x = 0; x < liqTypeCount; ++x)
        LiqType[dbc.getRecord(x).getUInt(0)] = dbc.getRecord(x).getUInt(3);

    SFileCloseFile(dbcFile);
    printf("Done! (%u LiqTypes loaded)\n", (uint32)liqTypeCount);
}

//
// Adt file convertor function and data
//

// Map file format data
static char const* MAP_MAGIC         = "MAPS";
static char const* MAP_VERSION_MAGIC = "v1.4";
static char const* MAP_AREA_MAGIC    = "AREA";
static char const* MAP_HEIGHT_MAGIC  = "MHGT";
static char const* MAP_LIQUID_MAGIC  = "MLIQ";

struct map_fileheader
{
    uint32 mapMagic;
    uint32 versionMagic;
    uint32 buildMagic;
    uint32 areaMapOffset;
    uint32 areaMapSize;
    uint32 heightMapOffset;
    uint32 heightMapSize;
    uint32 liquidMapOffset;
    uint32 liquidMapSize;
    uint32 holesOffset;
    uint32 holesSize;
    uint32 terrainMapOffset;
    uint32 terrainMapSize;
};

#define MAP_AREA_NO_AREA      0x0001

struct map_areaHeader
{
    uint32 fourcc;
    uint16 flags;
    uint16 gridArea;
};

#define MAP_HEIGHT_NO_HEIGHT  0x0001
#define MAP_HEIGHT_AS_INT16   0x0002
#define MAP_HEIGHT_AS_INT8    0x0004

struct map_heightHeader
{
    uint32 fourcc;
    uint32 flags;
    float  gridHeight;
    float  gridMaxHeight;
};

#define MAP_LIQUID_TYPE_NO_WATER    0x00
#define MAP_LIQUID_TYPE_WATER       0x01
#define MAP_LIQUID_TYPE_OCEAN       0x02
#define MAP_LIQUID_TYPE_MAGMA       0x04
#define MAP_LIQUID_TYPE_SLIME       0x08

#define MAP_LIQUID_TYPE_DARK_WATER  0x10
#define MAP_LIQUID_TYPE_WMO_WATER   0x20

#define MAP_LIQUID_NO_TYPE    0x0001
#define MAP_LIQUID_NO_HEIGHT  0x0002

struct map_liquidHeader
{
    uint32 fourcc;
    uint16 flags;
    uint16 liquidType;
    uint8  offsetX;
    uint8  offsetY;
    uint8  width;
    uint8  height;
    float  liquidLevel;
};

float selectUInt8StepStore(float maxDiff)
{
    return 255 / maxDiff;
}

float selectUInt16StepStore(float maxDiff)
{
    return 65535 / maxDiff;
}
// Temporary grid data store
uint16 area_flags[ADT_CELLS_PER_GRID][ADT_CELLS_PER_GRID];

float V8[ADT_GRID_SIZE][ADT_GRID_SIZE];
float V9[ADT_GRID_SIZE+1][ADT_GRID_SIZE+1];
uint16 uint16_V8[ADT_GRID_SIZE][ADT_GRID_SIZE];
uint16 uint16_V9[ADT_GRID_SIZE+1][ADT_GRID_SIZE+1];
uint8  uint8_V8[ADT_GRID_SIZE][ADT_GRID_SIZE];
uint8  uint8_V9[ADT_GRID_SIZE+1][ADT_GRID_SIZE+1];

uint16 liquid_entry[ADT_CELLS_PER_GRID][ADT_CELLS_PER_GRID];
uint8 liquid_flags[ADT_CELLS_PER_GRID][ADT_CELLS_PER_GRID];
bool  liquid_show[ADT_GRID_SIZE][ADT_GRID_SIZE];
float liquid_height[ADT_GRID_SIZE+1][ADT_GRID_SIZE+1];
uint16 holes[ADT_CELLS_PER_GRID][ADT_CELLS_PER_GRID];

// Debug helper to print string matches
bool IsRoadTexture(const char* fullPath)
{
    std::string path(fullPath);

    // 1. STRIP DIRECTORY (Only look at the filename)
    // This prevents "Tileset\Grass.blp" from matching "Tile"
    std::string filename = path;
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        filename = path.substr(lastSlash + 1);
    }

    // Convert to lowercase for comparison
    std::string s = filename;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    // 2. EXCLUSIONS (Stuff that is NEVER a road)
    if (s.find("grass") != std::string::npos) return false;
    if (s.find("cliff") != std::string::npos) return false;
    if (s.find("rock") != std::string::npos) return false;
    if (s.find("bush") != std::string::npos) return false;
    if (s.find("flower") != std::string::npos) return false;
    if (s.find("dirt") != std::string::npos) return false; // Explicitly exclude Dirt
    if (s.find("sand") != std::string::npos) return false;
    if (s.find("mud") != std::string::npos) return false;
    if (s.find("swamp") != std::string::npos) return false;

    // 3. INCLUSIONS (Strong Indicators)
    if (s.find("road") != std::string::npos) return true;
    if (s.find("path") != std::string::npos) return true;
    if (s.find("trail") != std::string::npos) return true;
    if (s.find("street") != std::string::npos) return true;
    if (s.find("highway") != std::string::npos) return true;
    if (s.find("pave") != std::string::npos) return true;
    if (s.find("cobble") != std::string::npos) return true;
    if (s.find("brick") != std::string::npos) return true;
    if (s.find("plank") != std::string::npos) return true;
    if (s.find("cart") != std::string::npos) return true; // Cart tracks

    // 4. WEAK INDICATORS (Context sensitive)

    // "Stone" is often cliffs. Only count if it implies a floor/structure.
    if (s.find("stone") != std::string::npos) {
        if (s.find("floor") != std::string::npos) return true;
        if (s.find("walk") != std::string::npos) return true;
        if (s.find("way") != std::string::npos) return true;
    }

    // "Tile" matches "Tileset" (which we stripped, but just in case)
    if (s.find("tile") != std::string::npos) {
        if (s.find("tileset") != std::string::npos) return false;
        return true; // Matches "FloorTile", "CityTile"
    }

    // "Wood" matches "Felwood". Only count if it implies planks/floor.
    if (s.find("wood") != std::string::npos) {
        if (s.find("floor") != std::string::npos) return true;
        if (s.find("plank") != std::string::npos) return true;
        if (s.find("bridge") != std::string::npos) return true;
    }

    return false;
}

static uint8 terrain_type[ADT_GRID_SIZE][ADT_GRID_SIZE];

// --- HELPER: Generate High-Res Detail Map (128x128) ---
void GenerateDetailMapSVG(int map_x, int map_y, map_id map_id)
{
    // Only generate for the map we are debugging (530)
    if (map_id.id != 530) return;

    char filename[128];
    sprintf(filename, "Detail_Map_%02d_%02d.svg", map_y, map_x);

    std::ofstream svg(filename);
    if (!svg.is_open()) return;

    // Viewbox is 128x128 (8 cells per chunk * 16 chunks)
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1024\" height=\"1024\" viewBox=\"0 0 128 128\">";

    // Background (Green/Grass)
    svg << "<rect x=\"0\" y=\"0\" width=\"128\" height=\"128\" fill=\"#228822\" />\n";

    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            // terrain_type: 2 = ROAD, 1 = GROUND (Default)
            if (terrain_type[y][x] == 2) {
                // Draw Road Pixel (Grey)
                svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"1\" height=\"1\" "
                    << "fill=\"#AAAAAA\" />\n";
            }
        }
    }

    // Overlay Chunk Borders (for reference)
    for (int i = 0; i <= 128; i += 8) {
        svg << "<line x1=\"" << i << "\" y1=\"0\" x2=\"" << i << "\" y2=\"128\" stroke=\"black\" stroke-width=\"0.1\" opacity=\"0.5\" />";
        svg << "<line x1=\"0\" y1=\"" << i << "\" x2=\"128\" y2=\"" << i << "\" stroke=\"black\" stroke-width=\"0.1\" opacity=\"0.5\" />";
    }

    svg << "</svg>";
    svg.close();
    printf("[OUTPUT] Generated Detail Map: %s\n", filename);
}

// --- HELPER: Generate SVG Debug Map (Detailed) ---
void GenerateTextureSVG(int cell_x, int cell_y,
    const std::vector<std::string>& texNames,
    const std::vector<bool>& texIsRoad,
    const std::vector<uint32> chunkTextures[16][16], map_id map_id) // Now accepts a list of textures per chunk
{
    char filename[128];
    if (map_id.id != 530000) {
        sprintf(filename, "Debug_Tile_%02d_%02d.svg", cell_y, cell_x);

        std::ofstream svg(filename);
        if (!svg.is_open()) return;

        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2048\" height=\"2048\" viewBox=\"0 0 16 16\">";
        // Smaller font to fit multiple lines
        svg << "<style>text { font: 0.08px monospace; fill: white; pointer-events: none; }</style>\n";

        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {

                // Determine Box Color
                // If ANY texture in this chunk is a road, color it dark grey.
                bool hasRoad = false;
                for (uint32 texID : chunkTextures[y][x]) {
                    if (texID < texIsRoad.size() && texIsRoad[texID]) {
                        hasRoad = true;
                        break;
                    }
                }
                std::string color = hasRoad ? "#444444" : "#228822";

                // Draw Box
                svg << "<rect x=\"" << x << "\" y=\"" << y << "\" width=\"1\" height=\"1\" "
                    << "fill=\"" << color << "\" stroke=\"black\" stroke-width=\"0.02\" />\n";

                // Draw Text List
                float textY = y + 0.15f;
                for (uint32 texID : chunkTextures[y][x])
                {
                    std::string name = (texID < texNames.size()) ? texNames[texID] : "???";

                    // Cleanup name (remove path)
                    size_t slash = name.find_last_of("\\/");
                    if (slash != std::string::npos) name = name.substr(slash + 1);

                    // Highlight road text in Yellow
                    std::string textColor = (texID < texIsRoad.size() && texIsRoad[texID]) ? "yellow" : "white";

                    svg << "<text x=\"" << (x + 0.05) << "\" y=\"" << textY << "\" fill=\"" << textColor << "\">"
                        << name << "</text>\n";

                    textY += 0.1f; // Move down for next layer
                }
            }
        }
        svg << "</svg>";
        svg.close();
        printf("Generated debug map: %s\n", filename);
    }
}

// Structure to read MTEX chunk (Texture Filenames)
struct adt_MTEX {
    uint32 fcc;
    uint32 size;
};

// --- Helper to find a specific MCNK chunk in a file by coordinates ---
adt_MCNK* GetMCNK(ChunkedFile& file, int x, int y)
{
    // Try standard MCNK
    auto range = file.chunks.equal_range("MCNK");
    for (auto it = range.first; it != range.second; ++it) {
        adt_MCNK* mcnk = it->second->As<adt_MCNK>();
        if (mcnk->ix == x && mcnk->iy == y) return mcnk;
    }
    // Try reversed KNCM
    range = file.chunks.equal_range("KNCM");
    for (auto it = range.first; it != range.second; ++it) {
        adt_MCNK* mcnk = it->second->As<adt_MCNK>();
        if (mcnk->ix == x && mcnk->iy == y) return mcnk;
    }
    return nullptr;
}

// --- Helper: Safe Decompress ADT Alpha Map (Relaxed) ---
// Returns true to indicate we should use the data (even if partial)
bool DecompressAlphaSafe(uint8* src, size_t srcLen, uint8* dst)
{
    // 1. Initialize destination to 0 (Transparent)
    memset(dst, 0, 4096);

    if (!src || srcLen == 0) return false;

    uint8* srcEnd = src + srcLen;
    int i = 0; // Destination index (0..4096)

    while (i < 4096 && src < srcEnd)
    {
        uint8 info = *src++;

        // Safety: If Fill Mode needs a value byte but we are at end
        if (src >= srcEnd && (info & 0x80)) break;

        bool fill = (info & 0x80); // Top bit = Mode (1=Fill, 0=Copy)
        int count = (info & 0x7F); // Bottom 7 bits = Count

        if (count == 0) continue;

        if (fill)
        {
            uint8 val = *src++;
            for (int k = 0; k < count && i < 4096; ++k) dst[i++] = val;
        }
        else
        {
            // Copy mode
            for (int k = 0; k < count && i < 4096; ++k) {
                if (src >= srcEnd) break;
                dst[i++] = *src++;
            }
        }
    }

    // Always return true. Partial data is better than no data.
    return true;
}

// --- Helper: Calculate In-Game Coordinates ---
void GetChunkCoordinates(int mapX, int mapY, int chunkX, int chunkY, float& outX, float& outY)
{
    // WoW Constants
    const float TILE_SIZE = 533.33333f;
    const float CHUNK_SIZE = TILE_SIZE / 16.0f;
    const float ZERO_POINT = 32.0f * TILE_SIZE;

    // Calculate Top-Left of the Tile
    float tileStartX = ZERO_POINT - (mapX * TILE_SIZE);
    float tileStartY = ZERO_POINT - (mapY * TILE_SIZE);

    // Calculate Center of the Chunk (Chunk Y corresponds to X axis in-game, Chunk X to Y axis... it's rotated)
    // Actually, standard ADT mapping:
    // Game X = (32 - TileY) * 533.33 - (ChunkY * 33.33)
    // Game Y = (32 - TileX) * 533.33 - (ChunkX * 33.33)

    outX = ((32 - mapY) * TILE_SIZE) - (chunkY * CHUNK_SIZE) - (CHUNK_SIZE / 2);
    outY = ((32 - mapX) * TILE_SIZE) - (chunkX * CHUNK_SIZE) - (CHUNK_SIZE / 2);
}

// --- Helper: Calculate In-Game Coordinates for a specific 8x8 Cell ---
void GetCellCoordinates(int tileX, int tileY, int chunkX, int chunkY, int cellX, int cellY, float& outX, float& outY)
{
    // WoW Constants
    const float TILE_SIZE = 533.33333f;
    const float CHUNK_SIZE = TILE_SIZE / 16.0f;     // ~33.33 yards
    const float UNIT_SIZE = CHUNK_SIZE / 8.0f;      // ~4.16 yards (8x8 grid)
    const float ZERO_POINT = 32.0f * TILE_SIZE;

    // Tile Start (Top-Left of the Tile)
    // Game X = (32 - TileY) * 533.33
    // Game Y = (32 - TileX) * 533.33
    float tileStartX = ZERO_POINT - (tileY * TILE_SIZE);
    float tileStartY = ZERO_POINT - (tileX * TILE_SIZE);

    // Chunk Start (Top-Left of the Chunk)
    // Game X decreases as ChunkY (Rows) increases
    // Game Y decreases as ChunkX (Cols) increases
    float chunkStartX = tileStartX - (chunkY * CHUNK_SIZE);
    float chunkStartY = tileStartY - (chunkX * CHUNK_SIZE);

    // Cell Center
    // We want the center of the 8x8 block
    outX = chunkStartX - (cellY * UNIT_SIZE) - (UNIT_SIZE / 2.0f);
    outY = chunkStartY - (cellX * UNIT_SIZE) - (UNIT_SIZE / 2.0f);
}

bool ConvertADT(char* filename, char* filename2, int cell_y, int cell_x, uint32 build, map_id map_id)
{
    ChunkedFile adt;
    if (!adt.loadFile(WorldMpq, filename))
        return false;

    // --- LOAD _tex0.adt ---
    ChunkedFile texAdt;
    bool hasTexFile = false;
    std::string texFilename = filename;
    size_t dotPos = texFilename.find_last_of('.');
    if (dotPos != std::string::npos) {
        texFilename.insert(dotPos, "_tex0");
        hasTexFile = texAdt.loadFile(WorldMpq, (char*)texFilename.c_str(), false);
        if (hasTexFile) printf("[DEBUG] Loaded Texture File: %s\n", texFilename.c_str());
    }

    // --- MAP TEXTURE CHUNKS TO GRID ---
    adt_MCNK* texChunkGrid[16][16];
    memset(texChunkGrid, 0, sizeof(texChunkGrid));

    if (hasTexFile)
    {
        auto range = texAdt.chunks.equal_range("MCNK");
        if (range.first == range.second) range = texAdt.chunks.equal_range("KNCM");

        int idx = 0;
        for (auto it = range.first; it != range.second; ++it)
        {
            adt_MCNK* mcnk = it->second->As<adt_MCNK>();
            if (mcnk->ix < 16 && mcnk->iy < 16) {
                texChunkGrid[mcnk->iy][mcnk->ix] = mcnk;
            }
            else {
                int r = idx / 16;
                int c = idx % 16;
                if (r < 16) texChunkGrid[r][c] = mcnk;
            }
            idx++;
        }
    }

    map_fileheader map;
    map.mapMagic = *(uint32 const*)MAP_MAGIC;
    map.versionMagic = *(uint32 const*)MAP_VERSION_MAGIC;
    map.buildMagic = build;

    // Reset Arrays
    memset(area_flags, 0xFF, sizeof(area_flags));
    memset(V9, 0, sizeof(V9));
    memset(V8, 0, sizeof(V8));
    memset(liquid_show, 0, sizeof(liquid_show));
    memset(liquid_flags, 0, sizeof(liquid_flags));
    memset(liquid_entry, 0, sizeof(liquid_entry));
    memset(holes, 0, sizeof(holes));
    memset(terrain_type, 1, sizeof(terrain_type)); // 1 = Ground

    bool hasHoles = false;

    // --- READ TEXTURE NAMES ---
    FileChunk* mtexChunk = hasTexFile ? texAdt.GetChunk("MTEX") : nullptr;
    if (!mtexChunk && hasTexFile) mtexChunk = texAdt.GetChunk("XETM");
    if (!mtexChunk) mtexChunk = adt.GetChunk("MTEX");
    if (!mtexChunk) mtexChunk = adt.GetChunk("XETM");

    std::vector<bool> textureIsRoad;
    std::vector<std::string> textureNames;

    if (mtexChunk)
    {
        adt_MTEX* mtex = mtexChunk->As<adt_MTEX>();
        char* filenames = (char*)mtex + 8;
        uint32 size = mtex->size;
        uint32 i = 0;
        while (i < size)
        {
            char* name = &filenames[i];
            textureNames.push_back(std::string(name));
            textureIsRoad.push_back(IsRoadTexture(name));
            i += strlen(name) + 1;
        }
    }

    std::vector<uint32> debugTextureLayers[16][16];

    // --- CREATE AUDIT FILE ---
    /*char auditFilename[128];
    sprintf(auditFilename, "TextureAudit_%u_%02d_%02d.txt", map_id.id, cell_y, cell_x);
    std::ofstream auditFile(auditFilename);
    if (auditFile.is_open()) {
        auditFile << "ChunkX,ChunkY,GameCoordX,GameCoordY,TextureName,Roadness,IsRoad\n";
        printf("[AUDIT] Writing full texture log to %s...\n", auditFilename);
    }*/

    // --- ITERATE MAIN CHUNKS ---
    auto itrRange = adt.chunks.equal_range("MCNK");
    if (itrRange.first == itrRange.second) itrRange = adt.chunks.equal_range("KNCM");

    for (auto itr = itrRange.first; itr != itrRange.second; ++itr)
    {
        adt_MCNK* mcnk = itr->second->As<adt_MCNK>();

        adt_MCNK* texMcnk = mcnk;
        if (hasTexFile && mcnk->iy < 16 && mcnk->ix < 16) {
            if (texChunkGrid[mcnk->iy][mcnk->ix]) {
                texMcnk = texChunkGrid[mcnk->iy][mcnk->ix];
            }
        }

        // --- 3. READ LAYERS & AUDIT ---
        bool chunkHasRoad = false;
        int maxLayers = 4;

        adt_MCNK* sourceMcnk = texMcnk;
        uint32 readOffset = texMcnk->offsMCLY;
        bool usingMain = (texMcnk == mcnk);
        bool dataFound = false;

        // [Offset Logic - Smart Rewind]
        bool standardOffsetBad = false;
        if (hasTexFile && readOffset > 0 && readOffset < texMcnk->size) {
            adt_MCLY* t = (adt_MCLY*)((uint8*)texMcnk + readOffset);
            if (t->textureId == 0xFFFFFFFF || (textureNames.size() > 0 && t->textureId > textureNames.size() + 50))
                standardOffsetBad = true;
        }
        else standardOffsetBad = true;

        if (standardOffsetBad && hasTexFile) {
            if (64 < texMcnk->size) {
                uint32 candidateOffset = 64;
                while (candidateOffset >= 32) {
                    uint32 prevOffset = candidateOffset - 16;
                    adt_MCLY* prev = (adt_MCLY*)((uint8*)texMcnk + prevOffset);
                    if (prev->textureId < textureNames.size()) candidateOffset = prevOffset;
                    else break;
                }
                adt_MCLY* t = (adt_MCLY*)((uint8*)texMcnk + candidateOffset);
                if (t->textureId < textureNames.size()) {
                    readOffset = candidateOffset;
                    standardOffsetBad = false;
                }
            }
        }
        if (standardOffsetBad && mcnk->offsMCLY > 0) {
            sourceMcnk = mcnk;
            readOffset = mcnk->offsMCLY;
            usingMain = true;
        }

        // Arrays for Pixel Logic
        float pixelRoadness[64][64];
        memset(pixelRoadness, 0, sizeof(pixelRoadness));
        int dominantTexID[64][64];
        memset(dominantTexID, -1, sizeof(dominantTexID));

        int validLayerCount = 0;
        if (readOffset > 0 && readOffset < sourceMcnk->size) {
            adt_MCLY* scan = (adt_MCLY*)((uint8*)sourceMcnk + readOffset);
            for (int l = 0; l < maxLayers; ++l) {
                if (readOffset + (l + 1) * sizeof(adt_MCLY) > sourceMcnk->size) break;
                if (scan[l].textureId >= textureNames.size()) break;
                validLayerCount++;
            }
        }

        if (validLayerCount > 0)
        {
            adt_MCLY* mcly = (adt_MCLY*)((uint8*)sourceMcnk + readOffset);
            uint32 mcalStart = readOffset + (validLayerCount * sizeof(adt_MCLY));

            for (int l = 0; l < validLayerCount; ++l)
            {
                uint32 texID = mcly[l].textureId;
                dataFound = true;
                debugTextureLayers[mcnk->iy][mcnk->ix].push_back(texID);
                bool isRoad = textureIsRoad[texID];

                if (l == 0) {
                    float val = isRoad ? 1.0f : 0.0f;
                    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
                        pixelRoadness[y][x] = val;
                        dominantTexID[y][x] = texID;
                    }
                }
                else {
                    uint32 offsetInMCAL = mcly[l].offsetInMCAL;
                    uint32 alphaPtrOffset = mcalStart + offsetInMCAL;
                    if (alphaPtrOffset < sourceMcnk->size) {
                        uint8* alphaSrc = (uint8*)sourceMcnk + alphaPtrOffset;
                        size_t remainingSize = sourceMcnk->size - alphaPtrOffset;
                        uint8 alphaMap[4096];
                        if ((sourceMcnk->flags & 0x200) || remainingSize < 4096)
                            DecompressAlphaSafe(alphaSrc, remainingSize, alphaMap);
                        else memcpy(alphaMap, alphaSrc, 4096);

                        float layerVal = isRoad ? 1.0f : 0.0f;
                        for (int y = 0; y < 64; ++y) {
                            for (int x = 0; x < 64; ++x) {
                                float alpha = alphaMap[y * 64 + x] / 255.0f;
                                pixelRoadness[y][x] = pixelRoadness[y][x] * (1.0f - alpha) + layerVal * alpha;
                                if (alpha > 0.5f) dominantTexID[y][x] = texID;
                            }
                        }
                    }
                }
            }
        }

        // Fallback
        if (!dataFound && !(mcnk->flags & 0x10000) && !textureNames.empty()) {
            uint32 defID = 0;
            debugTextureLayers[mcnk->iy][mcnk->ix].push_back(defID);
            if (textureIsRoad[defID]) {
                for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) pixelRoadness[y][x] = 1.0f;
            }
            for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) dominantTexID[y][x] = defID;
        }

        // --- DOWNSAMPLE & WRITE TO FILE ---
        for (int cy = 0; cy < ADT_CELL_SIZE; ++cy) { // 8 Cells High
            for (int cx = 0; cx < ADT_CELL_SIZE; ++cx) { // 8 Cells Wide
                float sum = 0.0f;
                float maxVal = 0.0f;
                int cellDominantID = -1;
                int maxCount = -1;
                std::vector<int> counts(textureNames.size(), 0);

                for (int py = 0; py < 8; ++py) {
                    for (int px = 0; px < 8; ++px) {
                        float v = pixelRoadness[cy * 8 + py][cx * 8 + px];
                        sum += v;
                        if (v > maxVal) maxVal = v;

                        int tid = dominantTexID[cy * 8 + py][cx * 8 + px];
                        if (tid >= 0 && tid < textureNames.size()) {
                            counts[tid]++;
                            if (counts[tid] > maxCount) {
                                maxCount = counts[tid];
                                cellDominantID = tid;
                            }
                        }
                    }
                }
                float avg = sum / 64.0f;

                // --- WRITE LINE TO FILE ---
                //if (auditFile.is_open()) {
                //    float gameX, gameY;
                //    GetCellCoordinates(cell_y, cell_x, mcnk->ix, mcnk->iy, cx, cy, gameX, gameY);
                //    std::string texName = (cellDominantID >= 0) ? textureNames[cellDominantID] : "None";

                //    // Simplify name (remove path)
                //    size_t slash = texName.find_last_of("\\/");
                //    if (slash != std::string::npos) texName = texName.substr(slash + 1);

                //    bool isRoad = (avg > 0.15f || maxVal > 0.60f);

                //    auditFile << mcnk->ix << "," << mcnk->iy << ","
                //        << gameX << "," << gameY << ","
                //        << texName << "," << avg << "," << (isRoad ? "1" : "0") << "\n";
                //}

                //if (avg > 0.15f || maxVal > 0.60f) {
                //    terrain_type[mcnk->iy * ADT_CELL_SIZE + cy][mcnk->ix * ADT_CELL_SIZE + cx] = 2;
                //}
            }
        }

        // -- Area Data --
        if (mcnk->areaid <= maxAreaId && areas[mcnk->areaid] != 0xFFFF)
            area_flags[mcnk->iy][mcnk->ix] = areas[mcnk->areaid];

        // -- Height Data --
        for (int y = 0; y <= ADT_CELL_SIZE; y++)
        {
            int cy = mcnk->iy * ADT_CELL_SIZE + y;
            for (int x = 0; x <= ADT_CELL_SIZE; x++)
            {
                int cx = mcnk->ix * ADT_CELL_SIZE + x;
                V9[cy][cx] = mcnk->ypos;
            }
        }

        for (int y = 0; y < ADT_CELL_SIZE; y++)
        {
            int cy = mcnk->iy * ADT_CELL_SIZE + y;
            for (int x = 0; x < ADT_CELL_SIZE; x++)
            {
                int cx = mcnk->ix * ADT_CELL_SIZE + x;
                V8[cy][cx] = mcnk->ypos;
            }
        }

        if (FileChunk* chunk = itr->second->GetSubChunk("MCVT"))
        {
            adt_MCVT* mcvt = chunk->As<adt_MCVT>();
            for (int y = 0; y <= ADT_CELL_SIZE; y++)
            {
                int cy = mcnk->iy * ADT_CELL_SIZE + y;
                for (int x = 0; x <= ADT_CELL_SIZE; x++)
                {
                    int cx = mcnk->ix * ADT_CELL_SIZE + x;
                    V9[cy][cx] += mcvt->height_map[y * (ADT_CELL_SIZE * 2 + 1) + x];
                }
            }
            for (int y = 0; y < ADT_CELL_SIZE; y++)
            {
                int cy = mcnk->iy * ADT_CELL_SIZE + y;
                for (int x = 0; x < ADT_CELL_SIZE; x++)
                {
                    int cx = mcnk->ix * ADT_CELL_SIZE + x;
                    V8[cy][cx] += mcvt->height_map[y * (ADT_CELL_SIZE * 2 + 1) + ADT_CELL_SIZE + 1 + x];
                }
            }
        }

        // -- Liquid Data --
        if (mcnk->sizeMCLQ > 8)
        {
            if (FileChunk* chunk = itr->second->GetSubChunk("MCLQ"))
            {
                adt_MCLQ* liquid = chunk->As<adt_MCLQ>();
                int count = 0;
                for (int y = 0; y < ADT_CELL_SIZE; ++y)
                {
                    int cy = mcnk->iy * ADT_CELL_SIZE + y;
                    for (int x = 0; x < ADT_CELL_SIZE; ++x)
                    {
                        int cx = mcnk->ix * ADT_CELL_SIZE + x;
                        if (liquid->flags[y][x] != 0x0F)
                        {
                            liquid_show[cy][cx] = true;
                            if (liquid->flags[y][x] & (1 << 7))
                                liquid_flags[mcnk->iy][mcnk->ix] |= MAP_LIQUID_TYPE_DARK_WATER;
                            ++count;
                        }
                    }
                }

                uint32 c_flag = mcnk->flags;
                if (c_flag & (1 << 2))
                {
                    liquid_entry[mcnk->iy][mcnk->ix] = 1;
                    liquid_flags[mcnk->iy][mcnk->ix] |= MAP_LIQUID_TYPE_WATER;
                }
                if (c_flag & (1 << 3))
                {
                    liquid_entry[mcnk->iy][mcnk->ix] = 2;
                    liquid_flags[mcnk->iy][mcnk->ix] |= MAP_LIQUID_TYPE_OCEAN;
                }
                if (c_flag & (1 << 4))
                {
                    liquid_entry[mcnk->iy][mcnk->ix] = 3;
                    liquid_flags[mcnk->iy][mcnk->ix] |= MAP_LIQUID_TYPE_MAGMA;
                }

                for (int y = 0; y <= ADT_CELL_SIZE; ++y)
                {
                    int cy = mcnk->iy * ADT_CELL_SIZE + y;
                    for (int x = 0; x <= ADT_CELL_SIZE; ++x)
                    {
                        int cx = mcnk->ix * ADT_CELL_SIZE + x;
                        liquid_height[cy][cx] = liquid->liquid[y][x].height;
                    }
                }
            }
        }

        // -- Hole Data --
        if (!(mcnk->flags & 0x10000))
        {
            if (uint16 hole = mcnk->holes)
            {
                holes[mcnk->iy][mcnk->ix] = mcnk->holes;
                hasHoles = true;
            }
        }
    } // End MCNK Loop

    //if (auditFile.is_open()) auditFile.close();

    // Write SVGs
    GenerateTextureSVG(cell_x, cell_y, textureNames, textureIsRoad, debugTextureLayers, map_id);
    GenerateDetailMapSVG(cell_x, cell_y, map_id);

    // Write .map file logic (MH2O, Headers, etc)...
    // (Ensure you keep the file writing logic from your original System.cpp)

    // ... [MH2O LIQUID DATA] ...
    if (FileChunk* chunk = adt.GetChunk("MH2O"))
    {
        adt_MH2O* h2o = chunk->As<adt_MH2O>();
        for (int i = 0; i < ADT_CELLS_PER_GRID; i++)
        {
            for (int j = 0; j < ADT_CELLS_PER_GRID; j++)
            {
                adt_liquid_header* h = h2o->getLiquidData(i, j);
                if (!h) continue;

                int count = 0;
                uint64 show = h2o->getLiquidShowMap(h);
                for (int y = 0; y < h->height; y++)
                {
                    int cy = i * ADT_CELL_SIZE + y + h->yOffset;
                    for (int x = 0; x < h->width; x++)
                    {
                        int cx = j * ADT_CELL_SIZE + x + h->xOffset;
                        if (show & 1)
                        {
                            liquid_show[cy][cx] = true;
                            ++count;
                        }
                        show >>= 1;
                    }
                }

                liquid_entry[i][j] = h->liquidType;
                switch (LiqType[h->liquidType])
                {
                case 1: liquid_flags[i][j] |= MAP_LIQUID_TYPE_WATER; break; // LIQUID_TYPE_WATER
                case 2: liquid_flags[i][j] |= MAP_LIQUID_TYPE_OCEAN; break; // LIQUID_TYPE_OCEAN
                case 4: liquid_flags[i][j] |= MAP_LIQUID_TYPE_MAGMA; break; // LIQUID_TYPE_MAGMA
                case 8: liquid_flags[i][j] |= MAP_LIQUID_TYPE_SLIME; break; // LIQUID_TYPE_SLIME
                }
                if (LiqType[h->liquidType] == 2) // LIQUID_TYPE_OCEAN
                {
                    uint8* lm = h2o->getLiquidLightMap(h);
                    if (!lm) liquid_flags[i][j] |= MAP_LIQUID_TYPE_DARK_WATER;
                }

                float* height = h2o->getLiquidHeightMap(h);
                int pos = 0;
                for (int y = 0; y <= h->height; y++)
                {
                    int cy = i * ADT_CELL_SIZE + y + h->yOffset;
                    for (int x = 0; x <= h->width; x++)
                    {
                        int cx = j * ADT_CELL_SIZE + x + h->xOffset;
                        if (height)
                            liquid_height[cy][cx] = height[pos];
                        else
                            liquid_height[cy][cx] = h->heightLevel1;
                        pos++;
                    }
                }
            }
        }
    }

    // ... [Original File Writing Logic] ...
    bool fullAreaData = false;
    uint32 areaflag = area_flags[0][0];
    for (int y = 0; y < ADT_CELLS_PER_GRID; y++)
    {
        for (int x = 0; x < ADT_CELLS_PER_GRID; x++)
        {
            if (area_flags[y][x] != areaflag)
            {
                fullAreaData = true;
                break;
            }
        }
    }

    map.areaMapOffset = sizeof(map);
    map.areaMapSize = sizeof(map_areaHeader);

    map_areaHeader areaHeader;
    areaHeader.fourcc = *(uint32 const*)MAP_AREA_MAGIC;
    areaHeader.flags = 0;
    if (fullAreaData)
    {
        areaHeader.gridArea = 0;
        map.areaMapSize += sizeof(area_flags);
    }
    else
    {
        areaHeader.flags |= MAP_AREA_NO_AREA;
        areaHeader.gridArea = (uint16)areaflag;
    }

    float maxHeight = -20000;
    float minHeight = 20000;
    for (int y = 0; y < ADT_GRID_SIZE; y++)
    {
        for (int x = 0; x < ADT_GRID_SIZE; x++)
        {
            float h = V8[y][x];
            if (maxHeight < h) maxHeight = h;
            if (minHeight > h) minHeight = h;
        }
    }
    for (int y = 0; y <= ADT_GRID_SIZE; y++)
    {
        for (int x = 0; x <= ADT_GRID_SIZE; x++)
        {
            float h = V9[y][x];
            if (maxHeight < h) maxHeight = h;
            if (minHeight > h) minHeight = h;
        }
    }

    if (CONF_allow_height_limit && minHeight < CONF_use_minHeight)
    {
        for (int y = 0; y < ADT_GRID_SIZE; y++)
            for (int x = 0; x < ADT_GRID_SIZE; x++)
                if (V8[y][x] < CONF_use_minHeight) V8[y][x] = CONF_use_minHeight;
        for (int y = 0; y <= ADT_GRID_SIZE; y++)
            for (int x = 0; x <= ADT_GRID_SIZE; x++)
                if (V9[y][x] < CONF_use_minHeight) V9[y][x] = CONF_use_minHeight;
        if (minHeight < CONF_use_minHeight) minHeight = CONF_use_minHeight;
        if (maxHeight < CONF_use_minHeight) maxHeight = CONF_use_minHeight;
    }

    map.heightMapOffset = map.areaMapOffset + map.areaMapSize;
    map.heightMapSize = sizeof(map_heightHeader);

    map_heightHeader heightHeader;
    heightHeader.fourcc = *(uint32 const*)MAP_HEIGHT_MAGIC;
    heightHeader.flags = 0;
    heightHeader.gridHeight = minHeight;
    heightHeader.gridMaxHeight = maxHeight;

    if (maxHeight == minHeight)
        heightHeader.flags |= MAP_HEIGHT_NO_HEIGHT;
    if (CONF_allow_float_to_int && (maxHeight - minHeight) < CONF_flat_height_delta_limit)
        heightHeader.flags |= MAP_HEIGHT_NO_HEIGHT;

    if (!(heightHeader.flags & MAP_HEIGHT_NO_HEIGHT))
    {
        float step = 0;
        if (CONF_allow_float_to_int)
        {
            float diff = maxHeight - minHeight;
            if (diff < CONF_float_to_int8_limit)
            {
                heightHeader.flags |= MAP_HEIGHT_AS_INT8;
                step = selectUInt8StepStore(diff);
            }
            else if (diff < CONF_float_to_int16_limit)
            {
                heightHeader.flags |= MAP_HEIGHT_AS_INT16;
                step = selectUInt16StepStore(diff);
            }
        }

        if (heightHeader.flags & MAP_HEIGHT_AS_INT8)
        {
            for (int y = 0; y < ADT_GRID_SIZE; y++)
                for (int x = 0; x < ADT_GRID_SIZE; x++)
                    uint8_V8[y][x] = uint8((V8[y][x] - minHeight) * step + 0.5f);
            for (int y = 0; y <= ADT_GRID_SIZE; y++)
                for (int x = 0; x <= ADT_GRID_SIZE; x++)
                    uint8_V9[y][x] = uint8((V9[y][x] - minHeight) * step + 0.5f);
            map.heightMapSize += sizeof(uint8_V9) + sizeof(uint8_V8);
        }
        else if (heightHeader.flags & MAP_HEIGHT_AS_INT16)
        {
            for (int y = 0; y < ADT_GRID_SIZE; y++)
                for (int x = 0; x < ADT_GRID_SIZE; x++)
                    uint16_V8[y][x] = uint16((V8[y][x] - minHeight) * step + 0.5f);
            for (int y = 0; y <= ADT_GRID_SIZE; y++)
                for (int x = 0; x <= ADT_GRID_SIZE; x++)
                    uint16_V9[y][x] = uint16((V9[y][x] - minHeight) * step + 0.5f);
            map.heightMapSize += sizeof(uint16_V9) + sizeof(uint16_V8);
        }
        else
            map.heightMapSize += sizeof(V9) + sizeof(V8);
    }

    uint8 type = liquid_flags[0][0];
    bool fullType = false;
    for (int y = 0; y < ADT_CELLS_PER_GRID; y++)
    {
        for (int x = 0; x < ADT_CELLS_PER_GRID; x++)
        {
            if (liquid_flags[y][x] != type)
            {
                fullType = true;
                y = ADT_CELLS_PER_GRID;
                break;
            }
        }
    }

    map_liquidHeader liquidHeader;

    if (type == 0 && !fullType)
    {
        map.liquidMapOffset = 0;
        map.liquidMapSize = 0;
    }
    else
    {
        int minX = 255, minY = 255;
        int maxX = 0, maxY = 0;
        maxHeight = -20000;
        minHeight = 20000;
        for (int y = 0; y < ADT_GRID_SIZE; y++)
        {
            for (int x = 0; x < ADT_GRID_SIZE; x++)
            {
                if (liquid_show[y][x])
                {
                    if (minX > x) minX = x;
                    if (maxX < x) maxX = x;
                    if (minY > y) minY = y;
                    if (maxY < y) maxY = y;
                    float h = liquid_height[y][x];
                    if (maxHeight < h) maxHeight = h;
                    if (minHeight > h) minHeight = h;
                }
                else
                    liquid_height[y][x] = CONF_use_minHeight;
            }
        }
        map.liquidMapOffset = map.heightMapOffset + map.heightMapSize;
        map.liquidMapSize = sizeof(map_liquidHeader);
        liquidHeader.fourcc = *(uint32 const*)MAP_LIQUID_MAGIC;
        liquidHeader.flags = 0;
        liquidHeader.liquidType = 0;
        liquidHeader.offsetX = minX;
        liquidHeader.offsetY = minY;
        liquidHeader.width = maxX - minX + 1 + 1;
        liquidHeader.height = maxY - minY + 1 + 1;
        liquidHeader.liquidLevel = minHeight;

        if (maxHeight == minHeight)
            liquidHeader.flags |= MAP_LIQUID_NO_HEIGHT;
        if (CONF_allow_float_to_int && (maxHeight - minHeight) < CONF_flat_liquid_delta_limit)
            liquidHeader.flags |= MAP_LIQUID_NO_HEIGHT;

        if (!fullType)
            liquidHeader.flags |= MAP_LIQUID_NO_TYPE;

        if (liquidHeader.flags & MAP_LIQUID_NO_TYPE)
            liquidHeader.liquidType = type;
        else
            map.liquidMapSize += sizeof(liquid_entry) + sizeof(liquid_flags);

        if (!(liquidHeader.flags & MAP_LIQUID_NO_HEIGHT))
            map.liquidMapSize += sizeof(float) * liquidHeader.width * liquidHeader.height;
    }

    if (map.liquidMapOffset)
        map.holesOffset = map.liquidMapOffset + map.liquidMapSize;
    else
        map.holesOffset = map.heightMapOffset + map.heightMapSize;

    if (hasHoles)
        map.holesSize = sizeof(holes);
    else
        map.holesSize = 0;

    // --- WRITE NEW SECTIONS ---
    if (map.holesSize)
        map.terrainMapOffset = map.holesOffset + map.holesSize;
    else
        map.terrainMapOffset = map.holesOffset;

    map.terrainMapSize = sizeof(terrain_type);

    FILE* output = fopen(filename2, "wb");
    if (!output)
    {
        printf("Can't create the output file '%s'\n", filename2);
        return false;
    }

    fwrite(&map, sizeof(map), 1, output);
    fwrite(&areaHeader, sizeof(areaHeader), 1, output);
    if (!(areaHeader.flags & MAP_AREA_NO_AREA))
        fwrite(area_flags, sizeof(area_flags), 1, output);

    fwrite(&heightHeader, sizeof(heightHeader), 1, output);
    if (!(heightHeader.flags & MAP_HEIGHT_NO_HEIGHT))
    {
        if (heightHeader.flags & MAP_HEIGHT_AS_INT16)
        {
            fwrite(uint16_V9, sizeof(uint16_V9), 1, output);
            fwrite(uint16_V8, sizeof(uint16_V8), 1, output);
        }
        else if (heightHeader.flags & MAP_HEIGHT_AS_INT8)
        {
            fwrite(uint8_V9, sizeof(uint8_V9), 1, output);
            fwrite(uint8_V8, sizeof(uint8_V8), 1, output);
        }
        else
        {
            fwrite(V9, sizeof(V9), 1, output);
            fwrite(V8, sizeof(V8), 1, output);
        }
    }

    if (map.liquidMapOffset)
    {
        fwrite(&liquidHeader, sizeof(liquidHeader), 1, output);
        if (!(liquidHeader.flags & MAP_LIQUID_NO_TYPE))
        {
            fwrite(liquid_entry, sizeof(liquid_entry), 1, output);
            fwrite(liquid_flags, sizeof(liquid_flags), 1, output);
        }

        if (!(liquidHeader.flags & MAP_LIQUID_NO_HEIGHT))
        {
            for (int y = 0; y < liquidHeader.height; y++)
                fwrite(&liquid_height[y + liquidHeader.offsetY][liquidHeader.offsetX], sizeof(float), liquidHeader.width, output);
        }
    }

    if (hasHoles)
        fwrite(holes, map.holesSize, 1, output);

    // Write Terrain Type
    fwrite(terrain_type, sizeof(terrain_type), 1, output);

    fclose(output);
    return true;
}

void ExtractMapsFromMpq(uint32 build)
{
    char mpq_filename[1024];
    char output_filename[1024];
    char mpq_map_name[1024];

    printf("Extracting maps...\n");

    uint32 map_count = ReadMapDBC();
	printf("Found %u maps in DBC\n", map_ids->id);

    ReadAreaTableDBC();
    ReadLiquidTypeTableDBC();

    std::string path = output_path;
    path += "/maps/";
    CreateDir(path);

    printf("Convert map files\n");
    for (uint32 z = 0; z < map_count; ++z)
    {
        if (map_ids[z].id != 530000) {
            printf("Extract %s (%d/%u)                  \n", map_ids[z].name, z + 1, map_count);
            // Loadup map grid data
            snprintf(mpq_map_name, sizeof(mpq_map_name), "World\\Maps\\%s\\%s.wdt", map_ids[z].name, map_ids[z].name);
            ChunkedFile wdt;
            if (!wdt.loadFile(WorldMpq, mpq_map_name, true))
                continue;

            FileChunk* chunk = wdt.GetChunk("MAIN");
            for (uint32 y = 0; y < WDT_MAP_SIZE; ++y)
            {
                for (uint32 x = 0; x < WDT_MAP_SIZE; ++x)
                {
                    if (!(chunk->As<wdt_MAIN>()->adt_list[y][x].flag & 0x1))
                        continue;

                    snprintf(mpq_filename, sizeof(mpq_filename), "World\\Maps\\%s\\%s_%u_%u.adt", map_ids[z].name, map_ids[z].name, x, y);
                    snprintf(output_filename, sizeof(output_filename), "%s/maps/%04u_%02u_%02u.map", output_path, map_ids[z].id, y, x);
                    ConvertADT(mpq_filename, output_filename, y, x, build, map_ids[z]);
                }

                // draw progress bar
                printf("Processing........................%d%%\r", (100 * (y + 1)) / WDT_MAP_SIZE);
            }
        }
    }

    printf("\n");
    delete [] areas;
    delete [] map_ids;
}

bool ExtractFile(HANDLE fileInArchive, char const* filename)
{
    FILE* output = fopen(filename, "wb");
    if(!output)
    {
        printf("Can't create the output file '%s'\n", filename);
        return false;
    }

    char  buffer[0x10000];
    DWORD readBytes = 1;

    while (readBytes > 0)
    {
        SFileReadFile(fileInArchive, buffer, sizeof(buffer), &readBytes, NULL);
        if (readBytes > 0)
            fwrite(buffer, 1, readBytes, output);
    }

    fclose(output);
    return true;
}

void ExtractDBCFiles(int l, bool basicLocale)
{
    printf("Extracting dbc files...\n");

    SFILE_FIND_DATA foundFile;
    memset(&foundFile, 0, sizeof(foundFile));
    HANDLE listFile = SFileFindFirstFile(LocaleMpq, "DBFilesClient\\*dbc", &foundFile, NULL);
    HANDLE dbcFile = NULL;
    uint32 count = 0;
    if (listFile)
    {
        std::string outputPath = output_path;
        outputPath += "/dbc/";

        CreateDir(outputPath);
        if (!basicLocale)
        {
            outputPath += Locales[l];
            outputPath += "/";
            CreateDir(outputPath);
        }

        std::string filename;

        do
        {
            if (!SFileOpenFileEx(LocaleMpq, foundFile.cFileName, SFILE_OPEN_FROM_MPQ, &dbcFile))
            {
                printf("Unable to open file %s in the archive\n", foundFile.cFileName);
                continue;
            }

            filename = foundFile.cFileName;
            filename = outputPath + filename.substr(filename.rfind('\\') + 1);

            if (FileExists(filename.c_str()))
                continue;

            if (ExtractFile(dbcFile, filename.c_str()))
                ++count;

            SFileCloseFile(dbcFile);
        } while (SFileFindNextFile(listFile, &foundFile));

        SFileFindClose(listFile);
    }

    printf("Extracted %u DBC files\n\n", count);
}

void ExtractCameraFiles(int locale, bool basicLocale)
{
    printf("Extracting camera files...\n");
    HANDLE dbcFile;
    if (!SFileOpenFileEx(LocaleMpq, "DBFilesClient\\CinematicCamera.dbc", SFILE_OPEN_FROM_MPQ, &dbcFile))
    {
        printf("Fatal error: Cannot find CinematicCamera.dbc in archive!\n");
        exit(1);
    }

    DBCFile camdbc(dbcFile);

    if (!camdbc.open())
    {
        printf("Unable to open CinematicCamera.dbc. Camera extract aborted.\n");
        return;
    }

    // get camera file list from DBC
    std::vector<std::string> camerafiles;
    size_t cam_count = camdbc.getRecordCount();

    for (size_t i = 0; i < cam_count; ++i)
    {
        std::string camFile(camdbc.getRecord(i).getString(1));
        size_t loc = camFile.find(".mdx");
        if (loc != std::string::npos)
            camFile.replace(loc, 4, ".m2");
        camerafiles.push_back(std::string(camFile));
    }
    SFileCloseFile(dbcFile);

    std::string path = output_path;
    path += "/cameras/";
    CreateDir(path);
    if (!basicLocale)
    {
        path += Locales[locale];
        path += "/";
        CreateDir(path);
    }

    // extract M2s
    uint32 count = 0;
    for (std::string thisFile : camerafiles)
    {
        std::string filename = path;
        std::string camerasFolder = "Cameras\\";
        HANDLE dbcFile = NULL;
        filename += (thisFile.c_str() + camerasFolder.length());

        if (FileExists(filename.c_str()))
            continue;

        if (!SFileOpenFileEx(WorldMpq, thisFile.c_str(), SFILE_OPEN_FROM_MPQ, &dbcFile))
        {
            printf("Unable to open file %s in the archive\n", thisFile.c_str());
            continue;
        }

        if (ExtractFile(dbcFile, filename.c_str()))
            ++count;

        SFileCloseFile(dbcFile);
    }
    printf("Extracted %u camera files\n", count);
}

void ExtractDB2Files(int l, bool basicLocale)
{
    printf("Extracting db2 files...\n");

    SFILE_FIND_DATA foundFile;
    memset(&foundFile, 0, sizeof(foundFile));
    HANDLE listFile = SFileFindFirstFile(LocaleMpq, "DBFilesClient\\*db2", &foundFile, NULL);
    HANDLE dbcFile = NULL;
    uint32 count = 0;
    if (listFile)
    {
        std::string outputPath = output_path;
        outputPath += "/db2/";

        CreateDir(outputPath);
        if (!basicLocale)
        {
            outputPath += Locales[l];
            outputPath += "/";
            CreateDir(outputPath);
        }

        std::string filename;

        do
        {
            if (!SFileOpenFileEx(LocaleMpq, foundFile.cFileName, SFILE_OPEN_FROM_MPQ, &dbcFile))
            {
                printf("Unable to open file %s in the archive\n", foundFile.cFileName);
                continue;
            }

            filename = foundFile.cFileName;
            filename = outputPath + filename.substr(filename.rfind('\\') + 1);
            if (ExtractFile(dbcFile, filename.c_str()))
                ++count;

            SFileCloseFile(dbcFile);
        } while (SFileFindNextFile(listFile, &foundFile));

        SFileFindClose(listFile);
    }

    printf("Extracted %u DB2 files\n\n", count);
}

bool LoadLocaleMPQFile(int locale)
{
    TCHAR buff[512];
    char const* prefix = "";

    memset(buff, 0, sizeof(buff));
    _stprintf(buff, _T("%s/Data/misc.MPQ"), input_path);
    if (!SFileOpenArchive(buff, 0, MPQ_OPEN_READ_ONLY, &LocaleMpq))
    {
        if (GetLastError() != ERROR_PATH_NOT_FOUND)
        {
            _tprintf(_T("\nLoading %s locale MPQs\n"), LocalesT[locale]);
            _tprintf(_T("Cannot open archive %s\n"), buff);
        }
        return false;
    }
    
    memset(buff, 0, sizeof(buff));
    _stprintf(buff, _T("%s/Data/%s/locale-%s.MPQ"), input_path, LocalesT[locale], LocalesT[locale]);
    if (!SFileOpenPatchArchive(LocaleMpq, buff, prefix, 0))
    {
        if (GetLastError() != ERROR_FILE_NOT_FOUND)
            _tprintf(_T("Cannot open patch archive %s\n"), buff);
        if (GetLastError() != ERROR_PATH_NOT_FOUND)
        {
            _tprintf(_T("\nLoading %s locale MPQs\n"), LocalesT[locale]);
            _tprintf(_T("Cannot open archive %s\n"), buff);
        }
        return false;
    }

    _tprintf(_T("\nLoading %s locale MPQs\n"), LocalesT[locale]);
   
    for (int i = 0; Builds[i] && Builds[i] <= CONF_TargetBuild; ++i)
    {
        memset(buff, 0, sizeof(buff));
        prefix = "";
         _stprintf(buff, _T("%s/Data/wow-update-base-%u.MPQ"), input_path, Builds[i]);

        if (!SFileOpenPatchArchive(LocaleMpq, buff, prefix, 0))
        {
            if (GetLastError() != ERROR_PATH_NOT_FOUND)
                _tprintf(_T("Cannot open patch archive %s\n"), buff);
            else
                _tprintf(_T("Not found %s\n"), buff);
            continue;
        }
        else
            _tprintf(_T("Loaded %s\n"), buff);
    }

    for (int i = 0; Builds[i] && Builds[i] <= CONF_TargetBuild; ++i)
    {
        memset(buff, 0, sizeof(buff));
        prefix = "";
        _stprintf(buff, _T("%s/Data/%s/wow-update-%s-%u.MPQ"), input_path, LocalesT[locale], LocalesT[locale], Builds[i]);
        
        if (!SFileOpenPatchArchive(LocaleMpq, buff, prefix, 0))
        {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
                _tprintf(_T("Cannot open patch archive %s\n"), buff);
            continue;
        }
        else
            _tprintf(_T("Loaded %s\n"), buff);
    }

    for (int i = 0; Builds[i] && Builds[i] <= CONF_TargetBuild; ++i)
    {
        memset(buff, 0, sizeof(buff));
        prefix = "";
        _stprintf(buff, _T("%s/Data/Cache/patch-base-%u.MPQ"), input_path, Builds[i]);

        if (!SFileOpenPatchArchive(LocaleMpq, buff, prefix, 0))
        {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
                _tprintf(_T("Cannot open patch archive %s\n"), buff);
            continue;
        }
        else
            _tprintf(_T("Loaded %s\n"), buff);
    }

    for (int i = 0; Builds[i] && Builds[i] <= CONF_TargetBuild; ++i)
    {
        // Load cached locales
        memset(buff, 0, sizeof(buff));
        prefix = "";
        _stprintf(buff, _T("%s/Data/Cache/%s/patch-%s-%u.MPQ"), input_path, LocalesT[locale], LocalesT[locale], Builds[i]);
        
        if (!SFileOpenPatchArchive(LocaleMpq, buff, prefix, 0))
        {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
                _tprintf(_T("Cannot open patch archive %s\n"), buff);
            continue;
        }
        else
            _tprintf(_T("Loaded %s\n"), buff);
    }

    printf("\n");
    return true;
}

void LoadCommonMPQFiles(uint32 build)
{
    TCHAR filename[512];
    _stprintf(filename, _T("%s/Data/world.MPQ"), input_path);
    _tprintf(_T("Loading common MPQ files\n"));
    if (!SFileOpenArchive(filename, 0, MPQ_OPEN_READ_ONLY, &WorldMpq))
    {
        if (GetLastError() != ERROR_PATH_NOT_FOUND)
            _tprintf(_T("Cannot open archive %s\n"), filename);
        return;
    }
    else
        _tprintf(_T("Loaded %s\n"), filename);

    int count = sizeof(CONF_mpq_list) / sizeof(char*);
    for (int i = 1; i < count; ++i)
    {
        if (build < NEW_BASE_SET_BUILD)   // 4.3.2 and higher MPQ
            continue;

        _stprintf(filename, _T("%s/Data/%s"), input_path, CONF_mpq_list[i]);
        if (!SFileOpenPatchArchive(WorldMpq, filename, "", 0))
        {
            if (GetLastError() != ERROR_PATH_NOT_FOUND)
                _tprintf(_T("Cannot open archive %s\n"), filename);
            else
                _tprintf(_T("Not found %s\n"), filename);
        }
        else
            _tprintf(_T("Loaded %s\n"), filename);
    }

    char const* prefix = NULL;
    for (int i = 0; Builds[i] && Builds[i] <= CONF_TargetBuild; ++i)
    {
        memset(filename, 0, sizeof(filename));
        prefix = "";
        _stprintf(filename, _T("%s/Data/wow-update-base-%u.MPQ"), input_path, Builds[i]);

        if (!SFileOpenPatchArchive(WorldMpq, filename, prefix, 0))
        {
            if (GetLastError() != ERROR_PATH_NOT_FOUND)
                _tprintf(_T("Cannot open patch archive %s\n"), filename);
            else
                _tprintf(_T("Not found %s\n"), filename);
            continue;
        }
        else
            _tprintf(_T("Loaded %s\n"), filename);
    }

    for (int i = 0; Builds[i] && Builds[i] <= CONF_TargetBuild; ++i)
    {
        memset(filename, 0, sizeof(filename));
        prefix = "";
        _stprintf(filename, _T("%s/Data/Cache/patch-base-%u.MPQ"), input_path, Builds[i]);

        if (!SFileOpenPatchArchive(WorldMpq, filename, prefix, 0))
        {
            if (GetLastError() != ERROR_PATH_NOT_FOUND)
                _tprintf(_T("Cannot open patch archive %s\n"), filename);
            else
                _tprintf(_T("Not found %s\n"), filename);
            continue;
        }
        else
            _tprintf(_T("Loaded %s\n"), filename);
    }

    printf("\n");
}

int main(int argc, char * arg[])
{
    printf("Map & DBC Extractor\n");
    printf("===================\n");

    HandleArgs(argc, arg);

    int FirstLocale = -1;
    uint32 build = 0;

    for (int i = 0; i < LOCALES_COUNT; ++i)
    {
        //Open MPQs
        if (!LoadLocaleMPQFile(i))
        {
            if (GetLastError() != ERROR_PATH_NOT_FOUND)
                printf("Unable to load %s locale archives!\n", Locales[i]);
            continue;
        }

        printf("Detected locale: %s\n", Locales[i]);
        if ((CONF_extract & EXTRACT_DBC) == 0)
        {
            FirstLocale = i;
            build = ReadBuild(i);
            if (build > CONF_TargetBuild)
            {
                printf("Base locale-%s.MPQ has build higher than target build (%u > %u), nothing extracted!\n", Locales[i], build, CONF_TargetBuild);
                return 0;
            }

            printf("Detected client build: %u\n", build);
            printf("\n");
            break;
        }

        //Extract DBC files
        uint32 tempBuild = ReadBuild(i);
        printf("Detected client build %u for locale %s\n", tempBuild, Locales[i]);
        if (tempBuild > CONF_TargetBuild)
        {
            SFileCloseArchive(LocaleMpq);
            printf("Base locale-%s.MPQ has build higher than target build (%u > %u), nothing extracted!\n", Locales[i], tempBuild, CONF_TargetBuild);
            continue;
        }

        printf("\n");
        ExtractDBCFiles(i, FirstLocale < 0);
        ExtractDB2Files(i, FirstLocale < 0);

        if (FirstLocale < 0)
        {
            FirstLocale = i;
            build = tempBuild;
        }

        //Close MPQs
        SFileCloseArchive(LocaleMpq);
    }

    if (FirstLocale < 0)
    {
        printf("No locales detected\n");
        return 0;
    }

    if (CONF_extract & EXTRACT_CAMERA)
    {
        printf("Using locale: %s\n", Locales[FirstLocale]);

        // Open MPQs
        LoadLocaleMPQFile(FirstLocale);
        LoadCommonMPQFiles(build);

        // Extract cameras
        ExtractCameraFiles(FirstLocale, true);

        // Close MPQs
        SFileCloseArchive(WorldMpq);
        SFileCloseArchive(LocaleMpq);
    }

    if (CONF_extract & EXTRACT_MAP)
    {
        printf("Using locale: %s\n", Locales[FirstLocale]);

        // Open MPQs
        LoadLocaleMPQFile(FirstLocale);
        LoadCommonMPQFiles(build);

        // Extract maps
        ExtractMapsFromMpq(build);

        // Close MPQs
        SFileCloseArchive(WorldMpq);
        SFileCloseArchive(LocaleMpq);
    }

    return 0;
}
