#!/usr/bin/env python3
"""
author_thenar_extend.py — procedurally author the thenar_extend morph target
on SKM_MannyXR_left and SKM_MannyXR_right via ECABridge.

For each hand:
  1. Query all vertices weighted to thumb_01, hand, or index_metacarpal.
  2. Identify the thenar webbing: vertices with non-trivial weight on BOTH
     thumb_01 AND a palm bone — that's where the thumb-to-index seam lives.
  3. Compute a push delta along each vertex normal (outward bulge),
     magnitude scaled by how "webbing-y" the vertex is (peaks where weights
     are roughly equal between thumb and palm).
  4. Send deltas to create_morph_target as "thenar_extend".

Run:
  python3 Tools/author_thenar_extend.py
Requires:
  UE editor running for the PSVR2 project, ECABridge on port 3020.
  Tunables PUSH_CM, THUMB_THRESHOLD, PALM_THRESHOLD below — iterate on these
  if the morph is too subtle or too aggressive.
"""
import json
import sys
import urllib.request

BRIDGE_URL = "http://127.0.0.1:3020/mcp"

# ── Tunables ────────────────────────────────────────────────
PUSH_CM         = 2.5   # max push distance along vertex normal (cm)
THUMB_THRESHOLD = 0.10  # min weight on thumb_01 for a vertex to count
PALM_THRESHOLD  = 0.05  # min combined weight on palm-side bones
# ────────────────────────────────────────────────────────────

def _http(payload):
    req = urllib.request.Request(BRIDGE_URL,
                                  data=json.dumps(payload).encode(),
                                  headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=180) as resp:
        body = json.loads(resp.read())
    if "error" in body:
        raise RuntimeError(f"jsonrpc error: {body['error']}")
    return body["result"]


def call(name, args):
    """Call a tool. Response must fit in one 64KB chunk — the ECABridge
    server chunker splits JSON strings without escape awareness, producing
    invalid JSON at boundaries. Callers paginate via tool-level params
    (e.g. get_skeletal_mesh_vertices's offset/limit) instead."""
    result = _http({"jsonrpc": "2.0", "method": "tools/call", "id": 1,
                    "params": {"name": name, "arguments": args}})
    content = result.get("content", [])
    text = content[0].get("text", "") if content else ""
    remaining = result.get("_remaining_chars", 0) or 0
    if remaining > 0:
        raise RuntimeError(
            f"{name} response chunked ({remaining} chars remaining); "
            f"paginate at the tool level instead — ECABridge's chunker "
            f"breaks JSON escapes across boundaries.")
    return json.loads(text)


def call_paged_vertices(mesh_path, bone_names, min_weight, page_size=500):
    """Page through get_skeletal_mesh_vertices, yielding each vertex."""
    offset = 0
    while True:
        result = call("get_skeletal_mesh_vertices", {
            "mesh_path": mesh_path,
            "lod_index": 0,
            "bone_names": bone_names,
            "min_weight": min_weight,
            "include_normals": True,
            "offset": offset,
            "limit": page_size
        })
        for v in result["vertices"]:
            yield v
        if not result.get("has_more"):
            break
        offset = result["next_offset"]


def weight_of(vertex, bone_name):
    for w in vertex["bone_weights"]:
        if w["bone"] == bone_name:
            return w["weight"]
    return 0.0


def thenar_for_hand(hand_suffix):
    """hand_suffix: '_l' or '_r'"""
    side = "left" if hand_suffix == "_l" else "right"
    mesh = f"/Game/Characters/MannequinsXR/Meshes/SKM_MannyXR_{side}"
    thumb_bone = f"thumb_01{hand_suffix}"
    palm_bones = {f"hand{hand_suffix}",
                  f"index_metacarpal{hand_suffix}",
                  f"index_01{hand_suffix}"}

    print(f"\n[{side}] paging vertices weighted to thumb_01 / hand / index ...")
    vertices = list(call_paged_vertices(
        mesh, list(palm_bones | {thumb_bone}), min_weight=0.05, page_size=100))
    print(f"[{side}]   got {len(vertices)} matching vertices")

    deltas = []
    for v in vertices:
        thumb_w = weight_of(v, thumb_bone)
        if thumb_w < THUMB_THRESHOLD:
            continue
        palm_w = sum(weight_of(v, b) for b in palm_bones)
        if palm_w < PALM_THRESHOLD:
            continue
        # blend peaks when weights are evenly split (true webbing seam),
        # falls off when one bone dominates (away from the seam)
        blend = min(thumb_w, palm_w) * 2.0
        magnitude = PUSH_CM * min(1.0, blend)
        n = v["normal"]
        nlen = max(1e-4, (n["x"]**2 + n["y"]**2 + n["z"]**2) ** 0.5)
        deltas.append({
            "source_idx": v["source_idx"],
            "position": {
                "x": n["x"] / nlen * magnitude,
                "y": n["y"] / nlen * magnitude,
                "z": n["z"] / nlen * magnitude
            }
        })

    print(f"[{side}]   {len(deltas)} webbing vertices identified")
    if not deltas:
        print(f"[{side}]   nothing to morph — try lowering thresholds")
        return

    print(f"[{side}] creating thenar_extend ...")
    result = call("create_morph_target", {
        "mesh_path": mesh,
        "morph_name": "thenar_extend",
        "vertex_deltas": deltas,
        "overwrite": True,
        "rebuild_render_data": False
    })
    print(f"[{side}]   applied {result['deltas_applied']}/"
          f"{result['deltas_submitted']}, "
          f"saved={result['saved_to_disk']}, "
          f"overwrite={result['was_overwrite']}")


def cleanup_test_morph():
    for side in ("left", "right"):
        try:
            call("delete_morph_target", {
                "mesh_path": f"/Game/Characters/MannequinsXR/Meshes/SKM_MannyXR_{side}",
                "morph_name": "eca_test_morph"
            })
            print(f"[cleanup] removed eca_test_morph from {side}")
        except Exception as e:
            # already gone or never existed — fine
            print(f"[cleanup] {side}: {e}")


if __name__ == "__main__":
    print("=== Cleaning up test morphs ===")
    cleanup_test_morph()

    print("\n=== Authoring thenar_extend ===")
    for suffix in ("_l", "_r"):
        try:
            thenar_for_hand(suffix)
        except Exception as e:
            print(f"[{suffix}] FAILED: {e}", file=sys.stderr)
            sys.exit(1)

    print("\n✅ Done. Open SKM_MannyXR_left/right in editor and slide "
          "the 'thenar_extend' morph target in the Skeletal Mesh Editor "
          "preview panel to verify the deformation looks right.")
