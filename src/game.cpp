// TICKER — 游戏核心（无平台依赖）
#include "game.h"

static void calcIndex(Game& g);

// 性格 → 当日倍率（隐藏，靠玩出来）
void gameTraitsFor(Game& g, uint8_t idx) {
  uint8_t tr = stockTrait(idx);
  g.evMul      = (tr & T_WILD) ? 1.35f : ((tr & T_COLD) ? 0.65f : 1.f);
  g.shockMul   = (tr & T_RESIST) ? 0.55f : 1.f;
  g.whaleMul   = (tr & T_BAIT) ? 1.6f : 1.f;
  g.scandalMul = (tr & T_LIAR) ? 3.0f : 1.f;
}
static float equityOf(const Game& g) { return (g.coins + g.price * 100.f * g.qty) / 100.f; }

// 异象剧本：方向不预设，严格随机。看到异象就买 / 就割，都是在赌
struct EvScript { int8_t shock; float shockSlope; int8_t tail; float tailSlope; };
// 鲸鱼（大资金进出）
static const EvScript S_WHALE[] = {
  { 1, -0.030f, 2, +0.020f },   // 吸筹：插针后拉回
  { 2, +0.015f, 3, -0.018f },   // 出货：先拉后砸
  { 1, -0.045f, 4, -0.002f },   // 假动作：砸完不回来
};
// 灰犀牛（已知风险）
static const EvScript S_RHINO[] = {
  { 3, -0.022f, 4, -0.006f },   // 一路碾压
  { 2, -0.010f, 3,  0.000f },   // 利空已消化：白紧张
  { 2, -0.030f, 3, +0.016f },   // 靴子落地反弹
};
// 黑天鹅
static const EvScript S_SWAN[] = {
  { 3, -0.030f, 5, -0.010f },   // 崩塌后继续阴跌
  { 2, -0.055f, 3, +0.022f },   // 砸完 V 型急拉（恐慌割肉被埋）
  { 4, -0.012f, 4, -0.008f },   // 慢刀子：不吓人但一直流血
};
// 瀑布（加密）
static const EvScript S_WATER[] = {
  { 2, -0.075f, 3, -0.015f },   // 连环瀑布
  { 1, -0.150f, 3, +0.045f },   // 插针清算后暴力反弹
  { 4, -0.040f, 2, -0.010f },   // 凌迟
};
// 升天（加密）
static const EvScript S_PUMP[] = {
  { 2, +0.070f, 3, -0.030f },   // 冲高后瀑布：追高被埋
  { 3, +0.055f, 2, +0.010f },   // 一路拉
  { 1, +0.060f, 3, -0.020f },   // 假突破
};
#include <string.h>
#include <math.h>

static void updateSysPow(Game& g);   // 定义在下方，trySell 先用

static inline uint32_t rnd32(Game& g) {
  g.rng ^= g.rng << 13; g.rng ^= g.rng >> 17; g.rng ^= g.rng << 5; return g.rng;
}
static inline float rnd01(Game& g) { return (float)(rnd32(g) & 0xFFFF) / 65535.0f; }

float assetVol(uint8_t a) {
  if (a == AS_CRYPTO) return 3.6f;
  switch (a) { case AS_BANK: return 0.6f; case AS_CONS: return 1.0f; default: return 2.2f; }
}
void assetRange(uint8_t a, float& lo, float& hi) {
  switch (a) {
    case AS_BANK: lo = 96.f; hi = 104.f; break;
    case AS_CONS: lo = 92.f; hi = 108.f; break;
    default:      lo = 84.f; hi = 116.f; break;
  }
}
// 9 只合成标的：代码 + 基准价 + 性格（性格决定波动权值）
struct StockDef { const char* code; float base; uint8_t asset; uint8_t trait; };
static const StockDef STOCKS[NSTOCK] = {
  // 代码    基准价     类别        隐藏性格
  { "BNK", 128.50f, AS_BANK,   T_RESIST },              // 大银行：抗跌，跌不动
  { "AGR",  74.80f, AS_BANK,   T_RESIST | T_COLD },     // 农业：抗跌但没人气
  { "UTL",  52.40f, AS_BANK,   T_RESIST },              // 公用事业：最呆
  { "CNS",  46.20f, AS_CONS,   T_COLD },                // 消费：闷
  { "MED", 156.30f, AS_CONS,   T_LIAR },                // 医药：财报爱造假
  { "FOD",  88.90f, AS_CONS,   T_COLD },                // 食品：闷
  { "TCH", 312.00f, AS_TECH,   T_WILD },                // 科技龙头：上蹿下跳
  { "NEO",  18.40f, AS_TECH,   T_WILD },                // 新势力：妖
  { "CHP", 211.70f, AS_TECH,   T_BAIT },                // 芯片：鲸鱼常来
  { "BTC", 6420.00f, AS_CRYPTO, T_WILD | T_BAIT },      // 比特币：疯 + 鲸鱼
  { "ETH", 3180.00f, AS_CRYPTO, T_WILD },               // 以太：疯
  { "SHB",  84.20f, AS_CRYPTO, T_COLD | T_BAIT },       // 小币：没量，一砸就崩
  { "GAO",   3.20f, AS_CRYPTO, T_WILD | T_LIAR },       // 妖股：集齐异象解锁
};
const char* stockCode(uint8_t idx) { return STOCKS[idx % NSTOCK].code; }
float       stockBase(uint8_t idx) { return STOCKS[idx % NSTOCK].base; }
uint8_t     stockAsset(uint8_t idx) { return STOCKS[idx % NSTOCK].asset; }
uint8_t     stockTrait(uint8_t idx) { return STOCKS[idx % NSTOCK].trait; }

