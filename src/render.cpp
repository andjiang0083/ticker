// TICKER — 渲染（无平台依赖）
#include "render.h"

static float equityOf(const Game& g) { return (g.coins + g.price * 100.f * g.qty) / 100.f; }

// 指数代码（与 game.cpp 的 IDX_CODE 同序）
static const char* IDX_CODE[5] = { "MKT", "BNK", "CNS", "TCH", "CRY" };
#include "palette.h"
#include "glyph.h"
#include <stdio.h>
#include <string.h>

static uint16_t* FB;
static int sOx;                 // 震屏偏移

// 红绿可换（0=绿涨红跌 国际 / 1=红涨绿跌 A股）
static uint16_t COL_UP = PAL_UP, COL_UP_HI = PAL_UP_HI;
static uint16_t COL_DN = PAL_DN, COL_DN_HI = PAL_DN_HI;
static void setColorMode(bool cn) {
  if (cn) { COL_UP = PAL_DN; COL_UP_HI = PAL_DN_HI; COL_DN = PAL_UP; COL_DN_HI = PAL_UP_HI; }
  else    { COL_UP = PAL_UP; COL_UP_HI = PAL_UP_HI; COL_DN = PAL_DN; COL_DN_HI = PAL_DN_HI; }
}

// 价格 → 屏 y：不再用固定区间，按"屏上 6 根蜡烛的高低点"自动缩放（带平滑）
static float sLo = 0.f, sHi = 0.f;
static float sEnergy = 0.f, sLastPx = 0.f;   // 活蜡烛的"振动能量"
static float sT = 0.f;                       // 本帧时间（给动画用）
static int mapY(float p) {
  float span = sHi - sLo;
  if (span < 0.5f) span = 0.5f;
  float t = (p - sLo) / span;
  if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
  return 118 - (int)(t * 100.f);      // 图表区 18(高)..118(低)
}
static void updateScale(const Game& g) {
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < g.histN; i++) {
    if (g.hist[i].l < lo) lo = g.hist[i].l;
    if (g.hist[i].h > hi) hi = g.hist[i].h;
  }
  if (g.live.l < lo) lo = g.live.l;
  if (g.live.h > hi) hi = g.live.h;
  float mid = (lo + hi) * 0.5f;
  float pad = (hi - lo) * 0.20f + mid * 0.02f;
  lo -= pad; hi += pad;
  float minSpan = mid * 0.06f;                       // 最小可见窗口 = 6% 价格
  if (hi - lo < minSpan) { lo = mid - minSpan * 0.5f; hi = mid + minSpan * 0.5f; }
  if (sLo == 0.f && sHi == 0.f || g.histN == 0) { sLo = lo; sHi = hi; return; }   // 换日/首帧：直接吸附
  bool far = (hi < sLo) || (lo > sHi) ||                            // 区间完全不重叠（换标的价位跳变）
             (lo < sLo - (sHi - sLo) * 2.f) || (hi > sHi + (sHi - sLo) * 2.f);
  if (far) { sLo = lo; sHi = hi; return; }          // 换日跳得太远：直接吸附，别慢慢漂
  sLo += (lo - sLo) * 0.12f;
  sHi += (hi - sHi) * 0.12f;
}


static inline void px(int x, int y, uint16_t c) {
  if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
  FB[y * SCREEN_W + x] = c;
}
static void rect(int x, int y, int w, int h, uint16_t c) {
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) px(x+i, y+j, c);
}
static void frameRect(int x, int y, int w, int h, uint16_t c) {
  for (int i = 0; i < w; i++) { px(x+i, y, c); px(x+i, y+h-1, c); }
  for (int j = 0; j < h; j++) { px(x, y+j, c); px(x+w-1, y+j, c); }
}
static void text(int x, int y, const char* s, uint16_t c, int sc) {
  int cx = x + sOx;
  for (; *s; s++) {
    uint8_t ch = (uint8_t)*s;
    if (ch > 127) ch = '?';
    for (int r = 0; r < 5; r++) {
      uint8_t bits = GLYPH5[ch][r];
      for (int i = 0; i < 3; i++)
        if (bits & (1 << (2 - i))) rect(cx + i*sc, y + r*sc, sc, sc, c);
    }
    cx += 4*sc;
  }
}
static int textW(const char* s, int sc) { return (int)strlen(s) * 4 * sc; }
static void intTxt(int x, int y, int32_t v, uint16_t c, int sc) {
  char b[16]; snprintf(b, sizeof(b), "%d", (int)v); text(x, y, b, c, sc);
}
// 定点小数：v 按 10^dec 存储
static void fixTxt(int x, int y, int32_t v, int dec, uint16_t c, int sc) {
  char b[20];
  int32_t div = 1; for (int i = 0; i < dec; i++) div *= 10;
  int32_t ip = v / div, fp = v % div; if (fp < 0) fp = -fp;
  if (dec == 1) snprintf(b, sizeof(b), "%d.%01d", (int)ip, (int)fp);
  else if (dec == 2) snprintf(b, sizeof(b), "%d.%02d", (int)ip, (int)fp);
  else snprintf(b, sizeof(b), "%d", (int)ip);
  text(x, y, b, c, sc);
}
static void priceTxt(int x, int y, float p, uint16_t c, int sc) {
  int32_t v = (int32_t)(p * 100.f + 0.5f);
  fixTxt(x, y, v, 2, c, sc);
}

static uint16_t chColor(char c) {
  switch (c) {
    case 'k': return PAL_INK;
    case 'n': return PAL_BG0;   case 'N': return PAL_BG1;  case 't': return PAL_GRID;
    case 'w': return PAL_SEA0;  case 'W': return PAL_SEA1; case '2': return PAL_SEA2;
    case 'g': return PAL_UP_D;  case 'G': return PAL_UP;   case '3': return PAL_UP_HI;
    case 'r': return PAL_DN_D;  case 'R': return PAL_DN;   case '4': return PAL_DN_HI;
    case 'y': return PAL_GOLD_D;case 'Y': return PAL_GOLD; case '5': return PAL_GOLD_HI;
    case 'u': return PAL_UI_D;  case 'U': return PAL_UI;   case 'H': return PAL_UI_HI;
    case 'K': return PAL_INK;
    case 'b': return PAL_WHALE_D; case 'B': return PAL_WHALE;
    case 'l': return PAL_WHALE_L; case 's': return PAL_SPOUT;
    default: return PAL_BG0;
  }
}
// 放大版精灵：每个像素画成 sc×sc 方块（给顶栏图标用，要和 3 倍字高的股价同高）
static void spriteS(int x0, int y0, const char* const* rows, int n, int sc) {
  for (int r = 0; r < n; r++) {
    const char* row = rows[r];
    for (int c = 0; row[c]; c++) {
      char ch = row[c];
      if (ch == '.' || ch == ' ') continue;
      uint16_t cc = chColor(ch);
      for (int dy = 0; dy < sc; dy++)
        for (int dx = 0; dx < sc; dx++)
          px(x0 + c * sc + dx + sOx, y0 + r * sc + dy, cc);
    }
  }
}

