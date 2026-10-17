// Implementations of the single-header libraries used by the assets module. Their headers are included
// as system headers, so their code is not subject to the engine's warning flags.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO // everything is decoded from memory (files come through the VirtualFileSystem)
#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