// 抽一只新标的：换代码也换性格，不让连续两天是同一只
static void pickStock(Game& g) {
  uint8_t prev = g.stockIdx, prevA = g.asset;
  for (int tries = 0; tries < 12; tries++) {
    uint8_t i = (uint8_t)(rnd32(g) % NSTOCK);
    if (i == prev) continue;
    if (i == 12 && !g.unlocked) continue;                 // 妖股：集齐异象才出现
    if (g.pxCoin[i] > 0.f && g.pxCoin[i] <= 0.55f) continue;   // 已退市
    if (stockAsset(i) == prevA && tries < 8) continue;
    if (stockAsset(i) != AS_CRYPTO) {                    // 整股买入：一手都买不起就别派给你
      float live = (g.pxCoin[i] > 0.f) ? g.pxCoin[i] : stockBase(i);
      if (live * 1.0025f * 100.f > g.coins * 0.33f) continue;
    }
    g.stockIdx = i; g.asset = stockAsset(i); return;
  }
  g.stockIdx = (uint8_t)((prev + 1) % NSTOCK); g.asset = stockAsset(g.stockIdx);
}

const char* assetName(uint8_t a) {
  switch (a) { case AS_BANK: return "BNK"; case AS_CONS: return "CNS"; default: return "TCH"; }
}

void gamePushSfx(Game& g, uint8_t id) {
  if (g.sfxN < sizeof(g.sfx)) g.sfx[g.sfxN++] = id;
}
bool gamePopSfx(Game& g, uint8_t& id) {
  if (!g.sfxN) return false;
  id = g.sfx[0];
  for (uint8_t i = 1; i < g.sfxN; i++) g.sfx[i-1] = g.sfx[i];
  g.sfxN--;
  return true;
}

int slotOfCandle(const Game& g, uint32_t idx) {
  // 必须用"本交易日内的序号"，不能用全局 rounds：否则新一天一开始就落在最右格
  if (idx > g.sessionCandle) return -1;
  if (g.sessionCandle < SLOTS) return (int)idx;                      // 未填满：左起生长
  int slot = (int)idx - (int)(g.sessionCandle - (SLOTS - 1));        // 满窗：整窗左滚
  return (slot < 0) ? -1 : slot;
}

static void addMark(Game& g, uint8_t buy, float price) {
  if (g.markN >= MAX_MARKS) {            // 挤掉最老的一条
    for (uint8_t i = 1; i < g.markN; i++) g.marks[i-1] = g.marks[i];
    g.markN--;
  }
  g.marks[g.markN].idx   = g.sessionCandle;
  g.marks[g.markN].buy   = buy;
  g.marks[g.markN].price = price;
  g.markN++;
}

static void pruneMarks(Game& g) {   // 只丢已滚出窗口的标记
  uint8_t w = 0;
  for (uint8_t i = 0; i < g.markN; i++)
    if (slotOfCandle(g, g.marks[i].idx) >= 0) g.marks[w++] = g.marks[i];
  g.markN = w;
}

// 开一只新标的（换日 / 跳过加密日都用它）。注意：erDay、scAmp、panic 必须一起重置，
// 否则上一只标的的财报日程和幅度倍数会漏到下一只（曾出现过 4.2 倍幅度的 bug）
static void beginStockDay(Game& g) {
  pickStock(g);
  g.sessionRound = 0;
  gameTraitsFor(g, g.stockIdx);
  g.histN = 0; g.markN = 0;
  g.posStep = 0;
  g.sessionCandle = 0;
  for (int i = 0; i < DAY_CANDLES; i++) g.dayPx[i] = 0.f;
  {   // 活价：第一次见这只标的用基准价，之后接着上次的价格继续（世界是连续的）
    uint8_t si = g.stockIdx % NSTOCK;
    if (g.pxCoin[si] <= 0.f) g.pxCoin[si] = stockBase(g.stockIdx);
    g.dayOpen = g.pxCoin[si];
  }
  g.price   = g.dayOpen;
  g.scenario = (uint8_t)(rnd32(g) % 6);   // 仅作记录，曲线由 ctl[] 决定
  g.panic = 0.f; g.scFrom = 0.f; g.scBase = g.price; g.wander = 0.f;
  g.scar = 0.f; g.evShock = 0; g.evTail = 0;
  g.dayEq[0] = equityOf(g);
  for (int i = 1; i <= 12; i++) g.dayEq[i] = -1.f;
  g.erDay = 0; g.erCandle = 0; g.erEst = 0.f;
  g.erRumor = 0; g.erRumorT = 0.f; g.erRumorKind = 0; g.erRumorTrue = 0;
  g.erResult = ER_NONE; g.erGap = 0.f; g.erVol = 1.f; g.erVolT = 0.f;
  g.liqT = 0.f; g.denyT = 0.f;
  // 每天一条全新的曲线：终点（当日总涨跌）+ 中途随机拐点
  bool cry = (g.asset == AS_CRYPTO);
  g.dayDrift = cry ? (rnd01(g) * 0.70f - 0.35f)      // 加密：-35% .. +35%
                   : (rnd01(g) * 0.36f - 0.04f);      // 股票：-4% .. +32%（世界连续，得留点长期上坡）
  float swing = cry ? (0.14f + rnd01(g) * 0.22f)      // 当日摆动幅度
                    : (0.030f + rnd01(g) * 0.085f);
  for (int i = 0; i < 4; i++) {
    float base = g.dayDrift * ((float)(i + 1) / 4.f);
    g.ctl[i] = base + (rnd01(g) - 0.5f) * swing * (1.f - 0.18f * i);
  }
  if (g.taint[g.stockIdx % NSTOCK] > 0) {              // 财务造假的疤：还没洗干净
    g.dayDrift -= 0.13f;
    for (int i = 0; i < 4; i++) g.ctl[i] -= 0.10f;
  }
  g.ctl[3] = g.dayDrift;                              // 终点固定在当日总涨跌
  {
    float r = rnd01(g);
    if (r < 0.36f) { g.erDay = 1; g.erCandle = (uint8_t)(5 + rnd32(g) % 4); g.erEst = 3.f + rnd01(g) * 12.f; }
    else if (r < 0.45f) { g.erDay = 2; g.erCandle = (uint8_t)(5 + rnd32(g) % 4); g.erEst = 3.f + rnd01(g) * 12.f; }
  }
  g.live.o = g.live.h = g.live.l = g.live.c = g.price;
  g.flashT = 0.18f;
}

