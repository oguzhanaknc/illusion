"""Downloads the third-party assets of the tech demo: CC0 models and textures from Poly Haven
(https://polyhaven.com, licence: CC0 1.0, no attribution required).

They are not committed. This script fetches pinned files into `samples/techdemo/assets/external/` (git-ignored),
checks the size and SHA-256 of every file, makes the RGBA leaf textures the materials use by merging each
plant's colour and alpha maps, and writes lighter copies of the trees (thin_gltf()). Files already present
with the right hash are kept. Run it from anywhere, with no packages beyond the Python standard library:

    python samples/techdemo/tools/fetch_assets.py           # download and prepare
    python samples/techdemo/tools/fetch_assets.py --check   # only verify what is there
"""

import argparse
import hashlib
import json
import math
import os
import struct
import sys
import tempfile
import urllib.request
import zlib
from array import array
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
    ("fern_02/textures/fern_02_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/fern_02/fern_02_diff_1k.png", 1986120, "f8f504bce117647f1df386cd7bc99a79c664bb49f82d1dd6412c83a700c48e94"),
    ("fern_02/textures/fern_02_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/fern_02/fern_02_alpha_1k.png", 114589, "ea3c64284c7ebdeedf484d2d816eae92825ec526140c3bb83859ddc7a64001f9"),
    ("shrub_01/textures/shrub_01_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/shrub_01/shrub_01_diff_1k.png", 926039, "4f4a46a0f96f8071e91692f38dd292446fdcc9b896b955d92298e1f7529f5484"),
    ("shrub_01/textures/shrub_01_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/shrub_01/shrub_01_alpha_1k.png", 132597, "eb693835205913dfc705a08fb45f3e8d1c338332bd0097e9837773765b421603"),
    ("grass_medium_01/textures/grass_medium_01_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/grass_medium_01/grass_medium_01_diff_1k.png", 2319925, "420dfd0fe7392244360b6f9ae1ada8893512941f73091d94718c8409e8658106"),
    ("fir_tree_01/fir_tree_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/fir_tree_01/fir_tree_01.bin", 478462204, "ba2ef15a1061a84c73b0a74643e88c118f801ce1664ca7a25a6a79bcc2c63790"),
    ("fir_tree_01/fir_tree_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/fir_tree_01/fir_tree_01_1k.gltf", 30279, "72e49eefb4ed4db6a8ca5e17fb10980f8463dd300d94589ac774cde95ade7709"),
    ("fir_tree_01/textures/fir_tree_01_bark_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/fir_tree_01_bark_arm_1k.jpg", 789264, "95d4e9e62cd309fbc38a175e2306379c76c062425b09c0fb72c5b7356a719123"),
    ("fir_tree_01/textures/fir_tree_01_bark_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/fir_tree_01_bark_diff_1k.jpg", 776567, "a5d25150ba1bee10008c039f98aa3c73b4477a404d6715c511ca9b5b0f4a9077"),
    ("fir_tree_01/textures/fir_tree_01_bark_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/fir_tree_01_bark_nor_gl_1k.jpg", 1137533, "6a570ab53ab2249ecdfa93e7d3b9a8a2cb1a8ad391400e5a64c708914779b2c3"),
    ("fir_tree_01/textures/fir_tree_01_trunk_a_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/fir_tree_01_trunk_a_arm_1k.jpg", 788967, "2e439391e98f427207a7fc180eb262f4c1ab3ca0ef570785ea7352826bac342b"),
    ("fir_tree_01/textures/fir_tree_01_trunk_a_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/fir_tree_01_trunk_a_diff_1k.jpg", 789806, "cfb646cbc4b6cb2db410eb4342174ebe7812a4ae03520491e788722cf1867286"),
    ("fir_tree_01/textures/fir_tree_01_trunk_a_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/fir_tree_01_trunk_a_nor_gl_1k.jpg", 997355, "9d46c05be22de0dcc4af7e3703fde4c7397644b9faeff4914c0469a6bde99caf"),
    ("fir_tree_01/textures/fir_tree_01_trunk_b_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/fir_tree_01_trunk_b_arm_1k.jpg", 732269, "c9e0571b2067cad25a4553119204874e6a5ea3156043cacbd5171155d856cff4"),
    ("fir_tree_01/textures/fir_tree_01_trunk_b_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/fir_tree_01_trunk_b_diff_1k.jpg", 687662, "9d39263a9243a4b6cd669de5e3a504eae32d52f5ab48e471cad5f7e52e052508"),
    ("fir_tree_01/textures/fir_tree_01_trunk_b_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/fir_tree_01_trunk_b_nor_gl_1k.jpg", 915273, "5e498ee6fee29437196085620c978346ba6731edad8bfc2a2642868f8bf64efe"),
    ("fir_tree_01/textures/fir_tree_01_twig_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/fir_tree_01/fir_tree_01_twig_alpha_1k.png", 371026, "3f399629337e7ac3af27a01b08ffb9c0d60224a9dd374a1427761a5c2eea9d6e"),
    ("fir_tree_01/textures/fir_tree_01_twig_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/fir_tree_01_twig_arm_1k.jpg", 407309, "d66c81fc073ee7ea97a76b6da2399769f16944e50ef0e007f09c95619f597c79"),
    ("fir_tree_01/textures/fir_tree_01_twig_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/fir_tree_01_twig_diff_1k.jpg", 371741, "8c0833a10e4fd44498d848cf9e659fe250a3813984cbd679f42430eb98c61fb8"),
    ("fir_tree_01/textures/fir_tree_01_twig_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/fir_tree_01/fir_tree_01_twig_diff_1k.png", 3411459, "76f6d0c0044d76bac08fbbd3d2ac1c9b66bb046de7d0b1a22aa9b319308f3833"),
    ("fir_tree_01/textures/fir_tree_01_twig_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_tree_01/fir_tree_01_twig_nor_gl_1k.jpg", 472604, "d470dc39b1d02bbeba06feb0ad818680ef3c24623b4db597270301a5fb8c9369"),
    ("fir_sapling_medium/fir_sapling_medium.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/fir_sapling_medium/fir_sapling_medium.bin", 74159400, "e3f3fabcaa1bacfc8b1feca5e6500b13c63a0e45cd812e663d01d31b042d73b1"),
    ("fir_sapling_medium/fir_sapling_medium_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/fir_sapling_medium/fir_sapling_medium_1k.gltf", 17059, "2e80cd49d716c99bdead5a42aa6441e89443044d9a0edc31b56b5dbf87fc41de"),
    ("fir_sapling_medium/textures/fir_sapling_medium_branches_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_sapling_medium/fir_sapling_medium_branches_arm_1k.jpg", 789264, "95d4e9e62cd309fbc38a175e2306379c76c062425b09c0fb72c5b7356a719123"),
    ("fir_sapling_medium/textures/fir_sapling_medium_branches_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_sapling_medium/fir_sapling_medium_branches_diff_1k.jpg", 776567, "a5d25150ba1bee10008c039f98aa3c73b4477a404d6715c511ca9b5b0f4a9077"),
    ("fir_sapling_medium/textures/fir_sapling_medium_branches_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_sapling_medium/fir_sapling_medium_branches_nor_gl_1k.jpg", 1137533, "6a570ab53ab2249ecdfa93e7d3b9a8a2cb1a8ad391400e5a64c708914779b2c3"),
    ("fir_sapling_medium/textures/fir_sapling_medium_twigs_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/fir_sapling_medium/fir_sapling_medium_twigs_alpha_1k.png", 371026, "3f399629337e7ac3af27a01b08ffb9c0d60224a9dd374a1427761a5c2eea9d6e"),
    ("fir_sapling_medium/textures/fir_sapling_medium_twigs_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_sapling_medium/fir_sapling_medium_twigs_arm_1k.jpg", 407309, "d66c81fc073ee7ea97a76b6da2399769f16944e50ef0e007f09c95619f597c79"),
    ("fir_sapling_medium/textures/fir_sapling_medium_twigs_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_sapling_medium/fir_sapling_medium_twigs_diff_1k.jpg", 297200, "db1113d37d68d960b84eb14ce67a4022d02bf10d6f0c078d689063f6296eaa1d"),
    ("fir_sapling_medium/textures/fir_sapling_medium_twigs_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/fir_sapling_medium/fir_sapling_medium_twigs_diff_1k.png", 1745129, "581df52e36fa3ea1d348df6b84e276fb3c88ee33cc182c2987eff61bf3daa513"),
    ("fir_sapling_medium/textures/fir_sapling_medium_twigs_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_sapling_medium/fir_sapling_medium_twigs_nor_gl_1k.jpg", 472604, "d470dc39b1d02bbeba06feb0ad818680ef3c24623b4db597270301a5fb8c9369"),
    ("fir_sapling/fir_sapling.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/fir_sapling/fir_sapling.bin", 21677180, "efdebfae6ee4b3ea20c7a85eb9f798ff4953259a3616bcff0b7e580b00f34f40"),
    ("fir_sapling/fir_sapling_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/fir_sapling/fir_sapling_1k.gltf", 10979, "a9466ca9d8c5b9b2eeeeafd3ce426f8912dcbd98a7281bad8c734833eb35b186"),
    ("fir_sapling/textures/fir_sapling_branches_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_sapling/fir_sapling_branches_arm_1k.jpg", 793567, "44c32b056b78c1046aeeb44c973a1d1352ea12d0d7ef2425527555f1256a0964"),
    ("fir_sapling/textures/fir_sapling_branches_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_sapling/fir_sapling_branches_diff_1k.jpg", 776633, "46d216526b6203dbd5dc3cfce5e49d7a4cc1770da2420faab45adf33671c7de4"),
    ("fir_sapling/textures/fir_sapling_branches_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_sapling/fir_sapling_branches_nor_gl_1k.jpg", 1137599, "dbe474f6cd593d51a19fb92a627ad900b9ab47dabd1bf60117a2cf9d110f79ea"),
    ("fir_sapling/textures/fir_sapling_twigs_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/fir_sapling/fir_sapling_twigs_alpha_1k.png", 107682, "91e8a6114e2f180de3d2fa60d397d69fee0d3c745a15e012da705c86cd4de976"),
    ("fir_sapling/textures/fir_sapling_twigs_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_sapling/fir_sapling_twigs_arm_1k.jpg", 300249, "d1ec27a55bb67bfe5fa2c8a41a545dc71769934b142db56ad7c703d4146e1770"),
    ("fir_sapling/textures/fir_sapling_twigs_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_sapling/fir_sapling_twigs_diff_1k.jpg", 252678, "a9900fafad850deeb5f79b928fd7590bca9e8ec8734d2368d4c64bba8b0c5b22"),
    ("fir_sapling/textures/fir_sapling_twigs_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/fir_sapling/fir_sapling_twigs_diff_1k.png", 1400815, "c29cb18884dc4cd24a72d0e39f01c1baeadb798d0fe3d196d82b8cb11b18f061"),
    ("fir_sapling/textures/fir_sapling_twigs_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/fir_sapling/fir_sapling_twigs_nor_gl_1k.jpg", 474717, "34928b2a9d1f722d2f64a7da468c790ecaca9b89c00ac0878a9e0bad26748368"),
    ("island_tree_01/island_tree_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/island_tree_01/island_tree_01.bin", 60709812, "1faa817b46e24ed6fc90d3ec72188a0dc5ac75e82c89b3dd3c5fdaecf7cc005c"),
    ("island_tree_01/island_tree_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/island_tree_01/island_tree_01_1k.gltf", 8576, "78e905a40670bfabb2760601d4d9a6c17193a50d68e6271c66893eb7bd75906d"),
    ("island_tree_01/textures/island_tree_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/island_tree_01/island_tree_01_arm_1k.jpg", 727403, "bf972dff0fbc71f146b4c02103fc3596477a49a9eff46b87b13c89b7b90d18ca"),
    ("island_tree_01/textures/island_tree_01_branches_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/island_tree_01/island_tree_01_branches_arm_1k.jpg", 484806, "9e3f82263247ce4f619692cea4c4f6fe9936ff465a86623481849062642c7d05"),
    ("island_tree_01/textures/island_tree_01_branches_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/island_tree_01/island_tree_01_branches_diff_1k.jpg", 528304, "bee5ec22196e9e31ad0c95a5a9b4636571cc0124d5bb27a92f1e307d289cc60d"),
    ("island_tree_01/textures/island_tree_01_branches_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/island_tree_01/island_tree_01_branches_nor_gl_1k.jpg", 819101, "2d67393ba76a0f49cf965fa0c99d8c16268e71e87b1203d35539bfdb971a514e"),
    ("island_tree_01/textures/island_tree_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/island_tree_01/island_tree_01_diff_1k.jpg", 791327, "51bff9a3a6ed7d89ff4c73f01708b7267ed8d7a82b0cb12343741894f708218f"),
    ("island_tree_01/textures/island_tree_01_leaves_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/island_tree_01/island_tree_01_leaves_alpha_1k.png", 57438, "f4341f147988dbe6b58d9ca841a8f0e4bcff25bc40d05f9877da82006d489c39"),
    ("island_tree_01/textures/island_tree_01_leaves_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/island_tree_01/island_tree_01_leaves_arm_1k.jpg", 489476, "90da3a072dbcb052715b1313b9dbc69a4aa8a09a31a4b3868411a2bb51d72037"),
    ("island_tree_01/textures/island_tree_01_leaves_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/island_tree_01/island_tree_01_leaves_diff_1k.jpg", 191886, "f56d762a1507754752abf164cc12acbc1e87ce87fa609ac44b98a1b0037c89d8"),
    ("island_tree_01/textures/island_tree_01_leaves_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/island_tree_01/island_tree_01_leaves_diff_1k.png", 1638916, "63d3563a22a62151f338ceb4e992de7c31b62e8e3582e2762d5071f02d286349"),
    ("island_tree_01/textures/island_tree_01_leaves_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/island_tree_01/island_tree_01_leaves_nor_gl_1k.jpg", 379443, "07ba461c0c95da4e49a42981766bdb6191d4c3c4d815796c082266dcc3cecdb8"),
    ("island_tree_01/textures/island_tree_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/island_tree_01/island_tree_01_nor_gl_1k.jpg", 1207134, "ca22e428e1b1ebac47b2fc67ec355700c8779d6628d2fc6c3ebae9002988f63e"),
    ("tree_small_02/textures/tree_small_02_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_small_02/tree_small_02_arm_1k.jpg", 708242, "891c5a1bb124292984b01558e9f2b0302d27d2fa3ccf9dfc6c34f9ea4ca24e6c"),
    ("tree_small_02/textures/tree_small_02_branch_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_small_02/tree_small_02_branch_arm_1k.jpg", 375732, "8968b46d76c67da09ebbf4eed04b8123c9e0ddf5b55d123968b35b32aa76d27a"),
    ("tree_small_02/textures/tree_small_02_branch_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_small_02/tree_small_02_branch_diff_1k.jpg", 449977, "a71b768e330c0c6cca097e30314f6b4e842ef724a1370f6aab14b4434eec64cb"),
    ("tree_small_02/textures/tree_small_02_branch_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_small_02/tree_small_02_branch_nor_gl_1k.jpg", 581596, "a2efae9e7fede259601188cd5cb7e561fc502bd87e04d862a9532b2d8e647a62"),
    ("tree_small_02/textures/tree_small_02_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_small_02/tree_small_02_diff_1k.jpg", 863246, "4e0093f407b3a1120a36de49a48ebe5a465a9a9b77e361c7a3e66866e115e404"),
    ("tree_small_02/textures/tree_small_02_leaves_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/tree_small_02/tree_small_02_leaves_alpha_1k.png", 264711, "4f143e2dda8a6a572f427ac378b92255d21761e4611de0d26e21816c01aa533b"),
    ("tree_small_02/textures/tree_small_02_leaves_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_small_02/tree_small_02_leaves_arm_1k.jpg", 633698, "71f81374daf5a8f3e6eb2c694264a1769fdb0bea508178e4105618eecea83c3e"),
    ("tree_small_02/textures/tree_small_02_leaves_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_small_02/tree_small_02_leaves_diff_1k.jpg", 340003, "261e7d7a5ac7d2e16ea8823ff8510be8f3f615999724232994039ad2885e5c36"),
    ("tree_small_02/textures/tree_small_02_leaves_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/tree_small_02/tree_small_02_leaves_diff_1k.png", 2338424, "d3fc48522c76aa1f91076c6b86f302669822ebe4e2acabd67d42c4d57eaa5ba0"),
    ("tree_small_02/textures/tree_small_02_leaves_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_small_02/tree_small_02_leaves_nor_gl_1k.jpg", 681604, "8c72e04fb23e16ae1df79c01385a638473d1dd4dede3d51e83d70153c129f09b"),
    ("tree_small_02/textures/tree_small_02_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_small_02/tree_small_02_nor_gl_1k.jpg", 1228646, "9c83be074c950ea1b94c732c0c0291b53b9ba357f932d378bafe3ca3b4418871"),
    ("tree_small_02/tree_small_02.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/tree_small_02/tree_small_02.bin", 95102324, "8da6c3c389ad8748286d1b7488cd827f75ebdad9d8f59ef7dfb0916df5edc634"),
    ("tree_small_02/tree_small_02_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/tree_small_02/tree_small_02_1k.gltf", 9075, "3062a709614c05b80fdc1e56583998bc3cb1027b0d52f956c8ecfb882ff8ca6d"),
    ("grass_medium_02/grass_medium_02.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/4k/grass_medium_02/grass_medium_02.bin", 272048, "aa8493bc1544a89e08bf440c7ba361042bfe6ea48b1fefc7e4af2dfff42fdf4a"),
    ("grass_medium_02/grass_medium_02_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/grass_medium_02/grass_medium_02_1k.gltf", 8864, "7037f8d2dfc6aeec0bf72f4eeb96cfa3f6b3a5e6c6f0bc5de537c055b2fd5d04"),
    ("grass_medium_02/textures/grass_medium_02_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/grass_medium_02/grass_medium_02_alpha_1k.png", 110617, "15976877b3dbd22cd496a9549f1eb7d86710846a3631fb87eedbf046519e339f"),
    ("grass_medium_02/textures/grass_medium_02_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/grass_medium_02/grass_medium_02_arm_1k.jpg", 241181, "f0d57bfd95bff7710526b49d47cf2326ee3796e425639d53b983d1472480b57d"),
    ("grass_medium_02/textures/grass_medium_02_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/grass_medium_02/grass_medium_02_diff_1k.jpg", 161235, "91ae99005b449011aceb207a68a937258fd9bfd66ff367d344c50398b1df389c"),
    ("grass_medium_02/textures/grass_medium_02_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/grass_medium_02/grass_medium_02_diff_1k.png", 851217, "5895add2d440b2aabe02cc8b0971b1f64956cf61d89e9a595944a64a38b5521d"),
    ("grass_medium_02/textures/grass_medium_02_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/grass_medium_02/grass_medium_02_nor_gl_1k.jpg", 118146, "0b464550366ed72a6cad9c357e19ac4c83fc9adc87272771479138129853b300"),
    ("shrub_02/shrub_02.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/4k/shrub_02/shrub_02.bin", 758692, "5c1b79da1cfdbac90599542ccd1f75ba433a3a4d356b1ea03655adb39b0e2c96"),
    ("shrub_02/shrub_02_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/shrub_02/shrub_02_1k.gltf", 8083, "99de0a70fe88ad414615cc8d8e020617c8512e2abfe08b911b153a6fcc40d93e"),
    ("shrub_02/textures/shrub_02_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/shrub_02/shrub_02_alpha_1k.png", 91741, "5c2c31bac311051db77443e1780078403c2df0a832c40fe6ed7bcd982695cdf0"),
    ("shrub_02/textures/shrub_02_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/shrub_02/shrub_02_arm_1k.jpg", 313715, "7186bf604d5c02f873aef6de2346cc06b6fa48f47d054edbef36d2c3772c0890"),
    ("shrub_02/textures/shrub_02_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/shrub_02/shrub_02_diff_1k.jpg", 395120, "9956eff294034d70516d867e9668428a23d782feb3d4c34061dce5e44bacd5a8"),
    ("shrub_02/textures/shrub_02_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/shrub_02/shrub_02_diff_1k.png", 1737013, "d6f8bcbd221e9229bc8d831a0819717dd74d327cd7f66219de942b7d60d01876"),
    ("shrub_02/textures/shrub_02_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/shrub_02/shrub_02_nor_gl_1k.jpg", 490815, "54c24e433365d5410223b7ebd2b33042b80831f80976e10768114db5ef6911d7"),
    ("shrub_03/shrub_03.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/4k/shrub_03/shrub_03.bin", 238684, "bf4df21e6d2a2bc5f00b4c9142f776171fe7da00cf960001825748067af542b6"),
    ("shrub_03/shrub_03_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/shrub_03/shrub_03_1k.gltf", 7296, "b76d7c14bc027e834e82d3f984c32b5d060e62908aa90ff9420a637ca253a8ea"),
    ("shrub_03/textures/shrub_03_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/shrub_03/shrub_03_alpha_1k.png", 79069, "daef00278879a024fd05216aaa1376067a5f25f1b911d08d9bbdc50fda4a0d66"),
    ("shrub_03/textures/shrub_03_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/shrub_03/shrub_03_arm_1k.jpg", 396238, "3e2b16812a623037138eedace958210c5f3443a827bbe69663045bba5612f815"),
    ("shrub_03/textures/shrub_03_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/shrub_03/shrub_03_diff_1k.jpg", 481841, "3cadc7aba46d3aabbd7d6d2b1d04aa32e2a7da0bc87c2d38ef3cb1ee42cb03ec"),
    ("shrub_03/textures/shrub_03_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/shrub_03/shrub_03_diff_1k.png", 2727771, "e4bcfd373b67f4bfecea93f2b3f2b51139438b09425983a665b61c78b24dfd5e"),
    ("shrub_03/textures/shrub_03_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/shrub_03/shrub_03_nor_gl_1k.jpg", 504734, "ff856bacfce859b8a7555dd9a3b92fbd36b2e775d486be71df485afb4235d6c9"),
    ("shrub_04/shrub_04.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/4k/shrub_04/shrub_04.bin", 749084, "cb442c39682e54cc63f912aa78ab17a03566f947a61adbbbaaa6473166744f01"),
    ("shrub_04/shrub_04_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/shrub_04/shrub_04_1k.gltf", 2882, "782832cbc57e96686b78c6ef9df6234c25f564f78421c8ed4bd42c3631f5aeae"),
    ("shrub_04/textures/shrub_04_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/shrub_04/shrub_04_alpha_1k.png", 42906, "6ddd60dcdbdcf55d7258ae747866f68f5acc9d88a7735c9eb05b02d6dec527e3"),
    ("shrub_04/textures/shrub_04_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/shrub_04/shrub_04_arm_1k.jpg", 332957, "d1085df021928b29b0f38dde7e58e9a2e304c4e8d838e390b37cf57f8aa492e8"),
    ("shrub_04/textures/shrub_04_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/shrub_04/shrub_04_diff_1k.jpg", 373350, "0b0fa0a474753d13f3093858cea79d0de05da1e6a0c5d6e181cd351080980b6c"),
    ("shrub_04/textures/shrub_04_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/shrub_04/shrub_04_diff_1k.png", 1594051, "d098301278a46d618118c95da476f737ac57ada38f96d0a354a454cc96133de8"),
    ("shrub_04/textures/shrub_04_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/shrub_04/shrub_04_nor_gl_1k.jpg", 449241, "200d791e0474495bfc876ba1c289dade669c6a5b124647c55bc840a875f6b51c"),
    ("nettle_plant/nettle_plant.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/nettle_plant/nettle_plant.bin", 956560, "24810f9d80b5fafdd71e1599b0cc81afa3eb9b744cfa8bf388c5e9f30271ba46"),
    ("nettle_plant/nettle_plant_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/nettle_plant/nettle_plant_1k.gltf", 10450, "45baafa16feb0017e6a46712e2aa7afe7841fd00905075ead8316a5fc8314b0e"),
    ("nettle_plant/textures/nettle_plant_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/nettle_plant/nettle_plant_alpha_1k.png", 189288, "61d98e886a20e401f7eb6e2d64e8513b2409e54feb41e83ecbf3b10aaa035810"),
    ("nettle_plant/textures/nettle_plant_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/nettle_plant/nettle_plant_arm_1k.jpg", 467172, "8fb33e5e939b4e089f08c867bf449d9e27aa32af94990c3cad1a89dcabd44f14"),
    ("nettle_plant/textures/nettle_plant_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/nettle_plant/nettle_plant_diff_1k.jpg", 388629, "7172a9971818a18a7cb7ebaa144d9d2313b83d2a6180c9582000f7260542ea7b"),
    ("nettle_plant/textures/nettle_plant_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/nettle_plant/nettle_plant_diff_1k.png", 2453311, "bd67027f682398bdf91c59d72194fa40b63d886e943977b63f3a56383e57fec1"),
    ("nettle_plant/textures/nettle_plant_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/nettle_plant/nettle_plant_nor_gl_1k.jpg", 654754, "ab5d0cdc9a487a8874b6e9a11bf67ef90319f02bbd74579576198aca90b0fa85"),
    ("celandine_01/celandine_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/celandine_01/celandine_01.bin", 325768, "7d2c6d2cc44da3a0c08e0fea725af42becdcbb06635d968f42892a1a3dfae7d6"),
    ("celandine_01/celandine_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/celandine_01/celandine_01_1k.gltf", 8770, "ae54536f79b426e69c33449b75a89b86fb127f91342445c26cc4142c5f4220d5"),
    ("celandine_01/textures/celandine_01_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/celandine_01/celandine_01_alpha_1k.png", 296498, "60c2b1387a71376b118de49e6354fbb82762a6ada0c01e5a2205b1502a1bc76c"),
    ("celandine_01/textures/celandine_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/celandine_01/celandine_01_arm_1k.jpg", 382047, "c04ac3ed56abfbb5193d59016c5b680ca3e53130518084e69c805f2edc1ad6e7"),
    ("celandine_01/textures/celandine_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/celandine_01/celandine_01_diff_1k.jpg", 398468, "d21558d8d101fd6601d620018f9efd2878b559666099aac3ab05df17da833b5f"),
    ("celandine_01/textures/celandine_01_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/celandine_01/celandine_01_diff_1k.png", 2406034, "3dcb40404be4c078199f21c2f12062f8951db4a3b13bf112938b3952f4a4daec"),
    ("celandine_01/textures/celandine_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/celandine_01/celandine_01_nor_gl_1k.jpg", 410515, "060fc4533ce82b9f9c9e86362920ba1a1934ba5a670bdd77aa803622ceec34ff"),
    ("dandelion_01/dandelion_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/dandelion_01/dandelion_01.bin", 1676648, "ccff5a564f56a69374c09c25ae86c3684c46329309b4eef4569255b1ad54bdd0"),
    ("dandelion_01/dandelion_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/dandelion_01/dandelion_01_1k.gltf", 8915, "68f804fe29f641f37b03c470cf9065d34aacd1b4f580ed6f35385626c2bb42b7"),
    ("dandelion_01/textures/dandelion_01_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/dandelion_01/dandelion_01_alpha_1k.png", 130962, "8fa462009b79d58741ba058418cd0ac7e9ec61b049b601f1351179204256c8eb"),
    ("dandelion_01/textures/dandelion_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/dandelion_01/dandelion_01_arm_1k.jpg", 448489, "dad70c20756581fd795548f52a23580d94a2af538664dbbfe64e66a76e4eebb6"),
    ("dandelion_01/textures/dandelion_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/dandelion_01/dandelion_01_diff_1k.jpg", 470035, "17e49d535dfce890e539038a80eaa77d90dab5f1781350266dd8f736ee7370d6"),
    ("dandelion_01/textures/dandelion_01_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/dandelion_01/dandelion_01_diff_1k.png", 2802892, "027a05c39dc1cd39db5511f5dc591b6ea27b6aa07a1a591d9f98d1b806a8539f"),
    ("dandelion_01/textures/dandelion_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/dandelion_01/dandelion_01_nor_gl_1k.jpg", 495857, "fd69513a4803145f634b5ace49fed8709e454b3026d060687071757a72c33250"),
    ("periwinkle_plant/periwinkle_plant.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/4k/periwinkle_plant/periwinkle_plant.bin", 1859752, "f039855d84d59f764838285c2b041a5cf255d103afc856e219a62482826d9655"),
    ("periwinkle_plant/periwinkle_plant_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/periwinkle_plant/periwinkle_plant_1k.gltf", 15189, "df781eea76123fc6eee97e6e8d466743e3a5fb08e1a2c3f5025a57e090d2cc55"),
    ("periwinkle_plant/textures/periwinkle_plant_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/periwinkle_plant/periwinkle_plant_arm_1k.jpg", 281673, "d964e2e1118c2ca88c137b45590f48d83d3471fef48e913cbd57bf7122bbe4a3"),
    ("periwinkle_plant/textures/periwinkle_plant_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/periwinkle_plant/periwinkle_plant_diff_1k.jpg", 349879, "32d61084e6c07fa3ca8e03b35b347a12717f971efc90e8fe1185931ee2298c56"),
    ("periwinkle_plant/textures/periwinkle_plant_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/periwinkle_plant/periwinkle_plant_diff_1k.png", 2407756, "57a211cafe1898a48a13bcbf509e19d3a690466554eeab9df98a2f16a9bbb70a"),
    ("periwinkle_plant/textures/periwinkle_plant_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/periwinkle_plant/periwinkle_plant_nor_gl_1k.jpg", 418428, "36d2464e7500f741500228b955c1fb0f26d954e869bd26f93dd25c0fafc5087d"),
    ("periwinkle_plant/textures/periwinkle_plant_opacity_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/periwinkle_plant/periwinkle_plant_opacity_1k.png", 86094, "e1dc2746b641606ae2772667c719e2b593b548759a51d86ce093322c22e903de"),
    ("weed_plant_02/textures/weed_plant_02_alpha_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/weed_plant_02/weed_plant_02_alpha_1k.png", 182453, "ceb47162756cba3d8306ce8aa7097f81786c9cd98785b7da5b191806a88ae44e"),
    ("weed_plant_02/textures/weed_plant_02_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/weed_plant_02/weed_plant_02_arm_1k.jpg", 571490, "82dd00b135a605aecf09d46bdaa6e71cdfc221d7b53e20e6f446da6e9e5bf942"),
    ("weed_plant_02/textures/weed_plant_02_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/weed_plant_02/weed_plant_02_diff_1k.jpg", 457595, "6bcbf7f0b8eaf5156b9021f998dd9233ba4b095aa590f3621b94f47550e21eb9"),
    ("weed_plant_02/textures/weed_plant_02_diff_1k.png", "https://dl.polyhaven.org/file/ph-assets/Models/png/1k/weed_plant_02/weed_plant_02_diff_1k.png", 3776137, "ce765bd697991c4681a7c9e3a4201f9093ee02dfd99179118e6962e76beb134f"),
    ("weed_plant_02/textures/weed_plant_02_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/weed_plant_02/weed_plant_02_nor_gl_1k.jpg", 679362, "53e277362656eaea87a59bc7c5b551e73d2cdb5b852b21d0ffb84becfc7b941d"),
    ("weed_plant_02/weed_plant_02.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/weed_plant_02/weed_plant_02.bin", 330884, "2b77b91a881055365a56156e8a131277187f72b82c1d55806bb5b0a5053e5e9d"),
    ("weed_plant_02/weed_plant_02_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/weed_plant_02/weed_plant_02_1k.gltf", 9128, "20fe9aa14ec8160de54d8027422ce6db93bcc6211299509067ee3edbceff218b"),
    ("pine_roots/pine_roots.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/pine_roots/pine_roots.bin", 3753376, "d9839cb9e62922c737ba3a564356a1956d17e8f0a673c83685320236a4fdbe98"),
    ("pine_roots/pine_roots_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/pine_roots/pine_roots_1k.gltf", 5161, "3f25ab982be6dee9328e012fcff9b82f21284fe8af8abdaafa6f316c819297aa"),
    ("pine_roots/textures/pine_roots_a_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/pine_roots/pine_roots_a_arm_1k.jpg", 883065, "68b28af436b6fd20a77a13b5b04de8ade25133b2ae2709c7a96dad38553ff20a"),
    ("pine_roots/textures/pine_roots_a_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/pine_roots/pine_roots_a_diff_1k.jpg", 770614, "0643bbc16321929246a6bdc40000795a54c2b40c2327afe4915fe602b49f15c7"),
    ("pine_roots/textures/pine_roots_a_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/pine_roots/pine_roots_a_nor_gl_1k.jpg", 1345180, "c42d77a9eafd6492cdeb4ea1ce8847f03ffaef0e1d65a6b2f362717c90c5d429"),
    ("pine_roots/textures/pine_roots_b_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/pine_roots/pine_roots_b_arm_1k.jpg", 869488, "9a3b614f71cebb75306e8a5c2bff1e3852e882c272e7067caf244985c8c7d8bd"),
    ("pine_roots/textures/pine_roots_b_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/pine_roots/pine_roots_b_diff_1k.jpg", 751988, "86e369ce696deed54e6d1638d7fbab7558dc78e27e6c97102c10a32a48bed569"),
    ("pine_roots/textures/pine_roots_b_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/pine_roots/pine_roots_b_nor_gl_1k.jpg", 1324029, "c948c41b50837a5a9030d4a3238b2db08b6fe740bc3f9df6164006a36d9a9bb6"),
    ("root_cluster_01/root_cluster_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/root_cluster_01/root_cluster_01.bin", 6464764, "8f91654c0b72edf978082e8edf97bf3f0cde72027a8a4d8d5e6c1b84231b5f93"),
    ("root_cluster_01/root_cluster_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/root_cluster_01/root_cluster_01_1k.gltf", 2827, "b9060cfe2c1db4326428a0bf834e8409a99988082c8e5465cb09738d72117463"),
    ("root_cluster_01/textures/root_cluster_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/root_cluster_01/root_cluster_01_arm_1k.jpg", 875276, "493dde9d58cd89366af63277148bcdfef50e3df9167ce965742eecd111cbd798"),
    ("root_cluster_01/textures/root_cluster_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/root_cluster_01/root_cluster_01_diff_1k.jpg", 769880, "3f4dbb0864982d228d37ee283125b8f8eb442b019fa5469249b332cb7b9d6d62"),
    ("root_cluster_01/textures/root_cluster_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/root_cluster_01/root_cluster_01_nor_gl_1k.jpg", 1340361, "4e16ca657a47112137cfbb2985e4473c1f2554631739826985fd61fc49c25e5b"),
    ("dry_branches_medium_01/dry_branches_medium_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/dry_branches_medium_01/dry_branches_medium_01.bin", 465172, "969da1e8cbb2d8201e7fa0fc0dac5b19ef354b32c3d8c62dd9faa7cb3833d8ca"),
    ("dry_branches_medium_01/dry_branches_medium_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/dry_branches_medium_01/dry_branches_medium_01_1k.gltf", 5844, "23f328ded9be1e151ccd7994b303c70221551544c4857a71474c1c01a14b8a22"),
    ("dry_branches_medium_01/textures/dry_branches_medium_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/dry_branches_medium_01/dry_branches_medium_01_arm_1k.jpg", 831180, "6d1ba6f4763d2815301a15bf62eaf68c345cecafb107e2681d12f26fbc657f5a"),
    ("dry_branches_medium_01/textures/dry_branches_medium_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/dry_branches_medium_01/dry_branches_medium_01_diff_1k.jpg", 778799, "1fbaf810a9ca29d2faf0a179834e8dce10cf4854bafe13f63af111030b3ec660"),
    ("dry_branches_medium_01/textures/dry_branches_medium_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/dry_branches_medium_01/dry_branches_medium_01_nor_gl_1k.jpg", 1170647, "62f5205bfec599000bb7a430e003bbada939bc8d7976a7d3ab7709fd5893413c"),
    ("bark_debris_01/bark_debris_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/bark_debris_01/bark_debris_01.bin", 4869268, "c22b98d6c42741cf9002b237c386d62e10fb90ce11210d974ae179bba9490a17"),
    ("bark_debris_01/bark_debris_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/bark_debris_01/bark_debris_01_1k.gltf", 7646, "478d222823c79a48c3eb58c2bbb91c81073dd28ce5f307b0d5a1467bd2e4e28b"),
    ("bark_debris_01/textures/bark_debris_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/bark_debris_01/bark_debris_01_arm_1k.jpg", 1006457, "14984c21e543b977837200faee33bdf4da8cbe1b7c0019b3f6254cf75dcd7c4c"),
    ("bark_debris_01/textures/bark_debris_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/bark_debris_01/bark_debris_01_diff_1k.jpg", 967420, "43cd69bf736a3847e09b5454684c0ec5acc20c1d1d5a96401a9bac5be98378bf"),
    ("bark_debris_01/textures/bark_debris_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/bark_debris_01/bark_debris_01_nor_gl_1k.jpg", 1216990, "ab9ba2ba5c99a1ac56055c3d2a91e986e597e86fc270ebb4ba6707f2fab2ccf6"),
    ("tree_stump_01/textures/tree_stump_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_stump_01/tree_stump_01_arm_1k.jpg", 825646, "d845cd6ab45288c546267d059ebe1f8a737e174330fe9375e5f99e753f2f95d8"),
    ("tree_stump_01/textures/tree_stump_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_stump_01/tree_stump_01_diff_1k.jpg", 907451, "781ebe420ac4d84f2e44b9e9937a596e3d5d90711d978a1392f8bf3771b5bf31"),
    ("tree_stump_01/textures/tree_stump_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_stump_01/tree_stump_01_nor_gl_1k.jpg", 1238404, "425117514b0386bb6df41cb64a61a624d2596472e05777483d4021b3285e3ceb"),
    ("tree_stump_01/tree_stump_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/tree_stump_01/tree_stump_01.bin", 965380, "b4d9ca70b3c908c4a638fbd6a23ed96c84055bbbf14cf44b89e31ad59fa21e0a"),
    ("tree_stump_01/tree_stump_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/tree_stump_01/tree_stump_01_1k.gltf", 2793, "9c66ba87d693f35eb404acaa96e0eeff831c295b61a0a903795584cbcc3527c9"),
    ("tree_stump_02/textures/tree_stump_02_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_stump_02/tree_stump_02_arm_1k.jpg", 796214, "d009168a1e043b9b980dd23a1699805d8888b8fe9cc79455f809a4e58d18b96b"),
    ("tree_stump_02/textures/tree_stump_02_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_stump_02/tree_stump_02_diff_1k.jpg", 901605, "318dbe92494887d137607144ef20bc170cbda1796dad15364ecf40683a79b8a2"),
    ("tree_stump_02/textures/tree_stump_02_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/tree_stump_02/tree_stump_02_nor_gl_1k.jpg", 1334839, "24c07761bbdfd43b5342690b4f8dd7fc0773f9e4b966dd310bb77fa5c5ede77a"),
    ("tree_stump_02/tree_stump_02.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/tree_stump_02/tree_stump_02.bin", 1433880, "db5e85d08beb889f0cc5be3b9bdfbcb1ae6df47e8e242af3c355766422f49d1d"),
    ("tree_stump_02/tree_stump_02_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/tree_stump_02/tree_stump_02_1k.gltf", 2802, "a79189160d1c7cfd6d875ab4470e4d2ff9cee08d58943be69d6028034a08c7ec"),
    ("dead_tree_trunk_02/dead_tree_trunk_02.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/dead_tree_trunk_02/dead_tree_trunk_02.bin", 1974352, "2b1b7ca07d98396ddc1987e367dcff9734df16b62e997c14837512d134687ded"),
    ("dead_tree_trunk_02/dead_tree_trunk_02_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/dead_tree_trunk_02/dead_tree_trunk_02_1k.gltf", 2849, "b486ab1f0c7f4972e0eec97b19de53b26a34d09f07da5f9c2a70f8621f9e574c"),
    ("dead_tree_trunk_02/textures/dead_tree_trunk_02_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/dead_tree_trunk_02/dead_tree_trunk_02_arm_1k.jpg", 805609, "ea15561ce36572ebd0db3c997f70d68a76d4bcbfef7efb4f91c8fd3b5f766e62"),
    ("dead_tree_trunk_02/textures/dead_tree_trunk_02_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/dead_tree_trunk_02/dead_tree_trunk_02_diff_1k.jpg", 932059, "fac23e81ab5639dcde93db605e5dd94c65e0aa2b1fb4a303f37de9afbb9af5b4"),
    ("dead_tree_trunk_02/textures/dead_tree_trunk_02_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/dead_tree_trunk_02/dead_tree_trunk_02_nor_gl_1k.jpg", 1192056, "9b26c29b89afced0ddb91dbe6f917e59038bed5ba6076dfceb8d1b4b00ca04f7"),
    ("rock_moss_set_02/rock_moss_set_02.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/rock_moss_set_02/rock_moss_set_02.bin", 1316572, "2df9d8eaaca4207529b0f613d4886d3962d0b9ffc0cdfd2bc4bf35dbcd391cb5"),
    ("rock_moss_set_02/rock_moss_set_02_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/rock_moss_set_02/rock_moss_set_02_1k.gltf", 11608, "c3b992a7622e94f7d8da97a47277a2a5177a513af0312a127ec908d4056c97db"),
    ("rock_moss_set_02/textures/rock_moss_set_02_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/rock_moss_set_02/rock_moss_set_02_diff_1k.jpg", 194307, "91dd2cc77d5198b067d77e5bdb3fa8ee7b8950aeff5aec2c1ccaddb76dcca481"),
    ("rock_moss_set_02/textures/rock_moss_set_02_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/rock_moss_set_02/rock_moss_set_02_nor_gl_1k.jpg", 273527, "dd2503789089fb72ef469efd14364d393e769c0de8781a251475cac7e0b6f51d"),
    ("rock_moss_set_02/textures/rock_moss_set_02_rough_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/rock_moss_set_02/rock_moss_set_02_rough_1k.jpg", 146226, "12252219a98a560940359973e83af831abdcfab0ead3f0a2a7b135a0143105f1"),
    ("rock_face_01/rock_face_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/rock_face_01/rock_face_01.bin", 476436, "873eac0319333e92bc96ddeedf8ebf69dc3c68ff1dfe32d8c14ddc165cdb20ac"),
    ("rock_face_01/rock_face_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/rock_face_01/rock_face_01_1k.gltf", 2793, "96c332c01d173a2431115f3bac29be101fba341e5500e2a08e384f4edd17299e"),
    ("rock_face_01/textures/rock_face_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/rock_face_01/rock_face_01_arm_1k.jpg", 693610, "59128c79d207a50a69a38002986135ade3b19642d26158cec141eca41578c9ca"),
    ("rock_face_01/textures/rock_face_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/rock_face_01/rock_face_01_diff_1k.jpg", 853749, "41bd26ed2db1332b1ebb4b1326fd8af4d21f0f0eb57b8ea385033115c722cfdb"),
    ("rock_face_01/textures/rock_face_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/rock_face_01/rock_face_01_nor_gl_1k.jpg", 1023400, "9a9d85b9a01f4e24fc9ceec8baf96c019f552bca69591ff544c7a725e28b1d97"),
    ("stone_01/stone_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/stone_01/stone_01.bin", 1213536, "ad037d15be3619be6aa3ddb144d450aea43b5efb66b670ba756272bf839418e3"),
    ("stone_01/stone_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/stone_01/stone_01_1k.gltf", 2750, "e06d59972ef910acde1cc64f9450ddb9c12dcd052031bc9982a7c46e108f6111"),
    ("stone_01/textures/stone_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/stone_01/stone_01_arm_1k.jpg", 846334, "1cb342823b2883e9885bf7bfe0ed12cd897d2f99a49d3c25b5e2c8f30f38263e"),
    ("stone_01/textures/stone_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/stone_01/stone_01_diff_1k.jpg", 649135, "c3171a6108bf1b3656b4a3576b329065d7acf6e337506a9e1a15511300c8fc88"),
    ("stone_01/textures/stone_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/stone_01/stone_01_nor_gl_1k.jpg", 1012665, "4d08a934f846913e96152260c824f7d5e642350ede76569ce82d6dc50c45f535"),
    ("wooden_lantern_01/textures/wooden_lantern_01_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/wooden_lantern_01/wooden_lantern_01_arm_1k.jpg", 842111, "6bbe63887f60d20445df75dcaef75653b86cb47fe9e139baab717de542cab429"),
    ("wooden_lantern_01/textures/wooden_lantern_01_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/wooden_lantern_01/wooden_lantern_01_diff_1k.jpg", 712250, "5a346e7e3335e9502eb4e25178d16ba29ae49786b75853d7bd36b386edf7a30d"),
    ("wooden_lantern_01/textures/wooden_lantern_01_glass_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/wooden_lantern_01/wooden_lantern_01_glass_arm_1k.jpg", 561615, "2d549dde55d65c690872f148581d81b3468100e8a0eb22847f31273e47d8f1fb"),
    ("wooden_lantern_01/textures/wooden_lantern_01_glass_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/wooden_lantern_01/wooden_lantern_01_glass_diff_1k.jpg", 410747, "1bfced8c9470ea01e73085439e8cee0c44c8ee8199b806a22cc4f2434ae2f151"),
    ("wooden_lantern_01/textures/wooden_lantern_01_glass_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/wooden_lantern_01/wooden_lantern_01_glass_nor_gl_1k.jpg", 216436, "0aaab86f76172aeb7ccfa5f5c7f047f616726e6b3ce6636e162caf633f173c0a"),
    ("wooden_lantern_01/textures/wooden_lantern_01_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/wooden_lantern_01/wooden_lantern_01_nor_gl_1k.jpg", 747945, "06a7d3bb7342b5aa96d8c22d46883153df8948b59cab26e19f0e91ba82dba9ef"),
    ("wooden_lantern_01/wooden_lantern_01.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/wooden_lantern_01/wooden_lantern_01.bin", 268136, "65c6b1a776c66d4170dab3657f0809b1a8a88219507589d6c9aad70ffbe687a3"),
    ("wooden_lantern_01/wooden_lantern_01_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/wooden_lantern_01/wooden_lantern_01_1k.gltf", 9540, "2380275a5c90bf3442c426f3363e826e6b1ceff12a7e65f83b03cc8fac70a099"),
    ("wooden_axe/textures/wooden_axe_arm_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/wooden_axe/wooden_axe_arm_1k.jpg", 698691, "32b74d486ac8246965f5ae1c168f589328baca306a6206a4fcb0f0f44a312a8e"),
    ("wooden_axe/textures/wooden_axe_diff_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/wooden_axe/wooden_axe_diff_1k.jpg", 694821, "941e5af6c2152ecc3e031d6044385d3949d758cf51e9dd65c61bead9b78f1d95"),
    ("wooden_axe/textures/wooden_axe_nor_gl_1k.jpg", "https://dl.polyhaven.org/file/ph-assets/Models/jpg/1k/wooden_axe/wooden_axe_nor_gl_1k.jpg", 542696, "003712db67956ee8a96557a7f5f997037fbe5fb9f5c1fe286c7197af04eeff7c"),
    ("wooden_axe/wooden_axe.bin", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/4k/wooden_axe/wooden_axe.bin", 91288, "f94ea2e160f43b6a50e7d0ca270faf55a3fa5cd6b6e424f6e56b8623da26f658"),
    ("wooden_axe/wooden_axe_1k.gltf", "https://dl.polyhaven.org/file/ph-assets/Models/gltf/1k/wooden_axe/wooden_axe_1k.gltf", 2744, "c1f0015f482a7ac0dd0378e28df5948d4281f951c89485015d392c8b7b1218ee"),
    ("forest_leaves_04/forest_leaves_04_diff_2k.jpg", "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/forest_leaves_04/forest_leaves_04_diff_2k.jpg", 1539210, "a4f964eb32909ef9b41dbf4000bde142995711b7b755850f6523eb4d99c4a5d4"),
    ("forest_leaves_04/forest_leaves_04_nor_gl_2k.jpg", "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/forest_leaves_04/forest_leaves_04_nor_gl_2k.jpg", 2187173, "4870de640c043377b2f992337477cab9ff26f915b6cd5302cf2cb41d94b0c339"),
    ("forest_leaves_04/forest_leaves_04_arm_2k.jpg", "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/forest_leaves_04/forest_leaves_04_arm_2k.jpg", 1292204, "f8880113d1d3ae2dbb984813bb0559bf8aba0490e68d6bcb85e412965ef056b9"),
]

