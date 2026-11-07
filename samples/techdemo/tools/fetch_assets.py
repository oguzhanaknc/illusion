"""Downloads the third-party assets of the tech demo: CC0 models and textures from Poly Haven
(https://polyhaven.com, licence: CC0 1.0, no attribution required).

They are not committed. This script fetches pinned files into `samples/techdemo/assets/external/` (git-ignored),
checks the size and SHA-256 of every file, and makes the RGBA leaf textures the materials use by merging
each plant's colour and alpha maps. Files already present with the right hash are kept. Run it from
anywhere, with no packages beyond the Python standard library:

    python samples/techdemo/tools/fetch_assets.py           # download and prepare
    python samples/techdemo/tools/fetch_assets.py --check   # only verify what is there
"""

import argparse
import hashlib
import os
import struct
import sys
import tempfile
import urllib.request
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "assets" / "external"
USER_AGENT = "illusion-fetch-assets/1.0"

# (path under assets/external, URL, size in bytes, SHA-256)
MANIFEST = [
    ("fern_02/fern_02_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/fern_02/fern_02_1k.gltf", 6899, "33974295ee19670af4877705fc7d91f33f42d6302f0d44a625e072271223031f"),
    ("fern_02/textures/fern_02_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fern_02/fern_02_arm_1k.jpg", 280359, "ebfb5d76ce9cbdaec20bcd22dce9b755f0b1b8650ef29ca13555e3e6961025d7"),
    ("fern_02/textures/fern_02_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fern_02/fern_02_diff_1k.jpg", 315705, "a828a6e7b4c01c826911e2eac4b3afc7457dab7b3b392a31c681ed88b7d50029"),
    ("fern_02/textures/fern_02_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fern_02/fern_02_nor_gl_1k.jpg", 367222, "45bce132c6e96a0864bc89ab5da51361ab3f4afa83a6669ae499ab5ca811f621"),
    ("fern_02/fern_02.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/fern_02/fern_02.bin", 176176, "fe1abb762bcbbc7ad9e86925c8080437a62add257d9280e9142fdc85c226e64c"),
    ("grass_medium_01/grass_medium_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/grass_medium_01/grass_medium_01_1k.gltf", 27110, "3e5e2a314c94e16de54fd7c28e41709731fbd2b1613ab539f1544f622ddb6a85"),
    ("grass_medium_01/textures/grass_medium_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/grass_medium_01/grass_medium_01_nor_gl_1k.jpg", 956166, "cae298d3d1d7ff7c7e210ad8ac772b96706e356f9315a6b04fde373ce01ccf1e"),
    ("grass_medium_01/textures/grass_medium_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/grass_medium_01/grass_medium_01_diff_1k.jpg", 395673, "71495bd6a11fddcab0aaff38246fea1a7b802a705483f1c7cbca53955daa8050"),
    ("grass_medium_01/textures/grass_medium_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/grass_medium_01/grass_medium_01_arm_1k.jpg", 679518, "9a3a30f72bc02f4398967c5b08f2d20cce30bd423a2613b42a727034296fa3ae"),
    ("grass_medium_01/grass_medium_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/grass_medium_01/grass_medium_01.bin", 1006632, "f4587cabdb96a2e96d8be644923dc954a248edfaaa717a0ff224cacb898bee1f"),
    ("shrub_01/shrub_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/shrub_01/shrub_01_1k.gltf", 2895, "c3bb3340687d46c684b9dab82f5b2f92b7b0fe761842423d3c9e19f59a839393"),
    ("shrub_01/textures/shrub_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/shrub_01/shrub_01_nor_gl_1k.jpg", 746497, "076870a39a4e21a364cf94566e6dd1852a29045fdf1f23393637235b19d1c2e8"),
    ("shrub_01/shrub_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/4k/shrub_01/shrub_01.bin", 5239664, "a8646f51386c84b12a6be8324dd0c2357c393c9108aac911847a203cf490b426"),
    ("shrub_01/textures/shrub_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/shrub_01/shrub_01_arm_1k.jpg", 386309, "8faecdc28d8f03ba0ac139be16e617e8bfd0778780ec1d7850812230378b89be"),
    ("shrub_01/textures/shrub_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/shrub_01/shrub_01_diff_1k.jpg", 502162, "fe60e639d694e1807c0a3ae97dd1d5a7e7b3f84e5fc32d0610b868ce48f36331"),
    ("rock_moss_set_01/rock_moss_set_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/rock_moss_set_01/rock_moss_set_01_1k.gltf", 9987, "c36675aca9c6b2be7785c2534cf413b03ec82a5a2a5407a2b1c0cdb0346c9727"),
    ("rock_moss_set_01/textures/rock_moss_set_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/rock_moss_set_01/rock_moss_set_01_nor_gl_1k.jpg", 209975, "eb4aad3f207f1dc5b624d74ae240372171626a66912bd71adf7bd3e518181d6e"),
    ("rock_moss_set_01/rock_moss_set_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/rock_moss_set_01/rock_moss_set_01.bin", 1466380, "de8360e6175879aaf02c171911069cf5f1852547f9ca7f10e440085dbcc3315e"),
    ("rock_moss_set_01/textures/rock_moss_set_01_rough_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/rock_moss_set_01/rock_moss_set_01_rough_1k.jpg", 82624, "4d6ec46623abd8e2cdb855f59c7ce31873ad61b36b60e4de579e9e776a497c6b"),
    ("rock_moss_set_01/textures/rock_moss_set_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/rock_moss_set_01/rock_moss_set_01_diff_1k.jpg", 168099, "40cea65d8aa4ee73a93b04af19963834d061eee9779c3fc2c1cba76eef812ccc"),
    ("boulder_01/boulder_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/boulder_01/boulder_01_1k.gltf", 2773, "51487934db898b4c24072780a451e34eca5c02e3ada24a62bbc600bbc4019e92"),
    ("boulder_01/textures/boulder_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/boulder_01/boulder_01_arm_1k.jpg", 659386, "c57187c5746b6be913fa53846cb44ff78743e7ca5000f131c9627b99563c2b87"),
    ("boulder_01/textures/boulder_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/boulder_01/boulder_01_diff_1k.jpg", 987467, "c38f0e918f6dc16b8d386ba3cdd89805c78320c119a1ceec85856f82b09a65b8"),
    ("boulder_01/boulder_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/boulder_01/boulder_01.bin", 2938808, "7f5b06503d62bd95bfe9c6b8a354a5cc8ff04a4d271a8cf87a3e3125db4232d7"),
    ("boulder_01/textures/boulder_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/boulder_01/boulder_01_nor_gl_1k.jpg", 1183552, "0810cf6e89d89bb6eb065b4d2e2855288f5dcbd2c15c6e40ab90d462021b50aa"),
    ("dead_tree_trunk/dead_tree_trunk_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/dead_tree_trunk/dead_tree_trunk_1k.gltf", 2812, "8baaf1ff66ba1f79acdceed0dc3797a882990a3abb10b18abe56762f34c26e39"),
    ("dead_tree_trunk/textures/dead_tree_trunk_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/dead_tree_trunk/dead_tree_trunk_arm_1k.jpg", 724384, "0e0f7d64ac4ee3a7e7861a48f4bbd1ec2c026facd8f48b756f6b5d4557af3fd3"),
    ("dead_tree_trunk/textures/dead_tree_trunk_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/dead_tree_trunk/dead_tree_trunk_nor_gl_1k.jpg", 1053075, "b7c393204c93f0c67535ff3b5d723b26598f9dbb2b5110b74249ab88189f026f"),
    ("dead_tree_trunk/textures/dead_tree_trunk_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/dead_tree_trunk/dead_tree_trunk_diff_1k.jpg", 713480, "bc31d7d23db90d90cfb568c583914eeec11b29716b79096818b706a9e3e0216c"),
    ("dead_tree_trunk/dead_tree_trunk.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/dead_tree_trunk/dead_tree_trunk.bin", 2293788, "c1ac6b3195246e8647a28eee2fbfdd4712a045c7cf2dcb4f5abcc2d40a446ca6"),
    ("Lantern_01/Lantern_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/Lantern_01/Lantern_01_1k.gltf", 4866, "d3f8d7a7afec96d55d3b7f20c3e329c909d19d37047902a826bc157f77c27819"),
    ("Lantern_01/textures/Lantern_01_brass_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/Lantern_01/Lantern_01_brass_arm_1k.jpg", 229054, "479e7105b170713373fdcca0c2f7dcce81913990f60cd4cfba1631f80f550191"),
    ("Lantern_01/Lantern_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/4k/Lantern_01/Lantern_01.bin", 864852, "cb0c7eaaea361546201ba1c4324581ac7147c221851657b6e23d2f854beabe49"),
    ("Lantern_01/textures/Lantern_01_brass_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/Lantern_01/Lantern_01_brass_nor_gl_1k.jpg", 180646, "95f89554c659712c7591a2a3d61178c9c0c0ef2c26e133005f24e2366a10f9b8"),
    ("Lantern_01/textures/Lantern_01_brass_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/Lantern_01/Lantern_01_brass_diff_1k.jpg", 140998, "d19d4c9417719a89ad824ce3a142f38f5e58c47b8640edca5eacca66918b608e"),
    ("forest_ground_04/forest_ground_04_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/forest_ground_04/forest_ground_04_diff_1k.jpg", 1113899, "50a4dda30875020c64385261aa7e00e2155b592302fa6794165f58fae73cbd2c"),
    ("forest_ground_04/forest_ground_04_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/forest_ground_04/forest_ground_04_nor_gl_1k.jpg", 1326812, "9c3a0200e02ce40edffbbf9f5f623ef08ea9b1b7812b556182ec978f88887151"),
    ("forest_ground_04/forest_ground_04_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/forest_ground_04/forest_ground_04_arm_1k.jpg", 882694, "1dea651a5626530a08544804a616847bfb47b97bb93ecb8c84f13c4f1d498d83"),
    ("fern_02/textures/fern_02_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/fern_02/fern_02_diff_1k.png", 1986120, "f8f504bce117647f1df386cd7bc99a79c664bb49f82d1dd6412c83a700c48e94"),
    ("fern_02/textures/fern_02_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/fern_02/fern_02_alpha_1k.png", 114589, "ea3c64284c7ebdeedf484d2d816eae92825ec526140c3bb83859ddc7a64001f9"),
    ("shrub_01/textures/shrub_01_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/shrub_01/shrub_01_diff_1k.png", 926039, "4f4a46a0f96f8071e91692f38dd292446fdcc9b896b955d92298e1f7529f5484"),
    ("shrub_01/textures/shrub_01_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/shrub_01/shrub_01_alpha_1k.png", 132597, "eb693835205913dfc705a08fb45f3e8d1c338332bd0097e9837773765b421603"),
    ("grass_medium_01/textures/grass_medium_01_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/grass_medium_01/grass_medium_01_diff_1k.png", 2319925, "420dfd0fe7392244360b6f9ae1ada8893512941f73091d94718c8409e8658106"),
]