void gameNewSession(Game& g) {
  g.dayPnl  = g.dayPeakEquity ? (g.coins - g.dayStartCoins) : 0;   // 供收盘帧显示
  g.dayStartCoins  = g.coins;
  g.dayPeakEquity  = g.coins;
  g.dayPeakPnl     = 0;
  beginStockDay(g);
}

static void startRound(Game& g) {
  g.phase  = PH_TRADE;
  g.phaseT = 0.f;
  g.roundT = 0.f;
  g.tickT  = 0.f;
  g.evKind = 0; g.evT = 0; g.evProg = 0; g.whaleRebound = 0;
  {
    bool cry = (g.asset == AS_CRYPTO);
    // 基础概率 × 性格 × 大盘情绪（大盘崩了，个股更容易出事）
    float mood = 1.f;
    if (g.marketTide < -0.02f) mood = 1.3f;            // 恐慌传染
    else if (g.marketTide > 0.03f) mood = 0.85f;       // 贪婪时异象少
    float pSwan  = (cry ? 0.08f : 0.030f) * g.evMul * mood;
    float pRhino = (cry ? 0.08f : 0.060f) * g.evMul * mood;
    float pWhale = (cry ? 0.08f : 0.100f) * g.evMul * g.whaleMul * mood;
    float pPump  = (cry ? 0.16f : 0.100f) * g.evMul * mood;
    float pFall  = (cry ? 0.160f : 0.f)  * g.evMul * mood;
    float r = rnd01(g) * 2.0f;                          // 归一化：基础约 15%/根
    uint8_t k = 0;
    if      (r < pFall)            { k = 4; }
    else if (r < pFall + pPump)    { k = 5; }
    else if (r < pFall + pPump + pSwan)  { k = 3; }
    else if (r < pFall + pPump + pSwan + pRhino) { k = 2; }
    else if (r < pFall + pPump + pSwan + pRhino + pWhale) { k = 1; }
    g.evNext = k;
    g.evAt = k ? (3.0f + rnd01(g) * 18.0f) : -1.f;
  }
  g.live.o = g.live.h = g.live.l = g.live.c = g.price;

}

void gameInit(Game& g) {
  memset(&g, 0, sizeof(g));
  g.rng = 0x9E3779B9u ^ 0x1234567u;
  g.coins = START_COINS;
  g.bestCoins = START_COINS;
  g.sysPow = 1.0f;
  g.cnColors = 1;                    // 默认红涨绿跌（美股惯例）
  g.asset = AS_BANK; g.stockIdx = 0;
  // 必须走 beginStockDay：scBase/scAmp/erDay/panic 等全在这里初始化。
  // 曾经手写初始化漏掉 scBase，导致剧本基准是 0，价格一路被拉向零。
  do { beginStockDay(g); } while (g.asset == AS_CRYPTO);   // 开局先给普通股
  g.dayStartCoins = g.coins;
  g.dayPeakEquity = g.coins;
  calcIndex(g);
  for (int k = 0; k < 5; k++) g.idxPrev[k] = g.idx[k];
  startRound(g);
}

// 开机续玩：仓位还在就接着这只票，从上一根收盘的价格继续（世界在你关机时暂停）
void gameResume(Game& g, uint8_t stockIdx, uint8_t sessionCandle) {
  g.stockIdx = stockIdx % NSTOCK;
  g.asset    = stockAsset(g.stockIdx);
  gameTraitsFor(g, g.stockIdx);
  float px   = (g.pxCoin[g.stockIdx] > 0.f) ? g.pxCoin[g.stockIdx] : stockBase(g.stockIdx);
  g.price    = px; g.dayOpen = px; g.scBase = px; g.scFrom = 0.f;
  g.scar = 0.f; g.wander = 0.f; g.panic = 0.f;
  g.evShock = 0; g.evTail = 0; g.evKind = 0; g.evAt = -1.f;
  g.live.o = g.live.h = g.live.l = g.live.c = px;
  g.histN = 0; g.markN = 0;
  g.sessionCandle = (sessionCandle < DAY_CANDLES) ? sessionCandle : 0;
  {   // 把关机前已经走完的蜡烛拼回来，不然续玩会只剩最右边一根
    float prev = px;
    for (uint32_t i = 0; i < g.sessionCandle; i++) {
      float cl = (g.dayPx[i] > 0.f) ? g.dayPx[i] : prev;
      Candle c; c.o = prev; c.c = cl;
      c.h = (c.o > c.c) ? c.o : c.c;
      c.l = (c.o < c.c) ? c.o : c.c;
      if (g.histN < HIST_N) g.hist[g.histN++] = c;
      else { for (int k = 0; k < HIST_N - 1; k++) g.hist[k] = g.hist[k+1]; g.hist[HIST_N-1] = c; }
      prev = cl;
    }
  }
  g.roundT = 0.f; g.tickT = 0.f;
  g.phase = PH_TRADE; g.phaseT = 0.f;
  g.dayStartCoins = g.coins;
  g.dayPeakEquity = g.coins;
  float eq = equityOf(g);
  for (int i = 0; i <= 12; i++) g.dayEq[i] = (i <= g.sessionCandle) ? eq : -1.f;
  g.ctl[0] = g.ctl[1] = g.ctl[2] = g.ctl[3] = 0.f;   // 本日剧本重新生成
  float swing = (g.asset == AS_CRYPTO) ? 0.16f : 0.05f;
  g.dayDrift = (g.asset == AS_CRYPTO) ? (rnd01(g) * 0.6f - 0.3f) : (rnd01(g) * 0.3f - 0.05f);
  for (int i = 0; i < 4; i++) {
    g.ctl[i] = g.dayDrift * ((float)(i + 1) / 4.f) + (rnd01(g) - 0.5f) * swing * (1.f - 0.18f * i);
  }
  g.ctl[3] = g.dayDrift;
  g.erDay = 0; g.erCandle = 0; g.erEst = 0.f;      // 今天的财报日程照常抽
  g.erRumor = 0; g.erRumorT = 0.f; g.erRumorKind = 0; g.erRumorTrue = 0;
  g.erResult = ER_NONE; g.erGap = 0.f; g.erVol = 1.f; g.erVolT = 0.f;
  {
    float r = rnd01(g);
    if (r < 0.36f) { g.erDay = 1; g.erCandle = (uint8_t)(5 + rnd32(g) % 4); g.erEst = 3.f + rnd01(g) * 12.f; }
    else if (r < 0.45f) { g.erDay = 2; g.erCandle = (uint8_t)(5 + rnd32(g) % 4); g.erEst = 3.f + rnd01(g) * 12.f; }
  }
}

