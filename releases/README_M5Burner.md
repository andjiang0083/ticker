# 《行情机》TICKER — 刷机说明 / Flashing guide

**设备 / Device:** M5Stack Cardputer ADV（ESP32-S3, 240×135）

---

## 方式一：M5Burner（推荐 / recommended）

1. 打开 [M5Burner](https://burner.m5stack.com) 或在 M5Burner 桌面端搜索 **TICKER**（作者 `andjiang`）
2. 选择设备 **Cardputer**
3. 点 Burn / 烧录，等待完成
4. 按一下复位键，屏幕出现启动画面 → 任意键进入游戏

> ⚠️ 商店里下载的是**整包镜像**，请让 M5Burner 自己烧（它固定从 0x0 写入）。
> 用别的工具烧这个文件时也要从 **0x0** 开始。

## 方式二：esptool 命令行

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX --baud 460800 \
  write_flash 0x0 ticker-0.1.0-merged.bin
```

Windows 把端口换成 `COM3` 之类；`--baud` 遇到不稳定可以降到 115200。

## 方式三：从源码

```bash
git clone https://github.com/andjiang0083/ticker
cd ticker
pio run -e cardputer -t upload
```

---

## 操作 / Controls

| 键 / Key | 作用 / Action |
|---|---|
| `B` / `空格` | 买入（现金的 1/3，最多三档） / Buy a third |
| `S` | 卖出（一档，最后一档清仓） / Sell one tranche |
| `L` / `回车` | 战绩册（`A`/`D` 翻页） / Stock book |
| `H` | 帮助 / Help |
| `M` | 静音 / Mute |
| `C` | 红绿互换（默认红涨绿跌） / Swap red & green |

## 玩法要点 / How it plays

- 一个交易日 = 12 根蜡烛（6 分钟），日终强制平仓，第二天换一只标的
- 一屏 6 根蜡烛，最右白框的是**活蜡烛**，只在它上面买卖
- **你的单子会推动价格**：越大的单子滑点越狠
- 异象（鲸鱼 / 灰犀牛 / 黑天鹅 / 瀑布 / 升天）方向严格随机，**不是买入信号**
- 财报 36% 预告、9% 突袭；传闻只有 55% 是真的
- 2 分钟不按键自动熄屏省电，任意键唤醒
- 持仓中关机再开机会接着玩（价格回到上一根收盘）

## 已知限制 / Known limits

- 没有联网、没有真实行情，全部是本地模拟
- 存档只在本机（NVS）；换固件或整包重刷会清空

---

版本 / Version: **0.1.0**
校验 / SHA-256: 见同目录 `ticker-0.1.0-merged.bin.sha256`
