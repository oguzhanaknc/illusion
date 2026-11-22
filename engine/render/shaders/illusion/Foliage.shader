// The foliage material, builtin:/shader/foliage (ADR 0063): the standard material in the TwoSidedFoliage shading
// model. Materials of the standard shader with diffuse transmission draw with it: leaves, grass and other thin
// surfaces that let part of the light through to their other side.

#define ILLUSION_STANDARD_SHADING_MODEL "TwoSidedFoliage"
#include "illusion/StandardMaterial.slang"
