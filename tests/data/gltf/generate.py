#!/usr/bin/env python3
"""Generates the small glTF files used by the asset import tests.

Run from this directory: python generate.py
The outputs are committed; rerun only when changing the test data.

- scene/two_nodes.gltf + two_nodes.bin: textured quad, node hierarchy, external buffer and an image
  referenced through "../textures/checker.png".
- embedded.gltf: triangle without normals, uint8 indices, buffer as a base64 data URI.
- binary.glb: the textured quad as a GLB with the PNG stored in the binary chunk.
- unsupported.gltf: requires KHR_draco_mesh_compression (must be rejected).
- translucent.gltf: the triangle with a leaf material that requires KHR_materials_diffuse_transmission.
- scene/bad_image_uri.gltf: the quad with a malformed base64 image data URI (must be rejected).
- rigged.gltf: a skinned strip with two skins and three animations (see rigged_document()).
- humanoid_a.gltf, humanoid_b.gltf: humanoid rigs that follow the joint naming and proportions of two common
  CC0 conventions, for rig maps and retargeting (see humanoid_a_joints() and humanoid_b_joints()).
- textures/checker.png: 2x2 RGBA checker (red, green / blue, white).
"""

import base64
import json
import math
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

    translucent = json.loads(json.dumps(embedded))
    translucent["extensionsUsed"] = ["KHR_materials_diffuse_transmission"]
    translucent["extensionsRequired"] = ["KHR_materials_diffuse_transmission"]
    translucent["materials"] = [{
        "name": "leaf",
        "doubleSided": True,
        "extensions": {"KHR_materials_diffuse_transmission": {
            "diffuseTransmissionFactor": 0.25, "diffuseTransmissionColorFactor": [1.0, 0.75, 0.5]}},
    }]
    translucent["meshes"][0]["primitives"][0]["material"] = 0
    write("translucent.gltf", (json.dumps(translucent, indent=2) + "\n").encode())

    doc = quad_document("two_nodes.bin", len(buffer), {"uri": "data:image/png;base64,===="})
    write("scene/bad_image_uri.gltf", (json.dumps(doc, indent=2) + "\n").encode())

    write("rigged.gltf", (json.dumps(rigged_document(), indent=2) + "\n").encode())
    write("humanoid_a.gltf", (json.dumps(humanoid_a_document(), indent=2) + "\n").encode())
    write("humanoid_b.gltf", (json.dumps(humanoid_b_document(), indent=2) + "\n").encode())


class BufferBuilder:
    """Packs accessors into one embedded buffer, each in its own 4-byte aligned buffer view."""

    def __init__(self):
        self.data = b""
        self.views = []
        self.accessors = []

    def add(self, fmt, rows, component_type, accessor_type, normalized=False, minmax=False):
        while len(self.data) % 4:
            self.data += b"\x00"
        offset = len(self.data)
        for row in rows:
            self.data += struct.pack("<" + fmt, *(row if isinstance(row, (list, tuple)) else [row]))
        self.views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(self.data) - offset})
        accessor = {"bufferView": len(self.views) - 1, "componentType": component_type, "count": len(rows),
                    "type": accessor_type}
        if normalized:
            accessor["normalized"] = True
        if minmax:
            columns = list(zip(*[row if isinstance(row, (list, tuple)) else [row] for row in rows]))
            accessor["min"] = [min(c) for c in columns]
            accessor["max"] = [max(c) for c in columns]
        self.accessors.append(accessor)
        return len(self.accessors) - 1


