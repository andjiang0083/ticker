// TICKER — Cardputer 入口
#include <M5Cardputer.h>
#include <Preferences.h>
#include "game.h"
#include "render.h"
#include "audio.h"

static M5Canvas  canvas(&M5.Display);
static Game      g;
static uint16_t* fb = nullptr;
static Preferences prefs;

struct Save {
  uint32_t magic;
  uint16_t ver;
  uint16_t crc;
  uint8_t  cn;
  uint8_t  stockIdx;
  uint8_t  sessionCandle;
  uint8_t  posStep;
  uint8_t  histDays;
  uint8_t  unlocked;
  uint8_t  taint[13];
  float    dayPx[12];
  uint8_t  pad;
  int32_t  coins;
  int32_t  best;
  uint32_t rounds;
  uint16_t dex[AS_COUNT];
  uint16_t evDex[6];
  float    px[12];
  float    qty, avgCost;
  float    histEq[24], histIdx[24];
};
static const uint32_t SAVE_MAGIC = 0x44545244u;   // "DTRD"
#define SAVE_VER 7

// CRC16-CCITT：掉电写坏就读不出来，整份丢弃（比读到半截数据安全）
static uint16_t crc16(const uint8_t* p, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= (uint16_t)p[i] << 8;
    for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}
static bool sDirty = false;
static uint32_t sLastSave = 0;

#define FRAME_MS 33

static uint32_t keyMask() {
  uint32_t m = 0;
  auto ks = M5Cardputer.Keyboard.keysState();
  for (auto k : ks.word) {
    if (k == 'b' || k == 'B') m |= 1u << 0;      // 买
    if (k == 's' || k == 'S') m |= 1u << 1;      // 卖
    if (k == 'm' || k == 'M') m |= 1u << 2;      // 静音
    if (k == 'h' || k == 'H') m |= 1u << 3;      // 帮助
    if (k == 'l' || k == 'L') m |= 1u << 4;      // 图鉴（tab 的同义键）
    if (k == 'c' || k == 'C') m |= 1u << 6;      // 红绿互换
    if (k == 'a' || k == 'A') m |= 1u << 7;      // 战绩册上一页
    if (k == 'd' || k == 'D') m |= 1u << 8;      // 战绩册下一页
    if (k == ' ' || k == 0x20) m |= 1u << 5;     // 空格=买
  }
  if (ks.space) m |= 1u << 5;                    // 空格=买（同义键）
  if (ks.enter) m |= 1u << 4;
  return m;
}

static uint32_t sPrev = 0;

// 启动画面 / 省电
#define IDLE_SLEEP_MS 120000u
static bool     sBooted    = false;
static bool     sPowerSave = false;
static bool     sResumed   = false;
static uint32_t sLastInput = 0;
static uint32_t sLastFrame = 0;

void setup() {
  auto cfg = M5.config();
  cfg.fallback_board = m5::board_t::board_M5Cardputer;
  M5Cardputer.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(110);

  canvas.setColorDepth(16);
  if (!canvas.createSprite(SCREEN_W, SCREEN_H)) {
    M5.Display.fillScreen(TFT_RED);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.drawString("sprite alloc fail", 10, 60);
    for (;;) delay(1000);
  }
  fb = (uint16_t*)canvas.getBuffer();
  audioInit();

  prefs.begin("ticker", false);
  Save s{};
  size_t n = prefs.getBytesLength("dt_save");
  bool ok = (n == sizeof(s)) && prefs.getBytes("dt_save", &s, n) == n &&
            s.magic == SAVE_MAGIC && s.ver == SAVE_VER;
  if (ok) {
    uint16_t want = s.crc; s.crc = 0;
    if (crc16((const uint8_t*)&s, sizeof(s)) != want) ok = false;   // 校验不过就当没存档
  }
  gameInit(g);
  sResumed = ok && s.qty > 0.f;
  if (ok) {
    g.coins = s.coins; g.bestCoins = s.best; g.rounds = s.rounds; g.cnColors = s.cn;
    for (int i = 0; i < AS_COUNT; i++) g.dex[i] = s.dex[i];
    for (int i = 0; i < 6; i++) g.evDex[i] = s.evDex[i];
    for (int i = 0; i < 12; i++) g.pxCoin[i] = s.px[i];
    g.histDays = s.histDays;
    g.unlocked = s.unlocked;
    for (int i = 0; i < 13; i++) g.taint[i] = s.taint[i];
    for (int i = 0; i < 12; i++) g.dayPx[i] = s.dayPx[i];
    for (int i = 0; i < 24; i++) { g.histEq[i] = s.histEq[i]; g.histIdx[i] = s.histIdx[i]; }
    if (s.qty > 0.f) {                       // 方案 A：仓位还活着 → 接着这只票
      g.qty = s.qty; g.avgCost = s.avgCost; g.posStep = s.posStep;
      gameResume(g, s.stockIdx, s.sessionCandle);
    }
  }
  sDirty = !ok;
  sLastSave = millis();
  sLastInput = millis();
  sLastFrame = millis();
}

// 把画布推上屏（sprite 是 swap565，要翻字节序）
static void present() {
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    uint16_t v = fb[i]; fb[i] = (uint16_t)((v >> 8) | (v << 8));
  }
  canvas.pushSprite(0, 0);
}

