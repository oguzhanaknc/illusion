"""Writes the generated assets of the tech demo: the terrain, three trees, neon tubes, campfire logs,
their textures, particle sprites and looping ambient sounds.

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

    def card(self, centre, right, up, normal, size):
        """A square leaf card (both faces are drawn: the material is double-sided)."""
        tangent = normalize(right)
        corners = ((-1, -1, 0, 1), (1, -1, 1, 1), (1, 1, 1, 0), (-1, 1, 0, 0))
        first = len(self.positions)
        for cx, cy, u, v in corners:
            position = add(centre, add(scale(right, cx * size * 0.5), scale(up, cy * size * 0.5)))
            self.vertex(position, normal, tangent, (u, v))
        self.indices += [first, first + 1, first + 2, first, first + 2, first + 3]


# --- Trees --------------------------------------------------------------------------------------------------


def tree(seed):
    """A broad-leaved tree 9 to 11 m tall, origin at the base of its trunk: a bark mesh and a leaf mesh."""
    rng = random.Random(seed)
    bark = Mesh()
    leaves = Mesh()
    tips = []
    golden = math.pi * (3.0 - math.sqrt(5.0))
    azimuth = [rng.uniform(0.0, 2.0 * math.pi)]

    def branch(start, direction, length, radius, depth, segments):
        position = start
        v = 0.0
        for s in range(segments):
            # Branches wander a little and the trunk stays nearly straight.
            spread = 0.08 if depth == 0 else 0.22
            wander = (rng.uniform(-spread, spread), rng.uniform(0.0, 0.06), rng.uniform(-spread, spread))
            direction = normalize(add(direction, wander))
            end = add(position, scale(direction, length / segments))
            r0 = radius * (1.0 - 0.65 * s / segments)
            r1 = radius * (1.0 - 0.65 * (s + 1) / segments)
            v = bark.tube(position, end, r0, r1, 12 if depth == 0 else 7, v)
            position = end
            grows = (depth == 0 and s >= 2) or (depth == 1 and s >= 1)
            if grows:
                for _ in range(2 if depth == 0 else rng.randint(1, 2)):
                    azimuth[0] += golden  # spread evenly around the trunk
                    out = (math.cos(azimuth[0]), 0.0, math.sin(azimuth[0]))
                    side, up = basis(direction)
                    out = normalize(sub(out, scale(direction, dot(out, direction))))
                    tilt = rng.uniform(0.8, 1.15) if depth == 0 else rng.uniform(0.5, 0.9)
                    child = normalize(add(scale(direction, math.cos(tilt)), scale(out, math.sin(tilt))))
                    reach = rng.uniform(0.5, 0.65) * (1.0 - 0.12 * (s - 2)) if depth == 0 else rng.uniform(0.45, 0.6)
                    branch(position, child, length * reach, r1 * 0.7, depth + 1, 4 if depth == 0 else 3)
            if depth >= 1 and s >= 1:
                tips.append(position)
        tips.append(position)

    trunk = rng.uniform(6.5, 7.5)
    branch((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), trunk, rng.uniform(0.3, 0.36), 0, 6)

    # Leaf clusters around the tips; normals point away from the canopy's centre, so it shades like a ball.
    count = len(tips)
    canopy_centre = (sum(t[0] for t in tips) / count, sum(t[1] for t in tips) / count - 0.5,
                     sum(t[2] for t in tips) / count)
    for tip in tips:
        for _ in range(3):
            centre = add(tip, (rng.uniform(-0.7, 0.7), rng.uniform(-0.3, 0.6), rng.uniform(-0.7, 0.7)))
            normal = normalize(add(sub(centre, canopy_centre), (0.0, 1.0, 0.0)))
            yaw = rng.uniform(0.0, math.pi)
            right = (math.cos(yaw), 0.0, math.sin(yaw))
            up = normalize((rng.uniform(-0.5, 0.5), 1.0, rng.uniform(-0.5, 0.5)))
            up = normalize(sub(up, scale(right, dot(up, right))))
            size = rng.uniform(1.5, 2.2)
            leaves.card(centre, right, up, normal, size)
            leaves.card(centre, normalize(cross(up, right)), up, normal, size * 0.9)
    return bark, leaves


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
    """A log along +x centred on the origin."""
    mesh = Mesh()
    mesh.tube((-length / 2.0, 0.0, 0.0), (length / 2.0, 0.0, 0.0), radius, radius * 0.9, 10, 0.0, 2.0)
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


class TileNoise:
    """Value noise that repeats every `period` cells, for seamless textures."""

    def __init__(self, period, seed):
        rng = random.Random(seed)
        self.period = period
        self.values = [rng.random() for _ in range(period * period)]

    def __call__(self, x, y):
        p = self.period
        x0 = math.floor(x)
        y0 = math.floor(y)
        fx = x - x0
        fy = y - y0
        fx = fx * fx * (3 - 2 * fx)
        fy = fy * fy * (3 - 2 * fy)
        v = self.values
        a = v[(y0 % p) * p + x0 % p]
        b = v[(y0 % p) * p + (x0 + 1) % p]
        c = v[((y0 + 1) % p) * p + x0 % p]
        d = v[((y0 + 1) % p) * p + (x0 + 1) % p]
        return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy


def bark_textures(size=512):
    """Bark colour and normal maps: vertical ridges and cracks."""
    ridges = TileNoise(8, 11)
    detail = TileNoise(32, 12)
    fine = TileNoise(64, 13)
    height = [[0.0] * size for _ in range(size)]
    for y in range(size):
        for x in range(size):
            u = x / size
            v = y / size
            # Stretched along v: ridges run up the trunk.
            h = (0.6 * ridges(u * 8, v * 2) + 0.3 * detail(u * 32, v * 6) + 0.1 * fine(u * 64, v * 16))
            height[y][x] = h
    colour_rows, normal_rows = [], []
    for y in range(size):
        colour = bytearray()
        normal = bytearray()
        for x in range(size):
            h = height[y][x]
            crack = smoothstep(0.42, 0.3, h)
            tone = 0.55 + 0.45 * h
            r = (0.30 * tone) * (1 - 0.6 * crack)
            g = (0.22 * tone) * (1 - 0.6 * crack)
            b = (0.16 * tone) * (1 - 0.6 * crack)
            colour += bytes((int(255 * r ** (1 / 2.2)), int(255 * g ** (1 / 2.2)), int(255 * b ** (1 / 2.2))))
            dx = (height[y][(x + 1) % size] - height[y][(x - 1) % size]) * 6.0
            dy = (height[(y + 1) % size][x] - height[(y - 1) % size][x]) * 6.0
            n = normalize((-dx, dy, 1.0))  # OpenGL convention (green up), like the Poly Haven maps
            normal += bytes(int(127.5 + 127.5 * c) for c in n)
        colour_rows.append(colour)
        normal_rows.append(normal)
    write_png(ROOT / "models" / "textures" / "bark_diff.png", size, size, colour_rows, 3)
    write_png(ROOT / "models" / "textures" / "bark_nor_gl.png", size, size, normal_rows, 3)


def leaf_texture(size=512, seed=21):
    """A cluster of leaves on twigs with alpha, for the tree cards."""
    rng = random.Random(seed)
    pixels = [[None] * size for _ in range(size)]
    centre = (size / 2, size / 2)
    leaves = []
    for _ in range(70):
        angle = rng.uniform(0, 2 * math.pi)
        distance = rng.uniform(0.05, 0.36) * size
        base = (centre[0] + math.cos(angle) * distance * 0.5, centre[1] + math.sin(angle) * distance * 0.5)
        direction = angle + rng.uniform(-0.6, 0.6)
        leaves.append((base, direction, rng.uniform(0.14, 0.22) * size, rng.uniform(0.045, 0.07) * size,
                       rng.uniform(0.0, 1.0)))
    # Twigs from the centre to each leaf's base.
    for base, _, _, _, _ in leaves:
        steps = int(math.dist(centre, base))
        for s in range(steps):
            t = s / max(steps, 1)
            x = int(centre[0] + (base[0] - centre[0]) * t)
            y = int(centre[1] + (base[1] - centre[1]) * t)
            for ox in (-1, 0, 1):
                if 0 <= x + ox < size and 0 <= y < size:
                    pixels[y][x + ox] = (0.16, 0.11, 0.07)
    for base, direction, length, width, tint in leaves:
        cos_d = math.cos(direction)
        sin_d = math.sin(direction)
        reach = int(length + width) + 2
        for y in range(max(0, int(base[1]) - reach), min(size, int(base[1]) + reach)):
            for x in range(max(0, int(base[0]) - reach), min(size, int(base[0]) + reach)):
                px = x - base[0]
                py = y - base[1]
                u = (px * cos_d + py * sin_d) / length  # along the leaf, 0 to 1
                w = (-px * sin_d + py * cos_d) / width  # across, -1 to 1 at its widest
                if 0.0 <= u <= 1.0:
                    edge = math.sin(math.pi * u) ** 0.8
                    if abs(w) < edge:
                        vein = 1.0 - 0.35 * math.exp(-(w * 8) ** 2)
                        shade = (0.75 + 0.25 * u) * (0.8 + 0.2 * (1 - abs(w))) * vein
                        green = (0.10 + 0.08 * tint, 0.30 + 0.12 * tint, 0.06 + 0.05 * tint)
                        pixels[y][x] = tuple(c * shade for c in green)
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            p = pixels[y][x]
            if p is None:
                row += bytes((40, 60, 25, 0))  # a leafy colour under the cut-out keeps mips from going dark
            else:
                row += bytes(tuple(int(255 * min(c, 1.0) ** (1 / 2.2)) for c in p) + (255,))
        rows.append(row)
    write_png(ROOT / "models" / "textures" / "tree_leaves.png", size, size, rows, 4)


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


# --- Main ---------------------------------------------------------------------------------------------------


def main():
    bark = {"name": "Bark", "colour": "textures/bark_diff.png", "normal": "textures/bark_nor_gl.png",
            "roughness": 0.9}
    leaves = {"name": "Leaves", "colour": "textures/tree_leaves.png", "alphaMask": True, "roughness": 0.7}
    write_gltf(ROOT / "models" / "terrain.gltf", [("Terrain",) + terrain_mesh() + (0,)], [{"name": "Ground"}])
    for index, seed in enumerate((101, 202, 303)):
        trunk, foliage = tree(seed)
        write_gltf(ROOT / "models" / f"tree_{'abc'[index]}.gltf",
                   [mesh_part("Trunk", trunk, 0), mesh_part("Leaves", foliage, 1)], [bark, leaves])
    write_gltf(ROOT / "models" / "neon.gltf",
               [mesh_part("Ring", torus(1.6, 0.035), 0), mesh_part("Arch", torus(2.2, 0.04, 96, 10, math.pi), 0),
                mesh_part("Bar", bar(2.4, 0.03), 0)], [{"name": "Neon", "emissive": [1.0, 0.2, 0.9]}])
    write_gltf(ROOT / "models" / "log.gltf", [mesh_part("Log", log(1.3, 0.09), 0)], [bark])
    bark_textures()
    leaf_texture()
    sprite(ROOT / "textures" / "flame.png", 64, flame)
    sprite(ROOT / "textures" / "smoke.png", 64, smoke)
    for name, samples in (("forest_wind", forest_wind()), ("crickets", crickets()), ("fire", fire())):
        write_wav(ROOT / "audio" / f"{name}.wav", samples)


if __name__ == "__main__":
    main()
