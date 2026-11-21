#pragma once

// "game:/walker.gltf" and the graphs over it, shared by the animation runtime and socket tests.

#include <illusion/core/Base.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::test::walker
{

inline constexpr std::string_view kSkeleton = "game:/walker.gltf#skin/0";
inline constexpr std::string_view kIdle = "game:/walker.gltf#animation/idle";
inline constexpr std::string_view kWalk = "game:/walker.gltf#animation/walk";
inline constexpr std::string_view kGraph = "game:/walker.animgraph";
inline constexpr std::string_view kRootGraph = "game:/striding.animgraph";
inline constexpr f32 kHalfTurn = std::numbers::pi_v<f32> / 4.0f;

// Every state and transition kind the tests need: "idle" (event "tick" at 0.5) moves to "move" (a blend of
// idle and walk by "speed", event "step" at 0.25) when speed > 0.5; any state goes to "spin" on "go"; "spin"
// returns to "idle" when "armed" and "count" > 2.
inline constexpr std::string_view kGraphText = R"({"format": "illusion.animgraph", "version": 1,
  "parameters": [{"name": "speed", "type": "Float"}, {"name": "count", "type": "Int"},
                 {"name": "armed", "type": "Bool"}, {"name": "go", "type": "Trigger"}],
  "layers": [{"name": "base", "entry": "idle", "states": [
      {"name": "idle", "clip": "game:/walker.gltf#animation/idle", "events": [{"name": "tick", "time": 0.5}]},
      {"name": "move", "blend1d": {"parameter": "speed", "points": [
          {"value": 0, "clip": "game:/walker.gltf#animation/idle"},
          {"value": 1, "clip": "game:/walker.gltf#animation/walk"}]},
       "events": [{"name": "step", "time": 0.25}]},
      {"name": "spin", "clip": "game:/walker.gltf#animation/turn"}],
    "transitions": [
      {"from": "idle", "to": "move", "conditions": [{"parameter": "speed", "compare": "Greater", "value": 0.5}]},
      {"from": "*", "to": "spin", "duration": 0.2, "conditions": [{"parameter": "go"}]},
      {"from": "spin", "to": "idle", "conditions": [{"parameter": "armed", "compare": "Equal", "value": true},
                                                    {"parameter": "count", "compare": "Greater", "value": 2}]}]}]})";

// Root motion from "hips": "walk" (2 m/s along +z) or, after "turn", "turn" (90 degrees per second about +y).
inline constexpr std::string_view kRootGraphText = R"({"format": "illusion.animgraph", "version": 1,
  "parameters": [{"name": "turn", "type": "Trigger"}], "rootMotion": "hips",
  "layers": [{"name": "base", "entry": "walk", "states": [
      {"name": "walk", "clip": "game:/walker.gltf#animation/walk"},
      {"name": "turn", "clip": "game:/walker.gltf#animation/turn"}],
    "transitions": [{"from": "walk", "to": "turn", "conditions": [{"parameter": "turn"}]}]}]})";

// Writes "game:/walker.gltf": the joints "root" and "hips" (one metre up) and one-second clips "idle" (hips
// still), "walk" (hips 2 m along +z), "turn" (hips 90 degrees about +y) and "wave" (root about z).
inline void writeWalker(VirtualFileSystem& files)
{
    const std::vector<f32> data{0.0f,
                                1.0f, // times
                                0.0f,
                                1.0f,
                                0.0f,
                                0.0f,
                                1.0f,
                                2.0f, // walk
                                0.0f,
                                1.0f,
                                0.0f,
                                0.0f,
                                1.0f,
                                0.0f, // idle
                                0.0f,
                                0.0f,
                                0.0f,
                                1.0f,
                                0.0f,
                                std::sin(kHalfTurn),
                                0.0f,
                                std::cos(kHalfTurn), // turn
                                0.0f,
                                0.0f,
                                0.0f,
                                1.0f,
                                0.0f,
                                0.0f,
                                std::sin(0.25f),
                                std::cos(0.25f)}; // wave
    std::vector<std::byte> bytes(data.size() * sizeof(f32));
    std::memcpy(bytes.data(), data.data(), bytes.size());
    REQUIRE(files.writeFile("game:/walker.bin", bytes) == FileError::None);
    const auto animation = [](std::string_view name, u32 output, u32 node, std::string_view path) {
        return std::string(R"({"name": ")") + std::string(name) +
               R"(", "samplers": [{"input": 0, "output": )" + std::to_string(output) +
               R"(, "interpolation": "LINEAR"}], "channels": [{"sampler": 0, "target": )" + R"({"node": )" +
               std::to_string(node) + R"(, "path": ")" + std::string(path) + R"("}}]})";
    };
    const std::string gltf =
        R"({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
"nodes": [{"name": "root", "children": [1]}, {"name": "hips", "translation": [0, 1, 0]}],
"skins": [{"joints": [0, 1]}], "animations": [)" +
        animation("walk", 1, 1, "translation") + ", " + animation("idle", 2, 1, "translation") + ", " +
        animation("turn", 3, 1, "rotation") + ", " + animation("wave", 4, 0, "rotation") +
        R"(], "buffers": [{"uri": "walker.bin", "byteLength": 120}],
"bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 8}, {"buffer": 0, "byteOffset": 8, "byteLength": 24},
{"buffer": 0, "byteOffset": 32, "byteLength": 24}, {"buffer": 0, "byteOffset": 56, "byteLength": 32},
{"buffer": 0, "byteOffset": 88, "byteLength": 32}],
"accessors": [{"bufferView": 0, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [1]},
{"bufferView": 1, "componentType": 5126, "count": 2, "type": "VEC3"},
{"bufferView": 2, "componentType": 5126, "count": 2, "type": "VEC3"},
{"bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC4"},
{"bufferView": 4, "componentType": 5126, "count": 2, "type": "VEC4"}]})";
    REQUIRE(files.writeText("game:/walker.gltf", gltf) == FileError::None);
    REQUIRE(files.writeText(kGraph, kGraphText) == FileError::None);
    REQUIRE(files.writeText(kRootGraph, kRootGraphText) == FileError::None);
}

} // namespace illusion::test::walker
