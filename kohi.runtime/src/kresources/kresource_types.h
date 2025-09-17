#pragma once

#include <containers/array.h>
#include <math/math_types.h>
#include <strings/kname.h>

/**
 * @brief Represents a texture to be used for rendering purposes,
 * stored on the GPU (VRAM)
 */
typedef u16 ktexture;

// The id representing an invalid texture.
#define INVALID_KTEXTURE INVALID_ID_U16

/**
 * @brief Represents a single static mesh, which contains geometry.
 */
typedef u16 kstatic_mesh;

#define INVALID_KSTATIC_MESH INVALID_ID_U16

/**
 * @brief Represents a single skinned mesh, which contains geometry.
 */
typedef u16 kskinned_mesh;

#define INVALID_KSKINNED_MESH INVALID_ID_U16

typedef struct font_glyph {
    i32 codepoint;
    u16 x;
    u16 y;
    u16 width;
    u16 height;
    i16 x_offset;
    i16 y_offset;
    i16 x_advance;
    u8 page_id;
} font_glyph;

typedef struct font_kerning {
    i32 codepoint_0;
    i32 codepoint_1;
    i16 amount;
} font_kerning;

typedef struct font_page {
    kname image_asset_name;
} font_page;

ARRAY_TYPE(font_glyph);
ARRAY_TYPE(font_kerning);
ARRAY_TYPE(font_page);

/**
 * Represents a Kohi Audio.
 */
typedef u16 kaudio;

// The id representing an invalid kaudio.
#define INVALID_KAUDIO INVALID_ID_U16
