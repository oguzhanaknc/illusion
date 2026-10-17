#!/usr/bin/env python3
"""Generates the small glTF files used by the asset import tests.

Run from this directory: python generate.py
The outputs are committed; rerun only when changing the test data.

- scene/two_nodes.gltf + two_nodes.bin: textured quad, node hierarchy, external buffer and an image
  referenced through "../textures/checker.png".
- embedded.gltf: triangle without normals, uint8 indices, buffer as a base64 data URI.
- binary.glb: the textured quad as a GLB with the PNG stored in the binary chunk.
- unsupported.gltf: requires KHR_draco_mesh_compression (must be rejected).
- scene/bad_image_uri.gltf: the quad with a malformed base64 image data URI (must be rejected).
- textures/checker.png: 2x2 RGBA checker (red, green / blue, white).
"""

import base64
import json
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def png_rgba(width, height, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(pixels[y * width * 4:(y + 1) * width * 4]) for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


CHECKER = png_rgba(2, 2, [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255])

# Quad in the XY plane facing +Z.
QUAD_POSITIONS = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
QUAD_NORMALS = [(0, 0, 1)] * 4
QUAD_UVS = [(0, 1), (1, 1), (1, 0), (0, 0)]
QUAD_INDICES = [0, 1, 2, 0, 2, 3]


def quad_buffer():
    data = b"".join(struct.pack("<3f", *p) for p in QUAD_POSITIONS)
    data += b"".join(struct.pack("<3f", *n) for n in QUAD_NORMALS)
    data += b"".join(struct.pack("<2f", *t) for t in QUAD_UVS)
    data += b"".join(struct.pack("<H", i) for i in QUAD_INDICES)
    while len(data) % 4:
        data += b"\x00"
    return data


def quad_document(buffer_uri, buffer_length, image):
    return {
        "asset": {"version": "2.0", "generator": "illusion tests"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "root", "translation": [1, 2, 3], "children": [1]},
            {"name": "child", "scale": [2, 2, 2], "mesh": 0},
        ],
        "meshes": [{"name": "quad", "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
            "indices": 3, "material": 0}]}],
        "materials": [{
            "name": "checker",
            "pbrMetallicRoughness": {
                "baseColorFactor": [0.5, 1.0, 1.0, 1.0],
                "baseColorTexture": {"index": 0},
                "metallicFactor": 0.25,
                "roughnessFactor": 0.75,
            },
            "emissiveFactor": [0.1, 0.2, 0.3],
            "alphaMode": "MASK",
            "alphaCutoff": 0.3,
            "doubleSided": True,
        }],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"wrapS": 33071, "wrapT": 33648}],
        "images": [image],
        "buffers": [{"uri": buffer_uri, "byteLength": buffer_length} if buffer_uri else {"byteLength": buffer_length}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 48},
            {"buffer": 0, "byteOffset": 48, "byteLength": 48},
            {"buffer": 0, "byteOffset": 96, "byteLength": 32},
            {"buffer": 0, "byteOffset": 128, "byteLength": 12},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-1, -1, 0], "max": [1, 1, 0]},
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"},
        ],
    }


def write(path, data, mode="wb"):
    full = os.path.join(HERE, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, mode) as f:
        f.write(data)


def main():
    write("textures/checker.png", CHECKER)

    buffer = quad_buffer()
    write("scene/two_nodes.bin", buffer)
    doc = quad_document("two_nodes.bin", len(buffer), {"uri": "../textures/checker.png"})
    write("scene/two_nodes.gltf", (json.dumps(doc, indent=2) + "\n").encode())

    # GLB: buffer + PNG in the BIN chunk.
    png = CHECKER + b"\x00" * ((4 - len(CHECKER) % 4) % 4)
    bin_chunk = buffer + png
    doc = quad_document(None, len(bin_chunk), {"bufferView": 4, "mimeType": "image/png"})
    doc["bufferViews"].append({"buffer": 0, "byteOffset": len(buffer), "byteLength": len(CHECKER)})
    json_chunk = json.dumps(doc).encode()
    json_chunk += b" " * ((4 - len(json_chunk) % 4) % 4)
    total = 12 + 8 + len(json_chunk) + 8 + len(bin_chunk)
    glb = struct.pack("<III", 0x46546C67, 2, total)
    glb += struct.pack("<II", len(json_chunk), 0x4E4F534A) + json_chunk
    glb += struct.pack("<II", len(bin_chunk), 0x004E4942) + bin_chunk
    write("binary.glb", glb)

    # Triangle without normals, uint8 indices, embedded buffer.
    tri = b"".join(struct.pack("<3f", *p) for p in [(0, 0, 0), (1, 0, 0), (0, 1, 0)]) + bytes([0, 1, 2, 0])
    embedded = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "buffers": [{"uri": "data:application/octet-stream;base64," + base64.b64encode(tri).decode(),
                     "byteLength": len(tri)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36},
                        {"buffer": 0, "byteOffset": 36, "byteLength": 3}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
            {"bufferView": 1, "componentType": 5121, "count": 3, "type": "SCALAR"},
        ],
    }
    write("embedded.gltf", (json.dumps(embedded, indent=2) + "\n").encode())

    unsupported = dict(embedded)
    unsupported["extensionsUsed"] = ["KHR_draco_mesh_compression"]
    unsupported["extensionsRequired"] = ["KHR_draco_mesh_compression"]
    write("unsupported.gltf", (json.dumps(unsupported, indent=2) + "\n").encode())

    doc = quad_document("two_nodes.bin", len(buffer), {"uri": "data:image/png;base64,===="})
    write("scene/bad_image_uri.gltf", (json.dumps(doc, indent=2) + "\n").encode())


if __name__ == "__main__":
    main()
