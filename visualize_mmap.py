#!/usr/bin/env python3
"""
MMAP NavMesh Visualizer
Reads Recast/Detour .mmtile files from C:/SMM/data/mmaps/ and renders the
nav-mesh geometry so you can diagnose pathfinding gaps, missing tiles, and
bad polygon coverage.

Requirements:
    pip install matplotlib numpy

Usage:
    # Visualize all loaded tiles for map 0 (Eastern Kingdoms / Kalimdor)
    python visualize_mmap.py --map 0

    # Zoom to a world-space rectangle (x_min, x_max, y_min, y_max)
    python visualize_mmap.py --map 0 --bounds -500 500 -500 500

    # Show only specific tiles (grid row/col)
    python visualize_mmap.py --map 0 --tile 32 48

    # Different mmaps directory
    python visualize_mmap.py --map 1 --dir D:/mmaps
"""

import struct
import os
import glob
import argparse
import sys

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.collections as mc
import matplotlib.patches as mpatches
from matplotlib.colors import Normalize
from matplotlib.cm import ScalarMappable

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

MMAPS_DIR = "D:\\World of Warcraft 5.4.8\\mmaps"

MMAP_MAGIC   = 0x4D4D4150   # "MMAP"
DT_NAVMESH_VERSION_MIN = 7  # accept 7+

# Area IDs used by this project (from Pathfinding2.h filter.setAreaCost calls)
AREA_NAMES = {
    0:  ("Null/Blocked",  "#222222"),
    1:  ("Ground",        "#4CAF50"),   # green
    2:  ("Magma",         "#F44336"),   # red
    4:  ("Slime",         "#9C27B0"),   # purple
    6:  ("Deep Water",    "#1565C0"),   # dark blue
    8:  ("Water",         "#42A5F5"),   # light blue
    16: ("Underwater",    "#F44336"),   # medium blue
    32: ("Road",          "#FFC107"),   # amber
    63: ("Walkable",      "#4CAF50"),   # fallback green
}
DEFAULT_AREA_COLOR = "#78909C"          # grey for unknown areas

# Detour DT_VERTS_PER_POLYGON
DT_VERTS_PER_POLY = 6

# ---------------------------------------------------------------------------
# Binary parsing helpers
# ---------------------------------------------------------------------------

# MmapTileHeader (Pathfinding2.h):
#   uint32  mmapMagic
#   uint32  dtVersion
#   float   mmapVersion
#   uint32  size
#   char    usesLiquids
#   char    padding[3]
_TILE_HDR_FMT  = "<IIfI4s"
_TILE_HDR_SIZE = struct.calcsize(_TILE_HDR_FMT)   # 20 bytes

# dtMeshHeader (DetourNavMesh.h):
#   int  magic, version
#   int  x, y, layer
#   uint userId
#   int  polyCount, vertCount, maxLinkCount
#   int  detailMeshCount, detailVertCount, detailTriCount
#   int  bvNodeCount, offMeshConCount, offMeshBase
#   float walkableHeight, walkableRadius, walkableClimb
#   float bmin[3], bmax[3]
#   float bvQuantFactor
_MESH_HDR_FMT  = "<iiiii I iiiiiiiii fff 3f 3f f"
_MESH_HDR_SIZE = struct.calcsize(_MESH_HDR_FMT)   # 100 bytes

# dtPoly:
#   uint  firstLink
#   ushort verts[6], neis[6]
#   ushort flags
#   uchar  vertCount, areaAndtype
_POLY_FMT  = "<I 6H 6H H BB"
_POLY_SIZE = struct.calcsize(_POLY_FMT)   # 32 bytes

# dtLink:
#   uint  ref
#   uint  next
#   uchar edge, side, bmin, bmax
_LINK_FMT  = "<II 4B"
_LINK_SIZE = struct.calcsize(_LINK_FMT)   # 12 bytes

# dtPolyDetail:
#   uint  vertBase, triBase
#   uchar vertCount, triCount
_DETAIL_MESH_FMT  = "<II BB2x"     # 4+4+1+1+2 padding = 12 bytes (MSVC default alignment)
_DETAIL_MESH_SIZE = struct.calcsize(_DETAIL_MESH_FMT)   # 12

# dtBVNode:
#   ushort bmin[3], bmax[3]
#   int    i
_BV_NODE_FMT  = "<3H 3H i"
_BV_NODE_SIZE = struct.calcsize(_BV_NODE_FMT)   # 16 bytes

