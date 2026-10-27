// TICKER — 平台无关游戏核心（不许 include M5，sim 与固件共用）
#pragma once
#include <stdint.h>

#define NSTOCK 13              // 12 只常驻 + 1 只收集解锁的妖股
#define DAY_CANDLES 12         // 一个交易日 = 12 根蜡烛（6 分钟）
#define SCREEN_W 240
#define SCREEN_H 135
#define HIST_N   5
#define SLOTS    (HIST_N + 1)     // 5 历史 + 1 活蜡烛
#define MAX_MARKS 12

enum { AS_BANK = 0, AS_CONS, AS_TECH, AS_CRYPTO, AS_COUNT };
enum { PH_TRADE = 0, PH_SETTLE, PH_DAYEND, PH_EARN };
// 财报结果（不是二元：超预期/不及/符合/砍指引/爆雷/利好出尽）
enum { ER_NONE = 0, ER_BEAT, ER_MISS, ER_INLINE, ER_GUIDE, ER_SCANDAL, ER_SELLNEWS };
enum { SFX_NONE = 0, SFX_TICK_UP, SFX_TICK_DN, SFX_TICK_BIG, SFX_BUY, SFX_SELL_WIN,
       SFX_SELL_LOSS, SFX_SETTLE, SFX_WHALE, SFX_RESCUE, SFX_DENY, SFX_DEX,
       SFX_RHINO, SFX_SWAN, SFX_ER_OPEN, SFX_ER_BEAT, SFX_ER_MISS };

struct Candle { float o, h, l, c; };
struct Mark   { uint32_t idx; uint8_t buy; float price; };  // idx=蜡烛编号，price=成交价（Y 轴会动，存价不存 y）

struct Input { bool buy, sell, skip; };

struct Game {
  Candle  hist[HIST_N];
  uint8_t histN;
  Candle  live;
  uint8_t asset;
  uint8_t sessionRound;      // 0..4，满 5 换标的

  float   price;
  int32_t coins;
  int32_t bestCoins;
  float   qty;               // 持仓份额（小数：按现金 1/3 建仓，任何价位都能玩）
  uint8_t posStep;           // 还持有几"档"（买入 +1，卖出 -1；卖完最后一档即清仓）
  float   avgCost;

  Mark    marks[MAX_MARKS];
  uint8_t markN;

  uint8_t phase;
  float   phaseT;
  float   tickT, roundT;

  float   sysPow, invested, returned;

  uint8_t evKind;            // 0无 1鲸鱼 2灰犀牛 3黑天鹅
  uint8_t evNext;            // 本局预定事件
  float   evT, evAt, evProg; // 剩余 / 触发时刻 / 0..1 动画进度
  float   whaleRebound;      // >0 = 插针后的回抽剩余 tick
  float   bgFade;            // >0 = 早盘→午后 的抖动过渡

  float   flashT, shakeT;
  int32_t lastGain; float gainT;
  int32_t lastFee;
  float   rescueT;
  uint8_t rescueOn;

  uint16_t dex[AS_COUNT];
  uint32_t rounds;

  // 战绩册：今日权益 / 跨日权益+大盘 / 异象收集
  float   dayEq[13];         // 今日每根蜡烛收盘的权益
  float   histEq[24], histIdx[24];
  uint8_t histDays;
  uint16_t evDex[6];         // 鲸鱼/灰犀牛/黑天鹅/瀑布/升天/财报
  uint8_t bookPage;          // 0 今日 1 战绩 2 异象
  // 内核：标的隐藏性格 + 因果
  float   evMul, shockMul, whaleMul, scandalMul;   // 当日由性格算出的倍率
  uint8_t taint[13];         // 财报造假留疤：还剩几次"带病开盘"
  uint8_t unlocked;          // 集齐 6 类异象解锁的妖股
  // 你的手推动市场
  float   flow;              // 买卖冲击残留（每根衰减一半）
  float   fxT, fxPrice;      // 成交特效计时 / 成交价
  float   fxQty;             // 成交股数（显示 +N SH）
  uint8_t fxUp;              // 1 买 0 卖
  float   dayFxT;            // 收日面板入场动画
  float   marketTide;        // 大盘潮汐：整轮市场共有的因子，连几根同向
  float   scar;              // 当日累积偏移：异象把价位拽走后不回头
  int8_t  evShock;           // 还剩几根冲击蜡烛
  int8_t  evTail;            // 还剩几根余波蜡烛
  float   evShockSlope, evTailSlope;
  float   pxPrev[NSTOCK];        // 各标的上一根收盘价
  float   idx[5];            // 5 个指数（1000 点起，等权跟着成分活价走）
  float   idxPrev[5];        // 上一个收盘的指数（箭头用）
  float   tickAcc;