def rigged_document():
    """rigged.gltf: a skinned strip on five joints listed children first, a second skin without inverse bind
    matrices, one morph target, and the animations "step" (Step), "linear" (Linear, with a channel on a
    non-joint node and a morph weights channel, both skipped on import) and "cubic" (CubicSpline)."""
    b = BufferBuilder()
    positions = [(-0.5, 0, 2), (0.5, 0, 2), (-0.5, 1, 2), (0.5, 1, 2), (-0.5, 2, 2), (0.5, 2, 2), (1.5, 0, 2),
                 (1.0, 1.0, 2)]
    # Skin joint order: tip 0, mid 1, root 2, extra 3, side 4. Vertex 7 has five influences.
    joints0 = [(2, 0, 0, 0), (2, 0, 0, 0), (2, 1, 0, 0), (2, 1, 0, 0), (1, 0, 0, 0), (0, 0, 0, 0), (4, 3, 0, 0),
               (0, 1, 2, 3)]
    weights0 = [(1, 0, 0, 0), (1, 0, 0, 0), (0.5, 0.5, 0, 0), (0.5, 0.5, 0, 0), (0.25, 0.75, 0, 0), (1, 0, 0, 0),
                (0.6, 0.4, 0, 0), (0.1, 0.15, 0.3, 0.05)]
    joints1 = [(0, 0, 0, 0)] * 7 + [(4, 0, 0, 0)]
    weights1 = [(0, 0, 0, 0)] * 7 + [(26214, 0, 0, 0)]  # unorm16 0.4
    position = b.add("3f", positions, 5126, "VEC3", minmax=True)
    joint0 = b.add("4B", joints0, 5121, "VEC4")
    weight0 = b.add("4f", weights0, 5126, "VEC4")
    joint1 = b.add("4H", joints1, 5123, "VEC4")
    weight1 = b.add("4H", weights1, 5123, "VEC4", normalized=True)
    morph = b.add("3f", [(0, 0, 0)] * len(positions), 5126, "VEC3", minmax=True)
    indices = b.add("H", [0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4, 1, 6, 7, 1, 7, 3], 5123, "SCALAR")
    bind = {"tip": (0, 2, 2), "mid": (0, 1, 2), "root": (0, 0, 2), "extra": (2, 0, 2), "side": (1, 0, 2)}
    inverse_binds = []
    for name in ["tip", "mid", "root", "extra", "side"]:
        x, y, z = bind[name]
        inverse_binds.append((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1))
    inverse_bind = b.add("16f", inverse_binds, 5126, "MAT4")

    def sampler(times, values, fmt, accessor_type, interpolation):
        times_accessor = b.add("f", times, 5126, "SCALAR", minmax=True)
        return {"input": times_accessor, "output": b.add(fmt, values, 5126, accessor_type),
                "interpolation": interpolation}

    step = {"name": "step", "samplers": [sampler([0, 1, 2], [(0, 1, 0), (0, 2, 0), (0, 3, 0)], "3f", "VEC3", "STEP")],
            "channels": [{"sampler": 0, "target": {"node": 2, "path": "translation"}}]}
    linear = {"name": "linear", "samplers": [
        sampler([0, 1], [(0, 0, 0, 1), (0, 0, 0.70710678, 0.70710678)], "4f", "VEC4", "LINEAR"),
        sampler([0.5, 1.5], [(0, 1, 0), (0, 3, 0)], "3f", "VEC3", "LINEAR"),
        sampler([0, 1], [(0, 0, 2), (0, 0, 4)], "3f", "VEC3", "LINEAR"),
        sampler([0, 1], [0, 1], "f", "SCALAR", "LINEAR"),
    ], "channels": [
        {"sampler": 0, "target": {"node": 1, "path": "rotation"}},
        {"sampler": 1, "target": {"node": 3, "path": "translation"}},
        {"sampler": 2, "target": {"node": 0, "path": "translation"}},
        {"sampler": 3, "target": {"node": 6, "path": "weights"}},
    ]}
    cubic = {"name": "cubic", "samplers": [sampler(
        [0, 2], [(0, 0, 0), (1, 1, 1), (1, 0, 0), (0, 1, 0), (2, 2, 2), (0, 0, 0)], "3f", "VEC3", "CUBICSPLINE")],
        "channels": [{"sampler": 0, "target": {"node": 4, "path": "scale"}}]}

    return {
        "asset": {"version": "2.0", "generator": "illusion tests"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "Armature", "translation": [0, 0, 2], "children": [1, 6]},
            {"name": "root", "children": [2, 4]},
            {"name": "mid", "translation": [0, 1, 0], "children": [3]},
            {"name": "tip", "translation": [0, 1, 0]},
            {"name": "side", "translation": [1, 0, 0], "children": [5]},
            {"name": "extra", "translation": [1, 0, 0]},
            {"name": "body", "mesh": 0, "skin": 0},
        ],
        "meshes": [{"name": "strip", "weights": [0], "primitives": [{
            "attributes": {"POSITION": position, "JOINTS_0": joint0, "WEIGHTS_0": weight0, "JOINTS_1": joint1,
                           "WEIGHTS_1": weight1},
            "targets": [{"POSITION": morph}],
            "indices": indices}]}],
        "skins": [
            {"name": "body", "joints": [3, 2, 1, 5, 4], "inverseBindMatrices": inverse_bind},
            {"name": "arm", "joints": [4]},
        ],
        "animations": [step, linear, cubic],
        "buffers": [{"uri": "data:application/octet-stream;base64," + base64.b64encode(b.data).decode(),
                     "byteLength": len(b.data)}],
        "bufferViews": b.views,
        "accessors": b.accessors,
    }


