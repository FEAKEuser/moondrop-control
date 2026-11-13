# MOONDROP GAIA 协议实测笔记

本文件记录逆向出来的协议细节。除非某一节另行注明，命令与字节格式都是**在本机
MOONDROP EDGE（固件 1.4.0）上逐条实测确认**的；第 11 节是同一台机器上对
**Moondrop Nekocake（猫饼，固件 1.0.0）**的实测记录。第 12、13 节来自**官方 MOONDROP
App**（产品目录 + Kotlin 层反编译），属于「App 这样做」的事实的记录，不等于本项目在真机
验证过。**EDGE 与猫饼是本项目唯一接过的真机**，其余机型（Robin / Pudding / Space Travel）
的内容摘自上游公开资料或 App 分析。
所有命令均可用 `moondrop-cli raw FEATURE COMMAND [HEX]` 复现。

---

## 1. 传输层

* **RFCOMM (SPP)**，标准 SPP UUID `00001101-0000-1000-8000-00805F9B34FB`
* **通道号**：EDGE 上是 **channel 1**（不是资料常说的 16）
  * Space Travel 用 16；EDGE / 部分新机型用 1
  * 因此本程序按 «上次成功 → 1 → 16 → 2..30» 顺序自动探测
  * 官方 App 不扫描：它用 SDP 查 UUID 再让系统解析通道（失败兜底通道 1），见第 13.1 节
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
ANC V1 的读命令是 `F2/4`（当前模式），写命令是 `F2/2`（开关状态）；官方 App 的命令表
（`V3ANCPlugin$COMMANDS`，见第 13.4 节）里另有 `F2/5`（按模式号写入）。

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

## 13. 官方 App 的控制栈：连接流程与命令表（反编译 Kotlin 层）

第 12 节只拿到了机型事实。这一节是**控制方法本身**：官方 App 的蓝牙栈其实在 Kotlin 里
（`classes.dex`，可直接反编译），Dart 只是通过 MethodChannel 调用。反编译用的工具是
`androguard`（DAD 反编译器 + smali 清单）；blutter 在本 APK 上失败，因为 Dart 3.14 的
unified snapshot（见第 12 节）。

反编译产物里的关键类（包名即原始路径，R8 没有改这些类的名字）：

| 类 | 作用 |
| --- | --- |
| `com.qualcomm.qti.gaiaclient.core.bluetooth.client.rfcomm.RfcommClient` | GAIA 的 SPP 传输 |
| `...client.rfcomm.uuids.UuidFetcher` / `UUIDReceiver` | 连接前先用 SDP 查 UUID |
| `com.qualcomm.qti.gaiaclient.core.gaia.qtil.*` | GAIA 各 feature 的插件与命令表 |
| `...native.handlers.AncHandler` / `AncV2Handler` / `EqHandler` / `DeviceInfoHandler` | App 的 MethodChannel 处理层 |
| `...native.factoryspp.FactorySppClient` / `FactorySppProtocol` | 工厂 SPP（产测）通道 |
| `...native.ble.source.*` | 蓝牙低功耗（BLE）9ECA 私有协议（另一条通道） |
| `com.airoha.sdk` | Airoha 官方 SDK（SPP） |
| `com.moondroplab.moondrop.moondrop_app.GaiaClientApplication` | 内嵌的 QTI GAIA Client（Qualcomm 开源） |

### 13.1 连接流程（GAIA/SPP，即本项目使用的那条）

固定 UUID：

```
SPP           00001101-0000-1000-8000-00805F9B34FB
GAIA_LEGACY   00001107-D102-11E1-9B23-00025B00A5A5
```

`RfcommClient.connect()` 的实际步骤：

1. `BluetoothAdapter.cancelDiscovery()`；
2. 先 `fetchUuidsWithSdp()`（`UuidFetcher`），等 `ACTION_UUID` 广播拿到设备的 SDP 服务列表；
3. 选 UUID：列表里有 SPP → 用 SPP；否则有 `GAIA_LEGACY` → 用它；都没有 → 仍用 SPP；
4. `createInsecureRfcommSocketToServiceRecord(uuid)`（**由 SDP 解析通道号**，不扫描通道）；
5. 若创建 socket 抛异常，退回反射 `createRfcommSocket(1)`（**通道 1**）；
6. 只接受 `BluetoothDevice.getType()` 为 1（CLASSIC）或 3（DUAL）的设备；
7. 连接在后台线程 `ConnectionThread` 里跑，`connect()` 成功后启动收发线程；
8. 连接类型（CLASSIC / LOW_ENERGY）来自 App 服务端/调用方给的 `transportType`，
   默认 CLASSIC；`LOW_ENERGY` 走 GAIA over GATT。