static void sprite(int x0, int y0, const char* const* rows, int n) {
  for (int r = 0; r < n; r++) {
    const char* row = rows[r];
    for (int c = 0; row[c]; c++) {
      char ch = row[c];
      if (ch == '.' || ch == ' ') continue;
      px(x0 + c + sOx, y0 + r, chColor(ch));
    }
  }
}

// ── 精灵 ──────────────────────────────────────────────
static const char* WHALE[] = {
".....sss..............................",
"......s....s..........................",
"......s...............................",
".....kkkkkk...........................",
"...kkBBBBBBkkkk.......................",
"..kBBBBBBBBBBBkkBBkk..................",
".kHkBBBBBBBBBBBBkkkk............kkk...",
".kkBBBBBBBBBBBBBBkkkk..........kBBBk..",
".kBBBBBBBBBBBBBBBBBBBBkkkkkkkBBBBBBBk.",
".kllllllllllllllBBBBBBBBkkkkBBBBBBBk..",
"..klllllllllllllllBBBBBBBkkBBBBBBk....",
"...kllllllllllllllBBBBBBBBkkkBBBk.....",
"....klllllllllllllBBBBBBBBBkkkk.......",
".....kkllllllllllBBBBBBBBBBB..........",
".......kkkllllllBBBBBBBBBBB...........",
"..........kkkkkkBBBBBBBBBB............",
"................kkkkkkkkkk............",
};
static const char* BULL[] = {
"..HH......HH..",
".HHHH....HHHH.",
".rrrrrrrrrrrr.",
"rrkrrrrrrkrrrr",
"rkHkrrrrkHkrrr",
"rrkkrrrrkkkkrr",
"rrrrkkkkrrrrr.",
".rrrkkkkrrrr..",
"..rrrrrrrrrr..",
"...rr..rr.....",
};
static const char* BEAR[] = {
"..HH......HH..",
".HHHH....HHHH.",
".uuuuuuuuuuuu.",
"uukuuuuuuukuuu",
"ukHkuuuuHkukuu",
"uukkuuuuuukkkku",
"uuuukkkkkkuuu.",
".uuuuuuuuuuu..",
"..uuu..uuu....",
};
static const char* RHINO[] = {
"..........................kkkk...",
".........................kUUUUk..",
"........................kUUUUUk..",
".......................kUUUUUUk..",
"..........kkkkkkkkkkkkUUUUUUUk...",
"........kkUUUUUUUUUUUUUUUUUUUk...",
".......kUUUUUUUUUUUUUUUUUUUUUUk..",
"......kUUUUkUUUUUUUUUUUUUUUUUUUk.",
".....kUUUUk.kUUUUUUUUUUUUUUUUUUk.",
".....kUUUUk.kUUUUUUUUUUUUUUUUUUk.",
".....kUUUUk.kUUUUUUUUUUUUUUUUUUk.",
"......kkkk..kUUUUUUUUUUUUUUUUUUk.",
".............kUUUk..kUUUk..kUUUk.",
".............kUUUk..kUUUk..kUUUk.",
".............kkkk...kkkk...kkkk..",
};
static const char* SWAN[] = {
"........kkk.............",
".......kHKKk............",
".......kKKKk............",
"......kKKKk.............",
"......kKKKk.............",
".....kKKKk..............",
".....kKKKk..............",
"....kKKKKkkkkkk.........",
"..kkkKKKKKKKKKKkkk......",
".kKKKKKKKKKKKKKKKKKkk...",
"kKKKKKKKKKKKKKKKKKKKKk..",
"kKKKKKKKKKKKKKKKKKKKKKk.",
".kKKKKKKKKKKKKKKKKKKKk..",
"..kkKKKKKKKKKKKKKKKkk...",
"....kkkkkkkkkkkkkk......",
};
static const char* WHALEMINI[] = {
".s........",
"..BBBB......",
".BBHBBBBBB..",
"BkBBBBBBBBB.",
"BBBBBBBBBBB.",
"BBBBBBBBBBB.",
".BBBBBBBBB..",
"..BBBBBB....",
};

