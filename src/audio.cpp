// TICKER — 音频：tone() + 单轨 SFX + 主循环驱动 + 看门狗
// 硬约束（实测）：单音 ≥45ms、每次 tone 前 stop()、音区 ≥370Hz、不用 PCM、音量 80
#include "audio.h"
#include <M5Cardputer.h>

struct Seq { uint16_t f[8]; uint8_t n; uint8_t ms; };

// 与 tools/audio_proto.py 的频率表逐条一致（WAV 试听稿 = 固件同一张表）
static const Seq TONES[] = {
  { {0},                                    0,  0  },  // NONE
  { {3136},                                 1, 45  },  // TICK_UP  价格上跳
  { {2349},                                 1, 45  },  // TICK_DN  价格下跳
  { {3136, 2637},                           2, 45  },  // TICK_BIG 插针大跳
  { {1568, 2093},                           2, 55  },  // BUY      买入
  { {2093, 2637, 3136},                     3, 50  },  // SELL_WIN 卖出盈利
  { {880, 698, 587},                        3, 60  },  // SELL_LOSS卖出亏损
  { {1760, 1320, 1760},                     3, 55  },  // SETTLE   结算
  { {1400, 1900, 1400, 1900, 1046},         5, 60  },  // WHALE    鲸鱼警报
  { {1046, 1318, 1568, 2093},               4, 60  },  // RESCUE   救济补给
  { {494},                                  1, 90  },  // DENY     无效/钱不够
  { {1568, 2093, 2637, 3136, 2637, 3136},   6, 45  },  // DEX      图鉴解锁
  { {440, 554, 440, 554, 440},               5, 90  },  // RHINO    灰犀牛：低沉碾压
  { {3136, 2093, 1568, 1046},                4, 60  },  // SWAN     黑天鹅：尖锐坠落
  { {784, 784, 784, 784},                    4, 70  },  // ER_OPEN  财报鼓点
  { {1568, 2093, 2637, 3136},                4, 55  },  // ER_BEAT  大吉
  { {880, 698, 587, 494},                    4, 65  },  // ER_MISS  大凶
};
static const int TONE_COUNT = (int)(sizeof(TONES)/sizeof(TONES[0]));


// ── BGM：D 小调 8 小节，旋律 + 三音琶音交替，三遍变奏只换重心（≈17.2s 一圈）
// 音区全部 392~880Hz（1W 小腔体推得动的音乐盒音区），走 ch1，与音效互不抢占
struct BgmNote { uint16_t f, ms; };
static const BgmNote BGM[] = {
  // 遍 1：旋律(280) → 琶音×3(140)
  {880,280},{587,140},{698,140},{880,140},{698,280},{466,140},{587,140},{698,140},
  {880,280},{523,140},{698,140},{880,140},{784,280},{523,140},{659,140},{784,140},
  {698,280},{587,140},{698,140},{880,140},{587,280},{392,140},{466,140},{587,140},
  {659,280},{466,140},{587,140},{698,140},{880,280},{440,140},{554,140},{659,140},
  // 遍 2：琶音先走，旋律接在后面（追逐感）
  {587,140},{698,140},{880,140},{880,280},{466,140},{587,140},{698,140},{698,280},
  {523,140},{698,140},{880,140},{880,280},{523,140},{659,140},{784,140},{784,280},
  {587,140},{698,140},{880,140},{698,280},{392,140},{466,140},{587,140},{587,280},
  {466,140},{587,140},{698,140},{659,280},{440,140},{554,140},{659,140},{880,280},
  // 遍 3：旋律拉长(300)、琶音收短(130)，退潮感
  {880,300},{587,130},{698,130},{880,130},{698,300},{466,130},{587,130},{698,130},
  {880,300},{523,130},{698,130},{880,130},{784,300},{523,130},{659,130},{784,130},
  {698,300},{587,130},{698,130},{880,130},{587,300},{392,130},{466,130},{587,130},
  {659,300},{466,130},{587,130},{698,130},{880,300},{440,130},{554,130},{659,130},
};
static const int BGM_COUNT = (int)(sizeof(BGM)/sizeof(BGM[0]));
static uint8_t  sBgmIdx = 0;
static uint32_t sBgmNext = 0;
static bool     sBgmWasOn = false;

static bool     sMute = false;
static uint8_t  sCur = 0;
static uint8_t  sIdx = 0;
static uint32_t sNext = 0;
static uint32_t sLastOk = 0;

static bool isHighPrio(uint8_t id) {
  return id == SFX_BUY || id == SFX_SELL_WIN || id == SFX_SELL_LOSS ||
         id == SFX_WHALE || id == SFX_SETTLE || id == SFX_RESCUE ||
         id == SFX_RHINO || id == SFX_SWAN || id == SFX_ER_OPEN ||
         id == SFX_ER_BEAT || id == SFX_ER_MISS;
}

void audioInit() {
  M5.Speaker.begin();
  M5.Speaker.setVolume(65);
  sLastOk = millis();
}

void audioPlay(uint8_t id) {
  if (sMute || id == 0 || id >= TONE_COUNT) return;
  if (isHighPrio(id) || sCur == 0) { sCur = id; sIdx = 0; sNext = 0; }
}

void audioUpdate(uint32_t nowMs, bool bgmOn) {
  // 看门狗：ES8311 会被高频重配拖死，超 6s 没成功发声就整段重启
  if ((uint32_t)(nowMs - sLastOk) > 6000) {
    M5.Speaker.end(); delay(15); M5.Speaker.begin();
    M5.Speaker.setVolume(65);
    sLastOk = nowMs; sCur = 0;
    return;
  }
  if (sMute) return;
  // BGM 轨（ch1）：鲸鱼/结算时让位
  if (bgmOn) {
    if (!sBgmWasOn) { sBgmWasOn = true; sBgmNext = nowMs; }
    if (nowMs >= sBgmNext) {
      const BgmNote& n = BGM[sBgmIdx];
      if (n.f) {
        M5.Speaker.stop(1);
        M5.Speaker.tone(n.f, n.ms, 1);
        sLastOk = nowMs;
      }
      sBgmNext = nowMs + n.ms + 5;
      sBgmIdx = (uint8_t)((sBgmIdx + 1) % BGM_COUNT);
    }
  } else sBgmWasOn = false;

  if (!sCur) return;
  if (nowMs < sNext) return;
  const Seq& s = TONES[sCur];
  if (sIdx < s.n) {
    M5.Speaker.stop(0);
    M5.Speaker.tone(s.f[sIdx], s.ms, 0);
    sLastOk = nowMs;
    sNext = nowMs + s.ms + 5;
    sIdx++;
  } else sCur = 0;
}

void audioSetMute(bool m) { sMute = m; if (m) M5.Speaker.stop(); }
bool audioMuted() { return sMute; }