**对本项目的意义**：Linux 上本项目没有 SDP 客户端（不依赖 `libbluetooth`），所以仍是
「上次成功 → 1 → 16 → 2..30」串行扫描；但 App 的兜底顺序说明 **通道 1 是常见值**，
而且真正的通道号应由 SDP 查询得到（BlueZ 的 `org.bluez.Device1.UUIDs` 只给 UUID，不给通道）。

### 13.2 GAIA 帧与 vendor id（与第 2 节互相印证）

`QTILVendorIDs.QTIL_V3_VENDOR_ID = 29`（0x001D），正是实测 EDGE 帧里的 `00 1d`；
`V3PacketType` 的取值 0..3 与第 2 节的 `type` 字段一致（COMMAND/NOTIFICATION/RESPONSE/ERROR）。
也就是说本项目用的帧格式就是 GAIA v3（QTIL vendor）。

### 13.3 Feature 编号（`QTILFeature`，与实测能力位图逐项吻合）

```
0 BASIC   1 EARBUD   2 ANC(V1)   3 VOICE_UI   4 DEBUG   5 MUSIC_PROCESSING
6 UPGRADE   7 HANDSET_SERVICE   8 AUDIO_CURATION   9 EARBUD_FIT   10 VOICE_PROCESSING
11 GESTURE_CONFIGURATION   12 STATISTICS   13 BATTERY   14 VOICE   15 DAC_GAIN
16 CODEC_TYPE   17 LIGHT_SENSOR   18 SPATIAL_AUDIO   19 LED
20 ONEBRINGTWO   21 BT_ADDRESS   32 ANC_V2
```

（`32` 不在 QTI 的枚举里，是水月雨固件自己的 ANC V2，编号沿用实测。）

### 13.4 命令表（`V3*Plugin$COMMANDS` 的常量，逐条来自反编译）

| Feature | Cmd | 方向 | 含义 |
| --- | --- | --- | --- |
| 0 BASIC | 0 | 读 | GAIA 版本 |
| 0 | 1 / 2 | 读 | 能力列表 / 下一页 |
| 0 | 3 | 读 | 序列号 |
| 0 | 4 | 读 | 型号（Variant） |
| 0 | 5 | 读 | 固件版本 |
| 0 | 7 / 8 | 写 | 订阅 / 取消通知 |
| 0 | 12 / 13 | 读写 | 传输信息 / 传输参数（含最大 payload） |
| 0 | 16 | 读 | 设备自身蓝牙地址 |
| 0 | 18 / 19 | 读 | 耳机颜色 / 语言 |
| 0 | 20 / 21 | 读 | 左耳 SN / 右耳 SN |
| 0 | 22 | 读 | TWS 连接状态 |
| 1 EARBUD | 0 / 1 | 读 | 主耳是哪只 / 副耳序列号 |
| 1 | 2 / 3 | 读/写 | 当前按钮动作 / 设置按钮动作 |
| 2 ANC(V1) | 1 / 2 | 读/写 | ANC 开关 |
| 2 | 3 / 4 | 读 | ANC 模式数 / 当前模式 |
| 2 | 5 | 写 | 设置 ANC 模式 |
| 2 | 6 / 7 | 读写 | 通透增益 |
| 5 MUSIC_PROCESSING | 0 | 读 | EQ 开关状态 |
| 5 | 1 / 2 / 3 | 读/读/写 | 预设列表 / 当前预设 / 选择预设 |
| 5 | 4 | 读 | 用户 EQ 段数 |
| 5 | 5 / 6 | 读/写 | 用户 EQ（频段 7 字节 × N，见 13.5） |
| 5 | 7 / 8 | 写 | 保存到存储 / 设置 NV ID |
| 8 AUDIO_CURATION | 2 / 3 | 读 | 模式数 / 当前模式 |
| 8 | 4 | 写 | 设置模式 |
| 8 | 41 / 42 | 读/写 | ANC 开关配置（5 字节，见 13.5） |
| 8 | 22–24 | 读写 | 风噪检测 |
| 8 | 25–29 | 读写 | 自动通透 |
| 8 | 30–33 | 读写 | 啸叫检测 / 反馈增益 |
| 8 | 34–37 | 读写 | 噪声识别 |
| 8 | 38–40 | 读写 | 恶劣声学 |
| 13 BATTERY | 0 | 读 | 支持的电池 id |
| 13 | 1 | 读 | 电量（请求 payload = 电池 id 列表） |
| 15 DAC_GAIN | 1 / 2 | 读/写 | 增益档位 |
| 16 CODEC_TYPE | 1 / 3 | 读/写 | LC3 |
| 16 | 2 / 4 | 读/写 | LDAC |
| 16 | 5 / 6 | 读/写 | LHDC |
| 20 ONEBRINGTWO | 1 / 2 | 读/写 | 多点开关 |
| 20 | 3 / 4 | 读/写 | 多点超时 |
| 20 | 5 / 6 | 读 | 当前多点设备 / 下一页 |
| 20 | 7 | 写 | 断开某台多点设备 |
| 21 BT_ADDRESS | 1 / 2 | 读/写 | 蓝牙地址 |
| 32 ANC_V2 | 3 / 4 | 读/写 | 当前模式（0..5 与 UI 一一对应） |
| 32 | 41 / 42 | 读/写 | ANC 开关配置（字节格式同 8/41） |

