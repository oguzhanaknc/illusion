"""Writes the generated assets of the tech demo: the terrain, neon tubes, campfire logs, particle
sprites and looping ambient sounds (the trees and plants come from Poly Haven, see
fetch_assets.py).

All of them are made here from numbers with fixed seeds, so they are the same on every machine and carry no
third-party licence. The scene builder (make_scene.py) uses terrain_height() from this file to put things
on the ground. Run it from anywhere, with only the Python standard library:

    python samples/techdemo/tools/make_assets.py
"""

import json
import math
import random
import struct
import wave
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "assets"

# --- Terrain ------------------------------------------------------------------------------------------------

TERRAIN_SIZE = 120.0  # metres, square, centred on the origin
TERRAIN_QUADS = 160
CLEARING_RADIUS = 9.0  # flat ground around the campfire at the origin
BOWL_RADIUS = 30.0  # beyond this the ground rises into hills that keep the player in


def smoothstep(edge0, edge1, x):
    t = min(max((x - edge0) / (edge1 - edge0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def terrain_height(x, z):
    """Height of the ground in metres at (x, z): a flat clearing, gentle swells, and hills around."""
    r = math.hypot(x, z)
    swell = (0.35 * math.sin(x * 0.21 + 1.3) * math.cos(z * 0.17 - 0.4)
             + 0.2 * math.sin(x * 0.53 - z * 0.41 + 2.1)
             + 0.08 * math.sin(x * 1.37 + z * 1.11))
    hills = 9.0 * smoothstep(BOWL_RADIUS, 58.0, r) ** 1.4 * (0.75 + 0.25 * math.sin(math.atan2(z, x) * 3.0 + 0.7))
    return swell * smoothstep(CLEARING_RADIUS * 0.7, CLEARING_RADIUS * 1.6, r) + hills


def terrain_mesh():
    n = TERRAIN_QUADS
    step = TERRAIN_SIZE / n
    half = TERRAIN_SIZE / 2.0
    positions, normals, tangents, uvs, indices = [], [], [], [], []
    for j in range(n + 1):
        for i in range(n + 1):
            x = -half + i * step
            z = -half + j * step
            y = terrain_height(x, z)
            dx = terrain_height(x + 0.05, z) - terrain_height(x - 0.05, z)
            dz = terrain_height(x, z + 0.05) - terrain_height(x, z - 0.05)
            normal = normalize((-dx / 0.1, 1.0, -dz / 0.1))
            tangent = normalize((1.0, dx / 0.1, 0.0))
            positions.append((x, y, z))
            normals.append(normal)
            tangents.append(tangent + (1.0,))
            uvs.append((x / 4.0, z / 4.0))  # the ground texture repeats every 4 m
    for j in range(n):
        for i in range(n):
            a = j * (n + 1) + i
            b = a + 1
            c = a + n + 1
            d = c + 1
            indices += [a, c, b, b, c, d]
    return positions, normals, tangents, uvs, indices


# --- Geometry helpers ---------------------------------------------------------------------------------------


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def normalize(a):
    length = math.sqrt(dot(a, a))
    return (a[0] / length, a[1] / length, a[2] / length) if length > 1e-9 else (0.0, 1.0, 0.0)


def basis(direction):
    """Two unit vectors perpendicular to `direction` and to each other."""
    helper = (1.0, 0.0, 0.0) if abs(direction[1]) > 0.9 else (0.0, 1.0, 0.0)
    side = normalize(cross(helper, direction))
    return side, cross(direction, side)


class Mesh:
    def __init__(self):
        self.positions, self.normals, self.tangents, self.uvs, self.indices = [], [], [], [], []

    def vertex(self, position, normal, tangent, uv):
        self.positions.append(position)
        self.normals.append(normal)
        self.tangents.append(tuple(tangent) + (1.0,))
        self.uvs.append(uv)
        return len(self.positions) - 1

    def tube(self, start, end, radius0, radius1, sides, v0=0.0, uv_scale=1.0):
        """A tapered open tube from `start` to `end`; returns the v coordinate at its end."""
        axis = sub(end, start)
        length = math.sqrt(dot(axis, axis))
        direction = normalize(axis)
        side, up = basis(direction)
        first = len(self.positions)
        v1 = v0 + length / (2.0 * math.pi * max(radius0, 0.02)) * uv_scale
        for ring, (centre, radius, v) in enumerate(((start, radius0, v0), (end, radius1, v1))):
            for k in range(sides + 1):
                angle = 2.0 * math.pi * k / sides
                normal = add(scale(side, math.cos(angle)), scale(up, math.sin(angle)))
                tangent = add(scale(side, -math.sin(angle)), scale(up, math.cos(angle)))
                self.vertex(add(centre, scale(normal, radius)), normal, tangent, (k / sides, v))
        for k in range(sides):
            a = first + k
            b = a + 1
            c = a + sides + 1
            d = c + 1
            self.indices += [a, b, c, b, d, c]
        return v1

    def disc(self, centre, normal, radius, sides):
        """A flat disc facing `normal` (the end of a log), its UVs a circle in the unit square."""
        side, up = basis(normal)
        first = self.vertex(centre, normal, side, (0.5, 0.5))
        for k in range(sides + 1):
            angle = 2.0 * math.pi * k / sides
            offset = add(scale(side, math.cos(angle) * radius), scale(up, math.sin(angle) * radius))
            self.vertex(add(centre, offset), normal, side, (0.5 + 0.5 * math.cos(angle), 0.5 + 0.5 * math.sin(angle)))
        for k in range(sides):
            # side x up is the normal, so rising angles run counter-clockwise seen from the front.
            self.indices += [first, first + 1 + k, first + 2 + k]



# --- Simple shapes ------------------------------------------------------------------------------------------


def torus(major, minor, segments=96, sides=10, arc=2.0 * math.pi):
    """A ring in the XY plane facing +z (a neon hoop when its material glows)."""
    mesh = Mesh()
    rings = segments + 1
    for i in range(rings):
        a = arc * i / segments
        centre = (major * math.cos(a), major * math.sin(a), 0.0)
        outward = (math.cos(a), math.sin(a), 0.0)
        along = (-math.sin(a), math.cos(a), 0.0)
        for k in range(sides + 1):
            b = 2.0 * math.pi * k / sides
            normal = add(scale(outward, math.cos(b)), scale((0.0, 0.0, 1.0), math.sin(b)))
            mesh.vertex(add(centre, scale(normal, minor)), normal, along, (i / segments, k / sides))
    for i in range(segments):
        for k in range(sides):
            a = i * (sides + 1) + k
            b = a + sides + 1
            mesh.indices += [a, b, a + 1, a + 1, b, b + 1]
    return mesh


def bar(length, radius, sides=10):
    """A tube along +y from 0 to `length`, closed by its material's glow (a neon bar)."""
    mesh = Mesh()
    mesh.tube((0.0, 0.0, 0.0), (0.0, length, 0.0), radius, radius, sides)
    return mesh


def log(length, radius):
    """A log along +x centred on the origin, closed at both ends."""
    mesh = Mesh()
    mesh.tube((-length / 2.0, 0.0, 0.0), (length / 2.0, 0.0, 0.0), radius, radius * 0.9, 10, 0.0, 2.0)
    mesh.disc((-length / 2.0, 0.0, 0.0), (-1.0, 0.0, 0.0), radius, 10)
    mesh.disc((length / 2.0, 0.0, 0.0), (1.0, 0.0, 0.0), radius * 0.9, 10)
    return mesh


# --- glTF ---------------------------------------------------------------------------------------------------


def write_gltf(path, parts, materials):
    """Writes `parts` [(name, positions, normals, tangents, uvs, indices, material)] as one glTF file with a
    `.bin` buffer, one mesh and node per part. `materials` are dicts: name, and optionally colour and normal
    (texture paths relative to the model), alphaMask, emissive, roughness."""
    images = []

    def texture(uri):
        if {"uri": uri} not in images:
            images.append({"uri": uri})
        return {"index": images.index({"uri": uri})}

    def gltf_material(m):
        pbr = {"metallicFactor": 0.0, "roughnessFactor": m.get("roughness", 0.8)}
        if "colour" in m:
            pbr["baseColorTexture"] = texture(m["colour"])
        result = {"name": m["name"], "pbrMetallicRoughness": pbr, "doubleSided": m.get("alphaMask", False)}
        if "normal" in m:
            result["normalTexture"] = texture(m["normal"])
        if m.get("alphaMask"):
            result["alphaMode"] = "MASK"
            result["alphaCutoff"] = 0.5
        if "emissive" in m:
            result["emissiveFactor"] = m["emissive"]
        return result

    blob = bytearray()
    views, accessors, meshes, nodes = [], [], [], []

    def add_view(data, target):
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(data)
        return len(views) - 1

    def add_accessor(values, kind, components):
        flat = [c for v in values for c in v]
        view = add_view(struct.pack(f"<{len(flat)}f", *flat), 34962)
        accessor = {"bufferView": view, "componentType": 5126, "count": len(values), "type": kind}
        if kind == "VEC3":
            accessor["min"] = [min(v[i] for v in values) for i in range(3)]
            accessor["max"] = [max(v[i] for v in values) for i in range(3)]
        accessors.append(accessor)
        return len(accessors) - 1

    for name, positions, normals, tangents, uvs, indices, material in parts:
        attributes = {
            "POSITION": add_accessor(positions, "VEC3", 3),
            "NORMAL": add_accessor(normals, "VEC3", 3),
            "TANGENT": add_accessor(tangents, "VEC4", 4),
            "TEXCOORD_0": add_accessor(uvs, "VEC2", 2),
        }
        view = add_view(struct.pack(f"<{len(indices)}I", *indices), 34963)
        accessors.append({"bufferView": view, "componentType": 5125, "count": len(indices), "type": "SCALAR"})
        meshes.append({"name": name, "primitives": [{"attributes": attributes, "indices": len(accessors) - 1,
                                                       "material": material}]})
        nodes.append({"name": name, "mesh": len(meshes) - 1})

    bin_path = path.with_suffix(".bin")
    document = {
        "asset": {"version": "2.0", "generator": "illusion techdemo make_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": [gltf_material(m) for m in materials],
        "images": images,
        "textures": [{"source": i} for i in range(len(images))],
        "buffers": [{"byteLength": len(blob), "uri": bin_path.name}],
        "bufferViews": views,
        "accessors": accessors,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    bin_path.write_bytes(bytes(blob))
    path.write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")


def mesh_part(name, mesh, material):
    return (name, mesh.positions, mesh.normals, mesh.tangents, mesh.uvs, mesh.indices, material)


# --- Textures -----------------------------------------------------------------------------------------------


def write_png(path, width, height, rows, channels):
    """Writes 8-bit rows (bytes of width * channels) as a PNG (channels 3: RGB, 4: RGBA)."""
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    colour = {3: 2, 4: 6}[channels]
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, colour, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def sprite(path, size, function):
    """An RGBA sprite: `function(dx, dy)` returns (r, g, b, a) in [0, 1] for -1 <= dx, dy <= 1."""
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            dx = (x + 0.5) / size * 2 - 1
            dy = (y + 0.5) / size * 2 - 1
            row += bytes(int(255 * min(max(c, 0.0), 1.0)) for c in function(dx, dy))
        rows.append(row)
    write_png(path, size, size, rows, 4)


def flame(dx, dy):
    # A teardrop, wide at the bottom (dy = 1) and pointed at the top.
    t = (1 - dy) / 2  # 0 at the bottom, 1 at the top
    width = 0.75 * (1 - t) ** 0.6 * (0.4 + 0.6 * math.sin(math.pi * min(t * 1.1 + 0.08, 1.0)))
    inside = max(0.0, 1.0 - abs(dx) / max(width, 1e-3))
    alpha = inside ** 1.2 * smoothstep(1.0, 0.85, t) * smoothstep(-0.05, 0.12, 1 - abs(dy))
    return (1.0, 1.0, 1.0, alpha)


def smoke(dx, dy):
    falloff = max(0.0, 1.0 - math.sqrt(dx * dx + dy * dy))
    return (0.9, 0.9, 0.9, falloff ** 1.8)


# --- Sounds -------------------------------------------------------------------------------------------------

RATE = 22050


def write_wav(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    peak = max(1e-6, max(abs(s) for s in samples))
    gain = min(1.0, 0.9 / peak)
    with wave.open(str(path), "wb") as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(RATE)
        file.writeframes(b"".join(struct.pack("<h", int(32767 * s * gain)) for s in samples))


def loop(samples, fade_seconds=0.5):
    """Crossfades the end into the start so the sound loops without a click."""
    fade = int(fade_seconds * RATE)
    body = samples[:len(samples) - fade]
    for i in range(fade):
        t = i / fade
        body[i] = body[i] * t + samples[len(samples) - fade + i] * (1 - t)
    return body


def forest_wind(seconds=12.0, seed=31):
    """Soft wind: brown noise, low-passed, swelling in slow gusts."""
    rng = random.Random(seed)
    out = []
    low = 0.0
    brown = 0.0
    for i in range(int(seconds * RATE)):
        brown = 0.995 * brown + rng.uniform(-1, 1) * 0.05
        low += (brown - low) * 0.08
        t = i / RATE
        gust = 0.55 + 0.3 * math.sin(2 * math.pi * t / seconds) + 0.15 * math.sin(2 * math.pi * 3 * t / seconds + 1)
        out.append(low * gust)
    return loop(out, 1.0)


def crickets(seconds=6.0, seed=32):
    """Crickets: groups of short 4.5 kHz chirps."""
    rng = random.Random(seed)
    out = [0.0] * int(seconds * RATE)
    for voice in range(3):
        frequency = 4300 + 300 * voice
        t = rng.uniform(0, 0.5)
        while t < seconds - 0.3:
            for pulse in range(3):
                start = int((t + pulse * 0.045) * RATE)
                for k in range(int(0.025 * RATE)):
                    if start + k < len(out):
                        envelope = math.sin(math.pi * k / (0.025 * RATE))
                        out[start + k] += 0.12 * envelope * math.sin(2 * math.pi * frequency * k / RATE)
            t += rng.uniform(0.45, 0.9)
    return loop(out, 0.3)


def fire(seconds=6.0, seed=33):
    """A campfire: a low roar with crackles."""
    rng = random.Random(seed)
    out = []
    low = 0.0
    for _ in range(int(seconds * RATE)):
        low += (rng.uniform(-1, 1) - low) * 0.02
        out.append(low * 0.6)
    for _ in range(int(seconds * 14)):
        start = rng.randrange(len(out))
        strength = rng.uniform(0.2, 0.8)
        length = int(rng.uniform(0.002, 0.012) * RATE)
        for k in range(length):
            if start + k < len(out):
                out[start + k] += strength * rng.uniform(-1, 1) * math.exp(-k / (length / 4 + 1))
    return loop(out, 0.3)


# --- The creature -------------------------------------------------------------------------------------------

# A wolf-like animal of boxes facing +z: its joints (name, parent, offset from the parent in metres; every
# joint starts unrotated) and the boxes skinned to them (joint, low corner, high corner, around the joint).
CREATURE_JOINTS = [
    ("hips", -1, (0.0, 0.55, -0.25)),
    ("chest", 0, (0.0, 0.03, 0.5)),
    ("neck", 1, (0.0, 0.08, 0.2)),
    ("head", 2, (0.0, 0.1, 0.12)),
    ("tail", 0, (0.0, 0.06, -0.14)),
    ("thigh_bl", 0, (0.12, -0.06, 0.0)),
    ("shin_bl", 5, (0.0, -0.24, 0.0)),
    ("thigh_br", 0, (-0.12, -0.06, 0.0)),
    ("shin_br", 7, (0.0, -0.24, 0.0)),
    ("thigh_fl", 1, (0.12, -0.09, 0.0)),
    ("shin_fl", 9, (0.0, -0.24, 0.0)),
    ("thigh_fr", 1, (-0.12, -0.09, 0.0)),
    ("shin_fr", 11, (0.0, -0.24, 0.0)),
]
CREATURE_BOXES = [
    (0, (-0.16, -0.12, -0.12), (0.16, 0.13, 0.28)),
    (1, (-0.18, -0.13, -0.24), (0.18, 0.15, 0.14)),
    (2, (-0.08, -0.06, -0.06), (0.08, 0.1, 0.14)),
    (3, (-0.11, -0.07, -0.06), (0.11, 0.11, 0.16)),
    (3, (-0.06, -0.07, 0.14), (0.06, 0.02, 0.3)),
    (3, (0.04, 0.1, -0.03), (0.1, 0.2, 0.03)),
    (3, (-0.1, 0.1, -0.03), (-0.04, 0.2, 0.03)),
    (4, (-0.03, -0.03, -0.38), (0.03, 0.04, 0.0)),
] + [box for thigh in (5, 7, 9, 11) for box in (
    (thigh, (-0.05, -0.24, -0.06), (0.05, 0.03, 0.07)),
    (thigh + 1, (-0.035, -0.25, -0.035), (0.035, 0.0, 0.04)),
    (thigh + 1, (-0.045, -0.25, -0.04), (0.045, -0.2, 0.08)))]
# Clip name: (seconds per cycle, keys per cycle).
CREATURE_CLIPS = {"idle": (3.0, 12), "walk": (1.0, 16), "run": (0.55, 16)}
# The walk sets the feet down one after another, the run (a gallop) the hind and the fore legs in pairs.
WALK_PHASES = {"bl": 0.0, "fl": 0.25, "br": 0.5, "fr": 0.75}
RUN_PHASES = {"bl": 0.0, "br": 0.1, "fl": 0.5, "fr": 0.6}


def creature_box(low, high):
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


def creature_model_positions():
    """Model-space position of every joint of the bind pose."""
    model = []
    for _, parent, offset in CREATURE_JOINTS:
        model.append(add(model[parent], offset) if parent >= 0 else offset)
    return model


def axis_rotation(axis, angle):
    s = math.sin(angle / 2.0)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle / 2.0))


def creature_pose(clip, u):
    """The local rotations (quaternions x, y, z, w by joint name) and the hips' translation of @p clip at
    @p u, the fraction of its cycle in [0, 1]. Legs swing about x: a positive angle moves the foot back."""
    turn = 2.0 * math.pi * u
    rotations = {}
    hips = list(CREATURE_JOINTS[0][2])
    x_axis, y_axis = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)
    if clip == "idle":
        rotations["chest"] = axis_rotation(x_axis, 0.03 * math.sin(turn))
        rotations["head"] = axis_rotation(y_axis, 0.35 * math.sin(turn))
        rotations["neck"] = axis_rotation(x_axis, -0.05 + 0.04 * math.sin(turn + 1.0))
        rotations["tail"] = axis_rotation(y_axis, 0.25 * math.sin(2.0 * turn))
        hips[1] += 0.008 * math.sin(turn)
    else:
        walk = clip == "walk"
        phases = WALK_PHASES if walk else RUN_PHASES
        swing, bend = (0.45, 0.7) if walk else (0.8, 1.1)
        for leg, phase in phases.items():
            angle = turn + 2.0 * math.pi * phase
            rotations[f"thigh_{leg}"] = axis_rotation(x_axis, swing * math.sin(angle))
            # The paw lifts while the leg swings forward.
            rotations[f"shin_{leg}"] = axis_rotation(x_axis, bend * max(0.0, -math.cos(angle)))
        if walk:
            hips[1] += 0.02 * math.sin(2.0 * turn)
            rotations["neck"] = axis_rotation(x_axis, 0.05 * math.sin(2.0 * turn))
            rotations["tail"] = axis_rotation(y_axis, 0.15 * math.sin(turn))
        else:
            hips[1] += 0.05 * math.sin(turn)
            rotations["chest"] = axis_rotation(x_axis, 0.12 * math.sin(turn))
            rotations["neck"] = axis_rotation(x_axis, 0.25 + 0.1 * math.sin(turn + 0.5))
            rotations["tail"] = axis_rotation(x_axis, -0.5 + 0.15 * math.sin(turn))
    return rotations, tuple(hips)


def write_creature(path):
    """The creature (`creature.gltf` with `creature.bin`): one skinned mesh, its skin and the looping clips
    "idle", "walk" and "run". Every channel of a clip has the same keys, the last equal to the first."""
    model = creature_model_positions()
    positions, normals, joints, weights, indices = [], [], [], [], []
    for joint, low, high in CREATURE_BOXES:
        origin = model[joint]
        box_positions, box_normals, box_indices = creature_box(add(origin, low), add(origin, high))
        base = len(positions)
        positions += box_positions
        normals += box_normals
        joints += [(joint, 0, 0, 0)] * len(box_positions)
        weights += [(1.0, 0.0, 0.0, 0.0)] * len(box_positions)
        indices += [base + i for i in box_indices]

    blob = bytearray()
    views, accessors = [], []

    def add_accessor(fmt, rows, component_type, kind, minmax=False):
        while len(blob) % 4:
            blob.append(0)
        offset = len(blob)
        for row in rows:
            blob.extend(struct.pack("<" + fmt, *(row if isinstance(row, tuple) else (row,))))
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(blob) - offset})
        accessor = {"bufferView": len(views) - 1, "componentType": component_type, "count": len(rows), "type": kind}
        if minmax:
            columns = list(zip(*[row if isinstance(row, tuple) else (row,) for row in rows]))
            accessor["min"] = [min(c) for c in columns]
            accessor["max"] = [max(c) for c in columns]
        accessors.append(accessor)
        return len(accessors) - 1

    attributes = {"POSITION": add_accessor("3f", positions, 5126, "VEC3", minmax=True),
                  "NORMAL": add_accessor("3f", normals, 5126, "VEC3"),
                  "JOINTS_0": add_accessor("4B", joints, 5121, "VEC4"),
                  "WEIGHTS_0": add_accessor("4f", weights, 5126, "VEC4")}
    index_accessor = add_accessor("H", indices, 5123, "SCALAR")
    inverse_binds = [(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1) for x, y, z in model]
    inverse_bind = add_accessor("16f", inverse_binds, 5126, "MAT4")

    names = [name for name, _, _ in CREATURE_JOINTS]
    animations = []
    for clip, (seconds, keys) in CREATURE_CLIPS.items():
        times = [seconds * k / keys for k in range(keys + 1)]
        poses = [creature_pose(clip, k / keys) for k in range(keys + 1)]
        time_accessor = add_accessor("f", times, 5126, "SCALAR", minmax=True)
        samplers, channels = [], []
        for joint in sorted({name for rotations, _ in poses for name in rotations}, key=names.index):
            output = add_accessor("4f", [rotations[joint] for rotations, _ in poses], 5126, "VEC4")
            samplers.append({"input": time_accessor, "output": output, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": names.index(joint), "path": "rotation"}})
        output = add_accessor("3f", [hips for _, hips in poses], 5126, "VEC3")
        samplers.append({"input": time_accessor, "output": output, "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": 0, "path": "translation"}})
        animations.append({"name": clip, "samplers": samplers, "channels": channels})

    nodes = []
    for joint, (name, _, offset) in enumerate(CREATURE_JOINTS):
        node = {"name": name, "translation": list(offset)}
        children = [child for child, (_, parent, _) in enumerate(CREATURE_JOINTS) if parent == joint]
        if children:
            node["children"] = children
        nodes.append(node)
    nodes.append({"name": "creature", "mesh": 0, "skin": 0})
    bin_path = path.with_suffix(".bin")
    document = {
        "asset": {"version": "2.0", "generator": "illusion techdemo make_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, len(nodes) - 1]}],
        "nodes": nodes,
        "meshes": [{"name": "creature", "primitives": [{"attributes": attributes, "indices": index_accessor}]}],
        "skins": [{"name": "creature", "joints": list(range(len(names))), "inverseBindMatrices": inverse_bind}],
        "animations": animations,
        "buffers": [{"byteLength": len(blob), "uri": bin_path.name}],
        "bufferViews": views,
        "accessors": accessors,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    bin_path.write_bytes(bytes(blob))
    path.write_text(json.dumps(document, separators=(",", ":")), encoding="utf-8")


# Speeds in m/s at which the graph plays only the walk and only the run; the module gives the creature these
# speeds when it wanders and when it chases.
CREATURE_WALK_SPEED = 1.4
CREATURE_RUN_SPEED = 4.2


def write_creature_graph(path):
    """The creature's animation graph: idle, walk and run blended by "speed", the NavAgent's speedParameter."""
    clip = "assets:/models/creature.gltf#animation/"
    graph = {
        "format": "illusion.animgraph",
        "version": 1,
        "parameters": [{"name": "speed", "type": "Float"}],
        "layers": [{"name": "base", "entry": "locomotion", "states": [{"name": "locomotion", "blend1d": {
            "parameter": "speed",
            "points": [{"value": 0, "clip": f"{clip}idle"}, {"value": CREATURE_WALK_SPEED, "clip": f"{clip}walk"},
                       {"value": CREATURE_RUN_SPEED, "clip": f"{clip}run"}]}}]}],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(graph, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_creature_tree(path):
    """The creature's behaviour tree, in order of priority: chase the target the perception chose and growl at
    it (an action the module answers); after losing it, search where it was last sensed and go home; else
    wander around "home" (the den, a starting value of the agent) with pauses."""
    tree = {
        "format": "illusion.behavior",
        "version": 1,
        "blackboard": [{"name": "target", "type": "Entity"}, {"name": "lastSeen", "type": "Vector3"},
                       {"name": "home", "type": "Vector3"}],
        "root": {"name": "creature", "selector": [
            {"name": "chase", "guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority",
                                        "child": {"sequence": [
                                            {"name": "run to the player", "moveTo": {"key": "target", "acceptance": 1.6}},
                                            {"name": "growl", "timeLimit": {"seconds": 1, "child": {"action": "growl"}}},
                                            {"name": "watch", "wait": {"seconds": 1.5}}]}}},
            {"name": "return", "guard": {"key": "lastSeen", "compare": "IsSet", "abort": "Self",
                                         "child": {"sequence": [
                                             {"name": "search", "moveTo": {"key": "lastSeen", "acceptance": 1}},
                                             {"name": "sniff", "wait": {"seconds": 2, "deviation": 0.5}},
                                             {"name": "go home", "moveTo": {"key": "home", "acceptance": 1.5}},
                                             {"setValue": {"key": "lastSeen", "value": None}}]}}},
            {"name": "wander", "sequence": [{"name": "roam", "wander": {"key": "home", "radius": 4}},
                                            {"name": "rest", "wait": {"seconds": 2.5, "deviation": 1.5}}]}]},
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(tree, indent=2) + "\n", encoding="utf-8", newline="\n")


# --- Main ---------------------------------------------------------------------------------------------------


def main():
    bark = {"name": "Bark", "roughness": 0.9}  # the scene's materials texture the logs
    write_gltf(ROOT / "models" / "terrain.gltf", [("Terrain",) + terrain_mesh() + (0,)], [{"name": "Ground"}])
    write_gltf(ROOT / "models" / "neon.gltf",
               [mesh_part("Ring", torus(1.6, 0.05), 0), mesh_part("Arch", torus(2.2, 0.06, 96, 10, math.pi), 0),
                mesh_part("Bar", bar(2.4, 0.045), 0)], [{"name": "Neon", "emissive": [1.0, 0.2, 0.9]}])
    write_gltf(ROOT / "models" / "log.gltf", [mesh_part("Log", log(1.3, 0.09), 0)], [bark])
    sprite(ROOT / "textures" / "flame.png", 64, flame)
    sprite(ROOT / "textures" / "smoke.png", 64, smoke)
    for name, samples in (("forest_wind", forest_wind()), ("crickets", crickets()), ("fire", fire())):
        write_wav(ROOT / "audio" / f"{name}.wav", samples)
    write_creature(ROOT / "models" / "creature.gltf")
    write_creature_graph(ROOT / "animations" / "creature.animgraph")
    write_creature_tree(ROOT / "ai" / "creature.behavior")


if __name__ == "__main__":
    main()
