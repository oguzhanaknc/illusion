"""Writes build.jsonl: the editor commands that build the tech demo's scene, assets:/scenes/forest.scene.

The vegetation, rocks and props are scattered with a fixed seed and set on the ground with terrain_height()
from make_assets.py, so the scene matches the terrain mesh. Replay the file against a headless editor on this
project (see the README) to build the scene:

    python samples/techdemo/tools/make_scene.py
"""

import json
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from make_assets import CLEARING_RADIUS, terrain_height  # noqa: E402

PROJECT = Path(__file__).resolve().parent.parent
EXTERNAL = "assets:/external"

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


def ground(x, z, sink=0.0):
    return [round(x, 3), round(terrain_height(x, z) - sink, 3), round(z, 3)]


def r3(values):
    return [round(v, 4) for v in values]


def material(name, values, textures=None):
    value = {"material": dict(values, name=name)}
    if textures:
        value["textures"] = textures
    call("material.create", {"path": f"assets:/materials/{name.lower().replace(' ', '_')}.material",
                             "value": value, "overwrite": True})
    return f"assets:/materials/{name.lower().replace(' ', '_')}.material"


def entity(name, components, parent=None):
    params = {"name": name, "components": components}
    if parent:
        params["parent"] = parent
    call("entity.create", params)


def textures(asset, colour=None, normal=True, arm=True, folder="textures/"):
    base = f"{EXTERNAL}/{asset}/{folder}{asset}"
    result = {"baseColor": colour or f"{base}_diff_1k.jpg"}
    if normal:
        result["normal"] = f"{base}_nor_gl_1k.jpg"
    if arm:
        result["metallicRoughness"] = f"{base}_arm_1k.jpg"
        result["occlusion"] = f"{base}_arm_1k.jpg"
    return result