# Leaf textures: the colour map with the alpha map as its alpha channel, written as 8-bit RGBA PNG.
LEAF_TEXTURES = [
    ("fern_02/textures/fern_02_diff_1k.png", "fern_02/textures/fern_02_alpha_1k.png",
     "fern_02/textures/fern_02_leaves_1k.png"),
    ("shrub_01/textures/shrub_01_diff_1k.png", "shrub_01/textures/shrub_01_alpha_1k.png",
     "shrub_01/textures/shrub_01_leaves_1k.png"),
]


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url, target, size, expected):
    target.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read()
    if len(data) != size or hashlib.sha256(data).hexdigest() != expected:
        raise RuntimeError(f"{url}: unexpected content ({len(data)} bytes); the pinned file changed upstream")
    # Write next to the target and rename, so an interrupted run leaves no half file behind.
    with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as temporary:
        temporary.write(data)
    os.replace(temporary.name, target)


def read_png(path):
    """Decodes a non-interlaced 8- or 16-bit grey, RGB or RGBA PNG to (width, height, channels, rows of
    8-bit values)."""
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    position = 8
    header = None
    compressed = bytearray()
    while position < len(data):
        (length,) = struct.unpack(">I", data[position:position + 4])
        kind = data[position + 4:position + 8]
        body = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            compressed += body
        elif kind == b"IEND":
            break
    width, height, depth, colour, _, _, interlace = header
    channels = {0: 1, 2: 3, 6: 4}.get(colour)
    if channels is None or depth not in (8, 16) or interlace != 0:
        raise ValueError(f"{path}: unsupported PNG layout")
    pixel = channels * depth // 8
    stride = width * pixel
    raw = zlib.decompress(bytes(compressed))
    rows = []
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        kind = raw[offset]
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        if kind == 1:
            for i in range(pixel, stride):
                line[i] = (line[i] + line[i - pixel]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                left = line[i - pixel] if i >= pixel else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                a = line[i - pixel] if i >= pixel else 0
                b = previous[i]
                c = previous[i - pixel] if i >= pixel else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + predictor) & 0xFF
        previous = line
        # 16-bit samples keep their high byte.
        rows.append(bytes(line) if depth == 8 else bytes(line[0::2]))
    return width, height, channels, rows


