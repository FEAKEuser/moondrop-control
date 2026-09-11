# Moondrop Control — KDE Plasma 小部件

控制水月雨（MOONDROP）蓝牙耳机的 KDE Plasma 6 小部件：降噪模式、调音预设、五段参数均衡器（PEQ）、
编解码器开关、电量等。使用 Qt 6 / QML 开发，直接通过 Linux 蓝牙 RFCOMM（SPP）与耳机通信。

> 已在真机实测：**MOONDROP EDGE（羽翼）**，固件 1.4.0。
> 协议层（GAIA v3/v4）与水月雨其它机型通用，差异集中在 ANC 指令族、电量布局和增益档位顺序，
> 这些由「型号档案」描述（见下）。

## 1. 设备适配

程序按设备上报的型号名（`BASIC/GET_VARIANT`）自动匹配型号档案：

| 档案 | 匹配名称 | 电量 | 降噪 | 参数均衡器 | 来源 |
| --- | --- | --- | --- | --- | --- |
| EDGE | `MOONDROP EDGE` / 羽翼 | 整机单值 | AudioCuration | ✅ 5 段 | **本项目真机实测**（固件 1.4.0） |
| Pudding | `MOONDROP Pudding` / 布丁 | 左右耳 + 充电盒 | ANC V2 | — | HyperEars 公开协议（实机验证） |
| Robin | `Robin's Earphones` / 知更鸟 | 左右耳 | AudioCuration | — | HyperEars 公开协议 |
| Space Travel | `MOONDROP Space Travel` | 左右耳 | 自动探测 | — | 社区逆向资料 |
| 通用回退 | 任何含 `MOONDROP` / `水月雨` 的名称 | 自动探测 | 自动探测 | 按能力位图 | — |

* 未实测的档案只声明「公开资料记载的帧格式」，界面上不会标记为已实测。
* 型号名上报较晚（先连上、后查询），档案切换是即时的；设置页可手动指定档案以覆盖自动识别。
* 电量语义按档案区分：TWS 机型的 `00`/`0xFF` 表示「未连接/不可读」并显示为未知，
  充电盒的 `0%` 则是真实状态；整机单值机型 `0%` 合法。
* 降噪指令族（AudioCuration / ANC V2 / ANC V1）可自动探测，档案只是把它变成「先问对的那个」，
  因此新型号即使不在表里也能工作。

---

## 2. 功能

| 页面 | 功能 |
| --- | --- |
| **Noise cancelling（降噪）** | 关闭 / 降噪 / 通透 三档切换（自动识别设备的 ANC 指令族） |
| **Tuning（调音）** | 图形化参数均衡器：直接拖动曲线上的频点调频率/增益、滚轮调 Q；预设下拉切换；支持粘贴 AutoEq / EqualizerAPO 滤波器列表导入 |
| **Sound（声音）** | LDAC、LC3、LHDC 开关，三档 DAC 增益，多点连接开关 |
| **Info（信息）** | 电量（左耳/右耳/充电盒，按机型）、型号、固件版本、GAIA 版本、序列号、设备地址、已匹配的型号档案与**数据来源**、能力列表 |
| 面板图标 | 自绘 SVG 耳机图形 + **竖立**的电池图标（电量用填充高度表示），整块约 24 x 16 px |
| 设置页 | 选择已配对耳机（从 BlueZ 读取）、RFCOMM 通道、自动连接 / 自动重连、型号档案、增益档位顺序 |
| 自动连接 | Plasma 启动后自动连；监听 BlueZ 事件，耳机出现时自动接管；断线后按退避策略重连 |
| 桌面（非面板） | 直接展开显示控制面板，不需要点击图标 |
| 界面语言 | 随系统语言（已内置简体中文翻译） |

所有写操作都带**验证**：写入 PEQ 后回读比对，不一致会自动重写并提示；降噪切换后回读当前模式。

## 3. 依赖与构建

### 运行时依赖

* Qt 6.5+（Core / Gui / Qml / Quick / DBus）
* BlueZ（系统自带，本机 5.87）
* KDE Plasma 6 桌面（含 `kpackagetool6`）