// 当日剧情：4 个随机控制点 + smoothstep 插值 —— 每天一条独一无二的曲线，
// 没有"固定剧本"可背；玩家只能读当下这根线的走势
static float curveAt(const Game& g, float t) {
  if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
  int i = (int)(t * 4.f); if (i > 3) i = 3;
  float f = t * 4.f - (float)i; if (i == 3) f = 1.f;
  float a0 = (i == 0) ? 0.f : g.ctl[i - 1];
  float b0 = g.ctl[i];
  float sm = f * f * (3.f - 2.f * f);        // smoothstep
  return a0 + (b0 - a0) * sm;
}

static void forceClose(Game& g);

static void doTick(Game& g) {
  float vol = assetVol(g.asset) * (g.erVolT > 0.f ? g.erVol : 1.f);
  float ev  = fminf(vol, 1.8f);          // 事件推力上限：不然加密会被一拳打穿
  float vn  = fminf(vol, g.asset == AS_CRYPTO ? 3.4f : 2.6f);   // 噪声上限
  float dayT = ((float)g.sessionCandle + g.roundT / ROUND_SEC) / (float)DAY_CANDLES;
  float span = 1.f - g.scFrom;  if (span < 0.05f) span = 0.05f;
  float u = (dayT - g.scFrom) / span;

  // 浪：均值回复的随机游走，几秒一个来回，给盘面呼吸感
  g.wander += (rnd01(g) - 0.5f) * 0.016f * vn;
  g.wander *= 0.988f;
  if (g.wander >  0.08f) g.wander =  0.08f;
  if (g.wander < -0.08f) g.wander = -0.08f;
  float target = g.scBase * (1.f + curveAt(g, u) + g.scar + g.flow + g.marketTide * 0.6f
                                 - 0.045f * g.panic + g.wander);
  float prev = g.price;

  g.price += (target - g.price) * 0.035f;                       // 朝剧情（含恐慌折价）靠拢
  g.price -= g.price * 0.0006f * ev * g.panic;                // 恐慌阴跌：情绪不会立刻修复
  g.price += g.price * (rnd01(g) - 0.5f) * 0.0075f * vn;      // 噪声（按百分比，四位数价格才推得动）
  // 异象不再逐 tick 推力：全部由剧本（scar）驱动，方向随机

  if (g.whaleRebound > 0.f) { g.price += g.price * 0.0025f * ev; g.whaleRebound -= 1.f; }
  if (g.price < 0.5f) g.price = 0.5f;
  if (g.price > 500000.f) g.price = 500000.f;

  if (g.qty > 0.f && g.asset == AS_CRYPTO && g.price < g.avgCost * 0.35f) {   // 加密爆仓线
    forceClose(g);
    g.liqT = 2.0f; g.shakeT = 0.6f;
    gamePushSfx(g, SFX_SELL_LOSS);
  }
  if (g.price > g.live.h) g.live.h = g.price;
  if (g.price < g.live.l) g.live.l = g.price;
  g.live.c = g.price;
  g.pxCoin[g.stockIdx % NSTOCK] = g.price;      // 活价持续记录：哪怕这只股已经跌到 0.5
  (void)prev;
}

// 盘口深度（元）：成交越大，滑点越狠。银行最深，加密最浅
static float depthOf(uint8_t idx) {
  switch (stockAsset(idx)) {
    case AS_BANK:   return 30000.f;
    case AS_CONS:   return 22000.f;
    case AS_TECH:   return 18000.f;
  }
  return 7000.f;
}
static float impactOf(const Game& g, float notional) {
  float im = notional / depthOf(g.stockIdx) * 0.8f;
  if (im > 0.04f) im = 0.04f;              // 单笔最多砸 4%
  return im;
}

