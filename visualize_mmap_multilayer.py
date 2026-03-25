#!/usr/bin/env python3
"""
Multi-Layer MMAP NavMesh Visualizer
Reads single-layer (.mmtile) and multi-layer (._NN.mmtile) Recast/Detour tile files
and renders them so you can compare layers side-by-side, stacked in 3-D, or overlaid.

Requirements:
    pip install matplotlib numpy

Usage:
    # 3-D stacked view of all layers for one tile
    python visualize_mmap_multilayer.py --map 571 --tile 17 33

    # Side-by-side 2-D subplots, one per layer
    python visualize_mmap_multilayer.py --map 571 --tile 17 33 --mode 2d

    # All layers overlaid on one 2-D plot (different colour per layer)
    python visualize_mmap_multilayer.py --map 571 --tile 17 33 --mode overlay

    # Load every tile in the directory (multi-layer aware)
    python visualize_mmap_multilayer.py --map 571 --mode 3d

    # Different mmaps directory
    python visualize_mmap_multilayer.py --map 571 --tile 17 33 --dir D:/mmaps
"""

import struct
import os
import glob
import argparse
import sys
import math

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.collections as mc
import matplotlib.patches as mpatches
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

MMAPS_DIR = "D:\\World of Warcraft 5.4.8\\mmaps"

MMAP_MAGIC = 0x4D4D4150   # "MMAP"

AREA_NAMES = {
    0:  ("Null/Blocked", "#222222"),
    1:  ("Ground",       "#4CAF50"),
    2:  ("Magma",        "#F44336"),
    4:  ("Slime",        "#9C27B0"),
    6:  ("Deep Water",   "#1565C0"),
    8:  ("Water",        "#42A5F5"),
    10: ("Indoor",       "#FF9800"),   # NAV_INDOOR — the tunnel area
    16: ("Underwater",   "#1976D2"),
    32: ("Road",         "#FFC107"),
    63: ("Walkable",     "#4CAF50"),
}
DEFAULT_AREA_COLOR = "#78909C"

# One distinct colour per layer index (up to 8 layers shown)
LAYER_COLORS = [
    "#4CAF50",   # 0 - green
    "#2196F3",   # 1 - blue
    "#FF9800",   # 2 - orange
    "#E91E63",   # 3 - pink
    "#9C27B0",   # 4 - purple
    "#00BCD4",   # 5 - cyan
    "#FF5722",   # 6 - deep orange
    "#8BC34A",   # 7 - light green
]

DT_VERTS_PER_POLY = 6

# ---------------------------------------------------------------------------
# Binary format helpers  (identical to visualize_mmap.py)
# ---------------------------------------------------------------------------

_TILE_HDR_FMT  = "<IIfI4s"
_TILE_HDR_SIZE = struct.calcsize(_TILE_HDR_FMT)   # 20 bytes

_MESH_HDR_FMT  = "<iiiii I iiiiiiiii fff 3f 3f f"
_MESH_HDR_SIZE = struct.calcsize(_MESH_HDR_FMT)

_POLY_FMT  = "<I 6H 6H H BB"
_POLY_SIZE = struct.calcsize(_POLY_FMT)

_LINK_FMT  = "<II 4B"
_LINK_SIZE = struct.calcsize(_LINK_FMT)

_DETAIL_MESH_FMT  = "<II BB2x"     # 4+4+1+1+2 padding = 12 bytes (MSVC default alignment)
_DETAIL_MESH_SIZE = struct.calcsize(_DETAIL_MESH_FMT)   # 12

_BV_NODE_FMT  = "<3H 3H i"
_BV_NODE_SIZE = struct.calcsize(_BV_NODE_FMT)

_OFF_MESH_FMT  = "<6f f H 2B I"
_OFF_MESH_SIZE = struct.calcsize(_OFF_MESH_FMT)

# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

