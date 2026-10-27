#pragma once
#include <stdint.h>
#include "game.h"
void audioInit();
void audioPlay(uint8_t id);
void audioUpdate(uint32_t nowMs, bool bgmOn);
void audioSetMute(bool m);
bool audioMuted();
