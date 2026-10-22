#pragma once

/// @file
/// Reflection of the asset descriptions that editors and settings files read and write as JSON: image
/// import settings, procedural skies and materials (see StructRegistry.hpp).

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/Environment.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <array>

namespace illusion
{

/// Reflects ImageImportSettings, SkyDesc, TextureRef and MaterialData as "illusion.<Name>" in
/// registry.structs().
void reflectAssetStructs(ComponentRegistry& registry = ComponentRegistry::instance());

} // namespace illusion

/// Names of AlphaMode values in JSON.
template <>
struct illusion::EnumReflection<illusion::AlphaMode>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"Opaque", 0}, illusion::EnumValue{"Mask", 1},
                                        illusion::EnumValue{"Blend", 2}};
};

/// Names of TextureWrap values in JSON.
template <>
struct illusion::EnumReflection<illusion::TextureWrap>
{
    /// The values.
    static constexpr std::array kValues{illusion::EnumValue{"Repeat", 0},
                                        illusion::EnumValue{"ClampToEdge", 1},
                                        illusion::EnumValue{"MirrorRepeat", 2}};
};
