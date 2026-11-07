"""Tests of the tech demo's asset tools; run by CI (Format job):

    python -m unittest discover -s samples/techdemo/tools -p "test_*.py"
"""

import json
import re
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import fetch_assets  # noqa: E402
import make_assets  # noqa: E402


def write_png(path, width, height, depth, colour, samples):
    """A PNG whose rows use every filter type in turn (the decoder must undo each)."""
    channels = {0: 1, 2: 3, 6: 4}[colour]
    size = depth // 8
    stride = width * channels * size
    raw = bytearray()
    previous = bytes(stride)
    for y in range(height):
        line = bytearray()
        for x in range(width):
            for c in range(channels):
                value = samples(x, y, c)
                line += value.to_bytes(size, "big")
        kind = y % 5
        encoded = bytearray(stride)
        pixel = channels * size
        for i in range(stride):
            a = line[i - pixel] if i >= pixel else 0
            b = previous[i]
            c = previous[i - pixel] if i >= pixel else 0
            if kind == 0:
                predictor = 0
            elif kind == 1:
                predictor = a
            elif kind == 2:
                predictor = b
            elif kind == 3:
                predictor = (a + b) >> 1
            else:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            encoded[i] = (line[i] - predictor) & 0xFF
        raw += bytes([kind]) + encoded
        previous = bytes(line)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth,
                                                                             colour, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


class FetchAssetsTest(unittest.TestCase):
    def test_every_file_is_pinned_once_with_a_size_and_sha256(self):
        paths = [entry[0] for entry in fetch_assets.MANIFEST]
        self.assertEqual(len(paths), len(set(paths)))
        for path, url, size, digest in fetch_assets.MANIFEST:
            self.assertTrue(url.startswith("https://dl.polyhaven.org/"), url)
            self.assertGreater(size, 0)
            self.assertRegex(digest, r"^[0-9a-f]{64}$")
            self.assertFalse(Path(path).is_absolute() or ".." in Path(path).parts, path)
        for colour, alpha, _ in fetch_assets.LEAF_TEXTURES:
            self.assertIn(colour, paths)
            self.assertIn(alpha, paths)

    def test_png_decoding_undoes_every_filter_at_8_and_16_bits(self):
        with tempfile.TemporaryDirectory() as directory:
            for depth in (8, 16):
                path = Path(directory) / f"rgb{depth}.png"
                maximum = (1 << depth) - 1
                write_png(path, 7, 6, depth, 2, lambda x, y, c: (x * 37 + y * 11 + c * 90) * 97 % (maximum + 1))
                width, height, channels, rows = fetch_assets.read_png(path)
                self.assertEqual((width, height, channels), (7, 6, 3))
                for y in range(height):
                    for x in range(width):
                        for c in range(3):
                            expected = (x * 37 + y * 11 + c * 90) * 97 % (maximum + 1)
                            self.assertEqual(rows[y][x * 3 + c], expected >> (depth - 8))

    def test_merging_puts_the_alpha_map_into_the_alpha_channel(self):
        with tempfile.TemporaryDirectory() as directory:
            colour = Path(directory) / "colour.png"
            alpha = Path(directory) / "alpha.png"
            target = Path(directory) / "leaves.png"
            write_png(colour, 4, 3, 8, 2, lambda x, y, c: [200, 120, 40][c])
            write_png(alpha, 4, 3, 16, 0, lambda x, y, c: 65535 if x < 2 else 0)
            fetch_assets.merge_alpha(colour, alpha, target)
            width, height, channels, rows = fetch_assets.read_png(target)
            self.assertEqual((width, height, channels), (4, 3, 4))
            self.assertEqual(rows[1][0:4], bytes([200, 120, 40, 255]))
            self.assertEqual(rows[1][12:16], bytes([200, 120, 40, 0]))


class MakeAssetsTest(unittest.TestCase):
    def test_the_clearing_is_flat_and_hills_rise_around_it(self):
        self.assertEqual(make_assets.terrain_height(0.0, 0.0), 0.0)
        self.assertLess(abs(make_assets.terrain_height(3.0, -4.0)), 0.05)
        self.assertGreater(make_assets.terrain_height(55.0, 0.0), 4.0)
        self.assertGreater(make_assets.terrain_height(0.0, -55.0), 4.0)

    def test_trees_are_deterministic_and_stand_on_their_origin(self):
        bark_a, leaves_a = make_assets.tree(101)
        bark_b, leaves_b = make_assets.tree(101)
        self.assertEqual(bark_a.positions, bark_b.positions)
        self.assertEqual(leaves_a.positions, leaves_b.positions)
        # The trunk's first ring tilts with it, a few centimetres into the ground.
        self.assertLess(abs(min(p[1] for p in bark_a.positions)), 0.05)
        self.assertGreater(max(p[1] for p in leaves_a.positions), 7.0)
        for mesh in (bark_a, leaves_a):
            count = len(mesh.positions)
            self.assertEqual(len(mesh.normals), count)
            self.assertEqual(len(mesh.tangents), count)
            self.assertEqual(len(mesh.uvs), count)
            self.assertTrue(all(0 <= i < count for i in mesh.indices))

    def test_written_gltf_files_reference_their_buffer_and_textures(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "log.gltf"
            material = {"name": "Bark", "colour": "textures/bark.png", "normal": "textures/bark_n.png"}
            make_assets.write_gltf(path, [make_assets.mesh_part("Log", make_assets.log(1.0, 0.1), 0)], [material])
            document = json.loads(path.read_text())
            self.assertEqual(document["buffers"][0]["uri"], "log.bin")
            self.assertEqual(document["buffers"][0]["byteLength"], (Path(directory) / "log.bin").stat().st_size)
            self.assertEqual([image["uri"] for image in document["images"]], ["textures/bark.png", "textures/bark_n.png"])
            attributes = document["meshes"][0]["primitives"][0]["attributes"]
            self.assertEqual(set(attributes), {"POSITION", "NORMAL", "TANGENT", "TEXCOORD_0"})
            for view in document["bufferViews"]:
                self.assertEqual(view["byteOffset"] % 4, 0)

    def test_sounds_loop_without_a_jump(self):
        samples = make_assets.crickets(1.0)
        self.assertEqual(len(samples), int(1.0 * make_assets.RATE) - int(0.3 * make_assets.RATE))
        self.assertTrue(all(abs(s) < 2.0 for s in samples))


if __name__ == "__main__":
    unittest.main()
