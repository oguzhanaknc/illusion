"""Writes the generated assets of the showcase project: a glTF model with two submeshes and a sine tone.

Both are made here from numbers, so they carry no third-party licence. Run it from anywhere:

    python samples/showcase/tools/make_assets.py
"""

import base64
import json
import math
import struct
import wave
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


def write_tone(path, seconds=0.5, frequency=440.0, rate=22050):
    """A mono 16-bit sine tone that fades in and out."""
    frames = int(seconds * rate)
    samples = bytearray()
    for i in range(frames):
        fade = min(1.0, i / (0.05 * rate), (frames - i) / (0.05 * rate))
        samples += struct.pack("<h", int(0.4 * fade * 32767 * math.sin(2 * math.pi * frequency * i / rate)))
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(samples))


if __name__ == "__main__":
    write_model(ROOT / "models" / "blocks.gltf")
    write_tone(ROOT / "audio" / "tone.wav")