# dtOffMeshConnection:
#   float pos[6], rad
#   ushort poly
#   uchar  flags, side
#   uint   userId
_OFF_MESH_FMT  = "<6f f H 2B I"
_OFF_MESH_SIZE = struct.calcsize(_OFF_MESH_FMT)

# ---------------------------------------------------------------------------

def area_color(area_id):
    name, color = AREA_NAMES.get(area_id, ("Unknown", DEFAULT_AREA_COLOR))
    return color

def parse_tile(filepath):
    """Parse one .mmtile file.  Returns a dict with mesh geometry or None on error."""
    try:
        with open(filepath, "rb") as f:
            raw = f.read()
    except OSError as e:
        print(f"  [WARN] Cannot open {filepath}: {e}")
        return None

    if len(raw) < _TILE_HDR_SIZE:
        print(f"  [WARN] {filepath}: file too small for header ({len(raw)} bytes)")
        return None

    mmap_magic, dt_ver, mmap_ver, data_size, _ = struct.unpack_from(_TILE_HDR_FMT, raw, 0)

    if mmap_magic != MMAP_MAGIC:
        print(f"  [WARN] {filepath}: bad magic 0x{mmap_magic:08X}")
        return None

    data_start = _TILE_HDR_SIZE
    data_end   = data_start + data_size

    if data_end > len(raw):
        print(f"  [WARN] {filepath}: declared data_size={data_size} exceeds file length")
        return None

    tile_data = raw[data_start:data_end]

    if len(tile_data) < _MESH_HDR_SIZE:
        print(f"  [WARN] {filepath}: tile data too small for dtMeshHeader")
        return None

    fields = struct.unpack_from(_MESH_HDR_FMT, tile_data, 0)
    (magic, version, tx, ty, layer, user_id,
     poly_count, vert_count, max_link_count,
     detail_mesh_count, detail_vert_count, detail_tri_count,
     bv_node_count, off_mesh_count, off_mesh_base,
     walkable_height, walkable_radius, walkable_climb,
     bmin_x, bmin_y, bmin_z,
     bmax_x, bmax_y, bmax_z,
     bv_quant_factor) = fields

    # Compute byte offsets for each section (matches Detour's createNavMeshData layout)
    off = _MESH_HDR_SIZE
    verts_size        = vert_count * 3 * 4
    polys_size        = poly_count * _POLY_SIZE
    links_size        = max_link_count * _LINK_SIZE
    detail_mesh_size  = detail_mesh_count * _DETAIL_MESH_SIZE
    detail_vert_size  = detail_vert_count * 3 * 4
    detail_tri_size   = detail_tri_count * 4
    bv_size           = bv_node_count * _BV_NODE_SIZE
    off_mesh_size     = off_mesh_count * _OFF_MESH_SIZE

    # --- Vertices ---
    verts = []
    for i in range(vert_count):
        vx, vy, vz = struct.unpack_from("<fff", tile_data, off + i * 12)
        verts.append((vx, vy, vz))
    off += verts_size

    # --- Polygons ---
    polys = []
    for i in range(poly_count):
        p = struct.unpack_from(_POLY_FMT, tile_data, off + i * _POLY_SIZE)
        first_link = p[0]
        vert_ids   = list(p[1:7])
        neis       = list(p[7:13])
        flags      = p[13]
        vert_cnt   = p[14]
        area_type  = p[15]
        area_id    = area_type & 0x3F
        poly_type  = (area_type >> 6) & 0x3
        polys.append({
            "verts":     vert_ids[:vert_cnt],
            "neis":      neis[:vert_cnt],   # same-tile neighbours (1-based, 0=none, 0x8000=cross-tile)
            "flags":     flags,
            "area_id":   area_id,
            "poly_type": poly_type,   # 0=ground, 1=off-mesh
        })
    off += polys_size

    return {
        "filepath":  filepath,
        "tile_x":    tx,
        "tile_y":    ty,
        "bmin":      (bmin_x, bmin_y, bmin_z),
        "bmax":      (bmax_x, bmax_y, bmax_z),
        "verts":     verts,
        "polys":     polys,
        "walkable_height": walkable_height,
    }



