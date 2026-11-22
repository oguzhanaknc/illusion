// The standard material, builtin:/shader/standard (ADR 0063): glTF 2.0 metallic-roughness PBR and the scene's
// wind, written against the surface API like any material shader. Materials with diffuse transmission draw with
// builtin:/shader/foliage, the same surface in the TwoSidedFoliage shading model.

#define ILLUSION_STANDARD_SHADING_MODEL "Default"
#include "illusion/StandardMaterial.slang"