static void bgDither(uint16_t c0, uint16_t c1, uint16_t c2) {
  static const uint8_t BAY[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
  for (int y = 0; y < SCREEN_H; y++) {
    float t = (float)y / SCREEN_H;
    for (int x = 0; x < SCREEN_W; x++) {
      float th = BAY[y & 3][x & 3] / 16.0f;
      FB[y*SCREEN_W+x] = (t < th) ? c0 : (t < 0.66f ? c1 : c2);
    }
  }
}
static void gridLines(int mode) {            // 0 冷 1 暖 2 加密紫
  uint16_t c = (mode == 1) ? PAL_WARM_GRID : (mode == 2 ? PAL_CRY_GRID : PAL_GRID);
  for (int y = 20; y < 112; y += 16)
    for (int x = 0; x < SCREEN_W; x += 2) px(x + sOx, y, c);
}

static int CX(int slot) { return 40 + slot * 36; }   // 40,76,112,148,184,220

static void drawCandle(const Game& g, int slot, const Candle& c, int w, bool live) {
  int cx = CX(slot);
  bool up = (c.c >= c.o);
  int yh = mapY(c.h), yl = mapY(c.l);
  int yt = mapY(c.o > c.c ? c.o : c.c);
  int yb = mapY(c.o > c.c ? c.c : c.o);
  if (yb - yt < 3) yb = yt + 3;
  uint16_t col = up ? COL_UP : COL_DN;
  uint16_t hi  = up ? COL_UP_HI : COL_DN_HI;
  for (int y = yh; y <= yl; y++) px(cx + sOx, y, col);
  int x0 = cx - w/2;
  rect(x0 + sOx, yt, w, yb - yt, col);
  for (int i = 0; i < w; i++) { px(x0+i+sOx, yt, hi); px(x0+i+sOx, yb-1, PAL_INK); }
  rect(x0 + 1 + sOx, yt + 1, 2, yb - yt - 2, hi);
  if (live) {
    // 振动表达：两侧速度线（长度随振幅呼吸）+ 尖端火花，让"活蜡烛"看着在跳
    float amp = (c.h - c.l) / (c.o > 0.01f ? c.o : 1.f);
    int e = (int)(amp * 45.f + sEnergy * 2.f);
    if (e < 1) e = 1; if (e > 4) e = 4;
    int flick = (int)(sT * 16.f);
    for (int i = 0; i < 3; i++) {
      int len  = 1 + e + ((flick + i) & 1);
      int yy   = (yt + yb) / 2 + (i - 1) * 7;
      if (yy < 18 || yy > 116) continue;
      int xr = x0 + w + sOx + 1;
      int xl = x0 - 2 + sOx - len;
      rect(xr, yy, len, 1, (i == 1) ? PAL_UI_HI : col);
      rect(xl, yy, len, 1, (i == 1) ? PAL_UI_HI : col);
    }
    if (e >= 1) {                               // 上影尖端火花
      int sx = cx + sOx;
      rect(sx - 3, yh - 2, 1, 2, PAL_UI_HI);
      rect(sx + 3, yh - 2, 1, 2, PAL_UI_HI);
    }
    if (e >= 2) {                               // 下影尖端火花
      int sx = cx + sOx;
      rect(sx - 3, yl + 1, 1, 2, PAL_UI_HI);
      rect(sx + 3, yl + 1, 1, 2, PAL_UI_HI);
    }
    frameRect(x0 - 2 + sOx, yt - 2, w + 4, (yb - yt) + 4, PAL_UI_HI);
  }
}

static const Candle* candleAtSlot(const Game& g, int slot) {
  if (slot < 0) return 0;
  if (slot == slotOfCandle(g, g.sessionCandle)) return &g.live;
  int i = g.histN - 1 - (slotOfCandle(g, g.rounds) - slot - 1);
  int oldest = (int)g.sessionCandle - (int)g.histN;   // hist[0] 的编号
  int idx = oldest + i;
  int k = (int)g.sessionCandle - g.histN;             // 兜底：直接按编号反查
  for (int j = 0; j < g.histN; j++) {
    int cand = k + j;
    if (slotOfCandle(g, (uint32_t)cand) == slot) return &g.hist[j];
  }
  (void)idx; (void)oldest;
  return 0;
}

// 标记画在蜡烛体外（B 在最低点下方 / S 在最高点上方），带黑底描边——
// 绝不能用蜡烛同色画在实体上，否则同色相融，蜡烛看起来像消失了。
static void drawMarks(const Game& g) {
  int stackB[SLOTS] = {0}, stackS[SLOTS] = {0};
  for (uint8_t i = 0; i < g.markN; i++) {
    const Mark& m = g.marks[i];
    int slot = slotOfCandle(g, m.idx);
    if (slot < 0) continue;
    const Candle* c = candleAtSlot(g, slot);
    if (!c) continue;
    int cx = CX(slot);
    int y;
    uint16_t col;
    if (m.buy) {
      y = mapY(m.price) + 4 + stackB[slot] * 14;
      if (y > 108) y = 108;
      stackB[slot]++;
      col = COL_UP_HI;
    } else {
      y = mapY(m.price) - 16 - stackS[slot] * 14;
      if (y < 14) y = 14;
      stackS[slot]++;
      col = COL_DN_HI;
    }
    rect(cx - 6 + sOx, y - 1, 13, 14, PAL_INK);
    frameRect(cx - 6 + sOx, y - 1, 13, 14, col);
    text(cx - 4, y + 1, m.buy ? "B" : "S", col, 2);
  }
}

// 标的类别图标（7x7）：股价左边的"这只票是干什么的"
static const char* IC_BANK[] = {"..UUU..", ".UUUUU.", "UUUUUUU", "U.U.U.U", "U.U.U.U", "UUUUUUU", "UUUUUUU"};
static const char* IC_CONS[] = {".U...U.", ".U...U.", "UUUUUUU", "UYYYYYU", "UY5Y5YU", "UYYYYYU", "UUUUUUU"};
static const char* IC_TECH[] = {"UUUUUUU", "UKKKKKU", "UKYYYKU", "UKY5YKU", "UKKKKKU", "UUUUUUU", "..UUU.."};
static const char* IC_CRY[]  = {".YYYYY.", "Y55555Y", "Y5YYY5Y", "Y5Y5Y5Y", "Y5YYY5Y", "Y55555Y", ".YYYYY."};

static void topBar(const Game& g) {
  rect(0, 0, SCREEN_W, 12, PAL_INK);
  // 金币
  static const char* COIN[] = {".YYY.","YY5YY","YY5YY","YY5YY",".YYY."};
  sprite(4, 3, COIN, 5);
  fixTxt(12, 1, g.coins, 2, PAL_GOLD, 2);
  // 现价（右对齐，sc3）
  bool up = g.live.c >= g.live.o;
  char b[16];
  int32_t v = (int32_t)(g.price * 100.f + 0.5f);
  snprintf(b, sizeof(b), "%d.%02d", (int)(v/100), (int)(v%100));
  int w = textW(b, 3);
  int x = SCREEN_W - 4 - w - 12;
  text(x, 0, b, up ? COL_UP : COL_DN, 3);
  {   // 类别图标：股价左边
    const char** ic = IC_BANK;
    if (g.asset == AS_CONS)   ic = IC_CONS;
    if (g.asset == AS_TECH)   ic = IC_TECH;
    if (g.asset == AS_CRYPTO) ic = IC_CRY;
    spriteS(x - 18, 0, ic, 7, 2);      // 14x14，与 3 倍字高的股价等高
  }
  // 涨跌箭头（5x3 放大 2 倍）
  uint16_t ac = up ? COL_UP : COL_DN;
  for (int i = 0; i < 5; i++) {
    int h = up ? (i < 3 ? 3 - i : 0) : (i < 3 ? i + 1 : 0);
    for (int j = 0; j < h; j++) { px(SCREEN_W-14+i*2, 10-j*2, ac); px(SCREEN_W-13+i*2, 10-j*2, ac); }
  }
}

// 底栏：5 个指数轮播（每 3.5 秒换一组），看大盘脸色

static void bottomBar(const Game& g) {
  rect(0, 121, SCREEN_W, 14, PAL_INK);
  int rot = ((int)(sT / 3.5f)) % 5;                 // 轮播偏移
  int x = 3;
  for (int n = 0; n < 3; n++) {
    int k = (rot + n) % 5;
    float v = g.idx[k] > 0.f ? g.idx[k] : 1000.f;
    bool up = (v >= (g.idxPrev[k] > 0.f ? g.idxPrev[k] : v));
    text(x, 122, IDX_CODE[k], PAL_UI_D, 2);
    x += 24;
    char b[10];
    snprintf(b, sizeof(b), "%d", (int)(v + 0.5f));
    text(x, 122, b, up ? COL_UP : COL_DN, 2);
    x += textW(b, 2) + 2;
    uint16_t ac = up ? COL_UP : COL_DN;
    for (int q = 0; q < 3; q++) {
      int h = up ? 3 - q : q + 1;
      for (int j = 0; j < h; j++) { px(x + q, 131 - j, ac); px(x + q, 132 - j, ac); }
    }
    x += 7;
  }
}

static void drawPosition(const Game& g) {
  if (g.qty <= 0.f) return;
  int n = g.posStep;                        // 还剩几档：卖一次少一格
  if (n < 1) n = 1; if (n > 3) n = 3;
  for (int i = 0; i < n; i++) {
    rect(10, 100 - i*6, 26, 5, i == 0 ? PAL_DN : (i == 1 ? PAL_GOLD : PAL_UI));
    rect(10, 100 - i*6, 26, 1, PAL_UI_HI);
  }
  char b[16];
  if (g.asset == AS_CRYPTO) snprintf(b, sizeof(b), "%d", (int)(g.qty * g.price + 0.5f));  // 市值(元)
  else                      snprintf(b, sizeof(b), "%d SH", (int)(g.qty + 0.5f));           // 整股数
  text(10, 74, b, COL_UP, 2);
  int32_t pl = (int32_t)((g.price - g.avgCost) * g.qty * 100.f);   // 浮动盈亏（分）
  int32_t ap = pl >= 0 ? pl : -pl;
  char pb[16]; snprintf(pb, sizeof(pb), "%s%d.%02d", pl >= 0 ? "+" : "-", (int)(ap / 100), (int)(ap % 100));
  text(10, 56, pb, pl >= 0 ? COL_UP : COL_DN, 2);
}

static void drawEvents(const Game& g) {
  if (g.evKind == 1) {                       // 鲸鱼：水面 + 横渡 + 红框
    for (int y = 0; y < 44; y++)
      for (int x = 0; x < SCREEN_W; x++) {
        static const uint8_t BAY[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
        float tt = (y + ((x/4) & 1)) / 44.0f;
        px(x + sOx, y, (BAY[y&3][x&3]/16.0f < tt) ? PAL_SEA0 : PAL_SEA1);
      }
    for (int x = 0; x < SCREEN_W; x += 8) rect(x + ((x/8)&1)*4 + sOx, 42, 4, 2, PAL_SEA2);
    sprite(36, 12, WHALE, 17);
    rect(196, 4, 38, 30, PAL_DN_D);
    rect(198, 6, 34, 26, PAL_DN);
    text(210, 12, "!", PAL_UI_HI, 3);
  } else if (g.evKind == 2) {                // 灰犀牛：从左碾到右，看得见躲得开
    rect(0, 12, SCREEN_W, 11, PAL_DN_D);
    text(4, 13, "GRAY RHINO", PAL_UI_HI, 2);
    int x = -46 + (int)(g.evProg * (SCREEN_W + 92));
    sprite(x, 92, RHINO, 15);
    for (int i = 0; i < 3; i++) {
      int dx = x + 8 - i * 8;
      int dy = 110 - (i * 3) - ((int)(g.evProg * 40) % 5);
      if (dx > 0) rect(dx, dy, 3, 2, PAL_GRID);
    }
  } else if (g.evKind == 4) {                // 瀑布：垂直雨幕一路砸
    for (int i = 0; i < 26; i++) {
      int x = (i * 53 + 7) % SCREEN_W;
      int y0 = (int)(g.evProg * 260.f) + i * 31;
      for (int k = 0; k < 18; k++) {
        int yy = (y0 + k) % (SCREEN_H + 40) - 20;
        if (yy >= 0 && yy < SCREEN_H)
          px(x, yy, (k < 3) ? PAL_UI_HI : PAL_SEA1);
      }
    }
    rect(0, 12, SCREEN_W, 11, PAL_DN_D);
    text(4, 13, "WATERFALL", PAL_UI_HI, 2);
  } else if (g.evKind == 5) {                // 升天：绿色粒子往上冲
    for (int i = 0; i < 26; i++) {
      int x = (i * 47 + 11) % SCREEN_W;
      int y0 = SCREEN_H + 20 - (((int)(g.evProg * 280.f) + i * 29) % (SCREEN_H + 40));
      for (int k = 0; k < 18; k++) {
        int yy = y0 - k;
        if (yy >= 0 && yy < SCREEN_H)
          px(x, yy, (k < 3) ? PAL_UP_HI : PAL_UP);
      }
    }
    rect(0, 12, SCREEN_W, 11, PAL_UP_HI);
    text(4, 13, "TO THE MOON", PAL_INK, 2);
  } else if (g.evKind == 3) {                // 黑天鹅：整屏压暗，剪影登场
    for (int y = 12; y < 121; y++)
      for (int x = 0; x < SCREEN_W; x++)
        if (((x + y) & 1) == 0) px(x, y, PAL_INK);
    sprite(SCREEN_W/2 - 11, 30, SWAN, 15);
    const char* t = "BLACK SWAN";
    int tw = textW(t, 2);
    rect((SCREEN_W - tw)/2 - 4, 54, tw + 8, 15, PAL_INK);
    text((SCREEN_W - tw)/2, 56, t, PAL_UI_HI, 2);
  }
}

void renderFrame(uint16_t* fb, const Game& g, float t) {
  FB = fb;
  sOx = (g.shakeT > 0.f) ? (((int)(t * 90.f) & 1) ? 2 : -2) : 0;

  setColorMode(g.cnColors != 0);
  updateScale(g);
  sT = t;
  {
    float d = (sLastPx > 0.01f) ? (g.price - sLastPx) / sLastPx : 0.f;
    sLastPx = g.price;
    sEnergy += (d > 0.f ? d : -d) * 220.f;      // 一次跳动蓄能
    sEnergy *= 0.90f;                            // 每帧衰减
    if (sEnergy > 1.6f) sEnergy = 1.6f;
  }
  const bool crypto_ = (g.asset == AS_CRYPTO);                 // 加密：24 小时紫黑盘
  bool morning = (g.sessionCandle < DAY_CANDLES / 2);    // 前 6 根 = 早盘（暖色）
  if (crypto_)       bgDither(PAL_CRY0, PAL_CRY1, PAL_CRY2);
  else if (morning)  bgDither(PAL_WARM0, PAL_WARM1, PAL_WARM2);
  else               bgDither(PAL_BG0, PAL_BG1, PAL_BG2);
  gridLines(crypto_ ? 2 : (morning ? 1 : 0));
  if (g.bgFade > 0.f) {                      // 抖动溶解：暖 → 冷
    float k = 1.f - g.bgFade / 0.28f;
    static const uint8_t BAY2[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    for (int y = 0; y < SCREEN_H; y++) {
      float tt = (float)y / SCREEN_H;
      for (int x = 0; x < SCREEN_W; x++) {
        float th = BAY2[y & 3][x & 3] / 16.0f;
        if (k > th) FB[y*SCREEN_W + x] = (tt < th) ? PAL_BG0 : (tt < 0.66f ? PAL_BG1 : PAL_BG2);
      }
    }
  }

  // 历史：hist[0] 是最老的一根，按编号算它在屏上的槽位（左起生长）
  int oldestIdx = (int)g.sessionCandle - (int)g.histN;
  for (int i = 0; i < g.histN; i++) {
    int slot = slotOfCandle(g, (uint32_t)(oldestIdx + i));
    if (slot >= 0) drawCandle(g, slot, g.hist[i], 16, false);
  }
  int liveSlot = slotOfCandle(g, g.sessionCandle);
  if (liveSlot >= 0) drawCandle(g, liveSlot, g.live, 20, true);
  drawMarks(g);

  // 标的吉祥物 + 名字
  const char* const* masc = (g.asset == AS_BANK) ? BULL : (g.asset == AS_CONS ? BEAR : WHALEMINI);
  sprite(4, 15, masc, g.asset == AS_TECH ? 8 : 10);
  text(4, 34, stockCode(g.stockIdx), PAL_UI, 2);
  if (g.erDay == 1 && g.erResult == ER_NONE) {   // 预告日：开盘就挂倒计时（揭晓后消失）
    char eb[8];
    int left = (int)g.erCandle - (int)g.sessionCandle;
    if (left < 0) left = 0;
    snprintf(eb, sizeof(eb), "ER%d", left);
    text(4, 46, eb, left <= 1 ? PAL_DN : PAL_GOLD, 2);
  } else if (crypto_) {
    text(4, 46, "24H", PAL_UI_D, 2);
  }
  if (crypto_ && (g.erDay == 1 || (g.erDay == 2 && g.erCandle - g.sessionCandle <= 1)))
    text(4, 58, "24H", PAL_UI_D, 2);
  if (0) {
    text(4, 46, "ER!", PAL_GOLD, 2);        // 突袭日：只剩一根才泄露
  }
  if (g.erDay) {                            // 分析师预期：永远诚实
    char es[12];
    snprintf(es, sizeof(es), "EST+%d", (int)(g.erEst + 0.5f));
    text(60, 1, es, PAL_GOLD, 2);
  }

  drawPosition(g);
  if (g.unlockT > 0.f) {                  // 解锁横幅
    rect(0, 12, SCREEN_W, 11, PAL_GOLD);
    text(4, 13, "NEW STOCK GAO 3.20", PAL_INK, 2);
  }
  if (g.liqT > 0.f) {                     // 爆仓横幅
    rect(0, 12, SCREEN_W, 11, PAL_DN);
    text(4, 13, "LIQUIDATED", PAL_UI_HI, 2);
  }
  if (g.erRumorT > 0.f && g.erRumor) {    // 传闻横幅
    rect(0, 12, SCREEN_W, 11, PAL_UI_D);
    text(2, 13, erRumorText(g), PAL_INK, 2);
    text(SCREEN_W - 18, 13, "?", PAL_GOLD, 2);
  }
  if (g.panic > 0.30f) {                  // 恐慌：顶栏红灯
    text(132, 1, "PANIC", PAL_DN, 2);
  }
  drawEvents(g);
  topBar(g);
  bottomBar(g);

  // 每根蜡烛：只闪一下盈亏数字，不弹框、不停顿
  if (g.flashT <= 0.f && g.phase != PH_DAYEND && g.gainT > 0.f) {
    char b[16];
    int32_t v = g.lastGain;
    snprintf(b, sizeof(b), "%s%d", v >= 0 ? "+" : "-", (int)(v >= 0 ? v : -v));
    uint16_t c = v >= 0 ? COL_UP : COL_DN;
    int w = textW(b, 3);
    text((SCREEN_W - w)/2, 56 - (int)((1.6f - g.gainT) * 6.f), b, c, 3);
    if (g.lastFee > 0) {
      char fb[16]; snprintf(fb, sizeof(fb), "FEE-%d.%02d", (int)(g.lastFee/100), (int)(g.lastFee%100));
      text((SCREEN_W - textW(fb, 2))/2, 74, fb, g.lastFee > 0 ? PAL_UI : PAL_UI_D, 2);
    }
  }
  if (g.fxT > 0.f) {                      // 成交特效：爆点 + 股数飘字
    float p = 1.f - g.fxT / 0.5f;          // 0→1
    int y0 = mapY(g.fxPrice);
    uint16_t c = g.fxUp ? COL_UP : COL_DN;
    int bx = CX(SLOTS - 1);                // 爆点跟着活蜡烛，不固定在屏幕中央
    int r = 3 + (int)(p * 11.f);
    for (int k = 0; k < 6; k++) {          // 六道放射短线，向外扩散
      int dx = (k == 0 ? 1 : k == 1 ? 1 : k == 2 ? 0 : k == 3 ? -1 : k == 4 ? -1 : 0);
      int dy = (k == 0 ? 0 : k == 1 ? -1 : k == 2 ? -1 : k == 3 ? -1 : k == 4 ? 0 : 1);
      for (int t2 = 2; t2 < r; t2 += 2) px(bx + dx * t2 + sOx, y0 + dy * t2, c);
    }
    {   // 股数飘字：一直可见，靠亮暗交替表达"跳"
      uint16_t tc = ((int)(p * 10.f) % 2 == 0) ? c : PAL_UI_HI;
      char b[16];
      snprintf(b, sizeof(b), "%s%d SH", g.fxUp ? "+" : "-", (int)(g.fxQty + 0.5f));
      int w = textW(b, 2);
      int yy = g.fxUp ? (y0 - 22 - (int)(p * 10.f)) : (y0 + 10 + (int)(p * 10.f));
      if (yy < 14) yy = 14;
      if (yy > 108) yy = 108;
      text((SCREEN_W - w) / 2, yy, b, tc, 2);
    }
  }
  if (g.denyT > 0.f) {                    // 钱不够：按 B 才提示，平常不打扰
    rect(0, 12, SCREEN_W, 11, PAL_DN_D);
    text(4, 13, "LOW CASH", PAL_UI_HI, 2);
  }

  // 财报揭晓
  if (g.phase == PH_EARN) {
    for (int y = 0; y < SCREEN_H; y++)
      for (int x = 0; x < SCREEN_W; x++)
        if (((x + y) & 1) == 0) px(x, y, PAL_INK);
    float el = 2.8f - g.phaseT;                    // 已过时间
    if (el < 0.9f) {                               // 鼓点阶段
      const char* t = "EARNINGS";
      text((SCREEN_W - textW(t, 3))/2, 44, t, PAL_GOLD, 3);
      text((SCREEN_W - textW(stockCode(g.stockIdx), 2))/2, 68, stockCode(g.stockIdx), PAL_UI, 2);
      int dots = (int)(el * 6.f) % 4;
      int dx = (SCREEN_W - 44) / 2;
      for (int i = 0; i < dots; i++) rect(dx + i * 12, 86, 6, 6, PAL_GOLD);
    } else {                                       // 结果卡
      rect(18, 22, SCREEN_W - 36, 66, PAL_INK);
      frameRect(18, 22, SCREEN_W - 36, 66, PAL_GOLD);
      static const char* NAME[7] = { "", "BEAT", "MISS", "INLINE", "GUIDANCE", "SCANDAL", "SELL NEWS" };
      uint16_t rc = (g.erResult == ER_BEAT || g.erResult == ER_INLINE) ? COL_UP : COL_DN;
      const char* t = NAME[g.erResult];
      text((SCREEN_W - textW(t, 3))/2, 28, t, rc, 3);
      char b[24];
      snprintf(b, sizeof(b), "ACT %s%d%%", g.erGap >= 0 ? "+" : "-", (int)((g.erGap >= 0 ? g.erGap : -g.erGap) * 100.f + 0.5f));
      text((SCREEN_W - textW(b, 2))/2, 54, b, rc, 2);
      snprintf(b, sizeof(b), "EST +%d%%", (int)(g.erEst + 0.5f));
      text((SCREEN_W - textW(b, 2))/2, 69, b, PAL_UI, 2);
      if (g.erRumor) {
        const char* rt = g.erRumorTrue ? "RUMOR TRUE" : "RUMOR FAKE";
        text((SCREEN_W - textW(rt, 2))/2, 84, rt, g.erRumorTrue ? PAL_GOLD : PAL_UI_D, 2);
      }
    }
  }

  // 收盘节拍：棋盘压暗 + 当日成绩单
  if (g.phase == PH_DAYEND) {
    for (int y = 12; y < 121; y++)
      for (int x = 0; x < SCREEN_W; x++)
        if (((x + y) & 1) == 0) px(x, y, PAL_INK);
    rect(20, 26, SCREEN_W - 40, 74, PAL_INK);
    frameRect(20, 26, SCREEN_W - 40, 74, PAL_GOLD);
    const char* t = "DAY CLOSE";
    text((SCREEN_W - textW(t, 2))/2, 32, t, PAL_GOLD, 2);
    char b[20];
    int32_t v = g.dayPnl;
    snprintf(b, sizeof(b), "%s%d.%02d", v >= 0 ? "+" : "-",
             (int)((v >= 0 ? v : -v) / 100), (int)((v >= 0 ? v : -v) % 100));
    {   // 数字弹入：先大一号再落下，赢的时候四角冒火花
      float dp = g.dayFxT;
      int sc = (dp < 0.18f) ? 4 : 3;
      int w2 = textW(b, sc);
      int ty = (dp < 0.18f) ? 44 : 48;
      text((SCREEN_W - w2) / 2, ty, b, v >= 0 ? COL_UP : COL_DN, sc);
      if (v >= 0 && dp < 0.45f) {
        int sx = (dp < 0.22f) ? 3 : 5;
        rect(60 - sx, 50, sx, 1, PAL_GOLD);      rect(SCREEN_W - 60, 50, sx, 1, PAL_GOLD);
        rect(70, 44 - sx, 1, sx, PAL_GOLD);      rect(SCREEN_W - 70, 44 - sx, 1, sx, PAL_GOLD);
      }
    }
    char b2[20];
    int32_t p = g.dayPeakPnl;
    snprintf(b2, sizeof(b2), "PEAK %s%d", p >= 0 ? "+" : "-", (int)((p >= 0 ? p : -p) / 100));
    text((SCREEN_W - textW(b2, 2))/2, 74, b2, PAL_UI, 2);
    const char* t2 = "NEXT DAY";
    text((SCREEN_W - textW(t2, 2))/2, 88, t2, PAL_UI_D, 2);
  }

  if (g.flashT > 0.f) frameRect(0, 0, SCREEN_W, SCREEN_H, PAL_UI_HI);
}

// ───────── 战绩册（L 键，A/D 翻页）─────────
static void plotPoly(const float* v, int n, int x0, int y0, int w, int h,
                     float vmin, float vmax, uint16_t col, int thick) {
  if (n < 2) return;
  if (vmax - vmin < 1e-5f) vmax = vmin + 1.f;
  for (int i = 0; i < n - 1; i++) {
    int xa = x0 + i * w / (n - 1), xb = x0 + (i + 1) * w / (n - 1);
    if (i == n - 2) xb = x0 + w;
    int ya = y0 + h - (int)((v[i]     - vmin) / (vmax - vmin) * (float)h);
    int yb = y0 + h - (int)((v[i + 1] - vmin) / (vmax - vmin) * (float)h);
    if (ya < y0) ya = y0; if (ya > y0 + h) ya = y0 + h;
    if (yb < y0) yb = y0; if (yb > y0 + h) yb = y0 + h;
    int span = xb - xa; if (span < 1) span = 1;
    for (int x = xa; x <= xb; x++) {                 // 沿 x 插值，线才连得上
      float f = (float)(x - xa) / (float)span;
      int y = ya + (int)((float)(yb - ya) * f);
      for (int k = 0; k < thick; k++) px(x + k, y, col);
    }
  }
}

static void bookHeader(const Game& g) {
  rect(0, 0, SCREEN_W, 12, PAL_INK);
  static const char* COIN[] = {".YYY.","YY5YY","YY5YY","YY5YY",".YYY."};
  sprite(4, 3, COIN, 5);
  fixTxt(12, 1, g.coins, 2, PAL_GOLD, 2);
  for (int i = 0; i < 3; i++) {                        // 页面指示
    int x = SCREEN_W - 34 + i * 10;
    if (i == g.bookPage) rect(x, 4, 7, 4, PAL_GOLD);
    else                 frameRect(x, 4, 7, 4, PAL_GRID);
  }
}

// 第 1 页：今日账户曲线
static void bookToday(const Game& g) {
  float base = g.dayEq[0] > 0.f ? g.dayEq[0] : 1.f;
  int n = (int)g.sessionCandle + 1;
  if (n > 13) n = 13;
  float vmin = base, vmax = base;
  for (int i = 0; i < n; i++) {
    if (g.dayEq[i] <= 0.f) continue;
    if (g.dayEq[i] < vmin) vmin = g.dayEq[i];
    if (g.dayEq[i] > vmax) vmax = g.dayEq[i];
  }
  float pad = (vmax - vmin) * 0.18f + base * 0.004f;
  vmin -= pad; vmax += pad;

  text(8, 16, "TODAY", PAL_UI, 2);
  float cur = equityOf(g);
  float pct = (cur - base) / base * 100.f;
  char b[20];
  snprintf(b, sizeof(b), "%s%.1f%%", pct >= 0 ? "+" : "", pct);
  int w = textW(b, 2);
  text(SCREEN_W - 8 - w, 16, b, pct >= 0 ? COL_UP : COL_DN, 2);

  int cx = 16, cy = 30, cw = SCREEN_W - 32, chh = 58;
  frameRect(cx - 1, cy - 1, cw + 2, chh + 2, PAL_GRID);
  int yb = cy + chh - (int)((base - vmin) / (vmax - vmin) * (float)chh);   // 起始线
  for (int x = cx; x < cx + cw; x += 3) px(x, yb, PAL_GRID);
  if (n >= 2) plotPoly(g.dayEq, n, cx, cy, cw, chh, vmin, vmax, PAL_GOLD, 2);

  char lo[16], hi[16];
  snprintf(lo, sizeof(lo), "%d", (int)vmin); snprintf(hi, sizeof(hi), "%d", (int)vmax);
  text(cx, cy + chh + 3, lo, PAL_UI_D, 1);
  text(cx + 42, cy + chh + 3, hi, PAL_UI_D, 1);
  snprintf(b, sizeof(b), "%d", (int)cur);
  text(SCREEN_W - 8 - textW(b, 2), cy + chh + 1, b, PAL_UI_HI, 2);
}

// 第 2 页：跨日战绩 vs 大盘
static void bookHistory(const Game& g) {
  text(8, 16, "RECORD", PAL_UI, 2);
  if (g.histDays < 2) {
    text(8, 60, "NEED 2 DAYS", PAL_UI_D, 2);
    return;
  }
  int n = g.histDays;
  float e0 = g.histEq[0], i0 = g.histIdx[0] > 0.f ? g.histIdx[0] : 1000.f;
  float e[24], m[24];
  float vmin = 0.f, vmax = 0.f;
  for (int i = 0; i < n; i++) {
    e[i] = (g.histEq[i] - e0) / e0 * 100.f;
    m[i] = (g.histIdx[i] - i0) / i0 * 100.f;
    if (e[i] < vmin) vmin = e[i]; if (e[i] > vmax) vmax = e[i];
    if (m[i] < vmin) vmin = m[i]; if (m[i] > vmax) vmax = m[i];
  }
  float pad = (vmax - vmin) * 0.2f + 1.f;
  vmin -= pad; vmax += pad;

  int cx = 16, cy = 32, cw = SCREEN_W - 32, chh = 62;
  frameRect(cx - 1, cy - 1, cw + 2, chh + 2, PAL_GRID);
  int y0 = cy + chh - (int)((0.f - vmin) / (vmax - vmin) * (float)chh);
  for (int x = cx; x < cx + cw; x += 3) px(x, y0, PAL_UI_D);
  plotPoly(m, n, cx, cy, cw, chh, vmin, vmax, PAL_WHALE, 1);   // 大盘
  plotPoly(e, n, cx, cy, cw, chh, vmin, vmax, PAL_GOLD, 2);    // 我的账户

  char b[20];
  snprintf(b, sizeof(b), "YOU %+.1f%%", e[n-1]);
  text(cx, cy + chh + 4, b, PAL_GOLD, 2);
  snprintf(b, sizeof(b), "MKT %+.1f%%", m[n-1]);
  text(SCREEN_W - 8 - textW(b, 2), cy + chh + 4, b, PAL_WHALE, 2);
}

// 第 3 页：异象收集
static const char* EV_NAME[6] = { "WHALE", "RHINO", "SWAN", "FALL", "PUMP", "EARN" };
static void bookBeasts(const Game& g) {
  text(8, 16, "BEASTS", PAL_UI, 2);
  uint8_t got = 0;
  for (int i = 0; i < 6; i++) if (g.evDex[i]) got++;
  char b[12]; snprintf(b, sizeof(b), "%d/6", (int)got);
  text(SCREEN_W - 8 - textW(b, 2), 16, b, PAL_UP, 2);

  for (int i = 0; i < 6; i++) {
    int col = i % 3, row = i / 3;
    int x = 6 + col * 78, y = 28 + row * 52;
    bool has = g.evDex[i] > 0;
    rect(x, y, 70, 46, PAL_BG1);
    frameRect(x, y, 70, 46, has ? PAL_GOLD : PAL_GRID);
    if (!has) {
      text(x + 26, y + 14, "?", PAL_UI_D, 3);
      continue;
    }
    const char* const* spr = WHALE;
    int sh = 10;
    if (i == 1) { spr = RHINO; sh = 10; }
    else if (i == 2) { spr = SWAN; sh = 8; }
    if (i <= 2) {
      if (i == 2) rect(x + 4, y + 4, 30, 20, PAL_UI_D);   // 黑天鹅是黑鸟，垫一块浅底
      sprite(x + 6, y + 6, spr, sh);
    }
    else {
      // 瀑布/升天/财报：用简单符号，避免再画大精灵
      uint16_t c = (i == 3) ? PAL_DN : (i == 4 ? PAL_UP : PAL_GOLD);
      for (int k = 0; k < 7; k++) {
        int hh = (i == 4) ? k + 1 : 7 - k;
        rect(x + 8 + k * 3, y + 6 + (7 - hh), 2, hh, c);
      }
    }
    char nb[10]; snprintf(nb, sizeof(nb), "X%d", (int)g.evDex[i]);
    text(x + 6, y + 30, EV_NAME[i], PAL_UI_D, 1);
    int w2 = textW(nb, 2);
    text(x + 70 - 4 - w2, y + 28, nb, PAL_UI_HI, 2);
  }
}

void renderBook(uint16_t* fb, const Game& g, float t) {
  FB = fb; sOx = 0; (void)t;
  bgDither(PAL_BG0, PAL_BG1, PAL_BG2);
  bookHeader(g);
  if (g.bookPage == 0)      bookToday(g);
  else if (g.bookPage == 1) bookHistory(g);
  else                      bookBeasts(g);
  rect(0, SCREEN_H - 12, SCREEN_W, 12, PAL_INK);
  // 左：A PREV
  text(6, SCREEN_H - 11, "A", PAL_GOLD, 2);
  text(15, SCREEN_H - 9, "PREV", PAL_UI_D, 1);
  // 中：L CLOSE
  text(SCREEN_W / 2 - textW("L CLOSE", 1) / 2, SCREEN_H - 9, "L CLOSE", PAL_UI, 1);
  // 右：D NEXT
  int rw = textW("NEXT", 1);
  text(SCREEN_W - 8 - rw, SCREEN_H - 9, "NEXT", PAL_UI_D, 1);
  text(SCREEN_W - 8 - rw - 9, SCREEN_H - 11, "D", PAL_GOLD, 2);
}

// ───────── 启动画面（任意键进入）─────────
void renderTitle(uint16_t* fbp, const Game& g, float t, bool resumed) {
  FB = fbp;
  bgDither(PAL_INK, PAL_BG0, PAL_BG1);
  gridLines(0);
  for (int x = 0; x < SCREEN_W; x += 4) px(x, 4, PAL_UI_D);       // 顶部灯带

  // 大标题
  const char* nm = "TICKER";
  int sc = 3, w = textW(nm, sc);
  int tx = (SCREEN_W - w) / 2;
  for (int dx = -1; dx <= 1; dx += 2)                              // 描边阴影，压住点阵背景
    text(tx + dx, 14, nm, PAL_GOLD_D, sc);
  text(tx, 14, nm, PAL_GOLD, sc);

  const char* sub = "DESK MARKET SIM";
  text((SCREEN_W - textW(sub, 2)) / 2, 42, sub, PAL_UI_HI, 2);

  char ver[24];
  snprintf(ver, sizeof(ver), "V0.1.0%s", resumed ? "  RESUME" : "");
  text((SCREEN_W - textW(ver, 2)) / 2, 58, ver, resumed ? PAL_GOLD : PAL_UI_D, 2);

  // 装饰：一排小蜡烛（静态，只为气质）
  int base = 92;
  for (int i = 0; i < 9; i++) {
    int x = 37 + i * 20;
    int hh = 5 + ((i * 7) % 5) * 3;
    bool up = ((i * 3) % 4) != 0;
    uint16_t c = up ? PAL_DN : PAL_UP;          // 美股惯例：红涨绿跌（默认配色）
    rect(x, base - hh, 5, hh, c);
    rect(x + 2, base - hh - 4, 1, 4, c);
  }

  // 键位
  {   // 两组对称排：B BUY ｜ S SELL
    int x1 = 54, x2 = 126;
    text(x1, 102, "B", PAL_UP, 2);
    text(x1 + 12, 102, "BUY", PAL_UI_HI, 2);
    text(x2, 102, "S", PAL_DN, 2);
    text(x2 + 12, 102, "SELL", PAL_UI_HI, 2);
  }

  // 任意键（呼吸闪烁）
  if (((int)(t * 1.6f) & 1) == 0) {
    const char* pk = "PRESS ANY KEY";
    text((SCREEN_W - textW(pk, 2)) / 2, 118, pk, PAL_UI_HI, 2);
  }
}

void renderHelp(uint16_t* fb, const Game& g, float t) {
  FB = fb; sOx = 0;
  bgDither(PAL_BG0, PAL_BG1, PAL_BG2);
  rect(6, 6, SCREEN_W - 12, SCREEN_H - 12, PAL_INK);
  frameRect(6, 6, SCREEN_W - 12, SCREEN_H - 12, PAL_UI);
  const char* lines[] = {
    "TICKER",
    "DESK MARKET",
    "B / SPACE  BUY",
    "S SELL 1/3 (3X = OUT)",
    "TAB  DEX   H HELP",
    "M MUTE   C RED/GREEN",
    "A/D TURN BOOK PAGES",
    "PANIC = STILL FALLING",
    "BTC = GAMBLE 24H",
  };
  for (int i = 0; i < 7; i++)
    text(16, 14 + i * 14, lines[i], i == 0 ? PAL_GOLD : PAL_UI_HI, 2);
  (void)g; (void)t;
}