# --- Humanoid rigs ---------------------------------------------------------------------------------------------
# Quaternions are (x, y, z, w). Joints are given in model space (position, rotation) and written as local TRS.

def quat_axis(axis, degrees):
    half = math.radians(degrees) / 2
    s = math.sin(half)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def quat_conj(q):
    return (-q[0], -q[1], -q[2], q[3])


def quat_rotate(q, v):
    x, y, z, _ = quat_mul(quat_mul(q, (v[0], v[1], v[2], 0.0)), quat_conj(q))
    return (x, y, z)


def quat_matrix(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


IDENTITY = (0.0, 0.0, 0.0, 1.0)


def rounded(values, digits=6):
    return [round(v, digits) + 0.0 for v in values]


def humanoid_a_joints():
    """Mixamo-style naming ("mixamorig:" prefix, LeftArm / LeftForeArm / LeftUpLeg, fingers 1 to 4 with an end
    joint), centimetres under an "Armature" node scaled by 0.01, T-pose, 1.8 m tall, hips at 1 m, identity
    joint rotations."""
    joints = []

    def add(name, parent, position, rotation=IDENTITY):
        joints.append(("mixamorig:" + name, None if parent is None else "mixamorig:" + parent, position, rotation))

    add("Hips", None, (0, 100, 0))
    add("Spine", "Hips", (0, 110, 0))
    add("Spine1", "Spine", (0, 122, 0))
    add("Spine2", "Spine1", (0, 135, 0))
    add("Neck", "Spine2", (0, 150, 0))
    add("Head", "Neck", (0, 160, 0))
    add("HeadTop_End", "Head", (0, 178, 0))
    add("LeftEye", "Head", (3, 166, 8))
    add("RightEye", "Head", (-3, 166, 8))
    for side, s in (("Left", 1), ("Right", -1)):
        add(side + "Shoulder", "Spine2", (6 * s, 145, 0))
        add(side + "Arm", side + "Shoulder", (18 * s, 145, 0))
        add(side + "ForeArm", side + "Arm", (46 * s, 145, 0))
        add(side + "Hand", side + "ForeArm", (72 * s, 145, 0))
        for finger, dx, dz in (("Thumb", 2, 4), ("Index", 9, 3), ("Middle", 9.5, 1), ("Ring", 9, -1),
                               ("Pinky", 8, -3)):
            parent = side + "Hand"
            for i in range(1, 5):
                name = f"{side}Hand{finger}{i}"
                add(name, parent, ((72 + dx + 3 * (i - 1)) * s, 145, dz))
                parent = name
        add(side + "UpLeg", "Hips", (9 * s, 95, 0))
        add(side + "Leg", side + "UpLeg", (9 * s, 52, 0))
        add(side + "Foot", side + "Leg", (9 * s, 10, 0))
        add(side + "ToeBase", side + "Foot", (9 * s, 4, 12))
        add(side + "Toe_End", side + "ToeBase", (9 * s, 4, 20))
    return joints


def humanoid_b_joints():
    """Unreal-mannequin-style naming as in CC0 animation libraries (root, pelvis, spine_01 to spine_03,
    clavicle_l, upperarm_l with a twist joint, thigh_l / calf_l / ball_l, fingers _01 to _03), metres,
    A-pose (arms 45 degrees down), 1.3 m tall, hips at 0.8 m, joint +x along the bone."""
    joints = []

    def add(name, parent, position, rotation):
        joints.append((name, parent, position, rotation))

    up = quat_axis((0, 0, 1), 90)
    down = quat_axis((0, 0, 1), -90)
    add("root", None, (0, 0, 0), IDENTITY)
    add("pelvis", "root", (0, 0.8, 0), up)
    add("spine_01", "pelvis", (0, 0.88, 0), up)
    add("spine_02", "spine_01", (0, 0.98, 0), up)
    add("spine_03", "spine_02", (0, 1.08, 0), up)
    add("neck_01", "spine_03", (0, 1.18, 0), up)
    add("head", "neck_01", (0, 1.26, 0), up)
    c = math.sqrt(0.5)
    for side, s in (("l", 1), ("r", -1)):
        along = (c * s, -c, 0)
        arm = quat_axis((0, 0, 1), -45 if s > 0 else 225)

        def at(base, distance):
            return (base[0] + along[0] * distance, base[1] + along[1] * distance, base[2])

        shoulder = (0.04 * s, 1.14, 0)
        upper = (0.14 * s, 1.14, 0)
        lower = at(upper, 0.22)
        hand = at(lower, 0.2)
        add("clavicle_" + side, "spine_03", shoulder, IDENTITY if s > 0 else quat_axis((0, 0, 1), 180))
        add("upperarm_" + side, "clavicle_" + side, upper, arm)
        add("upperarm_twist_01_" + side, "upperarm_" + side, at(upper, 0.11), arm)
        add("lowerarm_" + side, "upperarm_" + side, lower, arm)
        add("hand_" + side, "lowerarm_" + side, hand, arm)
        for finger, start, dz in (("thumb", 0.03, 0.03), ("index", 0.08, 0.02), ("middle", 0.085, 0.005),
                                  ("ring", 0.08, -0.01), ("pinky", 0.07, -0.025)):
            parent = "hand_" + side
            for i in range(1, 4):
                base = at(hand, start + 0.025 * (i - 1))
                name = f"{finger}_{i:02d}_{side}"
                add(name, parent, (base[0], base[1], dz), arm)
                parent = name
        add("thigh_" + side, "pelvis", (0.09 * s, 0.76, 0), down)
        add("calf_" + side, "thigh_" + side, (0.09 * s, 0.41, 0), down)
        add("foot_" + side, "calf_" + side, (0.09 * s, 0.07, 0), down)
        add("ball_" + side, "foot_" + side, (0.09 * s, 0.02, 0.11), quat_axis((0, 1, 0), -90))
    return joints


def humanoid_document(joints, armature_scale, hips, clip_name, clip_channels):
    """A rig of `joints` (name, parent, model position, model rotation) under an "Armature" node scaled by
    `armature_scale`, a triangle skinned to `hips`, and one animation `clip_name` whose channels are
    (joint, path, times, values)."""
    b = BufferBuilder()
    index = {name: i for i, (name, _, _, _) in enumerate(joints)}
    by_name = {name: (parent, position, rotation) for name, parent, position, rotation in joints}

    nodes = [{"name": "Armature", "scale": [armature_scale] * 3, "children": []}]
    for name, parent, position, rotation in joints:
        node = {"name": name}
        if parent is None:
            translation, local = position, rotation
            nodes[0]["children"].append(index[name] + 1)
        else:
            _, parent_position, parent_rotation = by_name[parent]
            inverse = quat_conj(parent_rotation)
            offset = tuple(position[k] - parent_position[k] for k in range(3))
            translation, local = quat_rotate(inverse, offset), quat_mul(inverse, rotation)
        if any(abs(v) > 1e-9 for v in translation):
            node["translation"] = rounded(translation)
        if any(abs(v - w) > 1e-9 for v, w in zip(local, IDENTITY)):
            node["rotation"] = rounded(local)
        nodes.append(node)
    for name, parent, _, _ in joints:
        if parent is not None:
            nodes[index[parent] + 1].setdefault("children", []).append(index[name] + 1)

    # Inverse binds: inverse(Armature scale * T(position) * R(rotation)) = R^T * T(-position) * S^-1.
    inverse_binds = []
    for _, _, position, rotation in joints:
        r = quat_matrix(rotation)
        rt = [[r[col][row] for col in range(3)] for row in range(3)]
        t = [-sum(rt[row][k] * position[k] for k in range(3)) for row in range(3)]
        columns = []
        for col in range(3):
            columns += [rt[row][col] / armature_scale for row in range(3)] + [0.0]
        columns += t + [1.0]
        inverse_binds.append(tuple(columns))

    _, hips_position, _ = by_name[hips]
    x, y, z = (v * armature_scale for v in hips_position)
    position = b.add("3f", [(x - 0.1, y, z), (x + 0.1, y, z), (x, y + 0.1, z)], 5126, "VEC3", minmax=True)
    joint = b.add("4B", [(index[hips], 0, 0, 0)] * 3, 5121, "VEC4")
    weight = b.add("4f", [(1, 0, 0, 0)] * 3, 5126, "VEC4")
    inverse_bind = b.add("16f", inverse_binds, 5126, "MAT4")

    samplers, channels = [], []
    for joint_name, path, times, values in clip_channels:
        fmt, accessor_type = ("4f", "VEC4") if path == "rotation" else ("3f", "VEC3")
        samplers.append({"input": b.add("f", times, 5126, "SCALAR", minmax=True),
                         "output": b.add(fmt, values, 5126, accessor_type), "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": index[joint_name] + 1, "path": path}})

    body = len(nodes)
    nodes.append({"name": "body", "mesh": 0, "skin": 0})
    return {
        "asset": {"version": "2.0", "generator": "illusion tests"},
        "scene": 0,
        "scenes": [{"nodes": [0, body]}],
        "nodes": nodes,
        "meshes": [{"name": "body", "primitives": [{"attributes": {"POSITION": position, "JOINTS_0": joint,
                                                                   "WEIGHTS_0": weight}}]}],
        "skins": [{"name": "body", "joints": list(range(1, len(joints) + 1)), "inverseBindMatrices": inverse_bind}],
        "animations": [{"name": clip_name, "samplers": samplers, "channels": channels}],
        "buffers": [{"uri": "data:application/octet-stream;base64," + base64.b64encode(b.data).decode(),
                     "byteLength": len(b.data)}],
        "bufferViews": b.views,
        "accessors": b.accessors,
    }


