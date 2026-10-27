// 自动生成 by tools/gen_assets.py — 与 mockup 定稿同色，勿手改
#pragma once
#include <stdint.h>

static const uint16_t PAL_INK = 0x0842;
static const uint16_t PAL_BG0 = 0x10C6;
static const uint16_t PAL_BG1 = 0x1928;
static const uint16_t PAL_BG2 = 0x21AB;
static const uint16_t PAL_GRID = 0x2A0D;
static const uint16_t PAL_SEA0 = 0x1B4F;
static const uint16_t PAL_SEA1 = 0x2C94;
static const uint16_t PAL_SEA2 = 0x5619;
static const uint16_t PAL_SEA3 = 0xA75D;
static const uint16_t PAL_UP_D = 0x1BC7;
static const uint16_t PAL_UP = 0x5E4B;
static const uint16_t PAL_UP_HI = 0xAF4F;
static const uint16_t PAL_DN_D = 0xA904;
static const uint16_t PAL_DN = 0xF286;
static const uint16_t PAL_DN_HI = 0xFC8C;
static const uint16_t PAL_GOLD_D = 0xA340;
static const uint16_t PAL_GOLD = 0xFDC0;
static const uint16_t PAL_GOLD_HI = 0xFF0B;
static const uint16_t PAL_UI_D = 0x6B91;
static const uint16_t PAL_UI = 0xB5D9;
static const uint16_t PAL_UI_HI = 0xFFDF;
static const uint16_t PAL_WHALE_D = 0x3315;
static const uint16_t PAL_WHALE = 0x4C19;
static const uint16_t PAL_WHALE_L = 0x961D;
static const uint16_t PAL_SPOUT = 0xA75D;
static const uint16_t PAL_WARM0 = 0x30C1;
static const uint16_t PAL_WARM1 = 0x4922;
static const uint16_t PAL_WARM2 = 0x61A3;
static const uint16_t PAL_WARM_GRID = 0x8284;
static const uint16_t PAL_CRY0 = 0x1845;
static const uint16_t PAL_CRY1 = 0x2888;
static const uint16_t PAL_CRY2 = 0x38CB;
static const uint16_t PAL_CRY_GRID = 0x492F;

static inline uint16_t PAL_UP_BY(bool up){ return up ? PAL_UP : PAL_DN; }
