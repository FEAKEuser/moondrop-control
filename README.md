# Moondrop Control — KDE Plasma 小部件

控制水月雨（MOONDROP）蓝牙耳机的 KDE Plasma 6 小部件：降噪模式、调音预设、参数均衡器（PEQ）、
编解码器开关、电量等。使用 Qt 6 / QML 开发，直接通过 Linux 蓝牙 RFCOMM（SPP）与耳机通信。

## 0. 实测范围（重要）

本项目**只在两台真机上实际测试过**：

| 机型 | 固件 | 官方 App 模块 | 实测内容 |
| --- | --- | --- | --- |
| **MOONDROP EDGE**（羽翼） | 1.4.0 | `bluetrum`（与猫饼同族） | 全部功能：降噪、五段 PEQ、LDAC/LC3、DAC 增益、多点、电量 |
| **Moondrop Nekocake**（猫饼） | 1.0.0 | `bluetrum`（中科蓝讯 BT8922E） | 连接、能力探测、调音预设、电量（经 BlueZ）；该机型**无** GAIA 降噪/电量通道 |

> EDGE 的「主控」一列早先写的是 Qualcomm（GAIA），依据只是它讲 GAIA 帧。官方 App 的产品目录
> 把 EDGE 和猫饼都归在同一个 `bluetrum` 模块里（见 [`docs/PROTOCOL.md`](docs/PROTOCOL.md) 第 12 节），
> 所以这里改成按官方模块记录；硅片型号本项目没有拆机确认。

**其余机型从未在本项目里接过真机**。它们的档案由两部分组成：上游公开资料（Pudding / Robin /
Space Travel 的帧格式），以及官方 App 产品目录里的机型事实（模块、电量布局、EQ 段数与参数范围）。
后者只能说明「官方 App 这样对待这台机器」，**不代表本项目已经能控制它**；
下面表格的「来源」一列逐行注明，界面上也不会把未实测的档案标为已实测。

---

## 1. 设备适配

程序按设备上报的型号名（`BASIC/GET_VARIANT`）自动匹配型号档案：

| 档案 | 匹配名称 | 电量 | 降噪 | 参数均衡器 | 来源 |
| --- | --- | --- | --- | --- | --- |
| EDGE | `MOONDROP EDGE` / 羽翼 | 整机单值 | AudioCuration | ✅ 5 段 | **本项目真机实测**（固件 1.4.0） |
| Pudding | `MOONDROP Pudding` / 布丁 | 左右耳 + 充电盒 | ANC V2 | — | 上游 HyperEars 称其已实机验证；**本项目未实测**（官方目录：`jieli` 模块，10 段） |
| Robin | `Robin's Earphones` / 知更鸟 | 左右耳 | AudioCuration | — | HyperEars 公开协议；**本项目未实测** |
| Space Travel | `MOONDROP Space Travel` | 左右耳 | 自动探测 | — | 社区逆向资料；**本项目未实测** |
| NEKOCAKE 猫饼 | `Moondrop Nekocake`（含 QBZ-191 / Acht Acht 限定版） | BlueZ（无 GAIA 电量） | **不支持**（长按耳机切换） | — | **本项目真机实测**（固件 1.0.0） |
| EDGE 2 | `MOONDROP EDGE 2` | 整机单值 | 先试 AudioCuration | — | 官方 App 产品目录；**未实测** |
| Space Travel 2 / 2 Ultra | `SPACE TRAVEL 2` / `... ULTRA` | 左右耳 | 先试 AudioCuration | — | 官方 App 产品目录；**未实测** |
| EVO 2 | `MOONDROP EVO 2` | 左右耳 | 先试 AudioCuration | —（目录称 10 段） | 官方 App 产品目录；**未实测** |
| Pill / MOCA / Golden Ages / MOON TRAVEL / Ultrasonic / SUSANOO | 对应全名 | 左右耳 | 先试 AudioCuration | — | 官方 App 产品目录；**未实测** |
| Voyager / littlewhite / Alice / Sparks | 对应全名 | 整机单值或左右耳 | 自动探测 | — | 官方 App 产品目录（`qualcomm` 模块）；**未实测** |
| Dots / MIRAGE | 对应全名 | 左右耳 | 自动探测 | —（目录称 10 段） | 官方 App 产品目录（`jieli` 模块）；**未实测** |
| The Garden | `MOONDROP The Garden` | 左右耳 | 自动探测 | —（目录称 10 段） | 官方 App 产品目录（`airoha` 模块）；**未实测** |
| 通用回退 | 任何含 `MOONDROP` / `水月雨` 的名称 | 自动探测 | 自动探测 | 按能力位图 | — |

