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


if __name__ == "__main__":
    main()