**不需要** `bluetoothctl` 交互、不需要 `libbluetooth-devel`、**不需要 root**：
程序直接使用 Linux `AF_BLUETOOTH` socket，普通用户即可连接已配对设备的 RFCOMM 通道。

### 构建依赖（Fedora）

```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtdeclarative-devel \
                 extra-cmake-modules kf6-ki18n-devel gettext
```

Debian/Ubuntu 对应：

```bash
sudo apt install cmake g++ qt6-base-dev qt6-declarative-dev extra-cmake-modules
```

### 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## 4. 安装

C++ 的 QML 插件必须装到 Qt 的导入路径（默认为 `/usr/lib64/qt6/qml`，需要 root）：

```bash
sudo cmake --install build          # 安装 QML 插件、命令行工具、桌面小程序包
moondrop-widget-install-applet      # 以当前用户安装 Plasma 小程序（无需 root）
```

或者手动指定：

```bash
cmake -B build -DMOONDROP_QML_DIR="$HOME/.local/lib/qt6/qml"
cmake --build build -j$(nproc) && cmake --install build
```

安装后重启 plasmashell 让已添加的小部件重新加载 QML：

```bash
kquitapp6 plasmashell && kstart plasmashell
```

然后右键面板/桌面 →「添加小部件」→ 搜索 **Moondrop**。

### 卸载

```bash
moondrop-widget-uninstall
sudo rm -rf $(qtpaths6 --query QT_INSTALL_QML)/org/moondrop
sudo rm -f /usr/bin/moondrop-cli /usr/bin/moondrop-widget-install-applet
```

## 5. 使用

1. 先在系统蓝牙设置里**配对**耳机（本程序不负责配对，只负责控制已配对设备）。
2. 添加小部件后打开设置页，从下拉框选择你的耳机（MOONDROP 设备排在最前）。

之后**不需要再点连接**：程序会自己连上。

* Plasma 启动后自动连接（可在设置里关掉）；
* 通过 BlueZ 监听蓝牙层，耳机之后开机/连上系统时会自动接管控制通道；
* 连接断开后自动重连，重试间隔从 2 秒逐步增加到 1 分钟（耳机没开机时不会反复猛试）；
* 界面上会显示「等待耳机…」，此时按钮变成「立即重试」。
* 如果控制通道被别的程序占着（例如手机 App、或另一个小部件实例），会明确提示而不是静默失败。

### 命令行工具

同一套后端也提供 CLI，便于脚本化和排查问题：

```bash
moondrop-cli list                       # 列出 BlueZ 已知设备
moondrop-cli info                       # 显示全部信息
moondrop-cli anc on                     # 降噪（off / on / transparency / wind）
moondrop-cli preset list                # 列出预设
moondrop-cli preset 0x3f                # 选择自定义 PEQ
moondrop-cli peq get                    # 读出五段 PEQ
moondrop-cli peq set "23:3.0:0.4;240:2.7:5.1;1400:3.0:6.3;2300:-2.9:1.8;6900:3.0:1.2"
moondrop-cli peq restore                # 回到耳机原本的曲线
moondrop-cli peq flat                   # 拉平
moondrop-cli battery
moondrop-cli ldac on
moondrop-cli raw 5 5 0004               # 任意 GAIA 命令：feature command hexpayload
MOONDROP_DEBUG=1 moondrop-cli info      # 打印收发帧
```

`moondrop-cli --address XX:XX:.. --channel N` 可临时指定设备与通道。

## 6. 架构

```
package/                     Plasma 小程序（纯 QML，Plasma 6 API）
  metadata.json
  contents/config/           main.xml (KConfig) + config.qml + configDevice.qml
  contents/ui/               main / Compact / Full / Anc / Eq / Sound / Info
src/
  gaia.{h,cpp}               GAIA v3/v4 帧编解码 + 流式解析
  transport.h                字节通道抽象接口
  rfcommclient.{h,cpp}       AF_BLUETOOTH RFCOMM 异步客户端（QSocketNotifier）
  deviceprofile.{h,cpp}      机型档案（ANC 指令族 / 电量布局 / 增益顺序 / PEQ）
  moondropdevice.{h,cpp}     高层设备模型：串行请求队列、能力探测、状态解析
  devicediscovery.{h,cpp}    通过 BlueZ D-Bus 列出已配对/已连接设备
  plugin.cpp                 QML 插件注册（singleton `Moondrop`）
cli/main.cpp                 命令行前端
tools/                       Python 逆向脚本 + QML 离屏预览工具
docs/PROTOCOL.md             协议逆向笔记（含全部实测命令与字节格式）
```

