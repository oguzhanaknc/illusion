"""Writes the generated assets of the showcase project: a glTF model with two submeshes, a skinned column
with two clips and an animation graph for them, a skinned two-legged figure, the sounds (music, wind, a lamp hum, a voice and a click), a smoke sprite and a nine-slice panel
for the HUD.

All of them are made here from numbers, so they carry no third-party licence. Run it from anywhere:

    python samples/showcase/tools/make_assets.py
"""

import base64
import json
import math
import struct
import wave
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "assets"


def box(low, high):
    """Positions, normals and indices of an axis-aligned box (24 vertices, 36 indices)."""
    (x0, y0, z0), (x1, y1, z1) = low, high
    faces = [
        ((1, 0, 0), [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)]),
        ((-1, 0, 0), [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)]),
        ((0, 1, 0), [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)]),
        ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
        ((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
        ((0, 0, -1), [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)]),
    ]
    positions, normals, indices = [], [], []
    for normal, corners in faces:
        base = len(positions)
        positions += corners
        normals += [normal] * 4
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, indices


def write_model(path):
    """Mesh 0: a slab (submesh 0) and a block on top of it (submesh 1). Mesh 1: a low step."""
    parts = [box((-2, -0.25, -2), (2, 0.25, 2)), box((-0.5, 0.25, -0.5), (0.5, 1.25, 0.5)),
             box((-1, 0, -0.5), (1, 0.2, 0.5))]
    data = bytearray()
    views, accessors, primitives = [], [], []
    for positions, normals, indices in parts:
        attributes = {}
        for name, values in (("POSITION", positions), ("NORMAL", normals)):
            offset = len(data)
            for value in values:
                data += struct.pack("<3f", *value)
            views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data) - offset})
            accessor = {"bufferView": len(views) - 1, "componentType": 5126, "count": len(values), "type": "VEC3"}
            if name == "POSITION":
                accessor["min"] = [min(v[i] for v in values) for i in range(3)]
                accessor["max"] = [max(v[i] for v in values) for i in range(3)]
            accessors.append(accessor)
            attributes[name] = len(accessors) - 1
        offset = len(data)
        for index in indices:
            data += struct.pack("<H", index)
        while len(data) % 4:
            data += b"\0"
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(indices) * 2})
        accessors.append({"bufferView": len(views) - 1, "componentType": 5123, "count": len(indices),
                          "type": "SCALAR"})
        primitives.append({"attributes": attributes, "indices": len(accessors) - 1})
    gltf = {
        "asset": {"version": "2.0", "generator": "illusion showcase make_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [{"name": "blocks", "mesh": 0}, {"name": "step", "mesh": 1, "translation": [0, 0, 3]}],
        "meshes": [{"name": "blocks", "primitives": primitives[:2]}, {"name": "step", "primitives": primitives[2:]}],
        "buffers": [{"byteLength": len(data),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(data)).decode()}],
        "bufferViews": views,
        "accessors": accessors,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(gltf, indent=1) + "\n", encoding="utf-8", newline="\n")