### 13.5 参数编码（反编译中确认）

**用户 EQ 频段记录（7 字节）**（`BandInfo`）：

| 偏移 | 类型 | 含义 |
| --- | --- | --- |
| 0..1 | uint16 | 频率 Hz |
| 2..3 | uint16 | Q × **4096** |
| 4 | uint8 | 滤波器类型，见下表 |
| 5..6 | int16 | 增益 dB × **60** |

滤波器类型字节（`Filter`）：0 BYPASS、1 LOW_PASS、2 HIGH_PASS、3 ALL_PASS、4 LOW_SHELF、
5 HIGH_SHELF、6 TILT、7–12 为各自的第二组、13 PARAMETRIC_EQUALIZER。
EDGE 实测恒为 0，写成 0 可以正确回环。

读 payload（`5/5`）：4 字节头 `[start][end][00][00]` + 每条 7 字节；
App 的判定函数 `isBluetrumConfigurationPacket` 用 `len >= 4 + (end-start+1)*7` 检查。
写 payload（`5/6`，App 的 `setUserSetGains`）：长度 `2 + 7N`，头 2 字节 `[start][end]`，
单段时偏移 2/4/6/7 正好是那 7 字节；但多段时 Kotlin 里用的是 `2+2i`/`4+2i`/`6+i`/`7+2i`
（应为 `+7i`），会互相覆盖——这段代码在本项目看来是错的，或以「一次只写一段」的方式使用。
EDGE 实测接受 4 字节头、记录从 4 开始的写法（39 字节）并原样回读，本项目按实测走。

**电池 id**（`Battery`）：0 SINGLE_DEVICE（整机）、1 LEFT、2 RIGHT、3 CHARGER_CASE。
与第 7 节的实测一致。

**按键手势（feature 11）**（`GestureFactory`、`GetGestureConfigurationResponse`、
`SetGestureConfiguration`、`Configuration`）：

* **支持列表**（命令 1/2/3，触摸板数 0）：位图，`id = 字节序号 × 8 + 位序号`
  （`GestureFactory.getDefaultValues`）。gesture 0..7、context 0..8、action 0..26。
* **读某手势的配置**（命令 4）：payload `[gestureId][offset]`；响应
  `[gestureId | (more<<7)][uint16 配置…]`。`offset` 是**已读配置条数**，`more=1` 时用累计条数再读一页。
* **写配置**（命令 5）：payload `[gestureId | (more<<7)][offset][uint16 配置…]`；
  每条配置 `action`（bit0..6）| `context`（bit7..13）| `touchpad`（bit14..15）。
  App 按最大 payload 分块，本项目按 16 条一包。
* **恢复默认**（命令 6），随后重新读一遍配置。


**ANC 开关配置（8/41、32/41）**：5 字节
`[STATE][ANC_ON][ANC_OFF][TP][ORDER]`（App 的 `SWITCH_CONF_FIELDS`）。
STATE = 0/1（`ACActions`）；后三个是「该模式是否纳入切换」；ORDER 取值 0..5
（`ACOrders`：0 ON_OFF_TP、1 OFF_ON_TP、2 TP_ON_OFF、3 ON_TP_OFF、4 OFF_TP_ON、5 TP_OFF_ON）。
EDGE 实测返回 `01 01 01 01 01` → STATE=开，三种模式都参与，顺序 OFF_ON_TP。

**AUDIO_CURATION 当前模式（8/3）**：payload 是
`[value][featureType][adaptationSupport][gainSupport][(howlingSupport)]`（`Mode` 类）。
featureType：1 STATIC_ANC、2 LEAKTHROUGH_ANC、3 ADAPTIVE_ANC、4 ADAPTIVE_LEAKTHROUGH_ANC。
EDGE 返回 `[mode][01][00][00]`，即 STATIC_ANC。