def _tile_world_extent(index):
    """Return (world_min, world_max) for a tile index using TrinityCore convention.
    TrinityCore: tileIndex = floor(32 - WoW_coord / 533.33333)
    Inverse:     WoW range = ((31 - index) * SIZE, (32 - index) * SIZE]
    """
    SIZE = 533.33333
    return (31 - index) * SIZE, (32 - index) * SIZE


def tiles_overlapping_bounds(files, map_id, bounds):
    """Return only the files whose tile grid cell overlaps the given world bounds.
    Uses only the filename (encodes TX/TY) — no file I/O needed.
    bounds: (x_min, x_max, z_min, z_max) in world space.
    Tile files use TrinityCore naming: tileIdx = floor(32 - coord / 533.333)
    so world extents are (31-idx)*SIZE to (32-idx)*SIZE.
    """
    x_min, x_max, z_min, z_max = bounds
    result = []
    for f in files:
        base = os.path.basename(f)
        # filename format: MAPID_A_B.mmtile  (TrinityCore: A=tileY from WoW-Y, B=tileX from WoW-X)
        parts = os.path.splitext(base)[0].split("_")
        if len(parts) != 3:
            continue
        try:
            ty, tx = int(parts[1]), int(parts[2])
        except ValueError:
            continue
        tile_x_min, tile_x_max = _tile_world_extent(tx)
        tile_z_min, tile_z_max = _tile_world_extent(ty)
        # AABB overlap test
        if tile_x_max < x_min or tile_x_min > x_max:
            continue
        if tile_z_max < z_min or tile_z_min > z_max:
            continue
        result.append(f)
    return result


def _diagnose_bounds(all_files, bounds):
    """Print diagnostic info when no tiles overlap the requested bounds.
    Tile files use TrinityCore naming: tileIdx = floor(32 - coord / 533.333)
    World extent for tileIdx n: ((31-n)*SIZE, (32-n)*SIZE]
    """
    SIZE = 533.33333
    x_min, x_max, z_min, z_max = bounds

    # Expected tile indices for the requested bounds (TrinityCore inverse)
    tx_min_exp = int(32 - x_max / SIZE)
    tx_max_exp = int(32 - x_min / SIZE)
    tz_min_exp = int(32 - z_max / SIZE)
    tz_max_exp = int(32 - z_min / SIZE)

    print(f"\n  Requested world bounds: X=[{x_min:.1f}, {x_max:.1f}]  Z=[{z_min:.1f}, {z_max:.1f}]")
    print(f"  Expected tile indices:  TX=[{tx_min_exp}, {tx_max_exp}]  TY=[{tz_min_exp}, {tz_max_exp}]")

    # Parse all available tiles from filenames
    found = []
    for f in all_files:
        base = os.path.basename(f)
        parts = os.path.splitext(base)[0].split("_")
        if len(parts) != 3:
            continue
        try:
            ty, tx = int(parts[1]), int(parts[2])
        except ValueError:
            continue
        found.append((tx, ty))

    if not found:
        print("  No tiles found at all in this directory for this map.")
        return

    found.sort()
    tx_vals = sorted(set(t[0] for t in found))
    ty_vals = sorted(set(t[1] for t in found))

    tx_lo, tx_hi = tx_vals[0], tx_vals[-1]
    ty_lo, ty_hi = ty_vals[0], ty_vals[-1]
    # TrinityCore: higher tile index = lower world coord
    world_x_lo, _ = _tile_world_extent(tx_hi)  # highest TX = lowest world X
    _, world_x_hi = _tile_world_extent(tx_lo)  # lowest TX = highest world X
    world_z_lo, _ = _tile_world_extent(ty_hi)
    _, world_z_hi = _tile_world_extent(ty_lo)

    print(f"\n  Available tiles in directory:")
    print(f"    TX range: [{tx_lo}, {tx_hi}]  →  world X [{world_x_lo:.1f}, {world_x_hi:.1f}]")
    print(f"    TY range: [{ty_lo}, {ty_hi}]  →  world Z [{world_z_lo:.1f}, {world_z_hi:.1f}]")
    print(f"    Total tiles: {len(found)}")

    # Find nearest tiles to the requested bounds centre
    cx = (x_min + x_max) / 2.0
    cz = (z_min + z_max) / 2.0
    cx_tile_f = 32 - cx / SIZE
    cz_tile_f = 32 - cz / SIZE

    def nearest(val, vals):
        return min(vals, key=lambda v: abs(v - val))

    near_tx = nearest(cx_tile_f, tx_vals)
    near_ty = nearest(cz_tile_f, ty_vals)
    near_wx_min, near_wx_max = _tile_world_extent(near_tx)
    near_wz_min, near_wz_max = _tile_world_extent(near_ty)

    print(f"\n  Nearest available tile to your coordinates:")
    print(f"    Tile TX={near_tx} TY={near_ty}")
    print(f"    World coverage: X=[{near_wx_min:.1f}, {near_wx_max:.1f}]  "
          f"Z=[{near_wz_min:.1f}, {near_wz_max:.1f}]")
    print(f"\n  Suggested bounds to view that tile:")
    print(f"    --bounds {near_wx_min:.0f} {near_wx_max:.0f} "
          f"{near_wz_min:.0f} {near_wz_max:.0f}")
    print()