* **只有 EDGE 与 Nekocake 两行是本项目实测结果**；HyperEars/Robin/Space Travel 行按公开资料写；
  其余行来自官方 App 产品目录（模块、电量布局、EQ 段数与参数范围），**命令族没有在本项目验证过**，
  界面不会把它们标成已实测。
* 产品目录里的机型只用于**自动匹配**，不出现在设置页的手动档案下拉框里——下拉框保持
  「自动探测 + 实证机型 + 通用回退」的短列表。
* 猫饼（中科蓝讯 BT8922E）只实现了 GAIA 的 BASIC/EARBUD/VOICE_UI/UPGRADE/调音族：
  电量读 BlueZ，**降噪只能长按耳机切换**，详见 [`docs/PROTOCOL.md`](docs/PROTOCOL.md) 第 11 节。
* 型号名上报较晚（先连上、后查询），档案切换是即时的；设置页可手动指定档案以覆盖自动识别。
* 电量语义按档案区分：TWS 机型的 `00`/`0xFF` 表示「未连接/不可读」并显示为未知，
  充电盒的 `0%` 则是真实状态；整机单值机型 `0%` 合法。
* 降噪指令族（AudioCuration / ANC V2 / ANC V1）可自动探测，档案只是把它变成「先问对的那个」，
  因此新型号即使不在表里也能工作。
* **连接方式与命令表**：官方 App 的蓝牙栈在 Kotlin 里（内嵌 QTI GAIA Client）。它连接时先用
  SDP 查服务 UUID（`00001101` = SPP、`00001107-…` = GAIA legacy），再用
  `createInsecureRfcommSocketToServiceRecord` 让系统解析通道号，失败才退回固定通道 1；
  本项目不依赖 `libbluetooth`，所以仍按「上次成功 → 1 → 16 → 2..30」串行扫描（App 的兜底
  也说明通道 1 常见）。完整的 feature/命令/参数编码（电池 id、滤波器类型字节、ANC 开关配置、
  预设 id 语义）见 [`docs/PROTOCOL.md`](docs/PROTOCOL.md) 第 13 节，术语与本项目实现一致。
* 目录里的 `bluetrum` 机型在 App 里走 GAIA（CLASSIC/SPP 或 BLE）；`qualcomm` 机型也走同一套
  GAIA 库；`airoha` 机型走 Airoha SDK（SPP）；`jieli` 机型另有私有命令族。本项目目前只实现
  GAIA 这一条，因此除 EDGE/猫饼外的机型仍只能靠能力探测。

---

## 2. 功能

