"""Writes build.jsonl: the editor commands that build the tech demo's scene, assets:/scenes/forest.scene.

A fir forest around a clearing with a campfire, made of Poly Haven models (downloaded by fetch_assets.py) and
the procedural terrain, logs and neon of make_assets.py. Everything is scattered with a fixed seed and set on
the ground with terrain_height(), so the scene matches the terrain mesh. Replay the file against a headless
editor on this project (see the README) to build the scene:

    python samples/techdemo/tools/make_scene.py
"""

import json
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from make_assets import BOWL_RADIUS, CLEARING_RADIUS, TERRAIN_SIZE, terrain_height  # noqa: E402

PROJECT = Path(__file__).resolve().parent.parent
EXTERNAL = "assets:/external"
PLAYER_START = (0.0, 7.0)
FAR_FIRS = BOWL_RADIUS + 6.0  # metres from the centre
SHADOWED_FIRS = 28.0
SHADOWED_YOUNG_FIRS = 22.0
FAR_LOD_BIAS = 2.0  # MeshRenderer.lodBias of the trees beyond FAR_FIRS and SHADOWED_YOUNG_FIRS

commands = []


def call(method, params):
    commands.append({"call": method, "params": params})


def yaw_quaternion(angle):
    return [0.0, math.sin(angle / 2.0), 0.0, math.cos(angle / 2.0)]


def quaternion_multiply(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]


def axis_quaternion(axis, angle):
    s = math.sin(angle / 2.0)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle / 2.0)]


def tilted(rng, yaw, tilt):
    """A turn about y, then a lean of up to @p tilt radians in a random direction."""
    lean_axis = rng.uniform(0.0, 2.0 * math.pi)
    lean = axis_quaternion((math.cos(lean_axis), 0.0, math.sin(lean_axis)), rng.uniform(0.0, tilt))
    return quaternion_multiply(lean, yaw_quaternion(yaw))


def ground(x, z, sink=0.0):
    return [round(x, 3), round(terrain_height(x, z) - sink, 3), round(z, 3)]


def r3(values):
    return [round(v, 4) for v in values]


def material(name, values, textures=None):
    path = f"assets:/materials/{name.lower().replace(' ', '_')}.material"
    value = {"material": dict(values, name=name)}
    if textures:
        value["textures"] = textures
    call("material.create", {"path": path, "value": value, "overwrite": True})
    return path


def entity(name, components, parent=None):
    params = {"name": name, "components": components}
    if parent:
        params["parent"] = parent
    call("entity.create", params)


def textures(asset, stem=None, colour=None, arm=True):
    """The Poly Haven texture set `<asset>/textures/<stem>_{diff,nor_gl,arm}_1k.jpg`; @p colour replaces the
    colour map (an RGBA leaf texture made by fetch_assets.py)."""
    base = f"{EXTERNAL}/{asset}/textures/{stem or asset}"
    result = {"baseColor": colour or f"{base}_diff_1k.jpg", "normal": f"{base}_nor_gl_1k.jpg"}
    if arm:
        result["metallicRoughness"] = f"{base}_arm_1k.jpg"
        result["occlusion"] = f"{base}_arm_1k.jpg"
    return result


def leaves(asset, stem=None):
    """Textures of a plant whose colour map has its alpha map merged in by fetch_assets.py."""
    stem = stem or asset
    return textures(asset, stem, f"{EXTERNAL}/{asset}/textures/{stem}_leaves_1k.png")


def solid(sway=0.0, roughness=1.0):
    """Values of an opaque material textured with an ARM map (roughness and metalness come from the map)."""
    values = {"metallicFactor": 1.0, "roughnessFactor": roughness}
    if sway:
        values["windSway"] = sway
    return values


# Light through leaves turns yellow-green; needles are thicker and let less through.
TRANSLUCENCY_TINT = [0.9, 1.0, 0.55]


def foliage(sway, flutter, cutoff=0.4, colour=None, translucency=0.35):
    values = {"metallicFactor": 1.0, "roughnessFactor": 1.0, "alphaMode": "Mask", "alphaCutoff": cutoff,
              "doubleSided": True, "windSway": sway, "windFlutter": flutter,
              "diffuseTransmission": translucency, "diffuseTransmissionColor": TRANSLUCENCY_TINT}
    if colour:
        values["baseColorFactor"] = colour
    return values


class Scatter:
    """Random points on the ground with a minimum distance to points of the same and of other layers."""

    def __init__(self, rng):
        self.rng = rng
        self.taken = []  # (x, z, radius) of things that others keep away from

    def points(self, count, inner, outer, spacing=0.0, keep_out=0.0, attempts=60):
        result = []
        for _ in range(count * attempts):
            if len(result) == count:
                break
            a = self.rng.uniform(0.0, 2.0 * math.pi)
            d = math.sqrt(self.rng.uniform(inner * inner, outer * outer))
            x, z = math.cos(a) * d, math.sin(a) * d
            if max(abs(x), abs(z)) > TERRAIN_SIZE / 2.0 - 2.0:
                continue
            if math.hypot(x - PLAYER_START[0], z - PLAYER_START[1]) < 1.5:
                continue
            if spacing and any(math.hypot(x - px, z - pz) < spacing for px, pz in result):
                continue
            if keep_out and any(math.hypot(x - tx, z - tz) < keep_out + tr for tx, tz, tr in self.taken):
                continue
            result.append((x, z))
        return result

    def take(self, points, radius):
        self.taken.extend((x, z, radius) for x, z in points)

    def clusters(self, centres, count, radius):
        """@p count points around each centre, closer to it more often than not."""
        result = []
        for cx, cz in centres:
            for _ in range(count):
                a = self.rng.uniform(0.0, 2.0 * math.pi)
                d = radius * math.sqrt(self.rng.random()) * self.rng.uniform(0.3, 1.0)
                result.append((cx + math.cos(a) * d, cz + math.sin(a) * d))
        return [(x, z) for x, z in result if math.hypot(x - PLAYER_START[0], z - PLAYER_START[1]) > 1.2]


