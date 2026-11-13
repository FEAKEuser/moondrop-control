# MOONDROP GAIA 协议实测笔记

本文件记录逆向出来的协议细节。除非某一节另行注明，命令与字节格式都是**在本机
MOONDROP EDGE（固件 1.4.0）上逐条实测确认**的；第 11 节是同一台机器上对
**Moondrop Nekocake（猫饼，固件 1.0.0）**的实测记录。
**这两台是本项目唯一接过的真机**，文档里其余机型（Robin / Pudding / Space Travel）
的内容均摘自上游公开资料，并未在本项目验证过。
所有命令均可用 `moondrop-cli raw FEATURE COMMAND [HEX]` 复现。

---

## 1. 传输层

* **RFCOMM (SPP)**，标准 SPP UUID `00001101-0000-1000-8000-00805F9B34FB`
* **通道号**：EDGE 上是 **channel 1**（不是资料常说的 16）
  * Space Travel 用 16；EDGE / 部分新机型用 1
  * 因此本程序按 «上次成功 → 1 → 16 → 2..30» 顺序自动探测
* 通道正确但无人应答时（例如连到 A2DP 端口）连接会成功但没有数据；本程序以
  「首帧 GAIA 响应」作为通道判定依据
* 同一时刻只能有一个 RFCOMM 连接，第二个连接返回 `EBUSY`（同一副耳机上所有通道共享
  这唯一一个槽位，因此 EBUSY 只能说明「槽位被占」，无法说明通道号对不对）；
  占位者可能是别的程序，也可能是自己上一次尚未完成回收的连接
* 通道扫描是**串行**的：并发探测会互相抢占上面那个唯一槽位，让正确通道反而拿到 EBUSY
* 内核拒绝不存在的通道只需 ~35 ms，所以正确通道能在 1 秒内确认；对「连上但不回包」
  的通道另有超时上限，不会让它拖住整个扫描
* 普通用户即可连接，无需 root，无需 `libbluetooth`

## 2. 帧格式

```
FF | 04 | len_hi len_lo | vendor_hi vendor_lo | cmd_hi cmd_lo | payload...
 0    1      2     3           4        5           6      7         8..
```

| 字段 | 说明 |
| --- | --- |
| `FF` | 起始字节 |
| `04` | 协议版本（EDGE 固件为 4；资料里有时写作 01，**以设备实际发送的为准**） |
| `len` | 大端 16 位，**仅 payload 长度**（不含 8 字节头） |
| `vendor` | 大端 16 位，`0x001D` = 水月雨/QTiL，`0x000A` = GAIA 通用 |
| `cmd` | 大端 16 位，`(feature << 9) \| (type << 7) \| command` |
| `type` | 0 = COMMAND，1 = NOTIFICATION，2 = RESPONSE，3 = ERROR |

实测例子（查固件版本）：

```
TX  ff 04 00 00 00 1d 01 05
RX  ff 04 00 05 00 1d 01 05 31 2e 34 2e 30      → "1.4.0"
```

响应里 `feature` 保持请求里的值（`01`），**不是**某些资料写的固定 0x01 或 0x03。

## 3. 时序 / 队列（重要）

固件是**单任务**的：

* 连续快速发送多条命令时，只有少数几条会被处理，其余被**静默丢弃**（没有响应也没有错误）。
* 因此必须**串行发送**、等回应再发下一条。
* **切换降噪后固件忙 ~2 秒**：这期间发送的任何命令都会被忽略，包括重试。
  实测：`SET mode=4` 后，`GET cmd=3` 在 2.08 s 才返回新值。
  （只做读写、不切换降噪时，响应在 ~0.4 s 内。）
* `SET` 类命令（降噪、PEQ 写入）**没有 ACK**，需要之后回读校验。
* 39 字节的 PEQ 写入偶尔会丢一次，重写即可成功 —— 程序会自动重试并回读比对。

## 4. 能力探测

```
TX  ff 04 00 00 00 1d 00 01                    (BASIC / GET_SUPPORTED_FEATURES)
RX  ... payload = 00 03 01 05 01 01 01 06 02 00 02 08 01 0f 01 10 01 11 01 14 01 15 01 0d 01
```

payload 是 **(feature, version) 键值对**，可含一个前导状态字节
（真实 feature id 不会连续递增，据此可判断是否有 1 字节前缀）。EDGE 返回：