  uint32_t rng;
  uint8_t  sfx[8];
  uint8_t  sfxN;

  uint8_t  dexOpen;          // 图鉴页
  uint8_t  helpOpen;

  uint8_t  cnColors;         // 0=绿涨红跌(国际) 1=红涨绿跌(美股/A股惯例，默认)
  uint8_t  scenario;         // 当日剧情形态（0涨 1跌 2V型 3冲高回落 4横盘 5跳水反抽）
  uint8_t  stockIdx;         // 当日标的（0..8，每只代码/基准价不同）
  float     dayOpen;          // 当日开盘价（剧情曲线基准）
  float     panic;            // 0..1 恐慌度：事件后阴跌，慢慢修复
  float     scFrom, scBase;   // 剧情重锚：从 scFrom(0..1) 起，以 scBase 为基准
  float     wander;           // 多秒级随机摆动（让盘面有"浪"，不是贴着剧情线不动）

  uint8_t   erDay;            // 0 无 1 预告 2 突袭
  uint8_t   erCandle;         // 第几根收盘公布
  float     erEst;            // 分析师预期 %
  uint8_t   erRumor;          // 有传闻
  uint8_t   erRumorGood;      // 传闻方向
  uint8_t   erRumorKind;      // 传闻文案 0..5
  uint8_t   erRumorTrue;      // 传闻是否属实（55%）
  float     erRumorT;         // 横幅剩余
  uint8_t   erResult;         // 结果
  float     erGap;            // 缺口 %
  float     erVol;            // 波动倍数
  float     erVolT;           // 剩余根数
  uint8_t   erFaced, erWon;   // 经历/赢过的财报
  float     dayPx[DAY_CANDLES];  // 当日每根蜡烛的收盘价（续玩时重建K线用）
  float     pxCoin[NSTOCK];       // 每只标的的"活价"：跨交易日连续记录，跌到 0.5 也不会重置
  float     ctl[4];           // 当日随机曲线控制点（t=0.25/0.5/0.75/1.0），每天都不一样
  float     dayDrift;         // 当日总涨跌（= ctl[3]）
  float     liqT;             // 爆仓横幅剩余
  float     unlockT;          // 解锁横幅剩余
  float     denyT;            // "钱不够"红条剩余
  uint32_t sessionCandle;    // 本交易日内第几根（0 起）
  int32_t  dayStartCoins;    // 开盘权益（分）
  int32_t  dayPeakEquity;    // 当日最高权益（分）
  int32_t  dayPnl;           // 收盘当日盈亏（分）
  int32_t  dayPeakPnl;       // 当日最高浮盈（分）
};

// 标的参数表（渲染与逻辑共用）
float assetVol(uint8_t a);          // 波动权值
void  assetRange(uint8_t a, float& lo, float& hi);
const char* assetName(uint8_t a);
const char* stockCode(uint8_t idx);
const char* erRumorText(const Game& g);
float       stockBase(uint8_t idx);
uint8_t     stockAsset(uint8_t idx);
uint8_t     stockTrait(uint8_t idx);

void gameInit(Game& g);
void gameResume(Game& g, uint8_t stockIdx, uint8_t sessionCandle);
void gameTraitsFor(Game& g, uint8_t idx);
   // 开机接着持仓那只票
void gameNewSession(Game& g);       // 换标的 + 清图（不动金币/图鉴）
void gameUpdate(Game& g, const Input& in, float dt);
bool gamePopSfx(Game& g, uint8_t& id);
void gamePushSfx(Game& g, uint8_t id);
int  slotOfCandle(const Game& g, uint32_t idx);   // 蜡烛编号 → 屏上槽位（<0 = 已出窗）

#define ROUND_SEC   30.0f
#define TICK_SEC    0.25f
#define START_COINS 60000   // 单位=分（price*100），显示时 /100
#define COMM        0.0025f    // 手续费：每笔（买/卖）各收 0.25%
#define RESCUE_SEC  3.5f
#define HOLD_FEE    0.0015f    // 每根蜡烛收持仓市值 0.15%
// 标的性格（隐藏，靠玩出来）
#define T_RESIST 0x01          // 抗跌：负面冲击 ×0.55
#define T_WILD   0x02          // 妖股：异象概率 ×1.6
#define T_LIAR   0x04          // 财报爱造假：SCANDAL ×3，且留疤
#define T_BAIT   0x08          // 鲸鱼常客：鲸鱼概率 ×2
#define T_COLD   0x10          // 冷门：异象概率 ×0.6，但跌了没人接
#define WHALE_CHANCE 0.18f