def parse_tile(filepath):
    """Parse one .mmtile file. Returns a dict or None on error."""
    try:
        with open(filepath, "rb") as f:
            raw = f.read()
    except OSError as e:
        print(f"  [WARN] Cannot open {filepath}: {e}")
        return None

    if len(raw) < _TILE_HDR_SIZE:
        return None

    mmap_magic, dt_ver, mmap_ver, data_size, _ = struct.unpack_from(_TILE_HDR_FMT, raw, 0)
    if mmap_magic != MMAP_MAGIC:
        print(f"  [WARN] {filepath}: bad magic 0x{mmap_magic:08X}")
        return None

    data_start = _TILE_HDR_SIZE
    tile_data  = raw[data_start : data_start + data_size]

    if len(tile_data) < _MESH_HDR_SIZE:
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

    off = _MESH_HDR_SIZE

    # Vertices (x, y, z) in world space — y is height
    verts = []
    for i in range(vert_count):
        vx, vy, vz = struct.unpack_from("<fff", tile_data, off + i * 12)
        verts.append((vx, vy, vz))
    off += vert_count * 3 * 4

    # Polygons
    polys = []
    for i in range(poly_count):
        p = struct.unpack_from(_POLY_FMT, tile_data, off + i * _POLY_SIZE)
        vert_ids  = list(p[1:7])
        vert_cnt  = p[14]
        area_type = p[15]
        area_id   = area_type & 0x3F
        poly_type = (area_type >> 6) & 0x3
        polys.append({
            "verts":     vert_ids[:vert_cnt],
            "area_id":   area_id,
            "poly_type": poly_type,
        })
    off += poly_count * _POLY_SIZE

    # Skip links
    off += max_link_count * _LINK_SIZE

    # Detail meshes
    detail_meshes = []
    for i in range(detail_mesh_count):
        dm = struct.unpack_from(_DETAIL_MESH_FMT, tile_data, off + i * _DETAIL_MESH_SIZE)
        detail_meshes.append({"vert_base": dm[0], "tri_base": dm[1],
                               "vert_count": dm[2], "tri_count": dm[3]})
    off += detail_mesh_count * _DETAIL_MESH_SIZE

    # Detail vertices (x, y, z) — y is height
    detail_verts = []
    for i in range(detail_vert_count):
        vx, vy, vz = struct.unpack_from("<fff", tile_data, off + i * 12)
        detail_verts.append((vx, vy, vz))
    off += detail_vert_count * 3 * 4

    # Detail triangles (4 bytes each: v0, v1, v2, flags)
    detail_tris = []
    for i in range(detail_tri_count):
        t = struct.unpack_from("<4B", tile_data, off + i * 4)
        detail_tris.append(t[:3])

    return {
        "filepath":      filepath,
        "tile_x":        tx,
        "tile_y":        ty,
        "layer":         layer,
        "bmin":          (bmin_x, bmin_y, bmin_z),
        "bmax":          (bmax_x, bmax_y, bmax_z),
        "verts":         verts,
        "polys":         polys,
        "detail_meshes": detail_meshes,
        "detail_verts":  detail_verts,
        "detail_tris":   detail_tris,
    }


def _parse_filename(basename):
    """
    Return (map_id, tile_y, tile_x, layer) from a .mmtile filename.
    Supports both:
      0571_17_33.mmtile    -> layer 0 (legacy single-layer)
      0571_17_33_01.mmtile -> layer 1 (multi-layer)
    Returns None if the filename doesn't match.
    """
    stem = os.path.splitext(basename)[0]
    parts = stem.split("_")
    if len(parts) == 3:
        try:
            return int(parts[0]), int(parts[1]), int(parts[2]), 0
        except ValueError:
            return None
    if len(parts) == 4:
        try:
            return int(parts[0]), int(parts[1]), int(parts[2]), int(parts[3])
        except ValueError:
            return None
    return None


GRID_SIZE = 533.33333   # WoW grid cell size in world units


def _tile_world_extent(index):
    """Return (world_min, world_max) along one axis for a tile grid index."""
    return (31 - index) * GRID_SIZE, (32 - index) * GRID_SIZE


def _filter_files_by_bounds(files, bounds):
    """
    Keep only files whose tile grid cell overlaps (x_min, x_max, z_min, z_max).
    Handles both 3-part (single-layer) and 4-part (multi-layer) filenames.
    """
    x_min, x_max, z_min, z_max = bounds
    result = []
    for f in files:
        parsed = _parse_filename(os.path.basename(f))
        if parsed is None:
            continue
        _map, ty, tx, _layer = parsed
        tile_x_min, tile_x_max = _tile_world_extent(tx)
        tile_z_min, tile_z_max = _tile_world_extent(ty)
        if tile_x_max < x_min or tile_x_min > x_max:
            continue
        if tile_z_max < z_min or tile_z_min > z_max:
            continue
        result.append(f)
    return result