def load_map_tiles(map_id, mmaps_dir, tile_filter=None, bounds=None):
    """Load .mmtile files for map_id from mmaps_dir.
    tile_filter: optional (tx, ty) to load a single tile.
    bounds:      optional (x_min, x_max, z_min, z_max) to restrict which tiles are loaded.
    Returns list of parsed tile dicts."""
    if tile_filter:
        tx, ty = tile_filter
        pattern = os.path.join(mmaps_dir, f"{map_id:04d}_{ty:02d}_{tx:02d}.mmtile")
        files = glob.glob(pattern)
        if not files:
            print(f"[ERROR] No tile found: {pattern}")
            return []
    else:
        pattern = os.path.join(mmaps_dir, f"{map_id:04d}_*.mmtile")
        files   = sorted(glob.glob(pattern))

    if not files:
        print(f"[ERROR] No .mmtile files found for map {map_id} in {mmaps_dir}")
        print(f"        Pattern tried: {pattern}")
        return []

    # Pre-filter by bounds before touching file data
    if bounds and not tile_filter:
        all_count = len(files)
        files = tiles_overlapping_bounds(files, map_id, bounds)
        print(f"Bounds filter: {all_count} total tiles → {len(files)} overlap the requested area.")
        if not files:
            print("[ERROR] No tiles overlap the requested bounds.")
            _diagnose_bounds(glob.glob(os.path.join(mmaps_dir, f"{map_id:04d}_*.mmtile")), bounds)
            return []

    print(f"Loading {len(files)} tile(s) for map {map_id}...")
    tiles = []
    for f in files:
        t = parse_tile(f)
        if t:
            tiles.append(t)
        else:
            print(f"  [SKIP] {os.path.basename(f)}")

    print(f"  Loaded {len(tiles)}/{len(files)} tiles successfully.")
    return tiles