| 页面 | 功能 |
| --- | --- |
| **Noise cancelling（降噪）** | 关闭 / 降噪 / 通透 三档切换（自动识别设备的 ANC 指令族）；可记住上次设置的降噪模式并在连接后写回；可配置耳机按键循环哪些模式、切换顺序（GAIA `8/41`、`8/42`） |
| **Tuning（调音）** | 图形化参数均衡器：直接拖动曲线上的频点调频率/增益、滚轮调 Q；预设下拉切换；支持粘贴 AutoEq / EqualizerAPO 滤波器列表导入 |
| **Sound（声音）** | LDAC、LC3、LHDC 开关，三档 DAC 增益，多点连接开关与已连接设备（可断开某一台；超时在 More 页） |
| **Info（信息）** | 电量（左耳/右耳/充电盒，按机型）、型号、固件版本、GAIA 版本、序列号、设备地址、已匹配的型号档案与**数据来源**、能力列表 |
| **More（更多）** | 设备上报的额外开关：LED、空间音频/头部跟踪、佩戴检测、动态低音、左右声道互换、关机/待机超时、风噪抑制、自动通透及其保持时间、降噪增益；以及按键手势（每个手势 × 场景 → 动作，可恢复默认） |
| 面板图标 | 自绘 SVG 耳机图形 + **竖立**的电池图标（电量用填充高度表示），整块约 24 x 16 px |
| 设置页 | 选择已配对耳机（从 BlueZ 读取）、RFCOMM 通道、自动连接 / 自动重连、降噪状态记忆、型号档案、增益档位顺序 |
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

按「省事程度」排序，任选一条即可。**所有渠道都不需要 root**（发行版包除外，它由包管理器提权）。

### 4.1 一行安装（推荐先试这个）

```bash
curl -fsSL https://raw.githubusercontent.com/FEAKEuser/moondrop-control/master/scripts/install.sh | bash
```

脚本按以下优先级办事，不用你选：

1. 系统里已有本项目的发行版包 → 交给 `dnf` / `apt` / `yay` / `paru` 装（之后跟着系统一起更新）；
2. 没有 → 从源码构建，装进 `~/.local`，**不碰系统目录、不需要 root**；
3. 两种情况都不可行 → 明确告诉你缺哪个包，而不是装到一半失败。

卸载（源码安装的部分）：

```bash
curl -fsSL .../scripts/install.sh | bash -s -- --uninstall
```

开发时想从本地检出构建，加 `--source-dir`：

```bash
./scripts/install.sh --from-source --source-dir "$PWD" --prefix ~/.local
```

### 4.2 KDE Store /「获取新部件」

