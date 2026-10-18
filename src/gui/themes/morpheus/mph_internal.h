#pragma once

// Between the primitives' own files only; screens use mph.h.

#include "mph.h"

// mph_draw.c
uint32_t mph_tint(uint32_t c);  // c with the transform's opacity
float mph_transform_scale(void);
// One textured quad into the batch, run by (texture, tint)
void mph_quad_textured(const vita2d_texture *t, uint32_t tint, float x0, float y0, float x1, float y1, float u0,
                       float v0, float u1, float v1);

// mph_text.c
void mph_text_init(void);
void mph_text_fini(void);
void mph_text_frame_begin(void);

// mph_anim.c
void mph_anim_init(void);

// mph_perf.c
void mph_perf_begin(void);
void mph_perf_end(bool hud);