# Leaf textures: the colour map with the alpha map as its alpha channel, written as 8-bit RGBA PNG.
LEAF_TEXTURES = [
    ("fern_02/textures/fern_02_diff_1k.png", "fern_02/textures/fern_02_alpha_1k.png",
     "fern_02/textures/fern_02_leaves_1k.png"),
    ("shrub_01/textures/shrub_01_diff_1k.png", "shrub_01/textures/shrub_01_alpha_1k.png",
     "shrub_01/textures/shrub_01_leaves_1k.png"),
    ("fir_tree_01/textures/fir_tree_01_twig_diff_1k.png", "fir_tree_01/textures/fir_tree_01_twig_alpha_1k.png",
     "fir_tree_01/textures/fir_tree_01_twig_leaves_1k.png"),
    ("fir_sapling_medium/textures/fir_sapling_medium_twigs_diff_1k.png", "fir_sapling_medium/textures/fir_sapling_medium_twigs_alpha_1k.png",
     "fir_sapling_medium/textures/fir_sapling_medium_twigs_leaves_1k.png"),
    ("fir_sapling/textures/fir_sapling_twigs_diff_1k.png", "fir_sapling/textures/fir_sapling_twigs_alpha_1k.png",
     "fir_sapling/textures/fir_sapling_twigs_leaves_1k.png"),
    ("island_tree_01/textures/island_tree_01_leaves_diff_1k.png", "island_tree_01/textures/island_tree_01_leaves_alpha_1k.png",
     "island_tree_01/textures/island_tree_01_leaves_leaves_1k.png"),
    ("tree_small_02/textures/tree_small_02_leaves_diff_1k.png", "tree_small_02/textures/tree_small_02_leaves_alpha_1k.png",
     "tree_small_02/textures/tree_small_02_leaves_leaves_1k.png"),
    ("grass_medium_02/textures/grass_medium_02_diff_1k.png", "grass_medium_02/textures/grass_medium_02_alpha_1k.png",
     "grass_medium_02/textures/grass_medium_02_leaves_1k.png"),
    ("shrub_02/textures/shrub_02_diff_1k.png", "shrub_02/textures/shrub_02_alpha_1k.png",
     "shrub_02/textures/shrub_02_leaves_1k.png"),
    ("shrub_03/textures/shrub_03_diff_1k.png", "shrub_03/textures/shrub_03_alpha_1k.png",
     "shrub_03/textures/shrub_03_leaves_1k.png"),
    ("shrub_04/textures/shrub_04_diff_1k.png", "shrub_04/textures/shrub_04_alpha_1k.png",
     "shrub_04/textures/shrub_04_leaves_1k.png"),
    ("nettle_plant/textures/nettle_plant_diff_1k.png", "nettle_plant/textures/nettle_plant_alpha_1k.png",
     "nettle_plant/textures/nettle_plant_leaves_1k.png"),
    ("celandine_01/textures/celandine_01_diff_1k.png", "celandine_01/textures/celandine_01_alpha_1k.png",
     "celandine_01/textures/celandine_01_leaves_1k.png"),
    ("dandelion_01/textures/dandelion_01_diff_1k.png", "dandelion_01/textures/dandelion_01_alpha_1k.png",
     "dandelion_01/textures/dandelion_01_leaves_1k.png"),
    ("periwinkle_plant/textures/periwinkle_plant_diff_1k.png", "periwinkle_plant/textures/periwinkle_plant_opacity_1k.png",
     "periwinkle_plant/textures/periwinkle_plant_leaves_1k.png"),
    ("weed_plant_02/textures/weed_plant_02_diff_1k.png", "weed_plant_02/textures/weed_plant_02_alpha_1k.png",
     "weed_plant_02/textures/weed_plant_02_leaves_1k.png"),
]