def visualize(tiles, map_id, world_bounds=None, show_areas=True, show_edges=True):
    """Render the nav-mesh polygons with matplotlib."""
    if not tiles:
        print("[ERROR] No tiles to visualize.")
        return

    fig, ax = plt.subplots(figsize=(14, 12))
    fig.patch.set_facecolor("#1a1a2e")
    ax.set_facecolor("#16213e")

    # --- Collect geometry per area ---
    area_polys   = {}   # area_id -> list of np arrays (N,2)
    edge_lines   = []   # list of (start, end) in world XY

    total_polys = 0
    tile_rects   = []   # for tile boundary overlay

    for tile in tiles:
        verts = tile["verts"]
        bmin  = tile["bmin"]
        bmax  = tile["bmax"]
        tile_rects.append((bmin, bmax))

        for poly in tile["polys"]:
            if poly["poly_type"] != 0:
                continue   # skip off-mesh connections
            vi = poly["verts"]
            if len(vi) < 3:
                continue
            # Build world-space 2D polygon (X, Y in Detour = world X, Z)
            pts = np.array([(verts[i][0], verts[i][2]) for i in vi])

            aid = poly["area_id"]
            area_polys.setdefault(aid, []).append(pts)

            if show_edges:
                n = len(vi)
                for k in range(n):
                    a = (verts[vi[k]][0],     verts[vi[k]][2])
                    b = (verts[vi[(k+1)%n]][0], verts[vi[(k+1)%n]][2])
                    edge_lines.append([a, b])
            total_polys += 1

    # --- Draw filled polygons ---
    for area_id, poly_list in area_polys.items():
        color = area_color(area_id)
        col = mc.PolyCollection(poly_list, facecolor=color, edgecolor="none", alpha=0.65, linewidth=0)
        ax.add_collection(col)

    # --- Draw edges ---
    if show_edges and edge_lines:
        lc = mc.LineCollection(edge_lines, colors="#FFFFFF", linewidths=0.3, alpha=0.4)
        ax.add_collection(lc)

    # --- Draw tile boundaries ---
    for bmin, bmax in tile_rects:
        rect_x = [bmin[0], bmax[0], bmax[0], bmin[0], bmin[0]]
        rect_y = [bmin[2], bmin[2], bmax[2], bmax[2], bmin[2]]
        ax.plot(rect_x, rect_y, color="#FF6B6B", linewidth=0.5, alpha=0.5, linestyle="--")

    # --- World bounds / zoom ---
    if world_bounds:
        ax.set_xlim(world_bounds[0], world_bounds[1])
        ax.set_ylim(world_bounds[2], world_bounds[3])
    else:
        ax.autoscale()

    ax.invert_yaxis()   # WoW world Y increases southward, flip so north is up
    ax.set_aspect("equal")
    ax.set_xlabel("World X", color="white")
    ax.set_ylabel("World Z (north-south)", color="white")
    ax.tick_params(colors="white")
    for spine in ax.spines.values():
        spine.set_edgecolor("#555555")

    # --- Legend ---
    legend_patches = []
    for aid, poly_list in sorted(area_polys.items()):
        name, color = AREA_NAMES.get(aid, (f"Area {aid}", DEFAULT_AREA_COLOR))
        legend_patches.append(mpatches.Patch(color=color, label=f"{name} (area {aid}) — {len(poly_list)} polys"))

    if legend_patches:
        legend = ax.legend(handles=legend_patches, loc="upper right",
                           facecolor="#0f3460", edgecolor="#555555", labelcolor="white",
                           fontsize=8, title="Area Types", title_fontsize=9)
        legend.get_title().set_color("white")

    tile_count = len(tiles)
    title = f"Map {map_id}  —  {tile_count} tile(s)  —  {total_polys:,} polygons"
    ax.set_title(title, color="white", fontsize=13, pad=10)

    plt.tight_layout()
    plt.show()


def print_tile_info(tiles):
    """Print a summary of each loaded tile to help diagnose problems."""
    print(f"\n{'File':<30} {'TX':>4} {'TY':>4} {'Polys':>7} {'Verts':>7}  BMin→BMax (world space)")
    print("-" * 100)
    for t in tiles:
        name = os.path.basename(t["filepath"])
        bmin = t["bmin"]
        bmax = t["bmax"]
        print(f"{name:<30} {t['tile_x']:>4} {t['tile_y']:>4} "
              f"{len(t['polys']):>7} {len(t['verts']):>7}  "
              f"({bmin[0]:.1f}, {bmin[2]:.1f}) → ({bmax[0]:.1f}, {bmax[2]:.1f})")
    print()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Visualize SMM MMAP navmesh tiles",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument("--map",    type=int,   default=0,        help="Map ID (default: 0)")
    parser.add_argument("--dir",    type=str,   default=MMAPS_DIR, help="Path to mmaps directory")
    parser.add_argument("--tile",   type=int,   nargs=2, metavar=("TX", "TY"),
                        help="Load a single tile (TX TY grid coords)")
    parser.add_argument("--bounds", type=float, nargs=4, metavar=("X_MIN", "X_MAX", "Z_MIN", "Z_MAX"),
                        help="World-space zoom window")
    parser.add_argument("--no-fill",  action="store_true", help="Don't fill polygons (edges only)")
    parser.add_argument("--no-edges", action="store_true", help="Don't draw polygon edges")
    parser.add_argument("--info",     action="store_true", help="Print tile info table and exit")
    args = parser.parse_args()

    tile_filter = tuple(args.tile) if args.tile else None
    tiles = load_map_tiles(args.map, args.dir, tile_filter, bounds=args.bounds)

    if not tiles:
        sys.exit(1)

    if args.info:
        print_tile_info(tiles)
        sys.exit(0)

    print_tile_info(tiles)
    visualize(
        tiles,
        map_id       = args.map,
        world_bounds = args.bounds,
        show_areas   = not args.no_fill,
        show_edges   = not args.no_edges,
    )


if __name__ == "__main__":
    main()