def build():
    rng = random.Random(2026)
    call("scene.new", {"discardChanges": True})

    # --- Materials ----------------------------------------------------------------------------------------
    m = {}
    m["ground"] = material("Forest Ground", {"baseColorFactor": [0.62, 0.72, 0.42, 1.0], "metallicFactor": 1.0, "roughnessFactor": 1.0},
                           textures("forest_ground_04", folder=""))
    m["bark"] = material("Bark", {"baseColorFactor": [0.5, 0.4, 0.33, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.9, "windSway": 0.0006},
                         {"baseColor": "assets:/models/textures/bark_diff.png",
                          "normal": "assets:/models/textures/bark_nor_gl.png"})
    m["leaves"] = material("Tree Leaves", {"metallicFactor": 0.0, "roughnessFactor": 0.7, "alphaMode": "Mask",
                                           "alphaCutoff": 0.45, "doubleSided": True, "windSway": 0.0006,
                                           "windFlutter": 0.008},
                           {"baseColor": "assets:/models/textures/tree_leaves.png"})
    m["grass"] = material("Grass", {"metallicFactor": 1.0, "roughnessFactor": 1.0, "alphaMode": "Mask",
                                    "alphaCutoff": 0.4, "doubleSided": True, "windSway": 0.3,
                                    "windFlutter": 0.004},
                          textures("grass_medium_01", f"{EXTERNAL}/grass_medium_01/textures/grass_medium_01_diff_1k.png"))
    m["fern"] = material("Fern", {"metallicFactor": 1.0, "roughnessFactor": 1.0, "alphaMode": "Mask",
                                  "alphaCutoff": 0.4, "doubleSided": True, "windSway": 0.15, "windFlutter": 0.004},
                         textures("fern_02", f"{EXTERNAL}/fern_02/textures/fern_02_leaves_1k.png"))
    m["shrub"] = material("Shrub", {"metallicFactor": 1.0, "roughnessFactor": 1.0, "alphaMode": "Mask",
                                    "alphaCutoff": 0.4, "doubleSided": True, "windSway": 0.05,
                                    "windFlutter": 0.005},
                          textures("shrub_01", f"{EXTERNAL}/shrub_01/textures/shrub_01_leaves_1k.png"))
    m["rock"] = material("Mossy Rock", {"metallicFactor": 0.0, "roughnessFactor": 1.0},
                         {"baseColor": f"{EXTERNAL}/rock_moss_set_01/textures/rock_moss_set_01_diff_1k.jpg",
                          "normal": f"{EXTERNAL}/rock_moss_set_01/textures/rock_moss_set_01_nor_gl_1k.jpg"})
    m["boulder"] = material("Boulder", {"metallicFactor": 1.0, "roughnessFactor": 1.0}, textures("boulder_01"))
    m["trunk"] = material("Fallen Trunk", {"metallicFactor": 1.0, "roughnessFactor": 1.0},
                          textures("dead_tree_trunk"))
    m["brass"] = material("Lantern Brass", {"metallicFactor": 1.0, "roughnessFactor": 1.0},
                          textures("Lantern_01", f"{EXTERNAL}/Lantern_01/textures/Lantern_01_brass_diff_1k.jpg")
                          | {"normal": f"{EXTERNAL}/Lantern_01/textures/Lantern_01_brass_nor_gl_1k.jpg",
                             "metallicRoughness": f"{EXTERNAL}/Lantern_01/textures/Lantern_01_brass_arm_1k.jpg",
                             "occlusion": f"{EXTERNAL}/Lantern_01/textures/Lantern_01_brass_arm_1k.jpg"})
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
    entity("Sky", {"illusion.SkyAtmosphere": {"timeOfDay": 17.2, "sunAzimuth": 0.5, "sunTilt": 0.75,
                                              "moonOffset": 1.5, "turbidity": 2.5, "sunIntensity": 4.5,
                                              "moonIntensity": 0.8, "starIntensity": 1.5,
                                              "groundAlbedo": [0.12, 0.13, 0.08]}})
    entity("Wind", {"illusion.Wind": {"direction": [0.8, 0.0, 0.45], "speed": 3.0, "gustStrength": 0.6,
                                      "gustFrequency": 0.12}})
    entity("Player", {"illusion.Transform": {"position": ground(0.0, 7.0, -0.05)},
                      "illusion.CharacterController": {"radius": 0.35, "height": 1.8, "stepHeight": 0.4,
                                                       "mass": 75, "maxPushForce": 400, "gravityFactor": 1.4}})
    entity("Eye", {"illusion.Transform": {"position": [0.0, 1.7, 7.0]},
                   "illusion.Camera": {"fovY": 1.15, "nearPlane": 0.05, "exposure": 1.0},
                   "illusion.AudioListener": {}})

    # --- Trees, in a ring around the clearing, apart from each other ---------------------------------------
    trees = []
    while len(trees) < 46:
        angle = rng.uniform(0.0, 2.0 * math.pi)
        distance = rng.uniform(CLEARING_RADIUS + 3.0, 46.0)
        x, z = math.cos(angle) * distance, math.sin(angle) * distance
        if all(math.hypot(x - tx, z - tz) > 5.5 for tx, tz in trees):
            trees.append((x, z))
    for index, (x, z) in enumerate(trees):
        name = f"Tree {index:02d}"
        variant = "abc"[index % 3]
        size = rng.uniform(0.85, 1.25)
        entity(name, {"illusion.Transform": {"position": ground(x, z, 0.1), "rotation": r3(yaw_quaternion(rng.uniform(0, 6.28))),
                                             "scale": [size, size, size]},
                      "illusion.RigidBody": {"motionType": "Static"},
                      "illusion.Collider": {"shape": "Cylinder", "radius": 0.35, "halfHeight": 2.0,
                                            "offset": [0.0, 2.0, 0.0]}})
        for part, mesh, mat in (("Trunk", 0, m["bark"]), ("Leaves", 1, m["leaves"])):
            entity(f"{name} {part}", {"illusion.Transform": {}, "illusion.LocalTransform": {},
                                      "illusion.MeshRenderer": {"mesh": f"assets:/models/tree_{variant}.gltf#mesh/{mesh}",
                                                                "material": mat}}, parent=f"/{name}")

    # --- Undergrowth ----------------------------------------------------------------------------------------
    def scatter(count, inner, outer, minimum=0.0):
        points = []
        while len(points) < count:
            angle = rng.uniform(0.0, 2.0 * math.pi)
            distance = math.sqrt(rng.uniform(inner * inner, outer * outer))
            x, z = math.cos(angle) * distance, math.sin(angle) * distance
            if math.hypot(x, z - 7.0) < 1.2:  # keep the player's start clear
                continue
            if minimum and any(math.hypot(x - px, z - pz) < minimum for px, pz in points):
                continue
            points.append((x, z))
        return points

    for index, (x, z) in enumerate(scatter(5000, 2.4, 36.0)):
        size = rng.uniform(3.0, 5.5)
        entity(f"Grass {index:04d}", {"illusion.Transform": {"position": ground(x, z, 0.02),
                                                             "rotation": r3(yaw_quaternion(rng.uniform(0, 6.28))),
                                                             "scale": [size, size * rng.uniform(0.9, 1.4), size]},
                                      "illusion.MeshRenderer": {"mesh": f"{EXTERNAL}/grass_medium_01/grass_medium_01_1k.gltf#mesh/{rng.randrange(8)}",
                                                                "material": m["grass"], "castShadows": False}})
    for index, (x, z) in enumerate(scatter(220, CLEARING_RADIUS - 2.0, 42.0, 1.0)):
        size = rng.uniform(1.4, 2.4)
        entity(f"Fern {index:03d}", {"illusion.Transform": {"position": ground(x, z, 0.03),
                                                            "rotation": r3(yaw_quaternion(rng.uniform(0, 6.28))),
                                                            "scale": [size, size, size]},
                                     "illusion.MeshRenderer": {"mesh": f"{EXTERNAL}/fern_02/fern_02_1k.gltf#mesh/{rng.randrange(4)}",
                                                               "material": m["fern"]}})
    for index, (x, z) in enumerate(scatter(18, CLEARING_RADIUS + 2.0, 40.0, 3.0)):
        size = rng.uniform(1.0, 1.5)
        entity(f"Shrub {index:02d}", {"illusion.Transform": {"position": ground(x, z, 0.05),
                                                             "rotation": r3(yaw_quaternion(rng.uniform(0, 6.28))),
                                                             "scale": [size, size, size]},
                                      "illusion.MeshRenderer": {"mesh": f"{EXTERNAL}/shrub_01/shrub_01_1k.gltf#mesh/0",
                                                                "material": m["shrub"]}})

    # --- Rocks and boulders (solid) ---------------------------------------------------------------------------
    for index, (x, z) in enumerate(scatter(26, CLEARING_RADIUS - 1.0, 44.0, 2.5)):
        size = rng.uniform(0.25, 0.7)
        mesh = f"{EXTERNAL}/rock_moss_set_01/rock_moss_set_01_1k.gltf#mesh/{rng.randrange(6)}"
        entity(f"Rock {index:02d}", {"illusion.Transform": {"position": ground(x, z, 0.15 * size),
                                                            "rotation": r3(yaw_quaternion(rng.uniform(0, 6.28))),
                                                            "scale": [size, size, size]},
                                     "illusion.MeshRenderer": {"mesh": mesh, "material": m["rock"]},
                                     "illusion.RigidBody": {"motionType": "Static"},
                                     "illusion.Collider": {"shape": "ConvexHull", "mesh": mesh}})
    for index, (x, z) in enumerate(((-13.0, -4.0), (12.0, 6.5), (4.0, -15.0), (-7.0, 15.0), (19.0, -9.0), (-20.0, 10.0))):
        size = rng.uniform(1.2, 2.0)
        mesh = f"{EXTERNAL}/boulder_01/boulder_01_1k.gltf#mesh/0"
        entity(f"Boulder {index}", {"illusion.Transform": {"position": ground(x, z, 0.2 * size),
                                                           "rotation": r3(yaw_quaternion(rng.uniform(0, 6.28))),
                                                           "scale": [size, size, size]},
                                    "illusion.MeshRenderer": {"mesh": mesh, "material": m["boulder"]},
                                    "illusion.RigidBody": {"motionType": "Static"},
                                    "illusion.Collider": {"shape": "ConvexHull", "mesh": mesh}})

    # --- The campfire at the centre ---------------------------------------------------------------------------
    for index in range(4):
        angle = index * math.pi / 4.0
        rotation = quaternion_multiply(yaw_quaternion(angle), axis_quaternion((0.0, 0.0, 1.0), 0.35 * (1 if index % 2 else -1)))
        entity(f"Fire Log {index}", {"illusion.Transform": {"position": [0.0, 0.12, 0.0], "rotation": r3(rotation)},
                                     "illusion.MeshRenderer": {"mesh": "assets:/models/log.gltf#mesh/0", "material": m["bark"]}})
    for index in range(9):
        angle = index * 2.0 * math.pi / 9.0
        x, z = math.cos(angle) * 0.85, math.sin(angle) * 0.85
        mesh = f"{EXTERNAL}/rock_moss_set_01/rock_moss_set_01_1k.gltf#mesh/{index % 6}"
        entity(f"Fire Stone {index}", {"illusion.Transform": {"position": [round(x, 3), 0.0, round(z, 3)],
                                                              "rotation": r3(yaw_quaternion(angle * 3.0)),
                                                              "scale": [0.13, 0.13, 0.13]},
                                       "illusion.MeshRenderer": {"mesh": mesh, "material": m["rock"]}})
    entity("Embers Bed", {"illusion.Transform": {"position": [0.0, 0.02, 0.0], "scale": [0.6, 0.04, 0.6]},
                          "illusion.MeshRenderer": {"mesh": "builtin:/mesh/sphere", "material": m["embers"],
                                                    "castShadows": False}})
    entity("Flames", {"illusion.Transform": {"position": [0.0, 0.15, 0.0]},
                      "illusion.ParticleEmitter": {"maxParticles": 400, "rate": 70, "lifetimeMin": 0.5, "lifetimeMax": 0.9,
                                                   "speedMin": 0.6, "speedMax": 1.3, "shape": "Sphere", "radius": 0.28,
                                                   "spread": 0.25, "gravityFactor": -0.15, "drag": 0.8, "windFactor": 0.4,
                                                   "sizeStart": 0.55, "sizeEnd": 0.15, "colorStart": [8.0, 3.2, 0.8, 1.0],
                                                   "colorEnd": [3.0, 0.4, 0.05, 0.0], "texture": "assets:/textures/flame.png",
                                                   "seed": 5}})
    entity("Fire Embers", {"illusion.Transform": {"position": [0.0, 0.3, 0.0]},
                           "illusion.ParticleEmitter": {"maxParticles": 300, "rate": 25, "lifetimeMin": 1.5, "lifetimeMax": 3.5,
                                                        "speedMin": 0.8, "speedMax": 2.2, "shape": "Sphere", "radius": 0.3,
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
                          "illusion.PointLight": {"color": [1.0, 0.55, 0.22], "intensity": 40, "range": 14}})
    entity("Fire Sound", {"illusion.Transform": {"position": [0.0, 0.4, 0.0]},
                          "illusion.AudioSource": {"clip": "assets:/audio/fire.wav", "loop": True, "volume": 0.8,
                                                   "minDistance": 1.5, "maxDistance": 25}})
    # Benches: two fallen trunks by the fire.
    for index, (x, z, angle) in enumerate(((0.0, -2.6, 0.0), (2.4, 1.2, 2.1))):
        mesh = f"{EXTERNAL}/dead_tree_trunk/dead_tree_trunk_1k.gltf#mesh/0"
        entity(f"Bench {index}", {"illusion.Transform": {"position": ground(x, z, 0.02), "rotation": r3(yaw_quaternion(angle))},
                                  "illusion.MeshRenderer": {"mesh": mesh, "material": m["trunk"]},
                                  "illusion.RigidBody": {"motionType": "Static"},
                                  "illusion.Collider": {"shape": "ConvexHull", "mesh": mesh}})

    # --- Fireflies: a GPU swarm the module lets loose at dusk ----------------------------------------------------
    entity("Fireflies", {"illusion.Transform": {"position": [0.0, 1.6, 0.0]},
                         "illusion.ParticleEmitter": {"maxParticles": 4000, "rate": 700, "playOnStart": False,
                                                      "lifetimeMin": 4.0, "lifetimeMax": 7.0, "speedMin": 0.05,
                                                      "speedMax": 0.35, "shape": "Box", "boxExtents": [16.0, 1.2, 16.0],
                                                      "gravityFactor": -0.005, "drag": 0.1, "windFactor": 0.15,
                                                      "sizeStart": 0.14, "sizeEnd": 0.05, "colorStart": [7.0, 12.0, 1.5, 1.0],
                                                      "colorEnd": [2.0, 6.0, 0.6, 0.0], "seed": 11, "simulation": "Gpu"}})

    # --- Lanterns on the benches and a rock ------------------------------------------------------------------------
    lantern = f"{EXTERNAL}/Lantern_01/Lantern_01_1k.gltf"
    for index, (x, z) in enumerate(((-1.1, -2.55), (2.0, 0.6), (-2.6, 2.4))):
        top = terrain_height(x, z) + (0.45 if index < 2 else 0.0)
        name = f"Lantern {index}"
        entity(name, {"illusion.Transform": {"position": [x, round(top, 3), z], "scale": [1.6, 1.6, 1.6]},
                      "illusion.MeshRenderer": {"mesh": f"{lantern}#mesh/0", "material": m["brass"]},
                      "illusion.RigidBody": {"motionType": "Kinematic", "mass": 2.0},
                      "illusion.Collider": {"halfExtents": [0.1, 0.24, 0.08], "offset": [0.0, 0.24, 0.0]}})
        entity(f"{name} Glass", {"illusion.Transform": {}, "illusion.LocalTransform": {},
                                 "illusion.MeshRenderer": {"mesh": f"{lantern}#mesh/1", "material": m["glass"],
                                                           "castShadows": False}}, parent=f"/{name}")
        entity(f"{name} Light", {"illusion.Transform": {}, "illusion.LocalTransform": {"position": [0.0, 0.16, 0.0]},
                                 "illusion.PointLight": {"color": [1.0, 0.7, 0.35], "intensity": 6, "range": 6}},
               parent=f"/{name}")

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

    # Neon bars among the trees, glowing without lights of their own.
    colours = ["cyan", "magenta", "violet"]
    for index, (x, z) in enumerate(scatter(6, CLEARING_RADIUS + 5.0, 24.0, 6.0)):
        tilt = axis_quaternion((0.0, 0.0, 1.0), rng.uniform(-0.25, 0.25))
        entity(f"Neon Bar {index}", {"illusion.Transform": {"position": ground(x, z, 0.1),
                                                            "rotation": r3(quaternion_multiply(yaw_quaternion(rng.uniform(0, 6.28)), tilt))},
                                     "illusion.MeshRenderer": {"mesh": "assets:/models/neon.gltf#mesh/2",
                                                               "material": neon[colours[index % 3]], "castShadows": False}})

    # --- Things to push: glowing orbs ---------------------------------------------------------------------------
    for index, (x, z) in enumerate(((3.5, 3.0), (-3.0, 4.5), (5.0, -2.0), (-4.5, -1.0), (1.5, 5.5))):
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