设计要点：

* **串行请求队列**：固件一次只处理一条命令，并发发送会被静默丢弃，因此所有请求排队并按序发送并带重试。
* **能力探测**：先发 `BASIC/GET_SUPPORTED_FEATURES`，再按返回的能力位图决定后续查询哪些功能（不支持的命令不会白等超时）。
* **ANC 指令族自适应**：支持 AudioCuration(F8) / ANC V2(F32) / ANC V1(F2) 三套编码，按能力自动选择并映射到统一的 UI 模式。
* **降噪切换后固件忙 ~2 秒**：该窗口内所有命令被忽略，队列为此留出静默期。
* **RFCOMM 通道自动探测**：默认按 «上次成功 → 1 → 16 → 其余» 顺序尝试，首帧响应即确认为正确通道并记住。
* **自动连接与退避**：连接由后端负责（不依赖 QML），并监听 BlueZ 的 `PropertiesChanged` 事件，
  耳机出现在系统里时自动接管；重试间隔 2 s → 60 s 递增。
* **通知去抖**：读取某个功能会触发设备推送通知，收通知又去读取会形成死循环
  （曾导致 `busy` 永远为真、界面按钮全部禁用），因此通知驱动的刷新有 1.5 s 去抖。

## 7. 已知限制

* ANC 的 `SET` 命令固件不回 ACK，界面采用乐观更新 + 400 ms 后回读校验。
* `DAC 增益`用 0/1/2 三个原始值表示，**固件是倒序编号**（0 = 最高）。界面按此映射，
  若你的机型相反，可在设置里关掉「反转输出增益档位」。
* 预设名默认按以下顺序显示：Standard / Extra Bass / Country Style / Old Studio Style / Violin Solo。
  若你的设备顺序不同，可在 `~/.config/moondrop-widget/config.ini` 中覆盖：

  ```ini
  [ui]
  presetNames=Balanced, Basshead, ...
  ```
* PEQ 频段滤波器类型字节（每条 6 字节处）EDGE 固件恒为 0，界面暂不暴露类型选择。
* 只实现了只读的“设备信息 / 电量 / 能力”，未实现固件升级（OTA）与触控手势配置。

## 8. 开发

```bash
# 渲染全部界面检查图（面板图标、曲线等）
./scripts/run-ui-checks.sh

# 离屏渲染单个页面为 PNG（不会打扰当前桌面会话）
cmake --build build -j$(nproc)
MOONDROP_PREVIEW_CONNECT=1 QT_QPA_PLATFORM=offscreen \
  ./build/cli/preview package/contents/ui/AncPage.qml /tmp/anc.png 430 660

# 无需耳机的界面检查（例如增益档位标签映射）
./build/cli/preview tests/output-gain.qml /tmp/gain.png 460 560

# 协议编解码自检（不需要蓝牙硬件）
./build/cli/moondrop-selftest
```

修改翻译：

```bash
vim po/zh_CN.po && ./scripts/update-translations.sh && ./scripts/install-applet.sh
```

`tools/gaia.py` 是一个独立的 Python GAIA 客户端，用于在真机上试验命令：

```bash
python3 tools/gaia.py enum                       # 枚举全部只读命令
python3 tools/gaia.py query 5 4                  # 查询 EQ 频段数
python3 tools/gaia.py listen 10                  # 只监听通知
```

## 9. 参考与致谢

这个项目的协议知识来自许多人的逆向工作。**本项目没有复制它们的源代码**——协议帧格式、
命令编号属于功能性事实，我据此用 C++/QML 独立实现；下面是知识来源，按对项目的贡献排序。

