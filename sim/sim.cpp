// TICKER — PC 模拟器：出帧到 PPM + 经济模拟（与固件共用 game.cpp / render.cpp）
#include "../src/game.h"
#include "../src/render.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

static uint16_t fb[SCREEN_W * SCREEN_H];

static void dumpPPM(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) { printf("open fail %s\n", path); return; }
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    uint16_t v = fb[i];
    unsigned char p[3] = {
      (unsigned char)(((v >> 11) & 0x1F) * 255 / 31),
      (unsigned char)(((v >> 5) & 0x3F) * 255 / 63),
      (unsigned char)((v & 0x1F) * 255 / 31)
    };
    fwrite(p, 1, 3, f);
  }
  fclose(f);
  printf("wrote %s\n", path);
}

// 傻瓜策略：低于本局均价 1% 就买，高于 1.2% 就卖（用于经济验证）
static int gStocksOnly = 0;

static void scriptedInput(const Game& g, Input& in) {
  in = Input{};
  if (gStocksOnly == 3) {                         // 基准策略：整天持有
    if (g.qty <= 0.f && g.sessionCandle == 1 && g.roundT > 2.f) in.buy = true;
    if (g.qty > 0.f && g.sessionCandle >= 10) in.sell = true;
    return;
  }
  // 熟练玩家模型：深跌抄底，成本/开盘上方止盈
  if (g.qty <= 0.f) {
    if (g.price < g.live.o * (gStocksOnly == 2 ? 0.97f : 0.992f)) in.buy = true;
  } else if (g.price > g.avgCost * (gStocksOnly == 2 ? 1.045f : 1.010f)) in.sell = true;
}