```
0 BASIC   1 EARBUD   2 ANC(V1)   3 VOICE_UI   5 EQ   6 UPGRADE   8 AUDIO_CURATION
13 BATTERY   15 DAC_GAIN   16 CODEC   17 LIGHT_SENSOR   20 MULTIPOINT   21 BT_ADDRESS
```

> 注意：算法上「按 32 位 word 解析位图」（FxxkMoondrop 的做法）解析同一份 payload 会得到
> 完全不同的集合（0,1,2,3,5,6,8,13,15,16,17,20,21 —— 恰好也能解释得通），
> 但**键值对解释与实际可用的命令一致**（例如不支持 GESTURE(11)/SPATIAL(18)/LED(19)，
> 实测这些命令确实无响应；而无分隔的位图解释在这些 feature 上自相矛盾）。
> 本程序用键值对解释，并在探测失败时回退为「全部尝试」。

## 5. 命令表（EDGE 实测）

`✓` = 有响应，`✗` = 无响应（固件未实现）

| Feature | Cmd | 方向 | 含义 | 实测 |
| --- | --- | --- | --- | --- |
| 0 BASIC | 0 | 读 | GAIA 版本 | ✓ `03 01` → 3.1 |
| 0 | 1 | 读 | 支持的功能列表 | ✓ |
| 0 | 3 | 读 | 序列号 | ✓ `ABCDEF0123456789` |
| 0 | 4 | 读 | 型号 | ✓ `MOONDROP EDGE` |
| 0 | 5 | 读 | 固件版本 | ✓ `1.4.0` |
| 1 EARBUD | 0/1/2 | 读 | 主耳 / 次耳序列号 | ✓ |
| 5 EQ | 0 | 读 | EQ 状态 | ✓ `01` |
| 5 | 1 | 读 | 预设列表 | ✓ `06 00 01 02 03 04 3f`（个数 + id） |
| 5 | 2 | 读 | 当前预设 | ✓ `04` |
| 5 | 3 | 写 | 选择预设 | ✓ |
| 5 | 4 | 读 | 用户 EQ 频段数 | ✓ `05` |
| 5 | 5 | 读 | 读用户 PEQ | ✓ payload `00 04`，39 字节 |
| 5 | 6 | 写 | 写用户 PEQ | ✓ 39 字节 |
| 8 AUDIO_CURATION | 2 | 读 | 模式数量 | ✓ `03 49 01 00`（3 种） |
| 8 | 3 | 读 | 当前模式 | ✓ `00` = 关 |
| 8 | 4 | 写 | 设置模式 | ✓ 无 ACK，见下 |
| 8 | 41 | 读 | ANC 开关配置 | ✓ `01 01 01 01 01` |
| 8 | 0/5/7/8/12/15/17/18/20/22-40 | — | 风噪 / 增益步进 / 自动通透 / 防啸叫 / 场景 … | ✗ EDGE 全部无响应 |
| 13 BATTERY | 0 | 读 | 支持的电池 id | ✓ `00` |
| 13 | 1 | 读 | 电量 | ✓ `00 64` → 100%（见下） |
| 15 DAC_GAIN | 1 | 读 | 增益档位 | ✓ `00` |
| 16 CODEC | 2 | 读 | LDAC 开关 | ✓ `00` |
| 16 | 1/5 | 读 | LC3 / LHDC | ✗ |
| 20 MULTIPOINT | 1 | 读 | 多点连接 | ✓ `01` |
| 21 BT_ADDRESS | 1 | 读 | 已连接主机地址 | ✓ 6 字节 MAC |
| 2 / 11 / 17 / 18 / 19 / 32 | — | — | ANC V1 / 手势 / 光感 / 空间音频 / LED / ANC V2 | ✗ |

EDGE 没有独立的“风噪”模式（与评测一致：靠固件抑制，非独立模式）。

## 6. 降噪编码

`AUDIO_CURATION / cmd 3`（读）与 `cmd 4`（写）用**两套编码**：

| UI 模式 | 读回 payload[0] | 写入 payload |
| --- | --- | --- |
| 关闭 | 0 | **1** |
| 降噪 | 1 | **2** |
| 通透 | 2 | **4** |
| （抗风噪） | (3) | (3) |

实测对照（真机逐项验证）：

```
SET 2 → GET 1     SET 4 → GET 2     SET 1 → GET 0
```

