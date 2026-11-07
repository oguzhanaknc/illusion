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
import make_scene  # noqa: E402


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


def write_card_model(directory, cards, transform=None):
    """A glTF with one mesh of two primitives: `cards` unit quads one metre apart along x (vertex-disjoint),
    with a second UV set and the material `transform` (KHR_texture_transform), and a trunk triangle."""
    positions, uvs, indices = [], [], []
    for card in range(cards):
        base = len(positions) // 3
        for x, y in ((0, 0), (1, 0), (1, 1), (0, 1)):
            positions += [card + x, y, 0.0]
            uvs += [float(x), float(y)]
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    trunk_positions = [0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 5.0, 0.0]
    trunk_indices = [0, 1, 2]
    blobs = [struct.pack(f"<{len(positions)}f", *positions), struct.pack(f"<{len(uvs)}f", *uvs),
             struct.pack(f"<{len(uvs)}f", *uvs), struct.pack(f"<{len(indices)}I", *indices),
             struct.pack("<9f", *trunk_positions), struct.pack("<3I", *trunk_indices)]
    views, offset = [], 0
    for blob in blobs:
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(blob)})
        offset += len(blob)
    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": len(positions) // 3, "type": "VEC3",
         "min": [0, 0, 0], "max": [cards, 1, 0]},
        {"bufferView": 1, "componentType": 5126, "count": len(uvs) // 2, "type": "VEC2"},
        {"bufferView": 2, "componentType": 5126, "count": len(uvs) // 2, "type": "VEC2"},
        {"bufferView": 3, "componentType": 5125, "count": len(indices), "type": "SCALAR"},
        {"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [0.5, 5, 0]},
        {"bufferView": 5, "componentType": 5125, "count": 3, "type": "SCALAR"},
    ]
    texture = {"index": 0}
    if transform:
        texture["extensions"] = {"KHR_texture_transform": transform}
    document = {
        "asset": {"version": "2.0"}, "buffers": [{"uri": "card.bin", "byteLength": offset}], "bufferViews": views,
        "accessors": accessors, "images": [{"uri": "leaf.png"}], "textures": [{"source": 0}],
        "materials": [{"name": "Leaves", "pbrMetallicRoughness": {"baseColorTexture": texture},
                       "occlusionTexture": {"index": 0, "texCoord": 1}}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1, "TEXCOORD_1": 2}, "indices": 3,
                                    "material": 0},
                                   {"attributes": {"POSITION": 4}, "indices": 5}]}],
        "nodes": [{"mesh": 0}], "extensionsUsed": ["KHR_texture_transform"],
        "extensionsRequired": ["KHR_texture_transform"],
    }
    (Path(directory) / "card.bin").write_bytes(b"".join(blobs))
    (Path(directory) / "card.gltf").write_text(json.dumps(document))
    return Path(directory) / "card.gltf"