static void saveNow(uint32_t now, bool urgent) {
  if (!sDirty || (!urgent && now - sLastSave <= 8000)) return;
  Save sv{};
  sv.magic = SAVE_MAGIC; sv.ver = SAVE_VER;
  sv.cn = g.cnColors; sv.stockIdx = g.stockIdx;
  sv.sessionCandle = g.sessionCandle; sv.posStep = g.posStep;
  sv.histDays = g.histDays;
  sv.unlocked = g.unlocked;
  for (int i = 0; i < 13; i++) sv.taint[i] = g.taint[i];
  for (int i = 0; i < 12; i++) sv.dayPx[i] = g.dayPx[i];
  sv.coins = g.coins; sv.best = g.bestCoins; sv.rounds = g.rounds;
  for (int i = 0; i < AS_COUNT; i++) sv.dex[i] = g.dex[i];
  for (int i = 0; i < 6; i++) sv.evDex[i] = g.evDex[i];
  for (int i = 0; i < 12; i++) sv.px[i] = g.pxCoin[i];
  sv.qty = g.qty; sv.avgCost = g.avgCost;
  for (int i = 0; i < 24; i++) { sv.histEq[i] = g.histEq[i]; sv.histIdx[i] = g.histIdx[i]; }
  sv.crc = 0;
  sv.crc = crc16((const uint8_t*)&sv, sizeof(sv));
  prefs.putBytes("dt_save", &sv, sizeof(sv));
  sDirty = false;
  sLastSave = now;
}

// 省电：关灯 + 关面板 + 降频，只留键盘轮询
static void powerEnter() {
  saveNow(millis(), true);                 // 睡前把状态写干净
  M5.Speaker.stop();
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  setCpuFrequencyMhz(40);
  sPowerSave = true;
}
static void powerWake(uint32_t now) {
  setCpuFrequencyMhz(240);                 // 先升频再画，否则唤醒头几帧掉帧
  M5.Display.wakeup();
  M5.Display.setBrightness(110);
  audioSetMute(audioMuted());
  sLastFrame = now;
  sLastInput = now;
  sPowerSave = false;
  sPrev = keyMask();                       // 唤醒这一下不触发交易
}

void loop() {
  M5Cardputer.update();
  uint32_t now = millis();

  uint32_t m = keyMask();
  if (m) sLastInput = now;

  if (sPowerSave) {                        // 睡着：只轮询键盘
    if (m) powerWake(now);
    else { delay(60); return; }
  }
  if (now - sLastInput > IDLE_SLEEP_MS) { powerEnter(); return; }

  if (now - sLastFrame < FRAME_MS) return;
  float dt = (now - sLastFrame) * 0.001f;
  if (dt > 0.12f) dt = 0.12f;
  sLastFrame = now;

  uint32_t edge = m & ~sPrev;
  sPrev = m;

  if (!sBooted) {                          // 启动画面：任意键进入，且这一下不触发交易
    if (m) { sBooted = true; edge = 0; audioSetMute(audioMuted()); }
    else {
      renderTitle(fb, g, now * 0.001f, sResumed);
      present();
      return;
    }
  }

  if (edge & (1u << 2)) audioSetMute(!audioMuted());
  if (edge & (1u << 6)) { g.cnColors = g.cnColors ? 0 : 1; sDirty = true; }
  if (edge & (1u << 3)) { g.helpOpen = g.helpOpen ? 0 : 1; g.dexOpen = 0; }
  if (edge & (1u << 4)) { g.dexOpen = g.dexOpen ? 0 : 1; g.helpOpen = 0; }
  if (g.dexOpen && (edge & (1u << 7))) g.bookPage = (uint8_t)((g.bookPage + 2) % 3);
  if (g.dexOpen && (edge & (1u << 8))) g.bookPage = (uint8_t)((g.bookPage + 1) % 3);

  // 每根蜡烛收盘 / 跨日 → 标脏（价格和战绩要落盘）
  static uint8_t  lastCandle = 255;
  static uint32_t lastRounds = 0xFFFFFFFFu;
  if (g.sessionCandle != lastCandle || g.rounds != lastRounds) {
    lastCandle = g.sessionCandle; lastRounds = g.rounds;
    sDirty = true;
  }

  Input in{};
  if (!g.dexOpen && !g.helpOpen) {
    in.buy  = (edge & ((1u << 0) | (1u << 5))) != 0;
    in.sell = (edge & (1u << 1)) != 0;
    gameUpdate(g, in, dt);
  }

  uint8_t s;
  while (gamePopSfx(g, s)) {
    if (s != SFX_TICK_UP && s != SFX_TICK_DN && s != SFX_TICK_BIG) sDirty = true;
    audioPlay(s);
  }
  audioUpdate(now, g.evKind == 0 && g.phase == PH_TRADE && !g.dexOpen && !g.helpOpen);

  if (g.dexOpen)      renderBook(fb, g, now * 0.001f);
  else if (g.helpOpen) renderHelp(fb, g, now * 0.001f);
  else                 renderFrame(fb, g, now * 0.001f);

  present();

  // 写盘策略：交易/换日立刻写，其余（价格、曲线）合并到 8 秒一次
  bool urgent = (edge & ((1u << 0) | (1u << 1) | (1u << 5))) != 0;
  saveNow(now, urgent);
}