static void tryBuy(Game& g) {
  float cash    = g.coins / 100.f;                       // 元
  float notional = cash * 0.33f;   // 每次 1/3：想满仓就连按三次 B，赌多大由玩家定
  if (notional < 5.f) { gamePushSfx(g, SFX_DENY); g.denyT = 1.4f; return; }
  float px = g.price * (1.f + COMM);
  float shares  = notional / px;
  if (g.asset != AS_CRYPTO) {                            // 股票：整股，不能无限加仓
    shares = floorf(shares);
    while (shares >= 1.f && shares * px * 100.f > (float)g.coins) shares -= 1.f;
    // 1/3 的仓位凑不出一整股时，退化成"最少一手"——只要现金买得起就不该拒
    if (shares < 1.f && g.posStep < 3 && px * 100.f <= (float)g.coins) shares = 1.f;
    if (shares < 1.f) { gamePushSfx(g, SFX_DENY); g.denyT = 1.4f; return; }   // 真买不起
  }
  float costC   = shares * px * 100.f;                   // 分
  if (costC > (float)g.coins) { gamePushSfx(g, SFX_DENY); g.denyT = 1.4f; return; }
  g.coins -= (int32_t)(costC + 0.5f);
  g.lastFee = (int32_t)(shares * px * 100.f * COMM + 0.5f);
  float n = g.qty + shares;
  g.avgCost = (g.avgCost * g.qty + px * shares) / n;
  g.qty = n;
  g.invested += costC / 100.f;
  {   // 你的买单把价格推上去（滑点），并留下会慢慢消退的痕迹
    float im = impactOf(g, costC / 100.f);
    g.price *= (1.f + im);
    g.flow += im;
    if (g.flow >  0.08f) g.flow =  0.08f;
    if (g.flow < -0.08f) g.flow = -0.08f;
    g.fxT = 0.5f; g.fxUp = 1; g.fxQty = shares; g.fxPrice = g.price;
    g.shakeT = 0.10f;
  }
  if (g.posStep < 3) g.posStep++;
  addMark(g, 1, px);
  gamePushSfx(g, SFX_BUY);
}

static void trySell(Game& g) {
  if (g.qty <= 0.f) { gamePushSfx(g, SFX_DENY); return; }
  if (g.posStep == 0) g.posStep = 1;                            // 兜底
  float sellQty = (g.posStep <= 1) ? g.qty                      // 最后一档：清空
                                   : g.qty / (float)g.posStep;  // 否则卖出 1/N
  if (g.asset != AS_CRYPTO) {                                   // 股票只能整股卖
    sellQty = floorf(sellQty);
    if (sellQty < 1.f || g.posStep <= 1) sellQty = g.qty;
  }
  float px = g.price * (1.f - COMM);
  float grossC = sellQty * px * 100.f;                                       // 分
  int32_t gain = (int32_t)((px - g.avgCost) * sellQty * 100.f);            // 分
  g.lastFee = (int32_t)(sellQty * g.price * 100.f * COMM + 0.5f);
  g.coins += (int32_t)(grossC + 0.5f);
  g.returned += grossC / 100.f;
  g.qty -= sellQty;
  if (g.qty < 1e-6f) { g.qty = 0.f; }
  g.posStep = (g.posStep <= 1) ? 0 : (uint8_t)(g.posStep - 1);
  if (g.qty <= 0.f) { g.qty = 0.f; g.posStep = 0; g.avgCost = 0.f; }
  g.lastGain = gain;
  g.gainT = 1.2f;
  {   // 卖单把价格砸下去
    float im = impactOf(g, grossC / 100.f);
    g.price *= (1.f - im);
    g.flow -= im;
    if (g.flow >  0.08f) g.flow =  0.08f;
    if (g.flow < -0.08f) g.flow = -0.08f;
    g.fxT = 0.5f; g.fxUp = 0; g.fxQty = sellQty; g.fxPrice = g.price;
    g.shakeT = 0.12f;
  }
  addMark(g, 0, px);
  updateSysPow(g);
  gamePushSfx(g, gain >= 0 ? SFX_SELL_WIN : SFX_SELL_LOSS);
}

static void updateSysPow(Game& g) {
  float payGain = g.invested / (g.returned > 1.f ? g.returned : 1.f);
  float t = (payGain - 0.5f) / 1.5f;
  if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
  g.sysPow = 1.5f - t * 1.0f;
}

// 强平：换股下架 或 付不起持仓费（爆仓）
static void forceClose(Game& g) {
  if (g.qty <= 0) return;
  float px = g.price * (1.f - COMM);
  int32_t gain = (int32_t)((px - g.avgCost) * g.qty * 100.f);
  g.lastFee = (int32_t)(g.price * 100.f * g.qty * COMM + 0.5f);
  g.coins += (int32_t)(px * 100.f * g.qty + 0.5f);
  g.returned += (px * g.qty);
  g.qty = 0.f; g.avgCost = 0.f; g.posStep = 0;
  g.lastGain = gain; g.gainT = 1.6f;
  updateSysPow(g);
  gamePushSfx(g, gain >= 0 ? SFX_SELL_WIN : SFX_SELL_LOSS);
}