def _diagnose_bounds(all_files, bounds):
    x_min, x_max, z_min, z_max = bounds
    tx_min_exp = int(32 - x_max / GRID_SIZE)
    tx_max_exp = int(32 - x_min / GRID_SIZE)
    tz_min_exp = int(32 - z_max / GRID_SIZE)
    tz_max_exp = int(32 - z_min / GRID_SIZE)
    print(f"\n  Requested world bounds: X=[{x_min:.1f}, {x_max:.1f}]  Z=[{z_min:.1f}, {z_max:.1f}]")
    print(f"  Expected tile indices:  TX=[{tx_min_exp}, {tx_max_exp}]  TY=[{tz_min_exp}, {tz_max_exp}]")

    found = []
    for f in all_files:
        parsed = _parse_filename(os.path.basename(f))
        if parsed:
            found.append((parsed[2], parsed[1]))   # (tx, ty)

    if not found:
        print("  No tiles found at all in this directory for this map.")
        return

    tx_vals = sorted(set(t[0] for t in found))
    ty_vals = sorted(set(t[1] for t in found))
    tx_lo, tx_hi = tx_vals[0], tx_vals[-1]
    ty_lo, ty_hi = ty_vals[0], ty_vals[-1]
    world_x_lo, _ = _tile_world_extent(tx_hi)
    _, world_x_hi = _tile_world_extent(tx_lo)
    world_z_lo, _ = _tile_world_extent(ty_hi)
    _, world_z_hi = _tile_world_extent(ty_lo)

    print(f"\n  Available tiles:")
    print(f"    TX [{tx_lo}, {tx_hi}]  →  world X [{world_x_lo:.1f}, {world_x_hi:.1f}]")
    print(f"    TY [{ty_lo}, {ty_hi}]  →  world Z [{world_z_lo:.1f}, {world_z_hi:.1f}]")

    cx = (x_min + x_max) / 2.0
    cz = (z_min + z_max) / 2.0
    near_tx = min(tx_vals, key=lambda v: abs(v - (32 - cx / GRID_SIZE)))
    near_ty = min(ty_vals, key=lambda v: abs(v - (32 - cz / GRID_SIZE)))
    nxlo, nxhi = _tile_world_extent(near_tx)
    nzlo, nzhi = _tile_world_extent(near_ty)
    print(f"\n  Nearest tile: TX={near_tx} TY={near_ty}")
    print(f"    Suggested: --bounds {nxlo:.0f} {nxhi:.0f} {nzlo:.0f} {nzhi:.0f}\n")


def load_tiles(map_id, mmaps_dir, tile_filter=None, bounds=None):
    """
    Load all .mmtile files for map_id, including multi-layer variants.
    tile_filter: optional (tx, ty) to load only that grid cell (all layers).
    bounds:      optional (x_min, x_max, z_min, z_max) world-space filter.
    Returns list of parsed tile dicts, each containing a 'layer' field.
    """
    prefix = f"{map_id:04d}_"

    if tile_filter:
        tx, ty = tile_filter
        patterns = [
            os.path.join(mmaps_dir, f"{map_id:04d}_{ty:02d}_{tx:02d}.mmtile"),
            os.path.join(mmaps_dir, f"{map_id:04d}_{ty:02d}_{tx:02d}_??.mmtile"),
        ]
        files = []
        for p in patterns:
            files.extend(glob.glob(p))
        files = sorted(set(files))
    else:
        files = sorted(glob.glob(os.path.join(mmaps_dir, f"{prefix}*.mmtile")))

    if not files:
        print(f"[ERROR] No .mmtile files found for map {map_id} in {mmaps_dir}")
        return []

    if bounds and not tile_filter:
        all_count = len(files)
        files = _filter_files_by_bounds(files, bounds)
        print(f"Bounds filter: {all_count} total files → {len(files)} overlap the requested area.")
        if not files:
            print("[ERROR] No tiles overlap the requested bounds.")
            _diagnose_bounds(
                glob.glob(os.path.join(mmaps_dir, f"{prefix}*.mmtile")), bounds)
            return []

    print(f"Loading {len(files)} file(s)...")
    tiles = []
    for f in files:
        t = parse_tile(f)
        if t:
            tiles.append(t)
        else:
            print(f"  [SKIP] {os.path.basename(f)}")

    layers_found = sorted(set(t["layer"] for t in tiles))
    print(f"  Loaded {len(tiles)} tile(s).  Layers present: {layers_found}")
    return tiles