def humanoid_a_document():
    """humanoid_a.gltf: the rig of humanoid_a_joints() with a "walk" clip (hips forward 1 m in a second, legs
    swinging 25 degrees about x)."""
    forward = quat_axis((1, 0, 0), 25)
    back = quat_axis((1, 0, 0), -25)
    walk = [
        ("mixamorig:Hips", "translation", [0, 0.5, 1], [(0, 100, 0), (0, 98, 50), (0, 100, 100)]),
        ("mixamorig:LeftUpLeg", "rotation", [0, 0.5, 1], [forward, back, forward]),
        ("mixamorig:RightUpLeg", "rotation", [0, 0.5, 1], [back, forward, back]),
    ]
    return humanoid_document(humanoid_a_joints(), 0.01, "mixamorig:Hips", "walk", walk)


def humanoid_b_document():
    """humanoid_b.gltf: the rig of humanoid_b_joints() with a "wave" clip (the right upper arm raised 30
    degrees about z)."""
    joints = humanoid_b_joints()
    rotations = {name: rotation for name, _, _, rotation in joints}
    rest = quat_mul(quat_conj(rotations["clavicle_r"]), rotations["upperarm_r"])
    raised = quat_mul(rest, quat_axis((0, 0, 1), -30))
    wave = [("upperarm_r", "rotation", [0, 1], [rest, raised])]
    return humanoid_document(joints, 1.0, "pelvis", "wave", wave)


if __name__ == "__main__":
    main()