def write_rigged(path):
    """A square column on two joints, "base" and "top" (one metre up), with the clip "sway" bending the top
    joint 0.5 radians around z and back over two seconds, and the clip "lean" bending it 0.3 radians around x
    and back. The middle ring is shared by both joints."""
    half = 0.2
    corners = [(-half, -half), (half, -half), (half, half), (-half, half)]
    positions, normals, joints, weights = [], [], [], []
    for ring, (low, high) in enumerate(((0, 0), (0, 1), (1, 1))):
        for x, z in corners:
            positions.append((x, float(ring), z))
            length = math.hypot(x, z)
            normals.append((x / length, 0.0, z / length))
            joints.append((low, high, 0, 0))
            weights.append((0.5, 0.5, 0, 0) if low != high else (1, 0, 0, 0))
    indices = []
    for ring in range(2):
        for side in range(4):
            a, b = ring * 4 + side, ring * 4 + (side + 1) % 4
            indices += [a, b, b + 4, a, b + 4, a + 4]
    times = [0.0, 1.0, 2.0]
    angles = [0.0, 0.5, 0.0]
    rotations = [(0.0, 0.0, math.sin(angle / 2), math.cos(angle / 2)) for angle in angles]
    inverse_binds = [(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1), (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1)]

    data = bytearray()
    views, accessors = [], []

    def add(fmt, rows, component_type, accessor_type, minmax=False):
        while len(data) % 4:
            data.extend(b"\0")
        offset = len(data)
        for row in rows:
            data.extend(struct.pack("<" + fmt, *(row if isinstance(row, tuple) else (row,))))
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data) - offset})
        accessor = {"bufferView": len(views) - 1, "componentType": component_type, "count": len(rows),
                    "type": accessor_type}
        if minmax:
            columns = list(zip(*[row if isinstance(row, tuple) else (row,) for row in rows]))
            accessor["min"] = [min(c) for c in columns]
            accessor["max"] = [max(c) for c in columns]
        accessors.append(accessor)
        return len(accessors) - 1

    attributes = {"POSITION": add("3f", positions, 5126, "VEC3", minmax=True),
                  "NORMAL": add("3f", normals, 5126, "VEC3"),
                  "JOINTS_0": add("4B", joints, 5121, "VEC4"),
                  "WEIGHTS_0": add("4f", weights, 5126, "VEC4")}
    index_accessor = add("H", indices, 5123, "SCALAR")
    inverse_bind = add("16f", inverse_binds, 5126, "MAT4")
    time_accessor = add("f", times, 5126, "SCALAR", minmax=True)
    sampler = {"input": time_accessor, "output": add("4f", rotations, 5126, "VEC4"), "interpolation": "LINEAR"}
    leans = [(math.sin(angle / 2), 0.0, 0.0, math.cos(angle / 2)) for angle in (0.0, 0.3, 0.0)]
    lean = {"input": time_accessor, "output": add("4f", leans, 5126, "VEC4"), "interpolation": "LINEAR"}
    gltf = {
        "asset": {"version": "2.0", "generator": "illusion showcase make_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 2]}],
        "nodes": [{"name": "base", "children": [1]}, {"name": "top", "translation": [0, 1, 0]},
                  {"name": "column", "mesh": 0, "skin": 0}],
        "meshes": [{"name": "column", "primitives": [{"attributes": attributes, "indices": index_accessor}]}],
        "skins": [{"name": "column", "joints": [0, 1], "inverseBindMatrices": inverse_bind}],
        "animations": [{"name": "sway", "samplers": [sampler],
                        "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}}]},
                       {"name": "lean", "samplers": [lean],
                        "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}}]}],
        "buffers": [{"byteLength": len(data),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(data)).decode()}],
        "bufferViews": views,
        "accessors": accessors,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(gltf, indent=1) + "\n", encoding="utf-8", newline="\n")


STRIDER_NAMES = ["hips", "thigh_l", "shin_l", "foot_l", "thigh_r", "shin_r", "foot_r", "spine", "head"]
# The second rig's joint names, Mixamo-style: the Strider's clip plays on it retargeted (ADR 0065).
SMALL_STRIDER_NAMES = ["mixamorig:Hips", "mixamorig:LeftUpLeg", "mixamorig:LeftLeg", "mixamorig:LeftFoot",
                       "mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:RightFoot", "mixamorig:Spine",
                       "mixamorig:Head"]


def write_strider(path, names=STRIDER_NAMES, scale=1.0, clip=True):
    """A small two-legged figure for look-at, two-bone IK and foot placement: "hips" (0.9 m up) with the legs
    "thigh_l", "shin_l", "foot_l" and "thigh_r", "shin_r", "foot_r" (ankles 0.08 m above the ground) and
    "spine" and "head" above it, one box per joint, and the clip "idle" swaying the spine 0.2 radians around x
    and back over two seconds. With other `names`, a `scale` and no `clip` it writes the second rig: the same
    skeleton renamed, every length scaled, without a clip of its own."""
    parents = [-1, 0, 1, 2, 0, 4, 5, 0, 7]
    offsets = [(0, 0.9, 0), (0.15, 0, 0), (0, -0.41, 0), (0, -0.41, 0), (-0.15, 0, 0), (0, -0.41, 0),
               (0, -0.41, 0), (0, 0.15, 0), (0, 0.45, 0)]
    if scale != 1.0:
        offsets = [tuple(round(v * scale, 6) + 0.0 for v in offset) for offset in offsets]
    model = []
    for joint, (parent, offset) in enumerate(zip(parents, offsets)):
        base = model[parent] if parent >= 0 else (0, 0, 0)
        model.append(tuple(b + o for b, o in zip(base, offset)))
    # Each box is skinned to one joint, given in model space around that joint.
    boxes = [(0, (-0.22, -0.1, -0.12), (0.22, 0.12, 0.12)), (1, (-0.06, -0.41, -0.06), (0.06, 0, 0.06)),
             (2, (-0.05, -0.41, -0.05), (0.05, 0, 0.05)), (3, (-0.06, -0.08, -0.06), (0.06, 0.02, 0.18)),
             (4, (-0.06, -0.41, -0.06), (0.06, 0, 0.06)), (5, (-0.05, -0.41, -0.05), (0.05, 0, 0.05)),
             (6, (-0.06, -0.08, -0.06), (0.06, 0.02, 0.18)), (7, (-0.18, 0, -0.1), (0.18, 0.45, 0.1)),
             (8, (-0.1, 0, -0.1), (0.1, 0.22, 0.12))]
    if scale != 1.0:
        boxes = [(joint, tuple(round(v * scale, 6) + 0.0 for v in low),
                  tuple(round(v * scale, 6) + 0.0 for v in high)) for joint, low, high in boxes]
    positions, normals, joints, weights, indices = [], [], [], [], []
    for joint, low, high in boxes:
        origin = model[joint]
        box_positions, box_normals, box_indices = box(tuple(o + l for o, l in zip(origin, low)),
                                                      tuple(o + h for o, h in zip(origin, high)))
        base = len(positions)
        positions += box_positions
        normals += box_normals
        joints += [(joint, 0, 0, 0)] * len(box_positions)
        weights += [(1.0, 0.0, 0.0, 0.0)] * len(box_positions)
        indices += [base + i for i in box_indices]
    inverse_binds = [(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1) for x, y, z in model]
    times = [0.0, 1.0, 2.0]
    sways = [(math.sin(angle / 2), 0.0, 0.0, math.cos(angle / 2)) for angle in (0.0, 0.2, 0.0)]

    data = bytearray()
    views, accessors = [], []

    def add(fmt, rows, component_type, accessor_type, minmax=False):
        while len(data) % 4:
            data.extend(b"\0")
        offset = len(data)
        for row in rows:
            data.extend(struct.pack("<" + fmt, *(row if isinstance(row, tuple) else (row,))))
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data) - offset})
        accessor = {"bufferView": len(views) - 1, "componentType": component_type, "count": len(rows),
                    "type": accessor_type}
        if minmax:
            columns = list(zip(*[row if isinstance(row, tuple) else (row,) for row in rows]))
            accessor["min"] = [min(c) for c in columns]
            accessor["max"] = [max(c) for c in columns]
        accessors.append(accessor)
        return len(accessors) - 1

    attributes = {"POSITION": add("3f", positions, 5126, "VEC3", minmax=True),
                  "NORMAL": add("3f", normals, 5126, "VEC3"),
                  "JOINTS_0": add("4B", joints, 5121, "VEC4"),
                  "WEIGHTS_0": add("4f", weights, 5126, "VEC4")}
    index_accessor = add("H", indices, 5123, "SCALAR")
    inverse_bind = add("16f", inverse_binds, 5126, "MAT4")
    if clip:
        time_accessor = add("f", times, 5126, "SCALAR", minmax=True)
        sway = {"input": time_accessor, "output": add("4f", sways, 5126, "VEC4"), "interpolation": "LINEAR"}
    nodes = []
    for joint, (name, offset) in enumerate(zip(names, offsets)):
        node = {"name": name, "translation": list(offset)}
        children = [child for child, parent in enumerate(parents) if parent == joint]
        if children:
            node["children"] = children
        nodes.append(node)
    nodes.append({"name": "strider", "mesh": 0, "skin": 0})
    gltf = {
        "asset": {"version": "2.0", "generator": "illusion showcase make_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, len(nodes) - 1]}],
        "nodes": nodes,
        "meshes": [{"name": "strider", "primitives": [{"attributes": attributes, "indices": index_accessor}]}],
        "skins": [{"name": "strider", "joints": list(range(len(names))), "inverseBindMatrices": inverse_bind}],
        **({"animations": [{"name": "idle", "samplers": [sway],
                            "channels": [{"sampler": 0, "target": {"node": 7, "path": "rotation"}}]}]} if clip else {}),
        "buffers": [{"byteLength": len(data),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(data)).decode()}],
        "bufferViews": views,
        "accessors": accessors,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(gltf, indent=1) + "\n", encoding="utf-8", newline="\n")