# ---------------------------------------------------------------------------
# Geometry helpers
# ---------------------------------------------------------------------------

def tile_polys_3d(tile):
    """
    Yield 3-D triangles (list of 3 (x,y,z) tuples) for the tile using
    detail mesh triangles where available, falling back to base polygon fans.
    """
    verts   = tile["verts"]
    polys   = tile["polys"]
    dms     = tile["detail_meshes"]
    dverts  = tile["detail_verts"]
    dtris   = tile["detail_tris"]

    for pi, poly in enumerate(polys):
        if poly["poly_type"] != 0:
            continue
        area_id = poly["area_id"]

        if pi < len(dms) and dms[pi]["tri_count"] > 0:
            dm   = dms[pi]
            vb   = dm["vert_base"]
            tb   = dm["tri_base"]
            vcnt = dm["vert_count"]
            tcnt = dm["tri_count"]

            # Detail vertex indices 0..poly_vert_count-1 alias the polygon's own base
            # vertices; indices poly_vert_count..vcnt-1 are extra detail verts stored
            # at dverts[vertBase + (idx - poly_vert_count)].
            pvc = len(poly["verts"])
            def get_dv(idx, _pvc=pvc, _vb=vb):
                if idx < _pvc:
                    vi = poly["verts"][idx]
                    return verts[vi]
                else:
                    dv_idx = _vb + (idx - _pvc)
                    if dv_idx < len(dverts):
                        return dverts[dv_idx]
                    # Fallback: clamp to last available detail vert
                    return dverts[-1] if dverts else verts[poly["verts"][0]]

            yielded = False
            for ti in range(tcnt):
                tri_idx = tb + ti
                if tri_idx >= len(dtris):
                    break   # detail tri table truncated — fall through to fan
                i0, i1, i2 = dtris[tri_idx]
                yield area_id, [get_dv(i0), get_dv(i1), get_dv(i2)]
                yielded = True

            if not yielded:
                # Fall back to base polygon fan if detail data was unusable
                vi = poly["verts"]
                p0 = verts[vi[0]]
                for k in range(1, len(vi) - 1):
                    yield area_id, [p0, verts[vi[k]], verts[vi[k + 1]]]
        else:
            # Fan triangulation of base polygon
            vi = poly["verts"]
            if len(vi) < 3:
                continue
            p0 = verts[vi[0]]
            for k in range(1, len(vi) - 1):
                yield area_id, [p0, verts[vi[k]], verts[vi[k + 1]]]


def tile_polys_2d(tile):
    """Yield (area_id, pts_2d) for each base polygon (XZ plane, top-down)."""
    verts = tile["verts"]
    for poly in tile["polys"]:
        if poly["poly_type"] != 0:
            continue
        vi = poly["verts"]
        if len(vi) < 3:
            continue
        pts = np.array([(verts[i][0], verts[i][2]) for i in vi])
        yield poly["area_id"], pts


def area_color(area_id):
    _, color = AREA_NAMES.get(area_id, ("Unknown", DEFAULT_AREA_COLOR))
    return color


# ---------------------------------------------------------------------------
# Visualisation modes
# ---------------------------------------------------------------------------