本项目采用 GPL-3.0-or-later，与其中多数上游项目一致，因此代码可以双向互用（见
[许可](#10-许可)）。

特别感谢：

* **[pubglite55/SpaceTravel-Protocol](https://github.com/pubglite55/SpaceTravel-Protocol)**（MIT）
  —— GAIA v3 帧结构、Feature/Command 映射。让我第一次看懂了 `FF 04 00 00 00 1D ...` 的含义。
* **[lingbai-rong/PuddingPods](https://github.com/lingbai-rong/PuddingPods)**
  —— 布丁（Pudding）实机验证的协议资料，本项目的 Pudding 档案据此实现。
* **[silverpoetry/HyperEars](https://github.com/silverpoetry/HyperEars)**（GPL-3.0）
  —— Robin 与 Pudding 的公开协议文档（帧格式、电量布局、降噪编码、型号判型规则），
  是「多机型适配」这一层的主要依据。

同样重要的其它资料：

* [bqj6666/FxxkMoondrop](https://github.com/bqj6666/FxxkMoondrop)（GPL-3.0）
  —— 从官方 App 提取的完整 Feature/Command 表与 ANC 路径选择逻辑
* [jeromeof/devicePEQ](https://github.com/jeromeof/devicePEQ)
  —— MOONDROP EDGE 五段 PEQ 的字节格式（增益移位存储、Q×4096、增益×60）
* [Zhaoyi-ya/TWS-Pods-PC](https://github.com/Zhaoyi-ya/TWS-Pods-PC)（GPL-3.0）
  —— Moondrop SPP 通道与降噪编码
* [SarahRoseLives/VR-N76](https://github.com/SarahRoseLives/VR-N76)
  —— GAIA over RFCOMM 的封装说明

协议细节与实测差异记录在 [`docs/PROTOCOL.md`](docs/PROTOCOL.md)。

## 10. 许可

**GPL-3.0-or-later**，完整文本见 [LICENSE](LICENSE)。

### 为什么逆向项目用 GPL

逆向工程用 GPL 是**传统且最合适**的选择，而不是矛盾：

* **目的是一致的。** 逆向的初衷是「让用户拿回自己买来的硬件的控制权」。GPL 保证这份
  控制权连同实现它的代码一起留在用户手里；宽松许可证则允许厂商拿走这些工作、改进后闭源，
  **不再回馈社区**，最终用户又回到被锁定的状态。
* **和资料来源一致。** 本项目依据的 HyperEars、FxxkMoondrop、TWS-Pods-PC 均为 GPL-3.0。
  采用同一许可证后，**代码可以双向合法互用**（它们可以并入本项目，本项目也可以回馈上游）；
  若采用 MIT，这个方向是被堵死的。这也是选择 `-or-later` 而非 `-only` 的原因。
* **生态一致。** KDE Plasma 本身及绝大多数小程序都是 GPL/LGPL，没有任何兼容性问题。
* **法律上无额外负担。** 「为互操作性而逆向」在多数法域是明确允许的
  （如欧盟软件指令第 6 条、美国 Sega v. Accolade / Google v. Oracle 等判例）；
  许可证管的是**你的代码怎么被使用**，与代码是否来自逆向无关。

### GPL 的代价（知情选择）

* 不能用于闭源商业产品：想把它塞进专有头戴控制 App 的公司用不了。
* 部分企业对 GPL 有「一律不碰」的政策，会降低被公司采用的机会。
* 如果你更希望**最大化传播**（包括被厂商采用），MIT 会更宽松——但那样就放弃了上面的好处。

对一个目标是「用户自己控制自己耳机」的业余逆向项目，我们认为这些代价是值得的。

### 范围和边界

* 本项目**不分发**厂商 App、固件、图片或任何上游程序，只包含自己编写的代码与实测记录。
* 协议帧格式、命令编号属于**功能性事实**，不受版权保护，因此实现是独立的。
* 参考资料中的 GPL 部分（如 HyperEars 的文档）为**阅读参考**，其代码未被复制进本项目。