读回 payload 是 4 字节：`[mode] [01] [00] [00]`。

其他指令族（供其它机型参考）：ANC V2(F32) 用 0..5 恒等映射；ANC V1(F2) 用 0/1。

## 7. 电量

`BATTERY / cmd 1` 的 payload 是 `(id, level)` 成对数据：

```
00 64  →  id 0, 100 %
```

> **与资料不同**：Space Travel 用 id 1=左、2=右，EDGE 返回 **id 0**。
> 程序按通用方式解析并保留原始 id 列表，界面里把未知 id 显示为“耳机”。
> 部分机型可用 `0x03` 表示充电盒。

## 8. 参数均衡器（PEQ）

> 本节按实测的 EDGE（**5 段**）写。官方 App 的产品目录里还有 10 段机型（Pudding、EVO 2、
> Dots、MIRAGE、The Garden 等），段数与参数范围见第 12 节；报文布局不变，只是段数变化 ——
> 读请求写 `00 (N-1)`，写报文头是 `00 (N-1) 00 00`，5 段 39 字节、10 段 74 字节。

读命令 `5/5`，payload `00 04`（起始频段 / 结束频段），返回 **39 字节**：

```
offset  0..3   : 00 04 00 00     头（固定）
之后 5 组、每组 7 字节（最后一组的“类型”字节在 payload 之外）：
      +0..1  频率 Hz        uint16 大端
      +2..3  Q              uint16 大端 ÷ 4096
      +4     滤波器类型      实测恒为 0
      +5..6  增益 dB         int16 大端 ÷ 60
```

最后的 `+5..6` 落在偏移 37..38，即 payload 的**最后两个字节**。

写命令 `5/6`，payload 与读回的 39 字节**逐字节相同**（真机回环实测：写入后
回读得到完全相同的字节，包括厂商未使用的字节）。

解码示例（本机读到的自定义曲线）：

| 频段 | 频率 | Q | 增益 |
| --- | --- | --- | --- |
| 1 | 23 Hz | 0.40 | −1.5 dB |
| 2 | 240 Hz | 5.10 | +2.7 dB |
| 3 | 1400 Hz | 6.30 | +3.0 dB |
| 4 | 2300 Hz | 1.80 | −2.9 dB |
| 5 | 6900 Hz | 1.20 | +3.0 dB |

> **增益是“后移一格”存储的**：频段 N 的增益出现在它**自己**记录的末尾
> （即频段 N 记录的 +5..6 字节）。devicePEQ 文档把它描述成“存在下一段的前 2 字节”，
> 但那只对前 4 段成立；按本文件（自包含）的解释，读与写都自洽。

预设 id：`0x00..0x04` 为出厂预设，`0x3f` 为用户 PEQ。EDGE 有 5 个出厂预设
（Standard / Extra Bass / Country Style / Old Studio Style / Violin Solo）。

滤波器的**类型字节**含义未知（EDGE 恒为 0）；范围参考：频率 20–20000 Hz、
Q 0.1–10、增益 −12…+3 dB。这些是 EDGE 的默认值；官方目录给出更具体范围时界面按档案
（例如 Pudding 的 Q 0.3…10）限制。

## 9. 型号差异（Robin / Pudding）

HyperEars 公开了 Robin 与 Pudding 的协议事实
（<https://github.com/silverpoetry/HyperEars>，`docs/moondrop-robin-protocol.md`、
`docs/moondrop-pudding-protocol.md`）。两者使用**与本文件完全相同的 GAIA 帧格式**
（`FF TT 00 LL HH CC SS OO`，其中 `TT` 就是版本字节，`LL HH` 小端长度 ——
EDGE 固件发的是 `FF 04 00 00 00 1D ...`，即版本 4、长度大端 0），只是业务字段不同：

| | EDGE（实测） | Robin（公开协议） | Pudding（公开协议 + 实机验证） |
| --- | --- | --- | --- |
| 电量查询 | `1D 1A 01` | `1D 1A 01` | `1D 1A 01 01 02` |
| 电量响应 | `1D 1B 01 00 64`（整机） | `1D 1B 01 01 LL 02 RR` | `1D 1B 01 01 LL 02 RR 03 CC` |
| 电量主动推送 | — | — | `1D 1A 81 ...` |
| 电量语义 | 0% 合法 | 左右耳 1..100，`00`/`FF` = 未连接 | 同左；充电盒 `00..100` 合法、`FF` = 不可读 |
| 降噪查询 | `10 03`（AudioCuration） | `10 03`，回包 `11 03 0x` | `40 03`，回包 `41 03 0x`（ANC V2） |
| 降噪设置 | `10 04 01/02/04` | `10 04 01/02/04` | `40 04 00/01/02`（与查询同值） |
| 握手 | 无 | `FF 01 00 00 00 0A 03 00` → `...0A 83 00 00 04 03 01` | 同 Robin（耳机也可能主动推送该帧）|