int main(int argc, char** argv) {
  const char* mode = (argc > 1) ? argv[1] : "main";
  Game g; gameInit(g);
  if (argc > 3) g.rng = (uint32_t)atoi(argv[3]);   // 换种子采样

  if (!strcmp(mode, "econ2")) gStocksOnly = 1;
  if (!strcmp(mode, "econ3")) gStocksOnly = 2;
  if (!strcmp(mode, "hold"))  gStocksOnly = 3;
  if (!strcmp(mode, "drift")) {
    int n = (argc > 2) ? atoi(argv[2]) : 200;
    double sum = 0, mn = 1e9, mx = -1e9; int bigU = 0, bigD = 0, cnt = 0;
    uint32_t startR = g.rounds, lastR = g.rounds;
    float prev = g.price;
    int guard = 0;
    while ((int)(g.rounds - startR) < n && guard++ < 4000000) {
      Input in{};
      gameUpdate(g, in, 1.f / 30.0f);
      uint8_t sfx; while (gamePopSfx(g, sfx)) {}
      if (g.rounds != lastR) {                       // 一根收完
        lastR = g.rounds;
        if (getenv("DBG") && (g.sessionCandle == 2 || g.sessionCandle == 11))
          printf("  [c%2u] price %.2f  coins %.2f  qty %.4f  sc %u\n",
                 g.sessionCandle, g.price, g.coins/100.0, g.qty, g.scenario);
        if (g.sessionCandle > 1) {                   // 换日首根不计（换了标的，价格不可比）
          double ch = ((double)g.price / prev - 1.0) * 100.0;
          sum += ch; cnt++;
          if (ch > 5) bigU++;
          if (ch < -5) bigD++;
          if (ch < mn) mn = ch;
          if (ch > mx) mx = ch;
        }
        prev = g.price;
      }
    }
    printf("candles %d  mean %+.3f%%/root  total %+.1f%%  >+5%%: %d  <-5%%: %d  [dbg rounds %u phase %d guard %d]\n",
           cnt, cnt ? sum / cnt : 0.0, sum, bigU, bigD, g.rounds - startR, (int)g.phase, guard);
    printf("range %.1f%% .. %.1f%%\n", mn, mx);
    {   // 逐日：首根收盘 → 末根收盘
      Game t = g;
      printf("last day: sessionCandle %u  price %.2f dayOpen %.2f scenario %u panic %.2f\n",
             t.sessionCandle, t.price, t.dayOpen, t.scenario, t.panic);
    }
    return 0;
  }

  if (!strcmp(mode, "econ") || !strcmp(mode, "econ2") || !strcmp(mode, "econ3") || !strcmp(mode, "hold")) {
    int rounds = (argc > 2) ? atoi(argv[2]) : 100;
    int64_t eqMin = 1LL << 40, eqMax = -1;
    int dead = 0, still = 0, liq = 0, erDays = 0, cryptoDays = 0;
    uint32_t startR = g.rounds, lastR = startR;
    int64_t lastEq = (int64_t)g.coins + (int64_t)(g.price * 100.f * g.qty);
    int guard = 0;
    while ((int)(g.rounds - startR) < rounds && guard++ < 4000000) {
      Input in; scriptedInput(g, in);
      gameUpdate(g, in, 1.f / 30.0f);
      uint8_t s; while (gamePopSfx(g, s)) {}
      if (g.rounds != lastR) {                       // 一根蜡烛结算一次
        lastR = g.rounds;
        int64_t eq = (int64_t)g.coins + (int64_t)(g.price * 100.f * g.qty);
        if (eq < eqMin) eqMin = eq;
        if (eq > eqMax) eqMax = eq;
        if (eq == lastEq) { if (++still > 2) dead++; } else still = 0;
        lastEq = eq;
      }
    }
    printf("\n=== SUMMARY ===\ncandles %d\nfinal equity %.2f  (start %.2f)\nmin %.2f  max %.2f\ndead-candles %d / %d\ninvested %.0f returned %.0f\n",
           (int)(g.rounds - startR), lastEq / 100.0, 600.0, eqMin / 100.0, eqMax / 100.0,
           dead, (int)(g.rounds - startR), g.invested, g.returned);
    return 0;
  }

  // 出帧模式：先跑若干局，再按 mode 摆到指定状态
  auto runFor = [&](float sec) {
    float t = 0.f;
    while (t < sec) {
      Input in;
      scriptedInput(g, in);
      gameUpdate(g, in, 1.0f / 30.0f);
      t += 1.0f / 30.0f;
      uint8_t s; while (gamePopSfx(g, s)) {}
    }
  };

  if (!strcmp(mode, "title")) {                 // 启动画面
    renderTitle(fb, g, 3.0f, false); dumpPPM("title0.ppm");
    renderTitle(fb, g, 3.2f, true);  dumpPPM("title1.ppm");
  } else if (!strcmp(mode, "help")) {                  // 帮助页
    renderHelp(fb, g, 3.0f); dumpPPM("helppage.ppm");
  } else if (!strcmp(mode, "resumeframe")) {           // 续玩开局的样子
    for (int i = 0; i < 7; i++) { runFor(ROUND_SEC + 0.05f); while (g.phase != PH_TRADE) runFor(0.2f); }
    uint8_t si = g.stockIdx;
    Game t{g};
    gameResume(t, si, (uint8_t)g.sessionCandle);   // 模拟关机重开
    for (int i = 0; i < 120; i++) { Input in{}; gameUpdate(t, in, 1.f/30.f); }
    renderFrame(fb, t, 3.0f); dumpPPM("resumeframe.ppm");
    printf("sessionCandle %u histN %d\n", (unsigned)t.sessionCandle, (int)t.histN);
  } else if (!strcmp(mode, "cash")) {                  // 现金 vs 股价 的买入判定
    int32_t cashs[4] = { 20000, 30000, 40000, 60000 };
    float   pxs[3]   = { 100.f, 142.f, 300.f };
    for (int a = 0; a < 4; a++) for (int b = 0; b < 3; b++) {
      Game t{g};
      t.coins = cashs[a]; t.qty = 0.f; t.avgCost = 0.f; t.posStep = 0;
      t.price = pxs[b]; t.pxCoin[t.stockIdx % NSTOCK] = pxs[b];
      t.asset = AS_BANK; t.phase = PH_TRADE; t.denyT = 0.f;
      float c0 = t.coins / 100.f;
      Input in{}; in.buy = true; gameUpdate(t, in, 1.f/30.f);
      printf("现金%-7.0f 价%-6.0f -> %s 买%.0f股 剩%.2f\n", c0, pxs[b],
             t.denyT > 0.f ? "LOW CASH" : "OK", t.qty, t.coins / 100.f);
    }
  } else if (!strcmp(mode, "fx")) {                    // 成交特效帧（手工摆状态）
    for (int i = 0; i < 5; i++) runFor(ROUND_SEC + 0.05f);
    while (g.phase != PH_TRADE) runFor(0.2f);
    g.fxT = 0.30f; g.fxUp = 1; g.fxQty = 12.f; g.fxPrice = g.price;
    renderFrame(fb, g, 3.0f); dumpPPM("fxbuy.ppm");
    printf("BUYFX up=%u qty=%.0f fxT=%.2f price=%.2f\n", (unsigned)g.fxUp, g.fxQty, g.fxT, g.price);
    g.fxT = 0.30f; g.fxUp = 0; g.fxQty = 4.f;
    renderFrame(fb, g, 3.0f); dumpPPM("fx_sell.ppm");
    printf("SELLFX done\n");
  } else if (!strcmp(mode, "impact")) {                // 冲击/滑点验证（空手，不用机器人）
    auto step = [&](float sec) {
      for (float t = 0.f; t < sec; t += 1.f/30.f) { Input in{}; gameUpdate(g, in, 1.f/30.f); }
    };
    step(5.f);
    printf("起始 %s %.2f  现金 %.2f\n", stockCode(g.stockIdx), g.price, g.coins/100.f);
    for (int i = 0; i < 3; i++) {
      { Input in{}; in.buy = true; gameUpdate(g, in, 1.f/30.f); }
      printf("第%d次买 均价%.2f 现价 %.2f  flow %+.2f%%  股数 %.0f\n",
             i+1, g.avgCost, g.price, g.flow*100.f, g.qty);
    }
    for (int c = 0; c < 3; c++) {
      step(ROUND_SEC + 0.1f);
      printf("收盘%d 价 %.2f flow %+.2f%% candle %u qty %.1f step %u cash %.2f\n", c+1, g.price, g.flow*100.f,
             (unsigned)g.sessionCandle, g.qty, (unsigned)g.posStep, g.coins/100.f);
    }
    printf("卖前 价 %.2f flow %+.2f%% qty %.1f step %u\n", g.price, g.flow*100.f, g.qty, (unsigned)g.posStep);
    { Input in{}; in.sell = true; gameUpdate(g, in, 1.f/30.f); }
    printf("卖出后 价 %.2f flow %+.2f%% 现金 %.2f qty %.1f\n", g.price, g.flow*100.f, g.coins/100.f, g.qty);
  } else if (!strcmp(mode, "traits")) {                // 性格倍率表
    for (int i = 0; i < NSTOCK; i++) {
      Game t{g}; gameTraitsFor(t, (uint8_t)i);
      uint8_t tr = stockTrait((uint8_t)i);
      printf("%-4s tr%02x  异象x%.2f 鲸鱼x%.2f 抗跌x%.2f 造假x%.2f\n",
             stockCode((uint8_t)i), (unsigned)tr, t.evMul, t.whaleMul, t.shockMul, t.scandalMul);
    }
  } else if (!strcmp(mode, "kernel")) {                // 性格与因果验证：逐日统计
    uint16_t prev[6] = {0,0,0,0,0,0};
    float lo, hi;
    for (int d = 0; d < 26; d++) {
      lo = 1e9f; hi = 0.f;
      int ev = 0;
      for (int c = 0; c < 12; c++) {
        float p0 = g.price;
        runFor(ROUND_SEC + 0.1f);
        if (g.price > hi) hi = g.price;
        if (g.price < lo) lo = g.price;
        while (g.phase != PH_TRADE) runFor(0.2f);
        (void)p0;
      }
      for (int i = 0; i < 6; i++) { ev += (int)(g.evDex[i] - prev[i]); }
      for (int i = 0; i < 6; i++) prev[i] = g.evDex[i];
      float rng = (hi - lo) / lo * 100.f;
      printf("d%02d %-3s tr%02x ev%d rng%5.1f%% drift%+.0f%% taint%u\n",
             d, stockCode(g.stockIdx), (unsigned)stockTrait(g.stockIdx), ev, rng,
             g.dayDrift * 100.f, (unsigned)g.taint[g.stockIdx % NSTOCK]);
    }
    printf("unlocked %u\n", (unsigned)g.unlocked);
  } else if (!strcmp(mode, "resume")) {                // 续玩验证：买入 → 模拟关机 → 开机恢复
    runFor(40.f);
    { Input in{}; in.buy = true; gameUpdate(g, in, 1.f/30.f); }
    runFor(30.f);
    uint8_t si = g.stockIdx, sc = g.sessionCandle;
    float pxSave = g.pxCoin[si], qty0 = g.qty, avg0 = g.avgCost, coins0 = g.coins/100.f;
    int32_t step0 = g.posStep;
    printf("[resume] 关机前: %s qty %.4f avg %.2f cash %.2f step %u candle %u px %.2f\n",
           stockCode(si), qty0, avg0, coins0, (unsigned)step0, (unsigned)sc, pxSave);
    Game g2; gameInit(g2);                       // 相当于重启
    g2.coins = g.coins; g2.pxCoin[si] = pxSave;
    g2.qty = qty0; g2.avgCost = avg0; g2.posStep = step0;
    gameResume(g2, si, sc);
    printf("[resume] 开机后: %s qty %.4f avg %.2f cash %.2f step %u candle %u px %.2f phase %u\n",
           stockCode(g2.stockIdx), g2.qty, g2.avgCost, g2.coins/100.f, (unsigned)g2.posStep,
           (unsigned)g2.sessionCandle, g2.price, (unsigned)g2.phase);
    for (int i = 0; i < 1800; i++) { Input in{}; gameUpdate(g2, in, 1.f/30.f); }   // 60 秒
    printf("[resume] 续跑 60 秒: candle %u px %.2f 权益 %.2f\n",
           (unsigned)g2.sessionCandle, g2.price, (g2.coins + g2.price*100.f*g2.qty)/100.f);
  } else if (!strcmp(mode, "book")) {                  // 三页战绩册
    for (int d = 0; d < 3; d++) {
      for (int c = 0; c < 12; c++) {
        runFor(ROUND_SEC + 0.2f);
        while (g.phase != PH_TRADE) runFor(0.2f);
      }
    }
    runFor(ROUND_SEC * 5.f);
    while (g.phase != PH_TRADE) runFor(0.2f);
    printf("[book] evDex %u %u %u %u %u %u\n", g.evDex[0],g.evDex[1],g.evDex[2],g.evDex[3],g.evDex[4],g.evDex[5]);
    printf("[book] days %u candle %u eq %.2f idx %.1f\n",
           (unsigned)g.histDays, (unsigned)g.sessionCandle,
           (g.coins + g.price * 100.f * g.qty) / 100.f, g.idx[0]);
    for (int pgi = 0; pgi < 3; pgi++) {
      g.bookPage = (uint8_t)pgi;
      renderBook(fb, g, 1.0f);
      char path[64]; snprintf(path, sizeof(path), "book%d.ppm", pgi);
      dumpPPM(path);
    }
  } else if (!strcmp(mode, "icons")) {                 // 四类图标各出一帧
    for (int k = 0; k < 4; k++) {
      uint8_t want = (k == 0) ? 0 : (k == 1) ? 3 : (k == 2) ? 6 : 9;   // BANK/CONS/TECH/CRYPTO
      gameInit(g); g.stockIdx = want; g.asset = stockAsset(want);
      runFor(3.0f);
      renderFrame(fb, g, 3.0f);
      char path[64]; snprintf(path, sizeof(path), "icon%d.ppm", k);
      dumpPPM(path);
      printf("icon %d asset %u price %.2f\n", k, (unsigned)g.asset, g.price);
    }
  } else if (!strcmp(mode, "selltest")) {              // 三买三卖对等性验证
    runFor(2.0f);
    for (int i = 0; i < 3; i++) { Input in{}; in.buy = true; gameUpdate(g, in, 1.f/30.f); }
    printf("after 3 buys : qty %.5f step %u coins %.2f\n", g.qty, (unsigned)g.posStep, g.coins/100.0);
    for (int i = 0; i < 4; i++) {
      Input in{}; in.sell = true; gameUpdate(g, in, 1.f/30.f);
      printf("after sell %d: qty %.5f step %u coins %.2f\n", i+1, g.qty, (unsigned)g.posStep, g.coins/100.0);
    }
  } else if (!strcmp(mode, "days")) {                 // 每天第一根的方向分布
    int up = 0, dn = 0;
    for (int d = 0; d < 40; d++) {
      float o0 = 0.f, c0 = 0.f;
      runFor(ROUND_SEC * 0.98f);
      const Candle& c = g.hist[g.histN ? g.histN - 1 : 0];
      o0 = c.o; c0 = c.c;
      printf("day %2d  ctl %+.2f/%+.2f/%+.2f -> %+.2f  candle0 %+.2f%%  range %.2f%%\n",
             d, g.ctl[0]*100, g.ctl[1]*100, g.ctl[2]*100, g.ctl[3]*100,
             (c0 - o0) / o0 * 100.f, (c.h - c.l) / c.o * 100.f);
      if (c0 >= o0) up++; else dn++;
      while (g.phase != PH_TRADE) { Input in{}; gameUpdate(g, in, 1.f/30.f); }
    }
    printf("candle0 up %d / down %d\n", up, dn);
  } else if (!strcmp(mode, "open")) {                // 开盘第一根（histN==0）
    runFor(12.0f);
    printf("[open] histN %u candle %u price %.2f dayOpen %.2f\n",
           (unsigned)g.histN, (unsigned)g.sessionCandle, g.price, g.dayOpen);
  } else if (!strcmp(mode, "amp")) {                 // 量蜡烛振幅
    for (int r = 0; r < 8; r++) {
      runFor(ROUND_SEC + 0.05f);
      const Candle& c = g.hist[g.histN - 1];
      printf("candle %d  range %.2f%%  body %.2f%%  o %.2f\n", r,
             (c.h - c.l) / c.o * 100.f, (c.c > c.o ? c.c - c.o : c.o - c.c) / c.o * 100.f, c.o);
    }
  } else if (!strcmp(mode, "crypto") || !strcmp(mode, "waterfall") || !strcmp(mode, "pump")) {
    g.stockIdx = 9; g.asset = AS_CRYPTO;
    g.dayOpen = 6420.f; g.price = g.dayOpen; g.scBase = g.price;
    for (int i = 0; i < 3; i++) runFor(ROUND_SEC + 0.9f);
    printf("crypto price after 3 candles: %.2f (open %.2f)\n", g.price, g.dayOpen);
    if (!strcmp(mode, "waterfall")) { g.evKind = 4; g.evT = 2.2f; g.evProg = 0.45f; runFor(0.5f); g.evKind = 4; g.evT = 1.9f; g.evProg = 0.5f; }
    if (!strcmp(mode, "pump"))      { g.evKind = 5; g.evT = 2.0f; g.evProg = 0.5f; runFor(0.4f); g.evKind = 5; g.evT = 1.8f; g.evProg = 0.55f; }
    printf("final: %.2f  coins %.2f\n", g.price, g.coins / 100.f);
  } else if (!strcmp(mode, "er") || !strcmp(mode, "ereveal") || !strcmp(mode, "erpost")) {
    g.erDay = 1; g.erCandle = 6; g.erEst = 9.f;
    if (!strcmp(mode, "er")) {
      runFor(ROUND_SEC * 4 + 1.0f);
      g.erRumor = 1; g.erRumorKind = 3; g.erRumorGood = 0; g.erRumorTrue = 1; g.erRumorT = 5.f;
    } else {
      int guard = 0;
      while (g.phase != PH_EARN && guard++ < 300000) {
        Input in; scriptedInput(g, in);
        gameUpdate(g, in, 1.0f / 30.0f);
        uint8_t s; while (gamePopSfx(g, s)) {}
      }
      g.erResult = ER_BEAT; g.erGap = 0.12f;
      if (!strcmp(mode, "ereveal")) g.phaseT = 1.6f;
      else {                                   // 落地后跑两根，看趋势反转
        guard = 0;
        while (g.phase == PH_EARN && guard++ < 1000) { Input in; gameUpdate(g, in, 1.0f/30.0f); }
        runFor(ROUND_SEC * 2.1f);
      }
    }
  } else if (!strcmp(mode, "day2")) {
    int guard = 0;
    while ((g.rounds < 13 || g.phase == PH_DAYEND) && guard++ < 400000) {   // 跨过第一个收盘
      Input in; scriptedInput(g, in);
      gameUpdate(g, in, 1.0f / 30.0f);
      uint8_t s; while (gamePopSfx(g, s)) {}
    }
  } else if (!strcmp(mode, "dayend")) {
    // 跑满 12 根进入收盘节拍
    int guard = 0;
    while (g.phase != PH_DAYEND && guard++ < 200000) {
      Input in; scriptedInput(g, in);
      gameUpdate(g, in, 1.0f / 30.0f);
      uint8_t s; while (gamePopSfx(g, s)) {}
    }
  } else if (!strcmp(mode, "one")) {
    runFor(ROUND_SEC + 1.0f);      // 只跑一局：验证历史蜡烛还能显示
    runFor(6.f);
  } else if (!strcmp(mode, "main")) {
    // 跑 4 局半，攒 4 根历史蜡烛 + 半个活蜡烛
    for (int i = 0; i < 5; i++) { runFor(ROUND_SEC + 1.4f); }
    runFor(14.f);
  } else if (!strcmp(mode, "whale")) {
    for (int i = 0; i < 4; i++) runFor(ROUND_SEC + 1.4f);
    g.evKind = 1; g.evT = 2.0f; g.evProg = 0.45f; g.shakeT = 0.6f; g.evAt = -1.f;
    runFor(1.4f);
    g.evKind = 1; g.evT = 1.0f; g.evProg = 0.55f;
  } else if (!strcmp(mode, "rhino")) {
    for (int i = 0; i < 2; i++) runFor(ROUND_SEC + 1.4f);
    g.evKind = 2; g.evT = 5.0f; g.evProg = 0.55f; g.evAt = -1.f;
    runFor(1.0f);
    g.evKind = 2; g.evT = 4.0f; g.evProg = 0.55f;
  } else if (!strcmp(mode, "swan")) {
    for (int i = 0; i < 2; i++) runFor(ROUND_SEC + 1.4f);
    g.evKind = 3; g.evT = 1.2f; g.evProg = 0.3f; g.evAt = -1.f;
    runFor(0.4f);
    g.evKind = 3; g.evT = 1.0f; g.evProg = 0.3f;
  } else if (!strcmp(mode, "fade")) {
    for (int i = 0; i < 5; i++) runFor(ROUND_SEC + 1.4f);
    runFor(ROUND_SEC - 0.5f);
    g.sessionCandle = 6; g.bgFade = 0.14f;
  } else if (!strcmp(mode, "settle")) {
    for (int i = 0; i < 4; i++) runFor(ROUND_SEC + 1.4f);
    runFor(ROUND_SEC - 0.2f);
    g.phase = PH_SETTLE; g.phaseT = 0.8f; g.gainT = 1.0f; g.lastGain = 37; g.flashT = 0.1f;
  } else if (!strcmp(mode, "dex")) {
    g.dex[AS_BANK] = 4; g.dex[AS_CONS] = 2; g.dex[AS_TECH] = 1;
    g.coins = 1284;
  }

  if (!strcmp(mode, "dex")) renderBook(fb, g, 0.f);
  else                      renderFrame(fb, g, 3.0f);

  char path[256];
  snprintf(path, sizeof(path), "%s.ppm", mode);
  dumpPPM(path);
  return 0;
}