# Lighter copies of the trees, whose foliage is too dense for a forest of them (fir_tree_01_a has four
# million needle triangles): (source, copy, {(mesh, primitive): (fraction of the cards kept, card scale)}).
# See thin_gltf().
THINNED = [
    ("fir_tree_01/fir_tree_01_1k.gltf", "fir_tree_01/fir_tree_01_lite.gltf",
     {(0, 2): (0.08, 2.1), (1, 2): (0.14, 1.8), (2, 1): (0.6, 1.2)}),
    ("fir_sapling_medium/fir_sapling_medium_1k.gltf", "fir_sapling_medium/fir_sapling_medium_lite.gltf",
     {(0, 1): (0.25, 1.45), (1, 1): (0.35, 1.35), (2, 1): (0.35, 1.35)}),
    ("fir_sapling/fir_sapling_1k.gltf", "fir_sapling/fir_sapling_lite.gltf",
     {(0, 1): (0.3, 1.4), (1, 1): (0.3, 1.4), (2, 1): (0.3, 1.4)}),
    ("island_tree_01/island_tree_01_1k.gltf", "island_tree_01/island_tree_01_lite.gltf",
     {(0, 1): (0.2, 1.6), (0, 2): (0.35, 1.0)}),
    ("tree_small_02/tree_small_02_1k.gltf", "tree_small_02/tree_small_02_lite.gltf", {(0, 1): (0.12, 1.9)}),
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


COMPONENT_FORMATS = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
COMPONENT_COUNTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
# Attributes the engine reads (Gltf.cpp); the others (vertex colours, second UV sets) are dropped.
KEPT_ATTRIBUTES = ("POSITION", "NORMAL", "TANGENT", "TEXCOORD_0")


def read_accessor(document, buffers, index):
    """Returns the values of an accessor as a flat array plus its component count."""
    accessor = document["accessors"][index]
    view = document["bufferViews"][accessor["bufferView"]]
    code = COMPONENT_FORMATS[accessor["componentType"]]
    components = COMPONENT_COUNTS[accessor["type"]]
    data = buffers[view["buffer"]]
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    element = components * array(code).itemsize
    stride = view.get("byteStride") or element
    values = array(code)
    if stride == element:
        values.frombytes(data[start:start + accessor["count"] * element])
    else:
        for i in range(accessor["count"]):
            values.frombytes(data[start + i * stride:start + i * stride + element])
    return values, components


def vertex_runs(indices):
    """Splits a triangle list into runs that share no vertices with each other: a new run starts at a
    triangle whose vertices all come after every vertex used so far. Exporters write the cards of foliage
    (leaves, needle sprigs) one after another, so each run is a card or a small cluster of cards. Returns
    (first triangle, end triangle, first vertex, end vertex) per run."""
    runs = []
    triangles = len(indices) // 3
    start = 0
    first_vertex = 0
    top = -1
    for t in range(triangles):
        a, b, c = indices[3 * t], indices[3 * t + 1], indices[3 * t + 2]
        if t > start and min(a, b, c) > top:
            runs.append((start, t, first_vertex, top + 1))
            start = t
            first_vertex = top + 1
        top = max(top, a, b, c)
    if triangles:
        runs.append((start, triangles, first_vertex, top + 1))
    return runs


def texture_transform(document, primitive):
    """The KHR_texture_transform of the base colour texture of a primitive's material as a function of
    (u, v), or None. glTF applies translation * rotation * scale to the texture coordinates."""
    material = document.get("materials", [])[primitive["material"]] if "material" in primitive else {}
    info = material.get("pbrMetallicRoughness", {}).get("baseColorTexture", {})
    transform = info.get("extensions", {}).get("KHR_texture_transform")
    if not transform:
        return None
    offset = transform.get("offset", [0.0, 0.0])
    scale = transform.get("scale", [1.0, 1.0])
    cos, sin = math.cos(transform.get("rotation", 0.0)), math.sin(transform.get("rotation", 0.0))

    def apply(u, v):
        u, v = u * scale[0], v * scale[1]
        return cos * u + sin * v + offset[0], -sin * u + cos * v + offset[1]
    return apply


def strip_texture_transforms(value):
    """Removes KHR_texture_transform (baked) and second UV set references (the copy keeps TEXCOORD_0 only)
    from every texture reference under @p value."""
    if isinstance(value, dict):
        value.pop("texCoord", None)
        extensions = value.get("extensions")
        if isinstance(extensions, dict):
            extensions.pop("KHR_texture_transform", None)
            if not extensions:
                del value["extensions"]
        for child in value.values():
            strip_texture_transforms(child)
    elif isinstance(value, list):
        for child in value:
            strip_texture_transforms(child)


def keeps_run(index, fraction):
    """Deterministic, evenly spread choice of runs: a hash of the run index against the fraction kept."""
    return ((index * 2654435761) & 0xFFFFFFFF) < fraction * 4294967296.0


def thinning_stamp(source, rules):
    """What a thinned copy is made from, kept in its `asset.extras` to tell when it must be made again."""
    return {"source": Path(source).name,
            "rules": [[mesh, primitive, fraction, scale] for (mesh, primitive), (fraction, scale) in sorted(rules.items())]}


def is_thinned_copy_current(source, target, rules):
    if not target.is_file() or source.stat().st_mtime > target.stat().st_mtime:
        return False
    try:
        extras = json.loads(target.read_text(encoding="utf-8")).get("asset", {}).get("extras", {})
    except (OSError, ValueError):
        return False
    return extras.get("illusionThinned") == thinning_stamp(source, rules)


def thin_gltf(source, target, rules):
    """Writes a lighter copy of a glTF model: in the primitives named by @p rules ({(mesh, primitive):
    (fraction kept, card scale)}) only that fraction of the vertex-disjoint runs (foliage cards) is kept, each
    scaled about its centre to make up for the lost leaf area. Other primitives are copied. The copy has its
    own buffer (`<target stem>.bin`) and refers to the source's images, so it must sit in the same folder.
    Texture transforms (KHR_texture_transform, which the engine does not read) are baked into the UVs."""
    source, target = Path(source), Path(target)
    document = json.loads(source.read_text(encoding="utf-8"))
    buffers = [(source.parent / buffer["uri"]).read_bytes() for buffer in document["buffers"]]
    out = bytearray()
    views = []
    accessors = []

    def add(values, components, kind, component_type, target_kind=None, bounds=False):
        while len(out) % 4:
            out.append(0)
        view = {"buffer": 0, "byteOffset": len(out), "byteLength": len(values) * values.itemsize}
        if target_kind:
            view["target"] = target_kind
        out.extend(values.tobytes())
        views.append(view)
        accessor = {"bufferView": len(views) - 1, "componentType": component_type,
                    "count": len(values) // components, "type": kind}
        if bounds and len(values):
            accessor["min"] = [min(values[c::components]) for c in range(components)]
            accessor["max"] = [max(values[c::components]) for c in range(components)]
        accessors.append(accessor)
        return len(accessors) - 1

    for mesh_index, mesh in enumerate(document["meshes"]):
        for primitive_index, primitive in enumerate(mesh["primitives"]):
            attributes = {}
            for name in KEPT_ATTRIBUTES:
                if name in primitive["attributes"]:
                    attributes[name] = read_accessor(document, buffers, primitive["attributes"][name])
            indices = array("I", read_accessor(document, buffers, primitive["indices"])[0])
            transform = texture_transform(document, primitive)
            if transform and "TEXCOORD_0" in attributes:
                uvs = attributes["TEXCOORD_0"][0]
                baked = array("f")
                for i in range(0, len(uvs), 2):
                    baked.extend(transform(uvs[i], uvs[i + 1]))
                attributes["TEXCOORD_0"] = (baked, 2)
            rule = rules.get((mesh_index, primitive_index))
            if rule:
                fraction, scale = rule
                kept = {name: array(values.typecode) for name, (values, _) in attributes.items()}
                kept_indices = array("I")
                for run_index, (t0, t1, v0, v1) in enumerate(vertex_runs(indices)):
                    if not keeps_run(run_index, fraction):
                        continue
                    base = len(kept["POSITION"]) // 3
                    for name, (values, components) in attributes.items():
                        part = values[v0 * components:v1 * components]
                        if name == "POSITION":
                            count = v1 - v0
                            centre = [sum(part[c::3]) / count for c in range(3)]
                            part = array("f", (centre[i % 3] + (v - centre[i % 3]) * scale for i, v in enumerate(part)))
                        kept[name].extend(part)
                    kept_indices.extend(i - v0 + base for i in indices[3 * t0:3 * t1])
                attributes = {name: (kept[name], attributes[name][1]) for name in attributes}
                indices = kept_indices
            new_attributes = {}
            for name, (values, components) in attributes.items():
                accessor = document["accessors"][primitive["attributes"][name]]
                new_attributes[name] = add(values, components, accessor["type"], accessor["componentType"], 34962,
                                           bounds=name == "POSITION")
            primitive["attributes"] = new_attributes
            primitive["indices"] = add(indices, 1, "SCALAR", 5125, 34963)

    document["accessors"] = accessors
    document["bufferViews"] = views
    binary = target.with_suffix(".bin")
    document["buffers"] = [{"uri": binary.name, "byteLength": len(out)}]
    for key in ("animations", "skins"):
        document.pop(key, None)
    document.setdefault("asset", {"version": "2.0"}).setdefault("extras", {})["illusionThinned"] = thinning_stamp(source, rules)
    strip_texture_transforms(document.get("materials", []))
    for key in ("extensionsRequired", "extensionsUsed"):
        if key in document:
            document[key] = [name for name in document[key] if name != "KHR_texture_transform"]
            if not document[key]:
                del document[key]
    with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as temporary:
        temporary.write(out)
    os.replace(temporary.name, binary)
    with tempfile.NamedTemporaryFile("w", dir=target.parent, delete=False, encoding="utf-8") as temporary:
        json.dump(document, temporary, indent=1)
    os.replace(temporary.name, target)


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
        for source, copy, rules in THINNED:
            if not is_thinned_copy_current(ROOT / source, ROOT / copy, rules):
                print(f"missing or out of date: {copy}")
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
    for source, copy, rules in THINNED:
        target = ROOT / copy
        if not is_thinned_copy_current(ROOT / source, target, rules):
            print(f"making {copy}")
            thin_gltf(ROOT / source, target, rules)
    print(f"assets ready in {ROOT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