def visualize_3d(tiles, map_id, world_bounds=None):
    """
    3-D view: each layer rendered at its actual world-space Y height.
    Useful for seeing how surface and tunnel layers relate vertically.
    world_bounds: optional (x_min, x_max, z_min, z_max) to restrict the XZ axes.
    """
    if not tiles:
        print("[ERROR] No tiles.")
        return

    layers = sorted(set(t["layer"] for t in tiles))
    print(f"Rendering 3-D view — layers: {layers}")

    fig = plt.figure(figsize=(16, 12))
    fig.patch.set_facecolor("#1a1a2e")
    ax  = fig.add_subplot(111, projection="3d")
    ax.set_facecolor("#16213e")

    legend_handles = []

    for layer_idx in layers:
        layer_tiles = [t for t in tiles if t["layer"] == layer_idx]
        lcolor      = LAYER_COLORS[layer_idx % len(LAYER_COLORS)]

        tris_by_area = {}
        for tile in layer_tiles:
            for area_id, tri in tile_polys_3d(tile):
                tris_by_area.setdefault(area_id, []).append(tri)

        for area_id, tri_list in tris_by_area.items():
            # Use area colour tinted with layer hue
            acolor = area_color(area_id) if len(layers) == 1 else lcolor
            col = Poly3DCollection(
                tri_list,
                facecolor=acolor,
                edgecolor="none",
                alpha=0.55,
                linewidth=0,
            )
            ax.add_collection3d(col)

        # Legend entry per layer
        label = f"Layer {layer_idx}  ({len(layer_tiles)} tile(s))"
        legend_handles.append(mpatches.Patch(color=lcolor, label=label))

    # Set axes extents
    all_verts = [v for t in tiles for v in t["verts"]]
    if all_verts:
        ys = [v[1] for v in all_verts]
        pad = 5.0
        if world_bounds:
            ax.set_xlim(world_bounds[0], world_bounds[1])
            ax.set_zlim(world_bounds[2], world_bounds[3])
        else:
            xs = [v[0] for v in all_verts]
            zs = [v[2] for v in all_verts]
            ax.set_xlim(min(xs) - pad, max(xs) + pad)
            ax.set_zlim(min(zs) - pad, max(zs) + pad)
        ax.set_ylim(min(ys) - pad, max(ys) + pad)

    ax.set_xlabel("World X", color="white", labelpad=8)
    ax.set_ylabel("World Y (height)", color="white", labelpad=8)
    ax.set_zlabel("World Z", color="white", labelpad=8)
    ax.tick_params(colors="white")
    ax.xaxis.pane.fill = False
    ax.yaxis.pane.fill = False
    ax.zaxis.pane.fill = False

    if legend_handles:
        legend = ax.legend(handles=legend_handles, loc="upper left",
                           facecolor="#0f3460", edgecolor="#555555",
                           labelcolor="white", fontsize=9,
                           title=f"Map {map_id}", title_fontsize=10)
        legend.get_title().set_color("white")

    total_polys = sum(len(t["polys"]) for t in tiles)
    ax.set_title(f"Map {map_id}  —  {len(tiles)} tile(s)  —  {len(layers)} layer(s)  —  {total_polys:,} polys",
                 color="white", fontsize=12, pad=12)

    print("3-D controls: left-drag = rotate, right-drag/scroll = zoom, middle-drag = pan")
    plt.tight_layout()
    plt.show()