def build():
    rng = random.Random(2026)
    scatter = Scatter(rng)
    call("scene.new", {"discardChanges": True})

    # --- Materials ----------------------------------------------------------------------------------------
    # Every part of a tree shares its windSway (the bend grows with the height above the origin, at the
    # base), so trunks and needles move together; the sway is matched to each plant's height.
    m = {}
    floor = f"{EXTERNAL}/forest_leaves_04/forest_leaves_04"
    m["ground"] = material("Forest Ground", {"baseColorFactor": [0.36, 0.47, 0.3, 1.0], "metallicFactor": 1.0,
                                             "roughnessFactor": 1.0},
                           {"baseColor": f"{floor}_diff_2k.jpg", "normal": f"{floor}_nor_gl_2k.jpg",
                            "metallicRoughness": f"{floor}_arm_2k.jpg", "occlusion": f"{floor}_arm_2k.jpg"})
    # Firewood and the logs of the fire: the procedural log mesh with the fir's bark.
    m["bark"] = material("Bark", solid(), textures("fir_tree_01", "fir_tree_01_bark"))
    fir = "fir_tree_01"
    fir_sway = 0.00028
    m["fir_bark"] = material("Fir Bark", solid(fir_sway), textures(fir, f"{fir}_bark"))
    for part in "ab":
        m[f"fir_trunk_{part}"] = material(f"Fir Trunk {part.upper()}", solid(fir_sway), textures(fir, f"{fir}_trunk_{part}"))
    m["fir_twig"] = material("Fir Needles", foliage(fir_sway, 0.006, 0.2, [0.82, 0.9, 0.8, 1.0], translucency=0.2), leaves(fir, f"{fir}_twig"))
    young = "fir_sapling_medium"
    young_sway = 0.0011
    m["young_branches"] = material("Young Fir Branches", solid(young_sway), textures(young, f"{young}_branches"))
    m["young_twigs"] = material("Young Fir Needles", foliage(young_sway, 0.006, 0.2, translucency=0.2), leaves(young, f"{young}_twigs"))
    m["sapling_branches"] = material("Fir Sapling Branches", solid(0.02), textures("fir_sapling", "fir_sapling_branches"))
    m["sapling_twigs"] = material("Fir Sapling Needles", foliage(0.02, 0.006, 0.2, translucency=0.2), leaves("fir_sapling", "fir_sapling_twigs"))
    island = "island_tree_01"
    m["island_trunk"] = material("Broadleaf Trunk", solid(0.0022), textures(island))
    m["island_branches"] = material("Broadleaf Branches", solid(0.0022), textures(island, f"{island}_branches"))
    m["island_leaves"] = material("Broadleaf Leaves", foliage(0.0022, 0.012, 0.4), leaves(island, f"{island}_leaves"))
    small = "tree_small_02"
    m["small_trunk"] = material("Small Tree Trunk", solid(0.0025), textures(small))
    m["small_branches"] = material("Small Tree Branches", solid(0.0025), textures(small, f"{small}_branch"))
    m["small_leaves"] = material("Small Tree Leaves", foliage(0.0025, 0.012, 0.4), leaves(small, f"{small}_leaves"))
    m["grass"] = material("Grass", foliage(0.3, 0.004, colour=[0.8, 0.92, 0.62, 1.0]), textures("grass_medium_01", colour=f"{EXTERNAL}/grass_medium_01/textures/grass_medium_01_diff_1k.png"))
    m["short_grass"] = material("Short Grass", foliage(0.35, 0.004), leaves("grass_medium_02"))
    m["fern"] = material("Fern", foliage(0.15, 0.004), leaves("fern_02"))
    m["shrub"] = material("Shrub", foliage(0.05, 0.005), leaves("shrub_01"))
    m["bush"] = material("Bush", foliage(0.02, 0.006), leaves("shrub_02"))
    m["sprout"] = material("Sprout", foliage(0.2, 0.006), leaves("shrub_03"))
    m["creeper"] = material("Creeper", foliage(0.2, 0.004), leaves("shrub_04"))
    m["nettle"] = material("Nettle", foliage(0.25, 0.006), leaves("nettle_plant"))
    m["celandine"] = material("Celandine", foliage(0.3, 0.006), leaves("celandine_01"))
    m["dandelion"] = material("Dandelion", foliage(0.3, 0.006), leaves("dandelion_01"))
    m["periwinkle"] = material("Periwinkle", foliage(0.25, 0.006), leaves("periwinkle_plant"))
    m["weed"] = material("Weed", foliage(0.25, 0.004), leaves("weed_plant_02"))
    m["rock"] = material("Mossy Rock", {"metallicFactor": 0.0, "roughnessFactor": 1.0},
                         {"baseColor": f"{EXTERNAL}/rock_moss_set_01/textures/rock_moss_set_01_diff_1k.jpg",
                          "normal": f"{EXTERNAL}/rock_moss_set_01/textures/rock_moss_set_01_nor_gl_1k.jpg"})
    m["rock2"] = material("Mossy Stone", {"metallicFactor": 0.0, "roughnessFactor": 1.0}, textures("rock_moss_set_02", arm=False))
    m["boulder"] = material("Boulder", solid(), textures("boulder_01"))
    m["rock_face"] = material("Rock Face", solid(), textures("rock_face_01"))
    m["stone"] = material("Fire Stone", solid(), textures("stone_01"))
    m["trunk"] = material("Fallen Trunk", solid(), textures("dead_tree_trunk"))
    m["log"] = material("Fallen Log", solid(), textures("dead_tree_trunk_02"))
    m["stump1"] = material("Stump", solid(), textures("tree_stump_01"))
    m["stump2"] = material("Old Stump", solid(), textures("tree_stump_02"))
    m["roots_a"] = material("Pine Roots", solid(), textures("pine_roots", "pine_roots_a"))
    m["roots_b"] = material("Pine Roots B", solid(), textures("pine_roots", "pine_roots_b"))
    m["root_cluster"] = material("Root Cluster", solid(), textures("root_cluster_01"))
    m["branches"] = material("Dry Branches", solid(), textures("dry_branches_medium_01"))
    m["debris"] = material("Bark Debris", solid(), textures("bark_debris_01"))
    m["axe"] = material("Axe", solid(), textures("wooden_axe"))
    m["brass"] = material("Lantern Brass", solid(), textures("Lantern_01", "Lantern_01_brass"))
    m["wood_lantern"] = material("Wooden Lantern", solid(), textures("wooden_lantern_01"))
    m["glass"] = material("Lantern Glow", {"baseColorFactor": [1.0, 0.85, 0.6, 1.0], "metallicFactor": 0.0,
                                           "roughnessFactor": 0.2, "emissiveFactor": [6.0, 3.6, 1.4]})
    neon = {
        "cyan": material("Neon Cyan", {"baseColorFactor": [0.05, 0.05, 0.05, 1], "metallicFactor": 0.0,
                                       "emissiveFactor": [0.6, 6.0, 9.0]}),
        "magenta": material("Neon Magenta", {"baseColorFactor": [0.05, 0.05, 0.05, 1], "metallicFactor": 0.0,
                                             "emissiveFactor": [9.0, 0.6, 6.5]}),
        "violet": material("Neon Violet", {"baseColorFactor": [0.05, 0.05, 0.05, 1], "metallicFactor": 0.0,
                                           "emissiveFactor": [3.5, 1.0, 10.0]}),
    }
    m["orb_cyan"] = material("Orb Cyan", {"baseColorFactor": [0.2, 0.9, 1.0, 1], "metallicFactor": 0.0,
                                          "roughnessFactor": 0.25, "emissiveFactor": [0.3, 2.5, 3.5]})
    m["orb_magenta"] = material("Orb Magenta", {"baseColorFactor": [1.0, 0.2, 0.8, 1], "metallicFactor": 0.0,
                                                "roughnessFactor": 0.25, "emissiveFactor": [3.5, 0.3, 2.5]})
    m["embers"] = material("Embers", {"baseColorFactor": [0.1, 0.05, 0.03, 1], "metallicFactor": 0.0,
                                      "roughnessFactor": 0.9, "emissiveFactor": [4.0, 1.2, 0.3]})

    # --- The world ----------------------------------------------------------------------------------------
    entity("Terrain", {"illusion.Transform": {},
                       "illusion.MeshRenderer": {"mesh": "assets:/models/terrain.gltf#mesh/0", "material": m["ground"]},
                       "illusion.RigidBody": {"motionType": "Static"},
                       "illusion.Collider": {"shape": "Mesh", "mesh": "assets:/models/terrain.gltf#mesh/0"}})
    entity("Sky", {"illusion.SkyAtmosphere": {"timeOfDay": 17.2, "sunAzimuth": -1.3, "sunTilt": 0.75,
                                              "moonOffset": 1.5, "turbidity": 2.5, "sunIntensity": 4.5,
                                              "moonIntensity": 0.8, "starIntensity": 1.5,
                                              "groundAlbedo": [0.08, 0.1, 0.06]}})
    entity("Camera Look", {"illusion.PostEffects": {"autoExposure": True, "minExposure": -1.0, "maxExposure": 1.2,
                                                    "adaptationSpeed": 1.2, "tonemapper": "AgX", "contrast": 1.15,
                                                    "saturation": 1.15, "temperature": 0.05, "vignette": 0.3,
                                                    "grain": 0.1, "chromaticAberration": 0.6}})
    entity("Air", {"illusion.VolumetricFog": {"density": 0.007, "heightFalloff": 0.4, "baseHeight": 0.0,
                                              "albedo": [0.88, 0.92, 1.0], "anisotropy": 0.55,
                                              "lightIntensity": 2.5, "ambientIntensity": 0.3, "maxDistance": 100.0,
                                              "noiseAmount": 0.7, "noiseSize": 9.0, "windFactor": 0.6}})
    entity("Wind", {"illusion.Wind": {"direction": [0.8, 0.0, 0.45], "speed": 3.0, "gustStrength": 0.6,
                                      "gustFrequency": 0.12}})
    entity("Player", {"illusion.Transform": {"position": ground(PLAYER_START[0], PLAYER_START[1], -0.05)},
                      "illusion.CharacterController": {"radius": 0.35, "height": 1.8, "stepHeight": 0.4,
                                                       "mass": 75, "maxPushForce": 400, "gravityFactor": 1.4}})
    entity("Eye", {"illusion.Transform": {"position": [PLAYER_START[0], 1.7, PLAYER_START[1]]},
                   "illusion.Camera": {"fovY": 1.15, "nearPlane": 0.05, "exposure": 1.0},
                   "illusion.AudioListener": {}})

    # The camp keeps the forest out: the fire, the benches and the space to walk around them.
    scatter.take([(0.0, 0.0)], CLEARING_RADIUS - 1.0)

    # --- The campfire at the centre ---------------------------------------------------------------------------
    for index in range(5):
        angle = index * 2.0 * math.pi / 5.0
        # Leaning together over the embers: the outer end on the ground, the inner end raised.
        rotation = quaternion_multiply(yaw_quaternion(angle), axis_quaternion((0.0, 0.0, 1.0), -0.55))
        entity(f"Fire Log {index}", {"illusion.Transform": {"position": [round(math.cos(angle) * 0.3, 3), 0.27,
                                                                         round(-math.sin(angle) * 0.3, 3)],
                                                            "rotation": r3(rotation), "scale": [0.8, 0.8, 0.8]},
                                     "illusion.MeshRenderer": {"mesh": "assets:/models/log.gltf#mesh/0", "material": m["bark"]}})
    for index in range(14):
        angle = index * 2.0 * math.pi / 14.0
        x, z = math.cos(angle) * 0.82, math.sin(angle) * 0.82
        size = rng.uniform(2.3, 2.9)
        entity(f"Fire Stone {index:02d}", {"illusion.Transform": {"position": [round(x, 3), -0.02, round(z, 3)],
                                                                  "rotation": r3(tilted(rng, rng.uniform(0, 6.28), 0.3)),
                                                                  "scale": [size, size, size]},
                                           "illusion.MeshRenderer": {"mesh": f"{EXTERNAL}/stone_01/stone_01_1k.gltf#mesh/0",
                                                                     "material": m["stone"]}})
    entity("Embers Bed", {"illusion.Transform": {"position": [0.0, 0.02, 0.0], "scale": [0.6, 0.04, 0.6]},
                          "illusion.MeshRenderer": {"mesh": "builtin:/mesh/sphere", "material": m["embers"],
                                                    "castShadows": False}})
    entity("Flames", {"illusion.Transform": {"position": [0.0, 0.15, 0.0]},
                      "illusion.ParticleEmitter": {"maxParticles": 500, "rate": 90, "lifetimeMin": 0.5, "lifetimeMax": 0.9,
                                                   "speedMin": 0.6, "speedMax": 1.4, "shape": "Sphere", "radius": 0.28,
                                                   "spread": 0.25, "gravityFactor": -0.15, "drag": 0.8, "windFactor": 0.4,
                                                   "sizeStart": 0.6, "sizeEnd": 0.15, "colorStart": [8.0, 3.2, 0.8, 1.0],
                                                   "colorEnd": [3.0, 0.4, 0.05, 0.0], "texture": "assets:/textures/flame.png",
                                                   "seed": 5}})
    entity("Fire Embers", {"illusion.Transform": {"position": [0.0, 0.3, 0.0]},
                           "illusion.ParticleEmitter": {"maxParticles": 400, "rate": 35, "lifetimeMin": 1.5, "lifetimeMax": 3.5,
                                                        "speedMin": 0.8, "speedMax": 2.4, "shape": "Sphere", "radius": 0.3,
                                                        "spread": 0.5, "gravityFactor": -0.06, "drag": 0.4, "windFactor": 1.2,
                                                        "sizeStart": 0.035, "sizeEnd": 0.01, "colorStart": [12.0, 4.0, 0.8, 1.0],
                                                        "colorEnd": [6.0, 0.8, 0.1, 0.0], "seed": 6}})
    entity("Fire Smoke", {"illusion.Transform": {"position": [0.0, 1.0, 0.0]},
                          "illusion.ParticleEmitter": {"maxParticles": 200, "rate": 12, "lifetimeMin": 4.0, "lifetimeMax": 6.0,
                                                       "speedMin": 0.4, "speedMax": 0.8, "shape": "Sphere", "radius": 0.2,
                                                       "spread": 0.3, "gravityFactor": -0.04, "drag": 0.3, "windFactor": 0.6,
                                                       "sizeStart": 0.5, "sizeEnd": 2.6, "colorStart": [0.07, 0.065, 0.06, 0.4],
                                                       "colorEnd": [0.09, 0.09, 0.09, 0.0], "blend": "Alpha",
                                                       "texture": "assets:/textures/smoke.png", "seed": 7}})
    entity("Fire Light", {"illusion.Transform": {"position": [0.0, 0.8, 0.0]},
                          "illusion.PointLight": {"color": [1.0, 0.55, 0.22], "intensity": 45, "range": 16,
                                                  # The flames throw soft, moving shadows of the
                                                  # trees, rocks, benches and the player.
                                                  "castShadows": True, "sourceRadius": 0.25,
                                                  "shadowNearPlane": 0.35}})
    entity("Fire Sound", {"illusion.Transform": {"position": [0.0, 0.4, 0.0]},
                          "illusion.AudioSource": {"clip": "assets:/audio/fire.wav", "loop": True, "volume": 0.8,
                                                   "minDistance": 1.5, "maxDistance": 25}})

    def solid_prop(name, mesh, mat, position, rotation, size, collider="ConvexHull"):
        components = {"illusion.Transform": {"position": position, "rotation": r3(rotation), "scale": [size, size, size]},
                      "illusion.MeshRenderer": {"mesh": mesh, "material": mat}}
        if collider:
            components["illusion.RigidBody"] = {"motionType": "Static"}
            components["illusion.Collider"] = {"shape": collider, "mesh": mesh}
        entity(name, components)

    # Benches: two fallen trunks and a stump by the fire; firewood and an axe beside the woodpile stump.
    trunk_mesh = f"{EXTERNAL}/dead_tree_trunk/dead_tree_trunk_1k.gltf#mesh/0"
    for index, (x, z, angle) in enumerate(((0.0, -2.6, 0.0), (2.4, 1.2, 2.1))):
        solid_prop(f"Bench {index}", trunk_mesh, m["trunk"], ground(x, z, 0.02), yaw_quaternion(angle), 1.0)
    stump = f"{EXTERNAL}/tree_stump_01/tree_stump_01_1k.gltf#mesh/0"
    solid_prop("Seat Stump", stump, m["stump1"], ground(-2.5, 2.2, 0.06), yaw_quaternion(0.7), 0.9)
    solid_prop("Woodpile Stump", stump, m["stump1"], ground(-3.6, -2.4, 0.06), yaw_quaternion(2.0), 1.0)
    stump_top = terrain_height(-3.6, -2.4) - 0.06 + 0.38
    axe_rotation = quaternion_multiply(yaw_quaternion(0.9), axis_quaternion((1.0, 0.0, 0.0), 2.8))
    entity("Axe", {"illusion.Transform": {"position": [-3.6, round(stump_top + 0.44, 3), -2.4], "rotation": r3(axe_rotation),
                                          "scale": [1.1, 1.1, 1.1]},
                   "illusion.MeshRenderer": {"mesh": f"{EXTERNAL}/wooden_axe/wooden_axe_1k.gltf#mesh/0", "material": m["axe"]}})
    # A woodpile of rows of 4, 3 and 2 logs lying along the bench direction.
    pile = [(row, column - (3 - row) / 2.0) for row in range(3) for column in range(4 - row)]
    for index, (row, offset) in enumerate(pile):
        x, z = -4.9 + 0.19 * offset, -1.4
        rotation = yaw_quaternion(math.pi / 2.0 + rng.uniform(-0.06, 0.06))
        entity(f"Firewood {index}", {"illusion.Transform": {"position": [round(x, 3), round(terrain_height(-4.9, -1.4) + 0.09 + row * 0.16, 3),
                                                                         round(z + rng.uniform(-0.05, 0.05), 3)],
                                                            "rotation": r3(rotation), "scale": [0.75, 0.95, 0.95]},
                                     "illusion.MeshRenderer": {"mesh": "assets:/models/log.gltf#mesh/0", "material": m["bark"]}})
    scatter.take([(-4.9, -1.4)], 1.0)

    # --- Lanterns: two brass ones on the benches, a wooden one on the stump ----------------------------------------
    # With the fire they are the four point lights that cast shadows (ShadowSettings::pointLightCount).
    brass = f"{EXTERNAL}/Lantern_01/Lantern_01_1k.gltf"
    for index, (x, z) in enumerate(((-1.1, -2.55), (2.0, 0.6))):
        top = terrain_height(x, z) + 0.45
        name = f"Lantern {index}"
        entity(name, {"illusion.Transform": {"position": [x, round(top, 3), z], "scale": [1.6, 1.6, 1.6]},
                      "illusion.MeshRenderer": {"mesh": f"{brass}#mesh/0", "material": m["brass"]},
                      "illusion.RigidBody": {"motionType": "Kinematic", "mass": 2.0},
                      "illusion.Collider": {"halfExtents": [0.1, 0.24, 0.08], "offset": [0.0, 0.24, 0.0]}})
        entity(f"{name} Glass", {"illusion.Transform": {}, "illusion.LocalTransform": {},
                                 "illusion.MeshRenderer": {"mesh": f"{brass}#mesh/1", "material": m["glass"],
                                                           "castShadows": False}}, parent=f"/{name}")
        entity(f"{name} Light", {"illusion.Transform": {}, "illusion.LocalTransform": {"position": [0.0, 0.16, 0.0]},
                                 "illusion.PointLight": {"color": [1.0, 0.7, 0.35], "intensity": 6, "range": 6,
                                                         # Past the lantern's own frame.
                                                         "castShadows": True, "sourceRadius": 0.04,
                                                         "shadowNearPlane": 0.2}},
               parent=f"/{name}")
    wooden = f"{EXTERNAL}/wooden_lantern_01/wooden_lantern_01_1k.gltf"
    seat_top = terrain_height(-2.5, 2.2) - 0.06 + 0.38 * 0.9
    entity("Lantern 2", {"illusion.Transform": {"position": [-2.5, round(seat_top, 3), 2.2], "rotation": r3(yaw_quaternion(0.4)),
                                                "scale": [1.3, 1.3, 1.3]},
                         "illusion.MeshRenderer": {"mesh": f"{wooden}#mesh/0", "material": m["wood_lantern"]},
                         "illusion.RigidBody": {"motionType": "Kinematic", "mass": 2.0},
                         "illusion.Collider": {"halfExtents": [0.11, 0.2, 0.11], "offset": [0.0, 0.2, 0.0]}})
    entity("Lantern 2 Glass", {"illusion.Transform": {}, "illusion.LocalTransform": {},
                               "illusion.MeshRenderer": {"mesh": f"{wooden}#mesh/0", "submesh": 1, "material": m["glass"],
                                                         "castShadows": False}}, parent="/Lantern 2")
    entity("Lantern 2 Door", {"illusion.Transform": {}, "illusion.LocalTransform": {"position": [0.06, 0.17, 0.09]},
                              "illusion.MeshRenderer": {"mesh": f"{wooden}#mesh/1", "material": m["wood_lantern"]}},
           parent="/Lantern 2")
    entity("Lantern 2 Handle", {"illusion.Transform": {}, "illusion.LocalTransform": {"position": [0.0, 0.35, 0.0]},
                                "illusion.MeshRenderer": {"mesh": f"{wooden}#mesh/2", "material": m["wood_lantern"]}},
           parent="/Lantern 2")
    entity("Lantern 2 Light", {"illusion.Transform": {}, "illusion.LocalTransform": {"position": [0.0, 0.17, 0.0]},
                               "illusion.PointLight": {"color": [1.0, 0.68, 0.32], "intensity": 6, "range": 6,
                                                       "castShadows": True, "sourceRadius": 0.04,
                                                       "shadowNearPlane": 0.2}},
           parent="/Lantern 2")

    # --- Neon: a gate to the north and three hoops around the clearing --------------------------------------------
    gate_z = -CLEARING_RADIUS - 2.0
    gate_y = terrain_height(0.0, gate_z)
    entity("Neon Gate", {"illusion.Transform": {"position": [0.0, round(gate_y, 3), gate_z]},
                         "illusion.MeshRenderer": {"mesh": "assets:/models/neon.gltf#mesh/1", "material": neon["cyan"],
                                                   "castShadows": False}})
    for side in (-1, 1):
        entity(f"Neon Post {'West' if side < 0 else 'East'}",
               {"illusion.Transform": {"position": [3.0 * side, round(gate_y, 3), gate_z]},
                "illusion.MeshRenderer": {"mesh": "assets:/models/neon.gltf#mesh/2", "material": neon["magenta"],
                                          "castShadows": False}})
    entity("Neon Gate Light", {"illusion.Transform": {"position": [0.0, round(gate_y + 1.8, 3), gate_z + 0.6]},
                               "illusion.PointLight": {"color": [0.2, 0.85, 1.0], "intensity": 30, "range": 10}})
    scatter.take([(x, gate_z) for x in (-3.0, 0.0, 3.0)], 1.8)
    for index, (angle, colour, light) in enumerate(((0.3, "magenta", [1.0, 0.25, 0.85]), (2.5, "violet", [0.55, 0.3, 1.0]),
                                                    (4.3, "cyan", [0.2, 0.9, 1.0]))):
        distance = CLEARING_RADIUS + 3.5
        x, z = math.cos(angle) * distance, math.sin(angle) * distance
        y = terrain_height(x, z) + 1.65
        facing = math.atan2(-x, -z)  # the hoop's +z faces the fire
        entity(f"Neon Hoop {index}", {"illusion.Transform": {"position": [round(x, 3), round(y, 3), round(z, 3)],
                                                             "rotation": r3(yaw_quaternion(facing))},
                                      "illusion.MeshRenderer": {"mesh": "assets:/models/neon.gltf#mesh/0", "material": neon[colour],
                                                                "castShadows": False}})
        entity(f"Neon Hoop {index} Light", {"illusion.Transform": {"position": [round(x, 3), round(y, 3), round(z, 3)]},
                                            "illusion.PointLight": {"color": light, "intensity": 22, "range": 9}})
        scatter.take([(x, z)], 1.6)

    # --- Trees: tall firs on the hills, young firs and broad-leaved trees nearer the clearing ---------------------
    def tree(name, model, mesh, parts, x, z, size, yaw, radius, height, lod_bias=1.0):
        entity(name, {"illusion.Transform": {"position": ground(x, z, 0.12 * size),
                                             "rotation": r3(tilted(rng, yaw, 0.03)), "scale": [size, size, size]},
                      "illusion.RigidBody": {"motionType": "Static"},
                      "illusion.Collider": {"shape": "Cylinder", "radius": radius, "halfHeight": height / 2.0,
                                            "offset": [0.0, height / 2.0, 0.0]}})
        for part, (submesh, mat, shadows) in parts.items():
            renderer = {"mesh": f"{model}#mesh/{mesh}", "material": mat}
            if submesh:
                renderer["submesh"] = submesh
            if not shadows:
                renderer["castShadows"] = False
            if lod_bias != 1.0:
                renderer["lodBias"] = lod_bias
            entity(f"{name} {part}", {"illusion.Transform": {}, "illusion.LocalTransform": {},
                                      "illusion.MeshRenderer": renderer}, parent=f"/{name}")

    fir_model = f"{EXTERNAL}/{fir}/{fir}_lite.gltf"
    fir_parts = [  # submeshes of the three firs: bark of the branches, trunk, needles, dead branches
        {"Branches": (0, m["fir_bark"], True), "Trunk": (1, m["fir_trunk_a"], True), "Needles": (2, m["fir_twig"], True),
         "Dead Branches": (3, m["fir_bark"], False)},
        {"Branches": (0, m["fir_bark"], True), "Trunk": (1, m["fir_trunk_b"], True), "Needles": (2, m["fir_twig"], True),
         "Dead Branches": (3, m["fir_bark"], False)},
        {"Branches": (0, m["fir_bark"], True), "Needles": (1, m["fir_twig"], True), "Dead Branches": (2, m["fir_bark"], False),
         "Trunk": (3, m["fir_bark"], True)},
    ]
    # Firs beyond FAR_FIRS, where the hills keep the player away, switch to their coarser levels of detail
    # (fewer, larger needle cards) at twice the usual distance; only the needles of firs within SHADOWED_FIRS
    # cast shadows (all trunks do).
    firs = scatter.points(40, CLEARING_RADIUS + 5.0, FAR_FIRS, spacing=6.0, keep_out=1.0)
    firs += scatter.points(70, FAR_FIRS, 58.0, spacing=5.0, keep_out=1.0)
    scatter.take(firs, 2.0)
    for index, (x, z) in enumerate(firs):
        variant = index % 3
        size = rng.uniform(0.8, 1.15)
        parts = fir_parts[variant]
        distance = math.hypot(x, z)
        if distance > SHADOWED_FIRS:
            parts = dict(parts, Needles=(parts["Needles"][0], m["fir_twig"], False))
        tree(f"Fir {index:02d}", fir_model, variant, parts, x, z, size, rng.uniform(0, 6.28), 0.45, 8.0,
             FAR_LOD_BIAS if distance > FAR_FIRS else 1.0)

    young_model = f"{EXTERNAL}/{young}/{young}_lite.gltf"
    young_parts = {"Branches": (0, m["young_branches"], True), "Needles": (1, m["young_twigs"], True),
                   "Dead Branches": (2, m["young_branches"], False)}
    youngs = scatter.points(80, CLEARING_RADIUS + 2.5, 36.0, spacing=3.2, keep_out=1.2)
    scatter.take(youngs, 1.5)
    for index, (x, z) in enumerate(youngs):
        # Only the young firs near the camp throw needle shadows; the others' would cost more than they show.
        # Those further out switch to their coarser levels of detail sooner.
        parts, lod_bias = young_parts, 1.0
        if math.hypot(x, z) >= SHADOWED_YOUNG_FIRS:
            parts = dict(young_parts, Needles=(1, m["young_twigs"], False))
            lod_bias = FAR_LOD_BIAS
        tree(f"Young Fir {index:02d}", young_model, index % 3, parts, x, z, rng.uniform(0.75, 1.1),
             rng.uniform(0, 6.28), 0.2, 4.0, lod_bias)

    broadleaf = [(f"{EXTERNAL}/{island}/{island}_lite.gltf",
                  {"Trunk": (0, m["island_trunk"], True), "Leaves": (1, m["island_leaves"], True),
                   "Branches": (2, m["island_branches"], True)}, 0.25),
                 (f"{EXTERNAL}/{small}/{small}_lite.gltf",
                  {"Branches": (0, m["small_branches"], True), "Leaves": (1, m["small_leaves"], True),
                   "Trunk": (2, m["small_trunk"], True)}, 0.2)]
    broadleaves = scatter.points(14, CLEARING_RADIUS + 1.5, 30.0, spacing=5.0, keep_out=1.5)
    scatter.take(broadleaves, 1.5)
    for index, (x, z) in enumerate(broadleaves):
        model, parts, radius = broadleaf[index % 2]
        tree(f"Broadleaf {index:02d}", model, 0, parts, x, z, rng.uniform(1.0, 1.4), rng.uniform(0, 6.28), radius, 3.0)

    sapling_model = f"{EXTERNAL}/fir_sapling/fir_sapling_lite.gltf"
    for index, (x, z) in enumerate(scatter.points(36, CLEARING_RADIUS, 48.0, spacing=1.5, keep_out=0.8)):
        size = rng.uniform(0.9, 1.6)
        entity(f"Sapling {index:02d}", {"illusion.Transform": {"position": ground(x, z, 0.03), "scale": [size, size, size],
                                                               "rotation": r3(tilted(rng, rng.uniform(0, 6.28), 0.08))}})
        for part, submesh, mat in (("Branches", 0, m["sapling_branches"]), ("Needles", 1, m["sapling_twigs"])):
            renderer = {"mesh": f"{sapling_model}#mesh/{index % 3}", "material": mat}
            if submesh:
                renderer["submesh"] = submesh
            entity(f"Sapling {index:02d} {part}", {"illusion.Transform": {}, "illusion.LocalTransform": {},
                                                   "illusion.MeshRenderer": renderer}, parent=f"/Sapling {index:02d}")

    # Roots spread from the feet of some firs.
    for index, (x, z) in enumerate(firs[:22]):
        variant = index % 2
        angle = rng.uniform(0, 6.28)
        entity(f"Roots {index:02d}", {"illusion.Transform": {"position": ground(x, z, 0.04), "rotation": r3(yaw_quaternion(angle)),
                                                             "scale": [1.4, 1.4, 1.4]},
                                      "illusion.MeshRenderer": {"mesh": f"{EXTERNAL}/pine_roots/pine_roots_1k.gltf#mesh/{variant}",
                                                                "material": m["roots_a"] if variant == 0 else m["roots_b"],
                                                                "castShadows": False}})

    # --- Rocks, cliffs and dead wood (solid) -----------------------------------------------------------------------
    for index, (x, z) in enumerate(scatter.points(6, BOWL_RADIUS - 2.0, 42.0, spacing=12.0, keep_out=1.0)):
        size = rng.uniform(1.4, 2.0)
        facing = math.atan2(-x, -z)
        solid_prop(f"Rock Face {index}", f"{EXTERNAL}/rock_face_01/rock_face_01_1k.gltf#mesh/0", m["rock_face"],
                   ground(x, z, 0.5 * size), yaw_quaternion(facing + rng.uniform(-0.4, 0.4)), size)
    boulders = scatter.points(9, CLEARING_RADIUS + 1.0, 40.0, spacing=8.0, keep_out=1.5)
    scatter.take(boulders, 1.5)
    for index, (x, z) in enumerate(boulders):
        size = rng.uniform(1.0, 1.8)
        solid_prop(f"Boulder {index}", f"{EXTERNAL}/boulder_01/boulder_01_1k.gltf#mesh/0", m["boulder"],
                   ground(x, z, 0.2 * size), yaw_quaternion(rng.uniform(0, 6.28)), size)
    rocks = scatter.points(48, CLEARING_RADIUS - 1.0, 46.0, spacing=2.0, keep_out=0.6)
    scatter.take(rocks, 0.6)
    for index, (x, z) in enumerate(rocks):
        if index % 2:
            mesh, mat, size = f"{EXTERNAL}/rock_moss_set_01/rock_moss_set_01_1k.gltf#mesh/{rng.randrange(6)}", m["rock"], rng.uniform(0.25, 0.7)
        else:
            mesh, mat, size = f"{EXTERNAL}/rock_moss_set_02/rock_moss_set_02_1k.gltf#mesh/{rng.randrange(7)}", m["rock2"], rng.uniform(0.5, 1.2)
        solid_prop(f"Rock {index:02d}", mesh, mat, ground(x, z, 0.15 * size), tilted(rng, rng.uniform(0, 6.28), 0.2), size)
    logs = scatter.points(8, CLEARING_RADIUS + 2.0, 40.0, spacing=8.0, keep_out=2.0)
    scatter.take(logs, 2.0)
    for index, (x, z) in enumerate(logs):
        solid_prop(f"Fallen Log {index}", f"{EXTERNAL}/dead_tree_trunk_02/dead_tree_trunk_02_1k.gltf#mesh/0", m["log"],
                   ground(x, z, 0.15), yaw_quaternion(rng.uniform(0, 6.28)), rng.uniform(0.9, 1.2))
    stumps = scatter.points(12, CLEARING_RADIUS + 1.0, 44.0, spacing=4.0, keep_out=1.0)
    scatter.take(stumps, 0.8)
    for index, (x, z) in enumerate(stumps):
        variant = index % 2
        solid_prop(f"Stump {index:02d}", f"{EXTERNAL}/tree_stump_0{variant + 1}/tree_stump_0{variant + 1}_1k.gltf#mesh/0",
                   m["stump1"] if variant == 0 else m["stump2"], ground(x, z, 0.06), yaw_quaternion(rng.uniform(0, 6.28)),
                   rng.uniform(0.8, 1.2))
    for index, (x, z) in enumerate(scatter.points(4, CLEARING_RADIUS + 6.0, 40.0, spacing=12.0, keep_out=2.0)):
        solid_prop(f"Root Cluster {index}", f"{EXTERNAL}/root_cluster_01/root_cluster_01_1k.gltf#mesh/0", m["root_cluster"],
                   ground(x, z, 0.1), yaw_quaternion(rng.uniform(0, 6.28)), rng.uniform(0.8, 1.1), collider="ConvexHull")

    # --- Undergrowth (no colliders) ----------------------------------------------------------------------------------
    def plants(prefix, points, mesh_of, mat, size_range, shadows, tilt=0.15, sink=0.02, stretch=(1.0, 1.0)):
        for index, (x, z) in enumerate(points):
            size = rng.uniform(*size_range)
            mesh, rotation = mesh_of()
            rotation = quaternion_multiply(tilted(rng, rng.uniform(0, 6.28), tilt), rotation)
            renderer = {"mesh": mesh, "material": mat}
            if not shadows:
                renderer["castShadows"] = False
            entity(f"{prefix} {index:04d}", {"illusion.Transform": {"position": ground(x, z, sink), "rotation": r3(rotation),
                                                                    "scale": r3([size, size * rng.uniform(*stretch), size])},
                                             "illusion.MeshRenderer": renderer})

    identity = [0.0, 0.0, 0.0, 1.0]

    def variants(asset, count, rotations=None):
        def pick():
            n = rng.randrange(count)
            return f"{EXTERNAL}/{asset}/{asset}_1k.gltf#mesh/{n}", rotations[n] if rotations else identity
        return pick

    plants("Grass", scatter.points(6500, 2.4, 34.0), variants("grass_medium_01", 8), m["grass"], (3.0, 5.5), False,
           tilt=0.1, stretch=(0.9, 1.4))
    meadow = scatter.points(90, 2.5, 30.0, spacing=1.5)
    plants("Short Grass", scatter.clusters(meadow, 24, 1.6), variants("grass_medium_02", 5), m["short_grass"], (2.0, 3.4), False)
    plants("Fern", scatter.points(1500, CLEARING_RADIUS - 1.5, 44.0, spacing=0.7, keep_out=0.3), variants("fern_02", 4),
           m["fern"], (1.3, 2.4), True)
    # The hills beyond the bowl get a sparser cover, seen from afar.
    plants("Hill Grass", scatter.points(1800, 34.0, 58.0, spacing=0.8), variants("grass_medium_01", 8), m["grass"], (3.5, 6.0), False,
           tilt=0.1, stretch=(0.9, 1.4))
    plants("Hill Fern", scatter.points(700, 44.0, 58.0, spacing=1.2, keep_out=0.3), variants("fern_02", 4), m["fern"], (1.6, 2.6), False)
    plants("Shrub", scatter.points(12, CLEARING_RADIUS + 1.0, 30.0, spacing=4.0, keep_out=0.5), variants("shrub_01", 1),
           m["shrub"], (1.0, 1.5), True, sink=0.05)
    bush_rotations = [[0.1259, -0.6948, 0.0061, 0.7081], [0.0836, -0.7466, 0.2623, 0.6056],
                      [0.1265, -0.713, 0.3944, 0.5658], [0.0474, -0.7383, -0.3004, 0.602]]
    plants("Bush", scatter.points(150, CLEARING_RADIUS - 0.5, 40.0, spacing=1.6, keep_out=0.6), variants("shrub_02", 4, bush_rotations),
           m["bush"], (0.8, 1.3), True, tilt=0.05, sink=0.05)
    plants("Sprout", scatter.clusters(scatter.points(40, CLEARING_RADIUS - 2.0, 44.0, spacing=3.0), 5, 1.2),
           variants("shrub_03", 4), m["sprout"], (1.5, 2.6), False)
    plants("Creeper", scatter.points(60, CLEARING_RADIUS - 2.0, 36.0, spacing=1.5), variants("shrub_04", 1), m["creeper"],
           (1.4, 2.2), False, tilt=0.05)
    plants("Nettle", scatter.clusters(scatter.points(36, CLEARING_RADIUS - 1.0, 40.0, spacing=3.0, keep_out=0.5), 9, 1.0),
           variants("nettle_plant", 6), m["nettle"], (2.2, 3.4), False)
    plants("Celandine", scatter.clusters(scatter.points(30, 3.0, 26.0, spacing=2.5), 9, 0.9), variants("celandine_01", 5),
           m["celandine"], (1.4, 2.0), False)
    plants("Dandelion", scatter.clusters(scatter.points(14, 3.0, 24.0, spacing=2.5), 7, 0.8), variants("dandelion_01", 5),
           m["dandelion"], (1.6, 2.4), False)
    plants("Periwinkle", scatter.clusters(scatter.points(24, CLEARING_RADIUS - 2.0, 36.0, spacing=3.0), 8, 0.8),
           variants("periwinkle_plant", 6), m["periwinkle"], (1.3, 1.9), False)
    plants("Weed", scatter.points(260, 2.5, 40.0, spacing=0.8), variants("weed_plant_02", 5), m["weed"], (1.8, 2.8), False)
    plants("Dry Branches", scatter.points(70, CLEARING_RADIUS - 1.0, 46.0, spacing=1.5), variants("dry_branches_medium_01", 3),
           m["branches"], (1.0, 1.6), True, tilt=0.1, sink=0.0)
    plants("Bark Debris", scatter.points(25, 3.0, 36.0, spacing=1.5), variants("bark_debris_01", 4), m["debris"], (1.0, 1.6),
           False, tilt=0.1, sink=0.0)

    # --- Fireflies: a GPU swarm the module lets loose at dusk ----------------------------------------------------
    entity("Fireflies", {"illusion.Transform": {"position": [0.0, 1.6, 0.0]},
                         "illusion.ParticleEmitter": {"maxParticles": 5000, "rate": 850, "playOnStart": False,
                                                      "lifetimeMin": 4.0, "lifetimeMax": 7.0, "speedMin": 0.05,
                                                      "speedMax": 0.35, "shape": "Box", "boxExtents": [20.0, 1.4, 20.0],
                                                      "gravityFactor": -0.005, "drag": 0.1, "windFactor": 0.15,
                                                      "sizeStart": 0.14, "sizeEnd": 0.05, "colorStart": [7.0, 12.0, 1.5, 1.0],
                                                      "colorEnd": [2.0, 6.0, 0.6, 0.0], "seed": 11, "simulation": "Gpu"}})

    # Neon bars among the trees, glowing without lights of their own.
    colours = ["cyan", "magenta", "violet"]
    for index, (x, z) in enumerate(scatter.points(6, CLEARING_RADIUS + 5.0, 24.0, 6.0, keep_out=0.5)):
        tilt = axis_quaternion((0.0, 0.0, 1.0), rng.uniform(-0.25, 0.25))
        entity(f"Neon Bar {index}", {"illusion.Transform": {"position": ground(x, z, 0.1),
                                                            "rotation": r3(quaternion_multiply(yaw_quaternion(rng.uniform(0, 6.28)), tilt))},
                                     "illusion.MeshRenderer": {"mesh": "assets:/models/neon.gltf#mesh/2",
                                                               "material": neon[colours[index % 3]], "castShadows": False}})

    # --- Things to push: glowing orbs ---------------------------------------------------------------------------
    for index, (x, z) in enumerate(((3.8, 2.6), (-5.2, 3.2), (5.0, -2.0), (-3.2, -4.6), (4.6, -4.8))):
        size = 0.55 if index % 2 else 0.75
        entity(f"Orb {index}", {"illusion.Transform": {"position": ground(x, z, -size / 2 - 0.3), "scale": [size, size, size]},
                                "illusion.MeshRenderer": {"mesh": "builtin:/mesh/sphere",
                                                          "material": m["orb_cyan"] if index % 2 else m["orb_magenta"]},
                                "illusion.RigidBody": {"motionType": "Kinematic", "mass": 4.0, "friction": 0.6, "restitution": 0.4},
                                "illusion.Collider": {"shape": "Sphere", "radius": size / 2}})

    # --- Sound ---------------------------------------------------------------------------------------------------
    entity("Forest Wind", {"illusion.Transform": {}, "illusion.AudioSource": {"clip": "assets:/audio/forest_wind.wav", "bus": "Ambience",
                                                    "attenuation": "None", "spatial": False, "loop": True, "volume": 0.5}})
    entity("Crickets", {"illusion.Transform": {}, "illusion.AudioSource": {"clip": "assets:/audio/crickets.wav", "bus": "Ambience",
                                                 "attenuation": "None", "spatial": False, "loop": True, "volume": 0.35,
                                                 "playOnStart": False}})

    call("scene.save_as", {"path": "assets:/scenes/forest.scene", "overwrite": True})


def main():
    build()
    header = {"comment": "Builds assets:/scenes/forest.scene of the tech demo through editor commands. Generated by "
                         "tools/make_scene.py: edit that and run it again instead of editing this file."}
    lines = [json.dumps(header)] + [json.dumps(command, separators=(", ", ": ")) for command in commands]
    with open(PROJECT / "build.jsonl", "w", encoding="utf-8", newline="\n") as file:
        file.write("\n".join(lines) + "\n")
    print(f"{len(commands)} commands")


if __name__ == "__main__":
    main()