因此本程序用同一套帧编解码，把这些差异收进「型号档案」（`src/deviceprofile.cpp`），
并且：

* 电量按档案语义过滤：TWS 机型的 `00`/`0xFF` 一律视为未知（避免把「耳在盒中」显示成 0%），
  充电盒的 `0%` 保留；
* 降噪优先用档案指定的指令族，但**依然保留能力探测回退**，所以档案没覆盖的机型也能用；
* EDGE 之外的电量布局（`01 LL 02 RR [03 CC]` 键值对）由同一个通用解析器处理，
  因此不需要为每个机型写单独的解析代码。

> 注：HyperEars 记录的部分帧在载荷之后还有一个不稳定的尾字节（`40`/`86`）。
> 本实现按长度字段解析，载荷之外的字节一律忽略，与之一致。

## 10. 其它

* `DAC_GAIN` 返回 0/1/2，低/中/高的对应关系未实测（UI 已标注为推测）。
* 未实现：OTA、触控手势、空间音频（设备不支持）。
* 固件在**切换降噪时不接受命令**，是当前实现里最需要注意的时序特性。

## 11. 中科蓝讯（Bluetrum）机型：NEKOCAKE 猫饼实测

猫饼使用中科蓝讯 BT8922E（USB 描述符里的 `9ECA` 前缀），**与 QCC 系（GAIA）机型差别很大**，
用同一根 SPP 通道连上后能问出来的东西少得多。本机实测（固件 1.0.0，GAIA 报 3.1）：

连接：SPP（RFCOMM 通道 1），帧格式与 GAIA v4 一致，因此握手与解析代码可以复用。

`BASIC/GET_SUPPORTED_FEATURES` 返回 `00 03 01 05 01 01 01 06 02 00 02`，
即能力位图为 **BASIC(0) / EARBUD(1) / VOICE_UI(3) / MUSIC_PROCESSING(5) / UPGRADE(6)**。

| 命令 | 结果 |
| --- | --- |
| `BASIC/GET_GAIA_VERSION` | `03 01`（3.1） |
| `BASIC/GET_VARIANT` | `Moondrop Nekocake` |
| `BASIC/GET_APPLICATION_VERSION` | `1.0.0` |
| `BASIC/GET_SERIAL_NUMBER` | `ABCDEF0123456789`（占位串，非真实序列号） |
| `BT_ADDRESS/GET`（F21） | **无响应** |
| `EQ/GET_PRESETS` | `06 00 01 02 03 04 3f`（5 个预设 + 自定义） |
| `EQ/GET_SET` | `04` |
| `EQ/GET_USER_CONFIG` | `00 01 00 00 00 00 00 00 00`（**不是** EDGE 的 5 段 PEQ 布局） |
| `BATTERY/*`（F13） | **全部无响应** |
| ANC：F2 / F8 / F32 的 GET/SET | **全部无响应**（逐条扫描命令号 0–23 亦无应答） |
| `GESTURE`（F11）、`UPGRADE`（F6） | 已申报支持，但查询命令无响应 |

结论与注意事项：

* **电量只能从 BlueZ 读**（`org.bluez.Battery1.Percentage`，本机为 100%）。
  本程序在设备没有 `BATTERY` 能力时回退到该值，否则界面会永远显示「未知」。
* **降噪不受软件控制**：猫饼通过**长按耳机柄 1 秒**在 `ANC OFF`/`ANC ON` 之间切换
  （触控可由 MOONDROP Link 自定义）。GAIA 侧没有任何 ANC 命令通道，
  因此降噪页对该机型恒为不可用，这是设备限制而不是缺陷。
* `EARBUD` 的查询能返回数据（`31 12 22 30 30 31 35 33 33 35 35` 等），但字段含义未知，
  未用于任何功能。
