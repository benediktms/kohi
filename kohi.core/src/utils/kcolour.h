#pragma once

#include "math/math_types.h"

typedef vec3 colour3;
typedef vec4 colour4;

typedef vec3i colour3i;
typedef vec4i colour4i;

// clang-format off
#define KCOLOUR4_WHITE       (colour4){1.0f, 1.0f, 1.0f, 1.0f}
#define KCOLOUR4_WHITE_75    (colour4){1.0f, 1.0f, 1.0f, 0.75f}
#define KCOLOUR4_WHITE_50    (colour4){1.0f, 1.0f, 1.0f, 0.5f}
#define KCOLOUR4_WHITE_25    (colour4){1.0f, 1.0f, 1.0f, 0.25f}
#define KCOLOUR4_BLACK       (colour4){0.0f, 0.0f, 0.0f, 1.0f}
#define KCOLOUR4_GRAY        (colour4){0.5f, 0.5f, 0.5f, 1.0f}
#define KCOLOUR4_DARK_GRAY   (colour4){0.25f, 0.25f, 0.25f, 1.0f}
#define KCOLOUR4_LIGHT_GRAY  (colour4){0.75f, 0.75f, 0.75f, 1.0f}
#define KCOLOUR4_RED         (colour4){1.0f, 0.0f, 0.0f, 1.0f}
#define KCOLOUR4_GREEN       (colour4){0.0f, 1.0f, 0.0f, 1.0f}
#define KCOLOUR4_BLUE        (colour4){0.0f, 0.0f, 1.0f, 1.0f}
#define KCOLOUR4_YELLOW      (colour4){1.0f, 1.0f, 0.0f, 1.0f}
#define KCOLOUR4_CYAN        (colour4){0.0f, 1.0f, 1.0f, 1.0f}
#define KCOLOUR4_MAGENTA     (colour4){1.0f, 0.0f, 1.0f, 1.0f}
#define KCOLOUR4_ORANGE      (colour4){1.0f, 0.5f, 0.0f, 1.0f}
#define KCOLOUR4_PURPLE      (colour4){0.5f, 0.0f, 0.5f, 1.0f}
#define KCOLOUR4_PINK        (colour4){1.0f, 0.75f, 0.8f, 1.0f}
#define KCOLOUR4_BROWN       (colour4){0.6f, 0.4f, 0.2f, 1.0f}
#define KCOLOUR4_TRANSPARENT (colour4){0.0f, 0.0f, 0.0f, 0.0f}

#define KCOLOUR3_WHITE       (colour3){1.0f, 1.0f, 1.0f}
#define KCOLOUR3_BLACK       (colour3){0.0f, 0.0f, 0.0f}
#define KCOLOUR3_GRAY        (colour3){0.5f, 0.5f, 0.5f}
#define KCOLOUR3_DARK_GRAY   (colour3){0.25f, 0.25f, 0.25f}
#define KCOLOUR3_LIGHT_GRAY  (colour3){0.75f, 0.75f, 0.75f}
#define KCOLOUR3_RED         (colour3){1.0f, 0.0f, 0.0f}
#define KCOLOUR3_GREEN       (colour3){0.0f, 1.0f, 0.0f}
#define KCOLOUR3_BLUE        (colour3){0.0f, 0.0f, 1.0f}
#define KCOLOUR3_YELLOW      (colour3){1.0f, 1.0f, 0.0f}
#define KCOLOUR3_CYAN        (colour3){0.0f, 1.0f, 1.0f}
#define KCOLOUR3_MAGENTA     (colour3){1.0f, 0.0f, 1.0f}
#define KCOLOUR3_ORANGE      (colour3){1.0f, 0.5f, 0.0f}
#define KCOLOUR3_PURPLE      (colour3){0.5f, 0.0f, 0.5f}
#define KCOLOUR3_PINK        (colour3){1.0f, 0.75f, 0.8f}
#define KCOLOUR3_BROWN       (colour3){0.6f, 0.4f, 0.2f}
#define KCOLOUR3_TRANSPARENT (colou34){0.0f, 0.0f, 0.0f}
// clang-format on