// 财报结果抽样：盘面走势和传闻都会改概率（这条不告诉玩家）
static void pickEarnings(Game& g) {
  float pre = g.price / g.dayOpen - 1.f;
  float w[6] = { 0.35f, 0.30f, 0.20f, 0.10f, 0.05f, 0.f };   // BEAT MISS INLINE GUIDE SCANDAL SELLNEWS
  if (pre > 0.06f)  { w[5] = 0.40f; w[0] *= 0.4f; }          // 涨太多 → 利好出尽
  if (pre < -0.06f) { w[0] += 0.15f; }                       // 跌太多 → 超跌反弹
  w[4] *= g.scandalMul;                                       // 爱造假的公司：SCANDAL 权重 ×3
  if (g.erRumor && g.erRumorTrue) {
    if (g.erRumorGood) w[0] += 0.20f; else w[1] += 0.20f;    // 传闻属实才加权重
  }
  float tot = 0.f; for (int i = 0; i < 6; i++) tot += w[i];
  float r = rnd01(g) * tot, acc = 0.f; int k = 5;
  for (int i = 0; i < 6; i++) { acc += w[i]; if (r <= acc) { k = i; break; } }
  g.erResult = (uint8_t)(ER_BEAT + k);
  switch (g.erResult) {
    case ER_BEAT:     g.erGap =   0.06f + rnd01(g) * 0.08f; break;
    case ER_MISS:     g.erGap = -(0.08f + rnd01(g) * 0.10f); break;
    case ER_INLINE:   g.erGap = (rnd01(g) - 0.5f) * 0.02f;   break;
    case ER_GUIDE:    g.erGap = -(0.03f + rnd01(g) * 0.03f); break;
    case ER_SCANDAL:  g.erGap = (rnd01(g) - 0.5f) * 0.24f;   break;
    default:          g.erGap = -(0.05f + rnd01(g) * 0.04f); break;
  }
}

// 财报落地：换剧本（趋势、波动、恐慌、成本）
static void applyEarnings(Game& g) {
  if (g.evDex[5] < 60000) g.evDex[5]++;

  float dayT = ((float)g.sessionCandle + g.roundT / ROUND_SEC) / (float)DAY_CANDLES;
  g.price *= (1.f + g.erGap);
  g.pxCoin[g.stockIdx % NSTOCK] = g.price;
  g.scBase  = g.price;
  g.scFrom  = (dayT > 0.92f) ? 0.92f : dayT;      // 剩下的时间从此刻重新起算
  switch (g.erResult) {
    case ER_BEAT:                                        // 一路向上
      g.ctl[0] = 0.03f; g.ctl[1] = 0.06f; g.ctl[2] = 0.09f; g.ctl[3] = 0.12f;
      g.panic *= 0.4f; g.erVol = 1.3f; g.erVolT = 3.f; break;
    case ER_MISS:                                        // 一路向下
      g.ctl[0] = -0.04f; g.ctl[1] = -0.08f; g.ctl[2] = -0.11f; g.ctl[3] = -0.14f;
      g.panic = fminf(1.f, g.panic + 0.7f); g.erVol = 1.2f; g.erVolT = 3.f; break;
    case ER_INLINE:                                      // 死水
      g.ctl[0] = 0.004f; g.ctl[1] = -0.004f; g.ctl[2] = 0.003f; g.ctl[3] = 0.f;
      g.panic *= 0.5f; g.erVol = 0.5f; g.erVolT = 3.f; break;
    case ER_GUIDE:                                       // 阴跌
      g.ctl[0] = -0.01f; g.ctl[1] = -0.02f; g.ctl[2] = -0.032f; g.ctl[3] = -0.045f;
      g.panic = fminf(1.f, g.panic + 0.9f); g.erVol = 1.0f; g.erVolT = 4.f; break;
    case ER_SCANDAL:                                     // 来回抽
      if (g.taint[g.stockIdx % NSTOCK] < 3) g.taint[g.stockIdx % NSTOCK] = 3;   // 造假被查：后面带病开盘
      g.ctl[0] = (rnd01(g) - 0.5f) * 0.10f; g.ctl[1] = (rnd01(g) - 0.5f) * 0.14f;
      g.ctl[2] = (rnd01(g) - 0.5f) * 0.12f; g.ctl[3] = (rnd01(g) - 0.5f) * 0.08f;
      g.panic = fminf(1.f, g.panic + 0.5f); g.erVol = 2.0f; g.erVolT = 4.f; break;
    default:                                             // 利好出尽：冲高回落
      g.ctl[0] = 0.05f; g.ctl[1] = 0.07f; g.ctl[2] = 0.01f; g.ctl[3] = -0.04f;
      g.panic = fminf(1.f, g.panic + 0.4f); g.erVol = 1.4f; g.erVolT = 3.f; break;
  }
  g.live.o = g.live.h = g.live.l = g.live.c = g.price;
  g.erFaced++;
  if (g.erResult == ER_BEAT || g.erResult == ER_INLINE) g.erWon++;
  gamePushSfx(g, g.erResult == ER_BEAT ? SFX_ER_BEAT : SFX_ER_MISS);
}

const char* erRumorText(const Game& g) {
  static const char* T[6] = {
    "RUMOR BIG ORDER", "RUMOR BUYBACK", "RUMOR UPGRADE",
    "RUMOR ORDERS DROP", "RUMOR CFO EXITS", "RUMOR PROBE"
  };
  return T[g.erRumorKind % 6];
}

// 指数：等权，1000 点起，跟着成分的活价走（0 大盘 1 银行 2 消费 3 科技 4 加密）
static const char* IDX_CODE[5] = { "MKT", "BNK", "CNS", "TCH", "CRY" };
static void calcIndex(Game& g) {
  for (int k = 0; k < 5; k++) {
    g.idxPrev[k] = g.idx[k];
    float sum = 0.f; int n = 0;
    for (int i = 0; i < NSTOCK; i++) {
      if (k > 0) {
        uint8_t a = stockAsset((uint8_t)i);
        if (k == 1 && a != AS_BANK) continue;
        if (k == 2 && a != AS_CONS) continue;
        if (k == 3 && a != AS_TECH) continue;
        if (k == 4 && a != AS_CRYPTO) continue;
      }
      float pr = (g.pxCoin[i] > 0.f) ? g.pxCoin[i] : stockBase((uint8_t)i);
      sum += pr / stockBase((uint8_t)i);
      n++;
    }
    g.idx[k] = (n > 0) ? 1000.f * sum / (float)n : 1000.f;
  }
}