**ANC V2 模式（32/3、32/4）**：0=关、1=降噪、2=通透、3=抗风噪、4=自适应、5=Live
（`AncV2Handler` 的常量，读写同值，与第 6 节实测一致）。

**预设 id**（`PreSetType`）：0 = **OFF（均衡器关闭）**，63（0x3F）= 用户 PEQ，
其余（1..62）= 出厂预设。设备返回的预设列表形如 `06 00 01 02 03 04 3f`，
即「关 + 4 个出厂预设 + 用户 PEQ」。本项目默认把 id 0 显示成 “Standard”，
这一点未在真机上核对名称，仅记录 App 的语义。

### 13.6 另一条通道：BLE 9ECA 私有协议（`native/ble/source`）

部分机型（`BleSourceSwitch*`）还可以走 BLE GATT：

```
Service        9ECA0000-7F3A-4F32-9A38-A91B2C6E0100
Command        9ECA0001-...
Notification   9ECA0003-...
CCCD           00002902-0000-1000-8000-00805F9B34FB
```

请求帧（`BleSourceSwitchFrames.buildCommand`）：`A5 01 <cmd> <len> <payload…>`；
`A5` 为 magic，`01` 为协议版本，`len` ≤ 255。命令 id：

```
0  GET_FW_VERSION        1  GET_AUDIO_SOURCE     2  SET_AUDIO_SOURCE
3  GET_CAPABILITY        4  SET_POWER_OFF        5  GET_DEVICE_VOLUME
6  SET_DEVICE_VOLUME     7  GET_PRESET_EQ         8  SET_PRESET_EQ
9  GET_PEQ_CONFIG        10 SET_PEQ_PREGAIN      11 GET_PEQ_POINT
12 SET_PEQ_POINT         13 COMMIT_PEQ           14 GET_MIC_GAIN
15 SET_MIC_GAIN          17 GET_EARBUD_COLOR     19 GET_EARBUD_LANGUAGE
21 GET_EARBUD_SN_LEFT    23 GET_EARBUD_SN_RIGHT
```

（枚举 ordinal 与线上 id 不一致：颜色/语言/SN 这几条线上 id 分别是 17/19/21/23。）
连接时请求 MTU 247，写 CCCD 订阅通知，之后按通知回包解析。
这一路没有 ANC 命令，属于「音频源切换 / 固件信息 / EQ」类控制，本项目未实现。

### 13.7 工厂 SPP 帧（产测通道，`FactorySppProtocol`）

```
55 AA 43 58 57        5 字节头：'U' 0xAA 'C' 'X' 'W'（不是 ASCII 字符串，按字节匹配）
00                    1 字节 0
LL LL                 小端 uint16：payload 长度 + 4
TT TT                 小端 uint16：命令类型（21 = SET_SN，22 = GET_SN）
PP PP                 小端 uint16：payload 长度
payload...
CC CC                 小端 uint16：CRC16-IBM（多项式 0x8005，从帧头算到 payload 末尾）
```

帧总长 = payload + 14。连接方式：标准 SPP UUID + `createRfcommSocketToServiceRecord`。

### 13.8 Airoha（`com.airoha.sdk`）

Airoha 机型（目录里的 `airoha` 模块）用厂商 SDK 走 SPP（`ConnectionProtocol.PROTOCOL_SPP`，
标准 SPP UUID），命令由 SDK 内部定义（`AirohaMMI`、`AirohaANC`、`AirohaSPP` 等），
本项目**没有解析**这些命令；APK 里的 `libnative-anc*.so`/`libnative-peq.so` 只是离线计算
ANC 系数与 PEQ 频响，不参与在线控制。

### 13.9 复现

```bash
# 反编译（本机实测；DAD 输出保留原始类名）
pip install androguard                     # 或 venv
androguard decompile -i classes.dex -o out -l '(Lcom/qualcomm/qti/gaiaclient/|Lcom/airoha/|Lcom/moondroplab/)'
# 然后看 out/com/qualcomm/qti/gaiaclient/... 与 out/com/moondroplab/...
```

> blutter（编译同版本 Dart VM 反序列化）在本 APK 上失败：
> `extract_dart_info.py` 找不到 `_kDartVmSnapshotData`（Dart 3.14 用 unified
> `_kDartSnapshotData/_Text`）。Kotlin 层可读，所以控制协议不需要动 Dart。