class ThinGltfTest(unittest.TestCase):
    def test_runs_split_triangles_that_share_no_vertices(self):
        # Two quads, then a triangle that reuses a vertex of the second quad (so it joins that run).
        indices = [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 7, 8, 9]
        self.assertEqual(fetch_assets.vertex_runs(indices), [(0, 2, 0, 4), (2, 5, 4, 10)])
        self.assertEqual(fetch_assets.vertex_runs([]), [])

    def test_the_kept_runs_are_deterministic_nested_and_close_to_the_fraction(self):
        for fraction in (0.05, 0.3, 0.8):
            kept = [i for i in range(20000) if fetch_assets.keeps_run(i, fraction)]
            self.assertAlmostEqual(len(kept) / 20000, fraction, delta=0.02)
            smaller = {i for i in range(20000) if fetch_assets.keeps_run(i, fraction / 2)}
            self.assertTrue(smaller <= set(kept))
        self.assertFalse(any(fetch_assets.keeps_run(i, 0.0) for i in range(1000)))
        self.assertTrue(all(fetch_assets.keeps_run(i, 1.0) for i in range(1000)))

    def test_thinning_keeps_a_fraction_of_the_cards_scaled_about_their_centres(self):
        with tempfile.TemporaryDirectory() as directory:
            source = write_card_model(directory, 200)
            target = Path(directory) / "card_lite.gltf"
            fetch_assets.thin_gltf(source, target, {(0, 0): (0.25, 2.0)})
            document = json.loads(target.read_text())
            self.assertEqual(document["buffers"], [{"uri": "card_lite.bin",
                                                    "byteLength": (Path(directory) / "card_lite.bin").stat().st_size}])
            buffers = [(Path(directory) / "card_lite.bin").read_bytes()]
            leaves, trunk = document["meshes"][0]["primitives"]
            self.assertEqual(set(leaves["attributes"]), {"POSITION", "TEXCOORD_0"})  # the second UV set is dropped
            positions, _ = fetch_assets.read_accessor(document, buffers, leaves["attributes"]["POSITION"])
            indices, _ = fetch_assets.read_accessor(document, buffers, leaves["indices"])
            expected = [i for i in range(200) if fetch_assets.keeps_run(i, 0.25)]
            self.assertEqual(len(positions) // 3, 4 * len(expected))
            self.assertEqual(len(indices), 6 * len(expected))
            self.assertTrue(all(0 <= i < len(positions) // 3 for i in indices))
            # The first kept card, quad [card, card + 1] x [0, 1], doubled about its centre.
            card = expected[0]
            self.assertEqual(list(positions[0:3]), [card - 0.5, -0.5, 0.0])
            self.assertEqual(list(positions[6:9]), [card + 1.5, 1.5, 0.0])
            accessor = document["accessors"][leaves["attributes"]["POSITION"]]
            self.assertEqual(accessor["min"][1], -0.5)
            self.assertEqual(accessor["max"][1], 1.5)
            # Primitives without a rule are copied whole.
            trunk_positions, _ = fetch_assets.read_accessor(document, buffers, trunk["attributes"]["POSITION"])
            self.assertEqual(list(trunk_positions), [0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 5.0, 0.0])

    def test_thinning_bakes_texture_transforms_and_drops_the_extension(self):
        with tempfile.TemporaryDirectory() as directory:
            source = write_card_model(directory, 3, {"offset": [0.5, 0.25], "scale": [2.0, 0.5]})
            target = Path(directory) / "card_lite.gltf"
            fetch_assets.thin_gltf(source, target, {})
            document = json.loads(target.read_text())
            self.assertNotIn("extensionsRequired", document)
            self.assertNotIn("extensionsUsed", document)
            material = document["materials"][0]
            self.assertEqual(material["pbrMetallicRoughness"]["baseColorTexture"], {"index": 0})
            self.assertEqual(material["occlusionTexture"], {"index": 0})
            buffers = [(Path(directory) / "card_lite.bin").read_bytes()]
            primitive = document["meshes"][0]["primitives"][0]
            uvs, components = fetch_assets.read_accessor(document, buffers, primitive["attributes"]["TEXCOORD_0"])
            self.assertEqual(components, 2)
            self.assertEqual(list(uvs[0:4]), [0.5, 0.25, 2.5, 0.25])  # (0, 0) and (1, 0) transformed

    def test_a_thinned_copy_is_out_of_date_when_its_rules_change(self):
        with tempfile.TemporaryDirectory() as directory:
            source = write_card_model(directory, 10)
            target = Path(directory) / "card_lite.gltf"
            self.assertFalse(fetch_assets.is_thinned_copy_current(source, target, {(0, 0): (0.5, 1.2)}))
            fetch_assets.thin_gltf(source, target, {(0, 0): (0.5, 1.2)})
            self.assertTrue(fetch_assets.is_thinned_copy_current(source, target, {(0, 0): (0.5, 1.2)}))
            self.assertFalse(fetch_assets.is_thinned_copy_current(source, target, {(0, 0): (0.4, 1.2)}))

    def test_every_thinned_model_is_downloaded_and_written_beside_it(self):
        paths = {entry[0] for entry in fetch_assets.MANIFEST}
        copies = [copy for _, copy, _ in fetch_assets.THINNED]
        self.assertEqual(len(copies), len(set(copies)))
        for source, copy, rules in fetch_assets.THINNED:
            self.assertIn(source, paths)
            self.assertNotIn(copy, paths)
            self.assertEqual(Path(source).parent, Path(copy).parent)
            for fraction, scale in rules.values():
                self.assertTrue(0.0 < fraction <= 1.0 and scale >= 1.0)


class MakeAssetsTest(unittest.TestCase):
    def test_the_clearing_is_flat_and_hills_rise_around_it(self):
        self.assertEqual(make_assets.terrain_height(0.0, 0.0), 0.0)
        self.assertLess(abs(make_assets.terrain_height(3.0, -4.0)), 0.05)
        self.assertGreater(make_assets.terrain_height(55.0, 0.0), 4.0)
        self.assertGreater(make_assets.terrain_height(0.0, -55.0), 4.0)

    def test_logs_are_deterministic_closed_index_lists_along_x(self):
        log = make_assets.log(1.3, 0.09)
        self.assertEqual(log.positions, make_assets.log(1.3, 0.09).positions)
        self.assertAlmostEqual(min(p[0] for p in log.positions), -0.65)
        self.assertAlmostEqual(max(p[0] for p in log.positions), 0.65)
        count = len(log.positions)
        for values in (log.normals, log.tangents, log.uvs):
            self.assertEqual(len(values), count)
        self.assertEqual(len(log.indices) % 3, 0)
        self.assertTrue(all(0 <= i < count for i in log.indices))

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


class MakeSceneTest(unittest.TestCase):
    def test_the_scene_has_unique_names_assets_that_exist_and_saves_once(self):
        make_scene.commands.clear()
        make_scene.build()
        names = [c["params"]["name"] for c in make_scene.commands if c["call"] == "entity.create"]
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual([c["call"] for c in make_scene.commands].count("scene.save_as"), 1)
        materials = {c["params"]["path"] for c in make_scene.commands if c["call"] == "material.create"}
        local = make_scene.PROJECT / "assets"
        external = ({entry[0] for entry in fetch_assets.MANIFEST} | {leaves for _, _, leaves in fetch_assets.LEAF_TEXTURES}
                    | {copy for _, copy, _ in fetch_assets.THINNED})
        for command in make_scene.commands:
            for value in re.findall(r'"(assets:/[^"#]+)', json.dumps(command)):
                if value in materials or value.endswith(".scene"):
                    continue
                relative = value[len("assets:/"):]
                if relative.startswith("external/"):
                    self.assertIn(relative[len("external/"):], external, value)
                else:
                    self.assertTrue((local / relative).is_file(), value)

    def test_parents_are_created_before_their_children(self):
        make_scene.commands.clear()
        make_scene.build()
        seen = set()
        for command in make_scene.commands:
            if command["call"] == "entity.create":
                parent = command["params"].get("parent")
                if parent:
                    self.assertIn(parent.lstrip("/"), seen)
                seen.add(command["params"]["name"])


if __name__ == "__main__":
    unittest.main()