RATE = 22050


def write_wav(path, samples):
    """Writes mono 16-bit PCM from floats in [-1, 1]."""
    data = bytearray()
    for value in samples:
        data += struct.pack("<h", int(max(-1.0, min(1.0, value)) * 32767))
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(bytes(data))


def note(frequency, seconds, start, buffer, volume):
    """Adds a soft plucked note (a few harmonics with a fast attack and an exponential decay) to buffer."""
    first = int(start * RATE)
    for i in range(int(seconds * RATE)):
        if first + i >= len(buffer):
            break
        t = i / RATE
        envelope = min(1.0, t / 0.01) * math.exp(-3.0 * t)
        wave_value = (math.sin(2 * math.pi * frequency * t) + 0.35 * math.sin(4 * math.pi * frequency * t)
                      + 0.12 * math.sin(6 * math.pi * frequency * t))
        buffer[first + i] += volume * envelope * wave_value


def music():
    """Four bars of a gentle arpeggio (C, A minor, F, G) that loop seamlessly: 6.4 seconds."""
    beat = 0.4
    loop = int(16 * beat * RATE)
    buffer = [0.0] * (loop + 2 * RATE)
    chords = [(261.63, 329.63, 392.00), (220.00, 261.63, 329.63), (174.61, 220.00, 261.63), (196.00, 246.94, 293.66)]
    for bar, chord in enumerate(chords):
        pattern = (chord[0], chord[1], chord[2], chord[1])
        for step, frequency in enumerate(pattern):
            note(frequency, 1.6, (bar * 4 + step) * beat, buffer, 0.18)
        note(chord[0] / 2, 1.6, bar * 4 * beat, buffer, 0.14)  # the bass of the bar
    # The tails of the last bar run past the end: fold them onto the start so the loop has no seam.
    for i in range(loop, len(buffer)):
        buffer[i - loop] += buffer[i]
    return buffer[:loop]