包已经按商店要求打包好（`dist/org.moondrop.control-<版本>.plasmoid`），上传到
[store.kde.org](https://store.kde.org) 后，用户可以直接在
**右键面板 → 添加部件 → 获取新部件** 里搜索安装。

自己打这个包：

```bash
./scripts/make-plasmoid.sh          # 产物在 dist/
```

然后在本机验证：

```bash
kpackagetool6 --type Plasma/Applet --install dist/org.moondrop.control-0.2.0.plasmoid
```

> **这个包为什么能直接装**：本项目的后端是 C++ 插件，商店的部件包默认按纯 QML 处理，
> 因此包内**自带**一份编译好的 `libmoondropplugin.so`，QML 通过**相对目录导入**
> （`import "backend"`）加载它。实测确认 Plasma 会从包内目录解析该导入，插件随包加载。
> 包内的 `qmldir` 用私有模块名 `org.moondrop.backend.bundled`，这样即使用户同时装了
> 发行版包（它提供全局的 `org.moondrop.backend`），两者也不会互相顶掉——
> 同名会触发 `Cannot install singleton type into protected module`，让商店包的每个页面都加载失败。

> **Qt 版本限制（重要）**：Qt 拒绝「比宿主 Qt 更新」的插件——
> `plugin.minor > host.minor` 直接报 `uses incompatible Qt library`，major 必须相同。
> 所以**商店包要在你打算支持的最老 Qt 上构建**，不要在最新开发机上构建。
> 仓库为此提供容器化基线构建：
>
> ```bash
> ./scripts/build-baseline-plugin.sh              # 默认 fedora:40 (Qt 6.8)
> MOONDROP_BACKEND_SO=build/baseline/libmoondropplugin.so ./scripts/make-plasmoid.sh
> ```
>
> 实测（Fedora 44 宿主机 Qt 6.11 / Debian 13 Qt 6.8.2）：
>
> | 包里插件的 Qt | 在 Debian 13（Qt 6.8.2）加载 |
> | --- | --- |
> | 6.11（本机构建） | ❌ `uses incompatible Qt library. (6.11.0)` |
> | 6.8（基线构建） | ✅ 0 错误，8 个页面全部解析包内后端 |
>
> `make-plasmoid.sh` 会打印包里插件的 Qt 版本，便于确认。

整个商店流程可以在本机离线验证，不用发布任何东西：

```bash
./scripts/store-check.sh
```

它会依次验证「打包 → 用**真实 KNewStuff 客户端**（即「获取新部件」背后的代码）从本地
OCS 提供者下载并安装 → 在真实 plasmashell 里加载」。依赖：`kf6-kpackage`（`kpackagetool6`）、
`kf6-knewstuff-devel`、`plasma-workspace`（`plasmawindowed`），容器里还需要 `dbus-daemon`。
缺依赖时它会明确报 SKIP 并以非零退出，不会伪装成通过。

### 4.3 发行版包

| 发行版 | 做法 | 实测 |
| --- | --- | --- |
| Fedora / RHEL | `packaging/fedora/moondrop-control.spec`，或 COPR | ✅ `rpmbuild` 成功，产物含 plasmoid/插件/metainfo |
| Arch / AUR | `packaging/arch/PKGBUILD`（`makepkg -si`，或提交到 AUR） | ✅ 容器内 `makepkg` 成功，产出 `.pkg.tar.zst` |
| Debian / Ubuntu | `packaging/debian/`（`./packaging/debian/make-deb.sh`） | ✅ `dpkg-buildpackage` 成功，产物依赖正确 |

自己构建：

```bash
./packaging/make-tarball.sh                      # 打包源码 tarball
rpmbuild -bb packaging/fedora/moondrop-control.spec --define "_topdir /tmp/rpmbuild"
# 或
podman run --rm -v "$PWD:/src:ro" -v "$PWD/dist:/out:z" debian:trixie \
       bash /src/packaging/debian/make-deb.sh /out
```

发行版包装的是**全局**安装：QML 插件进 Qt 导入目录、小程序包进
`/usr/share/plasma/plasmoids/`（这样所有用户都能看到，不必各自跑 `kpackagetool6`），
并附带 AppStream 元数据（`packaging/appstream/`，`appstreamcli validate` 通过）。
发行版包没有 Qt 版本闸门问题：它们是针对各发行版自己的 Qt 现场编译的。

推送 `v*` tag 后，`.github/workflows/build.yml` 会跑全套检查（含商店流程），
并**在 `fedora:40`（Qt 6.8）的作业容器里重新构建、打包、再跑一遍商店检查**，
把这一份挂到 Release —— 否则 Release 产物在老发行版上会因 Qt 版本闸门而装不上。
这一步是独立 job（`baseline`）而不是 build job 里的一步：基线 Qt 必须是 job
自己的环境，在容器里再起容器是 runner 不允许的。该 job 自带的商店检查意味着
发布的包是**在最老 Qt 上验证过**的，而不是只在开发机上能跑。

### 4.4 从源码手动装

C++ 的 QML 插件默认要装到 Qt 的导入路径（本机为 `/usr/lib64/qt6/qml`，需要 root）：

```bash
sudo cmake --install build          # QML 插件、命令行工具、小程序包
moondrop-widget-install-applet      # 以当前用户安装 Plasma 小程序（无需 root）
```

或者装到自己的 prefix：

```bash
cmake -B build -DMOONDROP_QML_DIR="$HOME/.local/lib/qt6/qml"
cmake --build build -j$(nproc) && cmake --install build
```

⚠️ `~/.local/lib/qt6/qml` **不在** Qt 的默认导入路径里（默认只有
`<prefix>/lib64/qt6/qml`、`/usr/lib64/qt6/qml` 和 `qrc:`），所以这条路必须再告诉
Qt 一次——这正是 4.1 的安装脚本改用「包内自带插件」布局的原因：

```ini
# ~/.config/environment.d/moondrop.conf
QML_IMPORT_PATH=/home/你的用户名/.local/lib/qt6/qml
```

改完要重新登录（或至少重启 plasmashell）才生效。

安装后重启 plasmashell 让小部件重新加载 QML：

```bash
kquitapp6 plasmashell && kstart plasmashell
```

然后右键面板/桌面 →「添加小部件」→ 搜索 **Moondrop**。

### 4.5 卸载

```bash
moondrop-widget-uninstall      # 移除小程序，并打印剩余文件的删除命令
```

`moondrop-widget-uninstall` 会按**实际安装位置**（自动推导前缀，并用 `qtpaths6` 查询 QML 目录）
打印出还需要手动删除的文件，直接复制执行即可——不要照抄某个固定的 `/usr/bin` 路径，
本机默认前缀是 `/usr/local`。发行版包用包管理器卸载即可。

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
moondrop-cli extra list                 # 额外设置（LED、超时、风噪、降噪增益…）
moondrop-cli extra led on               # 写一个额外设置
moondrop-cli anc-switch                 # 读按键循环配置
moondrop-cli anc-switch 1 1 1 1 1       # 写：state on off tp order
moondrop-cli mp devices                 # 多点：已连接设备 + 超时
moondrop-cli mp disconnect 1            # 断开 #1
moondrop-cli gesture list               # 手势 × 场景 → 动作
moondrop-cli gesture set 0 0 0 2        # 手势 0 / 触摸板 0 / 场景 0 → 动作 2
moondrop-cli gesture reset
MOONDROP_DEBUG=1 moondrop-cli info      # 打印收发帧
MOONDROP_FAKE=rich moondrop-cli ...     # 用 Rich 假耳机跑上面这些（无需真机）
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
scripts/                     安装 / 打包 / 翻译 / UI 检查
packaging/                   Fedora spec、Arch PKGBUILD、Debian、AppStream
```

安装渠道（详见 [4. 安装](#4-安装)）：

* **`.plasmoid`（KDE Store）**：包内自带编译好的后端插件，QML 用相对目录导入加载，
  因此不依赖系统 Qt 导入路径、不需要 root；
* **发行版包**：插件进系统 Qt 导入目录，小程序包进 `/usr/share/plasma/plasmoids/`；
* **一行脚本**：优先用发行版包，否则按第一种布局构建到 `~/.local`。

设计要点：

* **串行请求队列**：固件一次只处理一条命令，并发发送会被静默丢弃，因此所有请求排队并按序发送并带重试。
* **能力探测**：先发 `BASIC/GET_SUPPORTED_FEATURES`，再按返回的能力位图决定后续查询哪些功能（不支持的命令不会白等超时）。
* **ANC 指令族自适应**：支持 AudioCuration(F8) / ANC V2(F32) / ANC V1(F2) 三套编码，按能力自动选择并映射到统一的 UI 模式。
* **降噪切换后固件忙 ~2 秒**：该窗口内所有命令被忽略，队列为此留出静默期。
* **降噪状态记忆**：每次在小组件里切换降噪（或 CLI 设置）都会按耳机地址记下该模式；
  设置页打开「连接耳机时恢复上次的降噪状态」后，连接完成时若耳机上报的模式与记住的不同，
  就把记住的模式写回（每台耳机一份、每次连接最多一次）。默认关闭，只显示耳机自己上报的模式。
* **RFCOMM 通道自动探测**：默认按 «上次成功 → 1 → 16 → 其余» 顺序逐个尝试（**串行**，不是并发：
  耳机只提供**一个**控制连接，多个候选同时连会互相抢这一个通道，反而让正确通道拿到 EBUSY 而失败）。
  不存在的通道内核 ~35 ms 就拒绝，存在的通道立刻应答，因此整个扫描通常不到 1 秒；
  对「连接成功但一直不回答」的通道另加 ~0.9 s 上限，确认后记住通道。
* **免配置**：没有选过耳机时，启动会自动挑 BlueZ 里已配对的 MOONDROP 设备（多于一个时优先
  已连接的那个），所以配对后**不用进设置页**，也不会出现「选了设备却把通道重置成自动」的情况。
* **自动连接与退避**：连接由后端负责（不依赖 QML），并监听 BlueZ 的 `PropertiesChanged` 事件，
  耳机出现在系统里时自动接管；重试间隔 2 s → 60 s 递增。
* **通知去抖**：读取某个功能会触发设备推送通知，收通知又去读取会形成死循环
  （曾导致 `busy` 永远为真、界面按钮全部禁用），因此通知驱动的刷新有 1.5 s 去抖。

## 7. 已知限制

* **只在 EDGE（固件 1.4.0）和 Nekocake（固件 1.0.0）上实测过。**
  下列条目大多是基于 EDGE 的行为写的，其它机型可能不同；未实测的档案请当作「待验证」。
* ANC 的 `SET` 命令固件不回 ACK，界面采用乐观更新 + 约 2.2 s 静默期后回读校验。
* 降噪记忆只记录**在小组件里**设置的模式；用耳机触控切换的模式不会改写记忆值
  （否则耳机开机时上报的默认模式会把记忆值覆盖掉）。
* `DAC 增益`用 0/1/2 三个原始值表示，**固件是倒序编号**（0 = 最高）。界面按此映射，
  若你的机型相反，可在设置里关掉「反转输出增益档位」。
* 预设名默认按以下顺序显示：Standard / Extra Bass / Country Style / Old Studio Style / Violin Solo。
  若你的设备顺序不同，可在 `~/.config/moondrop-widget/config.ini` 中覆盖：

  ```ini
  [ui]
  presetNames=Balanced, Basshead, ...
  ```
* PEQ 不再写死五段：段数先问设备（`EQ/4`），设备不回时用官方目录的段数，最后才退回 5 段。
  读写的报文头是「首段..末段」，每段仍 7 字节，因此 5 段 39 字节、10 段 74 字节。
  增益/Q 的滑杆范围取自档案（官方目录里给出的值，默认 −12…+3 dB、Q 0.1…10）。
* PEQ 频段滤波器类型字节（每条 6 字节处）EDGE 固件恒为 0，界面暂不暴露类型选择。
  App 的滤波器类型表（0 旁路、1 低通、2 高通、4 低架、5 高架、13 参数均衡）已记录在
  `docs/PROTOCOL.md` 第 13.5 节；真机上写这些值会怎样还没验证。
* GAIA 的 feature/命令编号已经从 App 的 Kotlin 层解出来了（第 13 节，与本项目实现逐项一致）；
  仍然不清楚的是 `jieli` / `airoha` 的私有命令族，以及 10 段机型的 EQ 分块读写：
  官方 App 把用户 EQ 按 payload 上限分块（`[start][end]`），本项目一次读完；
  写入格式在 App 里是 2 字节头，EDGE 实测接受 4 字节头，10 段机型尚未上机验证。
* 官方 App 把预设 id `0` 当作 **OFF（均衡器关闭）**（`PreSetType`），`0x3F` 是用户 PEQ；
  本项目默认仍把 id `0` 显示成 “Standard”（名称可配置）——名称语义尚未在真机上核对。
* “More（更多）”页的额外设置按 App 的命令表实现（`LED 19/1,2`、`空间音频 18/1–4`、`光感 17/1,2`、
  `动态低音 27/1,2`、`声道 30/1,2`、`开关机/待机超时 25/1–4`、`风噪 8/22–24`、`自动通透 8/25–29`、
  `降噪增益 8/5,6`）。**除 EDGE 的开关配置（8/41 实测）外都没有真机验证**；设备不回的项会探测一次
  （600 ms）后在该连接内隐藏。通知订阅（`0/7`）默认不发：EDGE 不注册也会主动推送，
  连接后会按 App 的用法订阅 ANC/EQ/电量/多点四个通知（`0/7`，EDGE 实测会回空响应）。
* EDGE 实测：ANC 开关配置（`8/41`）第 1 字节写成 0 会被固件忽略（回读仍 1），其余字节可写；
  `8/5`（增益）、`22–24`（风噪）、`25–29`（自动通透）在固件 1.4.0 上全部无响应——版本门控
  （AC≥2 / ≥4）因此不会让 EDGE 白等；`0/16`（设备自身蓝牙地址）也无响应。
* 按键手势（feature 11）按 App 的位图列表 + 16 位配置（action/context/touchpad）实现，
  写配置按 16 条一包分页；**未在真机验证**（EDGE 没有 feature 11）。
* 只实现了只读的“设备信息 / 电量 / 能力”，未实现固件升级（OTA）与触控手势配置。
* 猫饼的降噪**无法由软件控制**（固件没有对应的 GAIA 命令，只能长按耳机切换），
  且电量必须经 BlueZ 读取；这是设备限制，不是缺陷。

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

# 无需耳机的自检（都不需要蓝牙硬件，且不会碰你的真实配置）
./build/cli/moondrop-selftest          # 协议编解码
./build/cli/moondrop-profile-check     # 型号档案匹配
./build/cli/moondrop-conncheck         # 连接 / 队列 / 通知行为
./build/cli/moondrop-extras-check      # 额外设置、ANC 开关配置、多点设备、按键手势
./build/cli/moondrop-scan-check        # 通道扫描（含「连上但不回包」的干扰通道）
./build/cli/moondrop-ebusy-check       # 控制通道被占用时的重试预算与报错
./build/cli/moondrop-switch-check      # 换耳机时不会残留旧机型信息
./build/cli/moondrop-watchstate-check  # BlueZ 的 "Connected" 不会被误读成断开
./build/cli/moondrop-stress            # 快速连断 + 稳定重连

# 需要真实 BlueZ 状态（没有时自行跳过）
./build/cli/moondrop-failover-check <已配对但离线的地址>
./build/cli/moondrop-reconnect-check

# 诊断工具：打印 applet 所响应的 BlueZ 事件
./build/cli/moondrop-hpwatch <地址> <秒数>   # 逐个打印耳机事件
./build/cli/moondrop-watchswitch <秒数>      # 换耳机时的连接日志

# 安装渠道的端到端检查（商店包 + 真实 KNewStuff 客户端 + 真实 plasmashell）
./scripts/store-check.sh
```

`store-check.sh` 需要 `kf6-kpackage`、`kf6-knewstuff-devel`、`plasma-workspace`（容器里还要
`dbus-daemon`）；缺依赖时会明确报 SKIP 并以非零退出，不会伪装成通过。它不联网、不发布任何东西，全部写在临时 XDG 目录下。

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

* **官方 MOONDROP App 的 Kotlin 层**（`classes.dex`，2026-11-13 版）：内嵌 QTI GAIA Client、
  工厂 SPP、Bluetrum BLE 9ECA、Airoha SDK。连接流程（SDP UUID → 不安全的 RFCOMM → 通道 1
  兜底）与 GAIA 的 feature/命令/参数编码都出自这里；反编译方法与逐项对照见
  [`docs/PROTOCOL.md`](docs/PROTOCOL.md) 第 13 节。
* **官方 MOONDROP App 的产品目录**（`https://cdn-service.moondroplab.tech/api/v1/products/all`，
  2026-11-13 抓取，109 条）：机型属于哪个命令模块、是否 TWS、EQ 段数与增益/Q 范围。
  逆向过程与对照表见 [`docs/PROTOCOL.md`](docs/PROTOCOL.md) 第 12 节；本项目只采用其中的
  事实性数据（机型分类、命令编号、参数编码），没有复制 App 的代码。

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
