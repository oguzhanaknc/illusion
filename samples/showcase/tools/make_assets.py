"""Writes the generated assets of the showcase project: a glTF model with two submeshes, the
sounds (music, wind, a lamp hum, a voice and a click) and a smoke sprite.

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


if __name__ == "__main__":
    write_model(ROOT / "models" / "blocks.gltf")
    for name, samples in (("music", music()), ("wind", wind()), ("hum", hum()), ("voice", voice()),
                          ("click", click())):
        write_wav(ROOT / "audio" / f"{name}.wav", samples)
    write_smoke(ROOT / "textures" / "smoke.png")