// 你没在交易的标的也在走：每根蜡烛收盘推一下（含加密，波动更大）
static void driftOthers(Game& g) {
  // 大盘潮汐：会连续几根同向（群涨/群跌），大盘因此能剧烈摆动
  g.marketTide = g.marketTide * 0.72f + (rnd01(g) - 0.5f) * 0.060f;
  if (g.marketTide >  0.05f) g.marketTide =  0.05f;
  if (g.marketTide < -0.05f) g.marketTide = -0.05f;
  for (int i = 0; i < NSTOCK; i++) {
    if (i == (int)g.stockIdx) continue;
    if (g.pxCoin[i] <= 0.f) continue;
    g.pxPrev[i] = g.pxCoin[i];
    bool cry = (stockAsset((uint8_t)i) == AS_CRYPTO);
    float vol = cry ? 0.032f : 0.010f;
    float tide = g.marketTide * (cry ? 1.8f : 1.f);   // 加密对大盘更敏感（杠杆）
    g.pxCoin[i] *= (1.f + tide + (rnd01(g) - 0.5f) * vol * 2.f);
    if (g.pxCoin[i] < 0.5f) g.pxCoin[i] = 0.5f;      // 跌到地板就趴着，不重置
  }
}

static void closeCandle(Game& g) {
  float c = g.price;
  int32_t pl = 0, fee = 0;
  if (g.qty > 0) {
    pl  = (int32_t)((c - g.avgCost) * g.qty * 100.f);   // 浮盈浮亏（分）：只显示，不进账
    float fMul = (g.erDay && g.sessionCandle == g.erCandle) ? 2.f : 1.f;  // 财报根：风险溢价翻倍
    fee = (int32_t)(c * 100.f * g.qty * HOLD_FEE * fMul + 0.5f);
    g.coins -= fee;
    g.lastGain = pl;
    g.lastFee  = fee;
    if (g.coins < 0) {                              // 付不起持仓费 → 爆仓
      g.coins = 0;
      g.lastFee = 0;
      forceClose(g);
    }
  } else {
    g.lastFee = 0;
  }
  g.gainT = 1.6f;

  // 蜡烛推进：hist 从 0 紧凑打包（最老的在前），满窗后左移
  if (g.histN < HIST_N) g.hist[g.histN++] = g.live;
  else { for (int i = 0; i < HIST_N - 1; i++) g.hist[i] = g.hist[i+1]; g.hist[HIST_N-1] = g.live; }

  g.rounds++;
  if (g.sessionCandle < DAY_CANDLES) g.dayPx[g.sessionCandle] = g.price;   // 记下这根收盘
  g.sessionCandle++;
  // 异象剧本推进：冲击 → 余波；scar 是累积偏移，价位不会自己弹回去
  if (g.evShock > 0)      { g.scar += g.evShockSlope; g.evShock--; }
  else if (g.evTail > 0)  { g.scar += g.evTailSlope;  g.evTail--;  }
  if (g.sessionCandle <= 12) g.dayEq[g.sessionCandle] = equityOf(g);
  g.flow *= 0.55f;                        // 冲击每根消退一半
  driftOthers(g);                         // 其他标的也在自己走
  calcIndex(g);
  g.panic *= 0.96f;                       // 一根蜡烛只修复 4%：情绪不会很快过去
  pruneMarks(g);
  g.dex[g.asset]++;
  if (g.coins > g.bestCoins) g.bestCoins = g.coins;
  {   // 收集奖励：6 类异象集齐 → 解锁妖股 GAO（3.20 起，最野的一只）
    uint8_t got = 0;
    for (int i = 0; i < 6; i++) if (g.evDex[i]) got++;
    if (got == 6 && !g.unlocked) { g.unlocked = 1; g.unlockT = 4.0f; gamePushSfx(g, SFX_ER_BEAT); }
  }

  g.flashT = 0.06f;
  if (g.erVolT > 0.f) g.erVolT -= 1.f;
  if (g.sessionCandle == DAY_CANDLES / 2) g.bgFade = 0.28f;   // 上午收盘 → 抖动过渡
  if (g.erDay && g.sessionCandle == g.erCandle && g.sessionCandle < DAY_CANDLES) {
    pickEarnings(g);
    g.phase = PH_EARN; g.phaseT = 2.8f;
    gamePushSfx(g, SFX_ER_OPEN);
    return;
  }
  if (g.sessionCandle >= DAY_CANDLES) {          // 收日：强制平仓 + 收盘节拍
    forceClose(g);
    {   // 造假的疤按"交易日"递减（当天先享受惩罚，日终再减）
      uint8_t ti = g.stockIdx % NSTOCK;
      if (g.taint[ti] > 0) g.taint[ti]--;
    }
    {   // 跨日战绩：账户权益 + 同期大盘（战绩册第 2 页）
      int n = g.histDays;
      if (n < 24) { g.histEq[n] = equityOf(g); g.histIdx[n] = g.idx[0]; g.histDays = (uint8_t)(n + 1); }
      else {
        for (int i = 0; i < 23; i++) { g.histEq[i] = g.histEq[i+1]; g.histIdx[i] = g.histIdx[i+1]; }
        g.histEq[23] = equityOf(g); g.histIdx[23] = g.idx[0];
      }
    }
    g.dayPnl     = g.coins - g.dayStartCoins;
    g.dayPeakPnl = g.dayPeakEquity - g.dayStartCoins;
    g.phase  = PH_DAYEND;
    g.phaseT = 2.6f;
    g.dayFxT = 0.f;
    gamePushSfx(g, SFX_DEX);
  } else {
    g.live.o = g.live.h = g.live.l = g.live.c = c;
    startRound(g);
  }
}

