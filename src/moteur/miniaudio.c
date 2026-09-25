/* The implementation of miniaudio (https://miniaud.io), with OGG Vorbis through stb_vorbis: the
   pattern miniaudio documents (stb_vorbis's declarations first, its implementation after). Built as
   C, in a library of its own without the project's warnings (third-party code). */

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
