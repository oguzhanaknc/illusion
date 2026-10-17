/* Implementation of miniaudio, with Ogg Vorbis decoding from the stb_vorbis copy in miniaudio's extras
   and its reverb node.
   miniaudio enables its Vorbis decoder when the stb_vorbis declarations come first; the stb_vorbis
   implementation must follow the miniaudio implementation. The configuration macros come from the
   illusion::miniaudio target, so every user of the header sees the same structures. */

#define STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>

/* The reverb node (Freeverb by way of verblib, MIT-0 or public domain) from miniaudio's extras. */
#include <extras/nodes/ma_reverb_node/ma_reverb_node.c>

/* Clears the reverb's delay lines, which verblib only does internally. Declared in AudioMixer.cpp. */
void illusion_clear_reverb(ma_reverb_node* node);
void illusion_clear_reverb(ma_reverb_node* node)
{
    verblib_mute(&node->reverb);
}