void gameUpdate(Game& g, const Input& in, float dt) {
  if (g.flashT > 0.f) g.flashT -= dt;
  if (g.bgFade  > 0.f) g.bgFade  -= dt;
  if (g.erRumorT > 0.f) g.erRumorT -= dt;
  if (g.liqT > 0.f) g.liqT -= dt;
  if (g.denyT > 0.f) g.denyT -= dt;
  if (g.unlockT > 0.f) g.unlockT -= dt;
  if (g.fxT > 0.f) g.fxT -= dt;
  if (g.shakeT > 0.f) g.shakeT -= dt;
  if (g.gainT  > 0.f) g.gainT  -= dt;

  if (g.phase == PH_EARN) {
    g.phaseT -= dt;
    if (g.phaseT <= 0.f) { applyEarnings(g); startRound(g); }
    return;
  }

  if (g.phase == PH_DAYEND) {
    g.dayFxT += dt;
    g.phaseT -= dt;
    if (g.phaseT <= 0.f) { gameNewSession(g); startRound(g); }
    return;
  }

  {   // 当日最高权益（用于收盘显示"最高浮盈"）
    int32_t eq = g.coins + (int32_t)(g.price * 100.f * g.qty);
    if (eq > g.dayPeakEquity) g.dayPeakEquity = eq;
  }

  // 特殊事件
  if (g.evKind) {
    g.evT -= dt;
    float dur = (g.evKind == 1) ? 2.5f : (g.evKind == 2 ? 8.0f : (g.evKind == 4 ? 3.0f : (g.evKind == 5 ? 2.5f : 1.6f)));
    g.evProg = 1.f - (g.evT / dur);
    if (g.evProg < 0.f) g.evProg = 0.f;
    if (g.evT <= 0.f) {
      if (g.evKind == 1) g.whaleRebound = 8.f;
      g.evKind = 0;
    }
  } else if (g.evAt > 0.f && g.roundT >= g.evAt) {
    g.evKind = g.evNext; g.evAt = -1.f; g.evProg = 0.f;
    g.evT = (g.evKind == 1) ? 2.5f : (g.evKind == 2 ? 8.0f : (g.evKind == 4 ? 3.0f : (g.evKind == 5 ? 2.5f : 1.6f)));
    g.shakeT = (g.evKind == 3) ? 0.6f : 0.3f;
    if (g.evKind == 3) {                       // 黑天鹅：即时冲击 + 后续交给剧本
      g.price *= 0.97f;
      g.panic = 1.0f;                        // 黑天鹅：恐慌拉满
      if (g.qty > 0.f && g.asset == AS_CRYPTO && g.price < g.avgCost * 0.35f) {   // 加密爆仓线
    forceClose(g);
    g.liqT = 2.0f; g.shakeT = 0.6f;
    gamePushSfx(g, SFX_SELL_LOSS);
  }
  if (g.price > g.live.h) g.live.h = g.price;
      if (g.price < g.live.l) g.live.l = g.price;
      g.live.c = g.price;
      g.whaleRebound = 4.f;
    }
    {   // 抽剧本：方向严格随机
      const EvScript* tab = S_WHALE; int n = 3;
      if (g.evKind == 2) tab = S_RHINO;
      else if (g.evKind == 3) tab = S_SWAN;
      else if (g.evKind == 4) tab = S_WATER;
      else if (g.evKind == 5) tab = S_PUMP;
      const EvScript& sc = tab[(int)(rnd32(g) % (uint32_t)n)];
      if (g.evKind >= 1 && g.evKind <= 5) {
        g.evDex[g.evKind - 1]++;
      }
      g.evShock = sc.shock;
      g.evShockSlope = (sc.shockSlope < 0.f) ? sc.shockSlope * g.shockMul : sc.shockSlope;
      g.evTail  = sc.tail;
      g.evTailSlope  = (sc.tailSlope  < 0.f) ? sc.tailSlope  * g.shockMul : sc.tailSlope;
      g.evT = (float)(sc.shock + sc.tail) * ROUND_SEC;   // 精灵按根数挂着
    }
    if (g.evKind == 2) g.panic = fminf(1.f, g.panic + 0.35f);   // 灰犀牛：慢慢积累
    if (g.evKind == 1) g.panic = fminf(1.f, g.panic + 0.18f);   // 鲸鱼：插针也吓人
    if (g.evKind == 4) g.panic = fminf(1.f, g.panic + 0.85f);   // 瀑布：恐慌直接拉满
    if (g.evKind == 5) g.panic *= 0.2f;                         // 升天：贪婪驱散恐慌
    gamePushSfx(g, g.evKind == 3 ? SFX_SWAN
                   : g.evKind == 2 ? SFX_RHINO
                   : g.evKind == 4 ? SFX_SWAN
                   : g.evKind == 5 ? SFX_ER_BEAT : SFX_WHALE);
  }

  g.roundT += dt;
  g.tickT  += dt;
  while (g.tickT >= TICK_SEC) { g.tickT -= TICK_SEC; doTick(g); }

  if (in.buy)  tryBuy(g);
  if (in.sell) trySell(g);

  // 破产救济：低于一手买不起时 3.5 秒补给（不计 invested）
  if (g.coins < 500 && g.qty == 0.f) {
    g.rescueT += dt;
    if (g.rescueT >= RESCUE_SEC) {
      g.coins = 20000;   // 固定补 200.00，不随标的价格膨胀
      g.rescueT = 0.f;
      gamePushSfx(g, SFX_RESCUE);
    }
  } else g.rescueT = 0.f;

  if (g.roundT >= ROUND_SEC) closeCandle(g);
}