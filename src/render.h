// DESK TRADER — 渲染（无平台依赖，sim 与固件共用）
#pragma once
#include <stdint.h>
#include "game.h"

void renderFrame(uint16_t* fb, const Game& g, float t);
void renderBook(uint16_t* fb, const Game& g, float t);
void renderHelp(uint16_t* fb, const Game& g, float t);
void renderTitle(uint16_t* fb, const Game& g, float t, bool resumed);