def write_rgba_png(path, width, height, rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rows)
    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    with tempfile.NamedTemporaryFile(dir=Path(path).parent, delete=False) as temporary:
        temporary.write(data)
    os.replace(temporary.name, path)


def merge_alpha(colour_path, alpha_path, target):
    width, height, channels, colour = read_png(colour_path)
    alpha_width, alpha_height, alpha_channels, alpha = read_png(alpha_path)
    if (width, height) != (alpha_width, alpha_height):
        raise ValueError(f"{colour_path} and {alpha_path} differ in size")
    rows = []
    for y in range(height):
        rgba = bytearray(width * 4)
        source = colour[y]
        rgba[0::4] = source[0::channels]
        rgba[1::4] = source[1::channels]
        rgba[2::4] = source[2::channels]
        rgba[3::4] = alpha[y][0::alpha_channels]
        rows.append(bytes(rgba))
    write_rgba_png(target, width, height, rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="only verify the files that are there")
    arguments = parser.parse_args()

    missing = 0
    for relative, url, size, expected in MANIFEST:
        target = ROOT / relative
        if target.is_file() and target.stat().st_size == size and sha256(target) == expected:
            continue
        if arguments.check:
            print(f"missing or changed: {relative}")
            missing += 1
            continue
        print(f"downloading {relative} ({size / 1e6:.1f} MB)")
        download(url, target, size, expected)
    if arguments.check:
        for _, _, leaves in LEAF_TEXTURES:
            if not (ROOT / leaves).is_file():
                print(f"missing: {leaves}")
                missing += 1
        print("all assets present" if missing == 0 else f"{missing} file(s) missing; run without --check")
        return 1 if missing else 0
    for colour, alpha, leaves in LEAF_TEXTURES:
        target = ROOT / leaves
        sources_newer = not target.is_file() or any(
            (ROOT / source).stat().st_mtime > target.stat().st_mtime for source in (colour, alpha))
        if sources_newer:
            print(f"making {leaves}")
            merge_alpha(ROOT / colour, ROOT / alpha, target)
    print(f"assets ready in {ROOT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