def visualize_2d_layers(tiles, map_id, world_bounds=None):
    """
    Side-by-side 2-D top-down subplots, one per layer.
    """
    if not tiles:
        print("[ERROR] No tiles.")
        return

    layers = sorted(set(t["layer"] for t in tiles))
    n      = len(layers)
    cols   = min(n, 3)
    rows   = math.ceil(n / cols)

    fig, axes = plt.subplots(rows, cols, figsize=(7 * cols, 7 * rows),
                             squeeze=False)
    fig.patch.set_facecolor("#1a1a2e")

    for ax_row in axes:
        for ax in ax_row:
            ax.set_facecolor("#16213e")
            ax.tick_params(colors="white")
            for spine in ax.spines.values():
                spine.set_edgecolor("#555555")

    for plot_i, layer_idx in enumerate(layers):
        ax = axes[plot_i // cols][plot_i % cols]
        layer_tiles = [t for t in tiles if t["layer"] == layer_idx]

        area_polys = {}
        edge_lines = []
        for tile in layer_tiles:
            for area_id, pts in tile_polys_2d(tile):
                area_polys.setdefault(area_id, []).append(pts)
                n_pts = len(pts)
                for k in range(n_pts):
                    a = tuple(pts[k])
                    b = tuple(pts[(k + 1) % n_pts])
                    edge_lines.append([a, b])

            # Tile boundary
            bmin, bmax = tile["bmin"], tile["bmax"]
            rx = [bmin[0], bmax[0], bmax[0], bmin[0], bmin[0]]
            ry = [bmin[2], bmin[2], bmax[2], bmax[2], bmin[2]]
            ax.plot(rx, ry, color="#FF6B6B", linewidth=0.6, alpha=0.5, linestyle="--")

        for area_id, poly_list in area_polys.items():
            color = area_color(area_id)
            col = mc.PolyCollection(poly_list, facecolor=color, edgecolor="none",
                                    alpha=0.65, linewidth=0)
            ax.add_collection(col)

        if edge_lines:
            lc = mc.LineCollection(edge_lines, colors="#FFFFFF", linewidths=0.3, alpha=0.35)
            ax.add_collection(lc)

        if world_bounds:
            ax.set_xlim(world_bounds[0], world_bounds[1])
            ax.set_ylim(world_bounds[2], world_bounds[3])
        else:
            ax.autoscale()

        ax.invert_yaxis()
        ax.set_aspect("equal")

        total_polys = sum(len(t["polys"]) for t in layer_tiles)
        y_vals = [v[1] for t in layer_tiles for v in t["verts"]]
        y_range = f"Y=[{min(y_vals):.1f}, {max(y_vals):.1f}]" if y_vals else ""
        ax.set_title(f"Layer {layer_idx}  —  {len(layer_tiles)} tile(s)  —  "
                     f"{total_polys:,} polys  {y_range}",
                     color="white", fontsize=10)
        ax.set_xlabel("World X", color="white")
        ax.set_ylabel("World Z", color="white")

        # Mini legend
        legend_patches = []
        for aid, plist in sorted(area_polys.items()):
            name, color = AREA_NAMES.get(aid, (f"Area {aid}", DEFAULT_AREA_COLOR))
            legend_patches.append(mpatches.Patch(color=color,
                                                  label=f"{name} ({len(plist)})"))
        if legend_patches:
            leg = ax.legend(handles=legend_patches, loc="upper right",
                            facecolor="#0f3460", edgecolor="#555555",
                            labelcolor="white", fontsize=7)

    # Hide unused subplots
    for plot_i in range(len(layers), rows * cols):
        axes[plot_i // cols][plot_i % cols].set_visible(False)

    fig.suptitle(f"Map {map_id}  —  {len(tiles)} tile(s)  —  {n} layer(s)",
                 color="white", fontsize=13)
    plt.tight_layout()
    plt.show()


def visualize_overlay(tiles, map_id, world_bounds=None):
    """
    All layers on a single 2-D plot. Each layer gets its own colour,
    drawn with transparency so layers don't fully obscure each other.
    """
    if not tiles:
        print("[ERROR] No tiles.")
        return

    layers = sorted(set(t["layer"] for t in tiles))

    fig, ax = plt.subplots(figsize=(14, 12))
    fig.patch.set_facecolor("#1a1a2e")
    ax.set_facecolor("#16213e")

    legend_handles = []

    for layer_idx in layers:
        layer_tiles = [t for t in tiles if t["layer"] == layer_idx]
        lcolor      = LAYER_COLORS[layer_idx % len(LAYER_COLORS)]
        poly_list   = []
        edge_lines  = []

        for tile in layer_tiles:
            for _area_id, pts in tile_polys_2d(tile):
                poly_list.append(pts)
                n_pts = len(pts)
                for k in range(n_pts):
                    edge_lines.append([tuple(pts[k]), tuple(pts[(k + 1) % n_pts])])

            bmin, bmax = tile["bmin"], tile["bmax"]
            rx = [bmin[0], bmax[0], bmax[0], bmin[0], bmin[0]]
            ry = [bmin[2], bmin[2], bmax[2], bmax[2], bmin[2]]
            ax.plot(rx, ry, color=lcolor, linewidth=0.5, alpha=0.4, linestyle="--")

        if poly_list:
            col = mc.PolyCollection(poly_list, facecolor=lcolor, edgecolor="none",
                                    alpha=0.40, linewidth=0)
            ax.add_collection(col)

        if edge_lines:
            lc = mc.LineCollection(edge_lines, colors=lcolor, linewidths=0.4, alpha=0.6)
            ax.add_collection(lc)

        y_vals  = [v[1] for t in layer_tiles for v in t["verts"]]
        y_range = f"  Y=[{min(y_vals):.1f},{max(y_vals):.1f}]" if y_vals else ""
        label   = f"Layer {layer_idx}  ({len(poly_list)} polys{y_range})"
        legend_handles.append(mpatches.Patch(color=lcolor, label=label))

    if world_bounds:
        ax.set_xlim(world_bounds[0], world_bounds[1])
        ax.set_ylim(world_bounds[2], world_bounds[3])
    else:
        ax.autoscale()

    ax.invert_yaxis()
    ax.set_aspect("equal")
    ax.set_xlabel("World X", color="white")
    ax.set_ylabel("World Z", color="white")
    ax.tick_params(colors="white")
    for spine in ax.spines.values():
        spine.set_edgecolor("#555555")

    if legend_handles:
        leg = ax.legend(handles=legend_handles, loc="upper right",
                        facecolor="#0f3460", edgecolor="#555555",
                        labelcolor="white", fontsize=9,
                        title="Layers (overlaid)", title_fontsize=10)
        leg.get_title().set_color("white")

    total_polys = sum(len(t["polys"]) for t in tiles)
    ax.set_title(f"Map {map_id}  —  {len(tiles)} tile(s)  —  {len(layers)} layer(s) overlaid  —  {total_polys:,} polys",
                 color="white", fontsize=13)
    plt.tight_layout()
    plt.show()


# ---------------------------------------------------------------------------
# Info table
# ---------------------------------------------------------------------------

def print_info(tiles):
    print(f"\n{'File':<35} {'TX':>4} {'TY':>4} {'L':>3} {'Polys':>6} {'Verts':>6}  "
          f"Y-range (height)       BMin XZ → BMax XZ")
    print("-" * 115)
    for t in sorted(tiles, key=lambda x: (x["tile_x"], x["tile_y"], x["layer"])):
        name   = os.path.basename(t["filepath"])
        bmin   = t["bmin"]
        bmax   = t["bmax"]
        y_vals = [v[1] for v in t["verts"]]
        y_str  = f"[{min(y_vals):7.1f}, {max(y_vals):7.1f}]" if y_vals else "           N/A"
        print(f"{name:<35} {t['tile_x']:>4} {t['tile_y']:>4} {t['layer']:>3} "
              f"{len(t['polys']):>6} {len(t['verts']):>6}  "
              f"{y_str}  "
              f"({bmin[0]:.1f}, {bmin[2]:.1f}) → ({bmax[0]:.1f}, {bmax[2]:.1f})")
    print()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Multi-layer MMAP navmesh visualizer",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument("--map",  type=int, default=571,       help="Map ID (default: 571)")
    parser.add_argument("--dir",  type=str, default=MMAPS_DIR, help="Path to mmaps directory")
    parser.add_argument("--tile", type=int, nargs=2, metavar=("TX", "TY"),
                        help="Load a single tile grid cell (all layers)")
    parser.add_argument("--bounds", type=float, nargs=4,
                        metavar=("X_MIN", "X_MAX", "Z_MIN", "Z_MAX"),
                        help="World-space zoom window (2D modes only)")
    parser.add_argument("--mode", choices=["3d", "2d", "overlay"], default="3d",
                        help="Visualisation mode: 3d (default), 2d (per-layer subplots), overlay")
    parser.add_argument("--info", action="store_true",
                        help="Print tile info table and exit")
    args = parser.parse_args()

    tile_filter = tuple(args.tile) if args.tile else None
    tiles = load_tiles(args.map, args.dir, tile_filter, bounds=args.bounds)

    if not tiles:
        sys.exit(1)

    if args.info:
        print_info(tiles)
        sys.exit(0)

    print_info(tiles)

    if args.mode == "3d":
        visualize_3d(tiles, args.map, world_bounds=args.bounds)
    elif args.mode == "2d":
        visualize_2d_layers(tiles, args.map, world_bounds=args.bounds)
    else:
        visualize_overlay(tiles, args.map, world_bounds=args.bounds)


if __name__ == "__main__":
    main()
