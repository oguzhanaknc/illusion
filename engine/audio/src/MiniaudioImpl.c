/* Implementation of miniaudio, with Ogg Vorbis decoding from the stb_vorbis copy in miniaudio's extras.
   miniaudio enables its Vorbis decoder when the stb_vorbis declarations come first; the stb_vorbis
   implementation must follow the miniaudio implementation. The configuration macros come from the
   illusion::miniaudio target, so every user of the header sees the same structures. */

#define STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>