def wind():
    """Soft wind: low-passed noise with a slow swell, 4 seconds, faded at both ends to loop quietly."""
    state = 12345
    low = 0.0
    lower = 0.0
    frames = int(4.0 * RATE)
    buffer = []
    for i in range(frames):
        state = (1103515245 * state + 12345) % 2**31
        white = state / 2**30 - 1.0
        low += 0.02 * (white - low)
        lower += 0.05 * (low - lower)
        swell = 0.6 + 0.4 * math.sin(2 * math.pi * i / frames)
        edge = min(1.0, i / (0.3 * RATE), (frames - i) / (0.3 * RATE))
        buffer.append(2.5 * lower * swell * edge)
    return buffer


def hum():
    """The lamp's mains hum: 100 Hz with odd harmonics, 2 seconds (a whole number of periods, so it loops)."""
    return [0.12 * (math.sin(2 * math.pi * 100 * i / RATE) + 0.3 * math.sin(2 * math.pi * 300 * i / RATE)
                    + 0.1 * math.sin(2 * math.pi * 500 * i / RATE)) for i in range(int(2.0 * RATE))]


def voice():
    """A friendly three-note greeting (rising, like "hel-lo there"), 0.9 seconds."""
    buffer = [0.0] * int(0.9 * RATE)
    for start, frequency in ((0.0, 523.25), (0.22, 659.25), (0.44, 783.99)):
        note(frequency, 0.45, start, buffer, 0.3)
    return buffer