* 触控自定义、OTA 需要中科蓝讯私有协议（`9ECA0000` GATT 服务），不在本项目范围内。

## 12. 官方 App 的产品目录：机型适配对照

这一节记录**从官方 MOONDROP App 里能拿到什么、拿不到什么**，以及由此改动的适配逻辑。
结论先行：能拿到的是**机型事实**（属于哪个命令模块、几节电池、EQ 段数与参数范围），
拿不到的是**各模块的命令编号**（Dart 是 AOT 编译，符号与常量不可读）。

### 12.1 目标与工具

* 目标：`moondrop-android-release.apk`（110,710,316 字节，
  sha256 `04756b49acfea523758d86101c96c1824b7b209ae3a8836c07837088e795d2d5`）。
* 结构：Flutter 应用（`lib/arm64-v8a/libapp.so` 12.6 MB）+ 原生库
  （Airoha 的 ANC/PEQ 系数计算、USB HID、FFmpeg 等，均与 SPP 控制无关）。
* 使用 [REA](https://rea.tools/get-started/)（`npx rea-agents@latest`，Node 24.22.1 本地安装）：
  * `rea identify-flutter-build`：确认 Flutter 构建事实 —— Dart
    `3.14.0-95.2.beta`，快照哈希 `7a1ea3f6f5cf1089a7f6e55d7f20dbfd`；
  * `rea inspect-dart-aot`：**拒绝**该 APK，因为 Dart 3.14 用的是 unified snapshot
    （`_kDartSnapshotText` / `_kDartSnapshotData`），REA 的解析器只覆盖 Dart 2.14 的
    legacy split profile。也就是说 AOT 代码/常量无法用现成工具读出来；
  * 字符串池与文件名仍可用（`strings libapp.so` 即可）：`package:moondrop_app/...` 的 325 个
    Dart 文件路径、类名（`ANCPage`、`DeviceV3`、`TouchV3` …）都在，但**数值常量不在**。
* App 运行期会拉取**公开的产品目录**：
  `GET https://cdn-service.moondroplab.tech/api/v1/products/all`（2026-11-13 抓取，109 条）。
  字段包括 `chipType`、`appResourceRoot.uri`、`istws`、`eqBands`、`gainRange`、`qRange`、
  `supportedFilterTypes`、`isPreGainSupported`、`isTotalGainSupported`、`freqResponse`。
  其中 `chipType` 与 `appResourceRoot.uri` 一致（`/bluetrum`、`/qualcomm`、`/jieli`、`/airoha`
  以及 USB 侧的 `/comture`、`/spv`、`/synopsys`、`/jiu`、`/jieliusb`、`/bluetrumusb`），
  说明这是 App 内部**命令模块**的划分。

### 12.2 蓝牙机型的模块与机型事实

只列 `type = BT`（本项目的 SPP 控制范围）；USB / 有线机型不在此列。

| 模块 | 机型（目录名称，同硬件不同配色/联名会多条） | 电量 | EQ 段数 | 目录给出的参数范围 |
| --- | --- | --- | --- | --- |
| `bluetrum` | **MOONDROP EDGE**、**MOONDROP EDGE 2**、BIANCA:STIGMATA、SINGER HEADPHONE | 整机单值 | 5 | — |
| `bluetrum` | **Moondrop Nekocake**（含 QBZ-191、Acht Acht Limited） | 左右耳 | 5 | — |
| `bluetrum` | **Robin's Earphones**（4 种大小写/包装）、**Moondrop Space Travel**、SPACE TRAVEL 2 / 2 Ultra（含 PANDAER 联名） | 左右耳 | 5 | — |
| `bluetrum` | MOONDROP Pill、PILL、PANDAER Open Air Pill、Pill Yamada Ryo / Kita Ikuyo / Ijichi Nijika / Gotoh Hitori | 左右耳 | 5 | Pill Yamada Ryo：−12…+3 dB、Q 0.2…10、含 HighShelf/LowShelf/LowPass/HighPass |
| `bluetrum` | MOONDROP MOCA、Me 163 Komet MOCA、Golden Ages / 2、MOON TRAVEL、Ultrasonic、SUSANOO TWS（含 IOS 版）、U.C.T.S.、H.I.D.E.404_Klukai、LAPLACE-OBA-Ⅱ、ZZZ-ANGELS-OWS、MOONDROP x YASUNO KIYONO、PUNISHING:GRAY RAVEN、Space Force | 左右耳 | 5 | — |
| `bluetrum` | **MOONDROP EVO 2** | 左右耳 | **10** | — |
| `qualcomm` | MOONDROP Voyager、MOONDROP littlewhite | 整机单值 | 5 | — |
| `qualcomm` | Moondrop Alice、Moondrop Sparks | 左右耳 | 5 | — |
| `jieli` | **MOONDROP Pudding**、MOONDROP Dots、MOONDROP MIRAGE | 左右耳 | **10** | Q 0.3…10 |
| `airoha` | MOONDROP The Garden | 左右耳 | **10** | — |

关键点：

* **EDGE 和猫饼在同一个 `bluetrum` 模块里**。本项目早先按「讲 GAIA = Qualcomm」把 EDGE 记成
  Qualcomm，只是推断；目录显示官方 App 用同一套模块驱动这两台，与实测（EDGE 与猫饼都只暴露
  GAIA 的部分 feature）一致。
* **Pudding 属于 `jieli` 模块、10 段 EQ、Q 0.3…10**，不是 Qualcomm 家族；HyperEars 记录的
  GAIA 兼容帧仍然成立（JieLi 也讲兼容帧），但段数与 Q 范围以目录为准。
* **EQ 段数不固定是 5**：目录里出现 5 / 10（USB 侧还有 8 / 9 / 32）。这是本项目把
  PEQ 报文从写死 5 段改成按段数编解码的直接依据。
* `istws` 与实测一致：EDGE 单值、猫饼左右耳。电池语义（`00`/`0xFF` = 不可读）沿用第 9 节。

### 12.3 由此改动的实现

* **段数探测**：连接后先发 `EQ/4`（本项目的 `CEqGetBandCount`），用它的回包决定段数；
  设备不回时用档案里的目录值，最后退回 5 段。段数一旦从 `EQ/5` 的实际报文里读出，
  以报文为准（某些固件不回 `EQ/4`）。
* **PEQ 报文**：`EQ/5` 读请求 = `00 (N-1)`；`EQ/6` 写报文 = `00 (N-1) 00 00` + 每段 7 字节，
  5 段 39 字节、10 段 74 字节。第 8 节的字节布局不变。
* **参数范围**：写报文按档案的 gain/Q 范围夹取；调音页的增益/Q 滑杆和导入解析也用同一组值
  （默认 −12…+3 dB、Q 0.1…10，即 EDGE 的范围）。
* **档案匹配**：目录里的机型只用于自动匹配（电量布局、段数、模块备注），**不出现在设置页的
  手动档案下拉框**；下拉框保持「自动探测 + 实证机型 + 通用回退」。EDGE 2、SPACE TRAVEL 2 这类
  与旧档案共享子串的名字排在前面，避免被旧模式吞掉。

### 12.4 没有拿到的东西（重要）

* **各模块的命令编号**：App 的蓝牙控制逻辑在 Dart 里（字符串池有 `GaiaService`、
  `isSPPConnected`、`factory_spp`、`applyPEQ BulkBTPEQ` 等痕迹，数值常量不可读）。
  因此 `qualcomm` / `jieli` / `airoha` 的 ANC、EQ、电量命令格式本项目仍然不知道，
  这些机型继续依赖**能力探测 + 第 6 节的 AudioCuration/V2/V1 映射**，不保证可用。
* **私有 SPP 通道号**：App 通过 JNI 调 Android `BluetoothSocket`（字符串池有 `package:jni`），
  但通道号没有以常量形式暴露；本项目仍按第 1 节的「上次成功 → 1 → 16 → 2..30」串行探测。
* **OTA / 触控 / 提示音**：与本项目范围无关，未分析。

### 12.5 复现

```bash
# 产品目录（公开接口，不需要登录）
curl -s https://cdn-service.moondroplab.tech/api/v1/products/all | jq -r '
  .data[] | select(.type=="BT") |
  [.chipType, .appResourceRoot.uri, .name, (.istws|tostring), (.eqBands|tostring),
   (.gainRange//"-"), (.qRange//"-")] | @tsv' | column -t -s$'\t'

# APK 的 Flutter 构建事实（REA）
rea identify-flutter-build moondrop-android-release.apk
rea inspect-dart-aot moondrop-android-release.apk   # 预期：unified snapshot 不受支持
```