def click():
    """A short UI click: 15 ms of a decaying 2 kHz burst."""
    return [0.5 * math.exp(-i / (0.002 * RATE)) * math.sin(2 * math.pi * 2000 * i / RATE)
            for i in range(int(0.015 * RATE))]


PNG_SIGNATURE = bytes([137, 80, 78, 71, 13, 10, 26, 10])


def write_smoke(path, size=64):
    """A soft grey puff for the dust particles: an RGBA PNG whose alpha falls off from the centre."""
    rows = bytearray()
    for y in range(size):
        rows.append(0)  # PNG filter type: none
        for x in range(size):
            dx = (x + 0.5) / size * 2 - 1
            dy = (y + 0.5) / size * 2 - 1
            falloff = max(0.0, 1.0 - math.sqrt(dx * dx + dy * dy))
            alpha = int(255 * falloff ** 1.5)
            rows += bytes((235, 230, 220, alpha))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = (PNG_SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def write_png(path, size, rows):
    """Writes @p rows (filter byte included) as an RGBA PNG."""

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = (PNG_SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def write_panel(path, size=32, corner=8, edge=3):
    """The HUD panel: a dark blue rounded rectangle with a light edge, to be drawn as a nine-slice with
    borders of `corner` pixels."""
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        for x in range(size):
            px = min(x, size - 1 - x)
            py = min(y, size - 1 - y)
            distance = min(px, py)
            if px < corner and py < corner:
                radius = math.hypot(corner - px - 0.5, corner - py - 0.5)
                if radius > corner:
                    rows += bytes((0, 0, 0, 0))
                elif radius > corner - edge:
                    rows += bytes((200, 210, 230, 255))
                else:
                    rows += bytes((30, 40, 70, 230))
            elif distance < edge:
                rows += bytes((200, 210, 230, 255))
            else:
                rows += bytes((30, 40, 70, 230))
    write_png(path, size, rows)


def write_column_graph(path):
    """The animation graph of the column: it holds "sway" until the trigger "bow" fades it into "swing", a
    blend of "sway" and "lean" by the Float "lean" with the event "peak" at its middle, which returns to
    "hold" once "calm" is set; "mood" (an Int) speeds nothing up but is there for the scripting API."""
    clip = "assets:/models/column.gltf#animation/"
    graph = {
        "format": "illusion.animgraph",
        "version": 1,
        "parameters": [{"name": "lean", "type": "Float", "default": 0.5}, {"name": "calm", "type": "Bool"},
                       {"name": "mood", "type": "Int"}, {"name": "bow", "type": "Trigger"}],
        "layers": [{
            "name": "base",
            "entry": "hold",
            "states": [
                {"name": "hold", "clip": clip + "sway"},
                {"name": "swing", "blend1d": {"parameter": "lean", "points": [
                    {"value": 0, "clip": clip + "sway"}, {"value": 1, "clip": clip + "lean"}]},
                 "events": [{"name": "peak", "time": 0.5}]}],
            "transitions": [
                {"from": "hold", "to": "swing", "duration": 0.3, "conditions": [{"parameter": "bow"}]},
                {"from": "swing", "to": "hold", "duration": 0.3, "exitTime": 0.9,
                 "conditions": [{"parameter": "calm", "compare": "Equal", "value": True}]}],
        }],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(graph, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_strider_ragdoll(path):
    """The ragdoll profile of the strider: a capsule across the hips, one along each thigh, shin and the spine,
    and a ball for the head, with tighter limits for the knees and the neck."""
    leg = {"radius": 0.06, "halfHeight": 0.145, "offset": [0, -0.205, 0]}
    profile = {
        "format": "illusion.ragdoll",
        "version": 1,
        "parts": [
            {"joint": "hips", "radius": 0.12, "halfHeight": 0.1, "rotation": [0, 0, 0.7071068, 0.7071068],
             "massShare": 0.3},
            dict(joint="thigh_l", **leg), dict(joint="shin_l", swingLimit=0.5, twistMin=-0.1, twistMax=0.1, **leg),
            dict(joint="thigh_r", **leg), dict(joint="shin_r", swingLimit=0.5, twistMin=-0.1, twistMax=0.1, **leg),
            {"joint": "spine", "radius": 0.14, "halfHeight": 0.08, "offset": [0, 0.225, 0]},
            {"joint": "head", "radius": 0.11, "halfHeight": 0, "offset": [0, 0.1, 0], "swingLimit": 0.6},
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(profile, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_guard_tree(path):
    """The behaviour tree of the AI guard: it walks to a target its perception sees and asks game code to
    "greet" it, and otherwise wanders around "home" (a starting value of the agent) with pauses. Game code
    writes "alert", "greetings" and "boldness" through the AiService."""
    tree = {
        "format": "illusion.behavior",
        "version": 1,
        "blackboard": [{"name": "target", "type": "Entity"}, {"name": "lastSeen", "type": "Vector3"},
                       {"name": "home", "type": "Vector3"}, {"name": "alert", "type": "Bool"},
                       {"name": "greetings", "type": "Int"}, {"name": "boldness", "type": "Float"}],
        "root": {"selector": [
            {"guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority", "child": {"sequence": [
                {"moveTo": {"key": "target", "acceptance": 1.5}}, {"action": "greet"}]}}},
            {"sequence": [{"wander": {"key": "home", "radius": 5}},
                          {"wait": {"seconds": 1.5, "deviation": 0.5}}]}]},
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(tree, indent=2) + "\n", encoding="utf-8", newline="\n")


SHOWCASE_SHADER = """// A pulsing emissive material that declares every parameter type: a float with a range, a vector, two colours,
// an int, a bool and a texture. The material pulse.material sets each of them.
struct Parameters
{
    [Color(1, 0.4, 0.1, 1)] float4 tint;
    [Color(0.1, 0.1, 0.1, 1)] float3 base;
    [Default(2.5)] [Range(0, 10)] float intensity;
    [Default(3)] int bands;
    [Default(1)] bool pulse;
    [Default2(0.5, 0.25)] float2 scroll;
    [Texture("white", "mask")] Texture2DSlot mask;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    const float2 uv = input.uv + p.scroll * input.time;
    const float band = step(0.5, frac(uv.y * float(p.bands)));
    const float beat = p.pulse ? 0.5 + 0.5 * sin(input.time * 3.0) : 1.0;
    SurfaceOutput output = defaultSurface();
    output.baseColor = p.base;
    output.emission = p.tint.rgb * p.intensity * band * beat * sampleTexture(input, p.mask, uv).r;
    return output;
}
"""


def write_pulse_shader(path):
    """The material shader of the showcase (ADR 0063)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(SHOWCASE_SHADER, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    write_model(ROOT / "models" / "blocks.gltf")
    write_rigged(ROOT / "models" / "column.gltf")
    write_strider(ROOT / "models" / "strider.gltf")
    write_strider(ROOT / "models" / "small_strider.gltf", SMALL_STRIDER_NAMES, 0.75, clip=False)
    write_column_graph(ROOT / "animations" / "column.animgraph")
    write_strider_ragdoll(ROOT / "animations" / "strider.ragdoll")
    write_guard_tree(ROOT / "ai" / "guard.behavior")
    write_pulse_shader(ROOT / "shaders" / "pulse.shader")
    for name, samples in (("music", music()), ("wind", wind()), ("hum", hum()), ("voice", voice()),
                          ("click", click())):
        write_wav(ROOT / "audio" / f"{name}.wav", samples)
    write_smoke(ROOT / "textures" / "smoke.png")
    write_panel(ROOT / "textures" / "panel.png")
