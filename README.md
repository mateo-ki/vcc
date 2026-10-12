# VCC

> Windows 桌面代理订阅客户端 · Qt 6 Widgets · 内嵌 mihomo / sing-box / xray

收集订阅与节点、真实握手测速、按规则分流跑代理，同时对外提供一个本地 HTTP API。
C++20 编写，原生 Widgets 界面（不是 Webview 套壳），Release 版空闲常驻内存约 80 MB
（离屏内存探针 `vccMemProbe` 实测：五页全建 + 反复切页，看 private 提交内存的增量）。

## 功能

### 订阅与节点
- **订阅源**：HTTP(S) 拉取，支持条件请求（`If-None-Match` / `If-Modified-Since`）、失败重试、定时自动拉取；支持 Gist 发现与 Telegram 频道抓取。
- **节点解析**：`ss://` `vmess://` `vless://` `trojan://` `hysteria2://`（`hy2://`）`socks5://` 与带鉴权的 `http(s)://` 分享链接；Clash / Clash Meta 的 YAML 配置（含 `ssr` `tuic` `wireguard` `hysteria` 等类型）；以及两者混排的裸文本。
- **导入**：任意页面 Ctrl+V 从剪贴板导入，自动识别 JSON 源列表 / 节点链接 / 订阅 URL。
- **测速**：真实握手延迟，可选 TCP 预筛；按协议 / 关键词 / 延迟档 / 国家地区筛选与排序，分页浏览；拉取或粘贴成功后自动测速。
- **清理**：按指纹去重、删除失效节点、删除不可用节点、出口 IP 风险判定（机房 / 公共代理 / 住宅线路）。

### 代理与内核
- **三种内核**：mihomo、sing-box、xray。自动检测本地版本，从 GitHub Release（可选加速镜像）下载与更新，进度就地显示；首次运行会自动补齐缺失内核与 geo 数据。
- **两种接管方式**：系统代理（读写 HKCU Internet Settings，退出时还原快照）与 TUN。TUN 需要管理员权限，点启动时自动申请 UAC，并把配置交接给提权后的实例。
- **五种分流模式**：自定义规则 / 中国大陆直连 / GFW 列表代理 / 全局代理 / 全部直连。
- **故障自动切换**：按可配置的基准站点（多个 URL，任一 2xx 即视为连通）周期性探测出口；探测失败计入断路器，按延迟与冷却期挑备选节点、TCP 复测后热切换，全过程记入「切换记录」。
- **进程编排**：内核子进程挂进 Windows 作业对象，主程序被强杀时不会留下还在监听端口的孤儿内核。

### 分流规则
- 规则表编辑（注释、去重、遮蔽检查、一键优化），推荐分流规则一键覆盖，6 组常用场景预设（AI / 海外流媒体 / 即时通讯 / 国内开发镜像 / 游戏平台 / 广告与 QUIC 拦截）。
- `GEOSITE` / `GEOIP` 匹配与 geo 数据库一键更新，中国大陆直连兜底规则自动补齐。
- 今日规则命中统计（按规则与目标聚合）。

### 观测
- 实时上下行速率曲线、24 小时出口健康条（按小时分桶，失败优先）。
- 活跃连接表（含命中的规则与进程名）、内核日志视图。
- 状态卡显示当前节点、延迟、最近实测时间与连续失败次数，降级运行时会明说。

### 外部接口
- 本地 HTTP API：**29 条路由**，覆盖订阅源、节点、测速、内核、代理启停、运行配置、日志与导出（JSON / Clash YAML / URI）。
- 默认监听 `127.0.0.1:8080`，可选 Bearer token 鉴权；`--headless` 只跑 API 不开界面。

### 桌面集成
- 系统托盘（可配置「点 X 是最小化到托盘还是直接退出」）、单实例互斥、五套主题、退出时停止代理并还原系统代理。

## 数据

SQLite，schema 与同作者的 Rust/egui 版本逐字段一致，**同一份 `data/` 目录两边可以直接互换**（含按订阅源拆分的分库）。默认路径 `data/vcc.db`，启动时自动迁移。

## 分层与解耦约束

每个 `src/<目录>` 对应一个独立的静态库目标，依赖只允许从下往上：

```
core → models → net / sysproxy → parser / export / kernel → kernel-manager
  → db / geo / platform → proxy / tasks / router → probe / collector / telegram
  → speedtest / updater → scheduler → api → context → gui → app → vcc(exe)
```

静态库允许"双向依赖"——链接 exe 时才把符号凑齐，所以环**能编过、测试也能全绿**，构建系统里写的依赖关系却是假的。为此仓库带一个不依赖编译的静态守卫：

```bash
node tools/check-decoupling.mjs          # 非零退出即有问题
node tools/selftest-decoupling-guard.mjs # 反向验：把六条规则逐个弄坏，确认守卫会红
```

它钉住六件事：反向依赖、include 边不在声明闭包里、目标级依赖环、头文件 include 环、依赖名打错（`vcc_port_layer` 用 `if(TARGET)` 过滤，名字写错会被静默丢掉）、include 了不存在的文件。第三条"只靠上游 PUBLIC 传染"只提示不判红。

两处刻意的形状，改之前先读注释：

- `AppContext` 单独成 `vccContext` 目标，就是为了断开 `vccGui ↔ vccApp` 的环（五个页面都要拿它访问数据库与代理）。
- `vccKernelConfig` 是 `vccProxy` 的 ALIAS：sing-box / xray 的配置生成器住在 vccProxy 里，拆成独立目标会形成链接环。

## 构建

平台：Windows + MSVC（Linux 侧只有可行性检查脚本，见「已知限制」）。

| 依赖 | 用途 |
| --- | --- |
| Qt 6（Core / Gui / Widgets / Network / Concurrent / Svg） | 界面与并发 |
| yaml-cpp | Clash 配置解析 |
| sqlite3 | 数据层 |
| libarchive | 内核压缩包解包 |
| OpenSSL | TLS 握手测速 |
| nlohmann_json、zlib | JSON 与压缩 |

依赖用 vcpkg **经典模式**安装（仓库根目录故意不放 `vcpkg.json`：manifest 模式会重新解析依赖图、把 Qt 整个从源码重编一遍）。CI 也走经典模式并固定 vcpkg 的 commit —— 可复现靠"钉住 commit"，不需要 manifest 的 `builtin-baseline`。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCC_BUILD_TESTS=ON
cmake --build build --parallel 8
```

跑测试（40 个 gtest 套件；GUI 用例走 `QT_QPA_PLATFORM=offscreen`，不会弹窗口）：

```bash
ctest --test-dir build -C Debug
```

出安装包（Release 编译 → 裁剪 dist → Inno Setup，产物在 `build-rel/installer/`）：

```bash
node tools/package.mjs build-rel
```

脚本里两个外部工具都可以用环境变量指路，不设就用本机的默认位置：
`ISCC`（Inno Setup 的命令行编译器）、`OBJDUMP`（`make-dist` 用它做依赖闭包自查，缺了就跳过那一步）。

### CI 与发版

`.github/workflows/build.yml`：

- 推 `main` 或手动触发 → 装依赖、编译、跑 40 个套件、跑两条静态守卫（分层解耦、颜色单一来源）。
- 推 `v*` 的 tag → 上面全过之后再出安装包，并把 `build-rel/installer/*.exe` 发成一个 GitHub Release。
- 版本号只写在一个地方：`cmake/vcc.iss` 的 `#define MyAppVersion`。tag 与它不一致时工作流会直接失败，而不是发出一个名字和内容不符的包。
- 首次运行会从源码编 Qt6/OpenSSL（很慢，job 上限 6 小时）；vcpkg 的安装树与归档都进了 `actions/cache`，key 跟着"用的 vcpkg commit + 依赖清单"走，之后是恢复缓存的速度。
- CI 用 vcpkg 的**经典模式**（和本机一致），并把 vcpkg 固定在 `e456309491fd875487bf02b8a0dca76c1b1b00cc`：可复现靠固定 commit，而不是靠 manifest 的 `builtin-baseline`。

```bash
# 发版
git tag v1.0.0 && git push origin v1.0.0     # 安装包由 CI 产出
```

## 运行

```bat
vcc.exe [--listen 127.0.0.1:8080] [--db data/vcc.db] [--headless] [--api-token <token>]
```

程序启动后把工作目录切到 exe 所在目录，`data/`、内核二进制、`startup.log` 都按相对路径查找。

## 目录结构

```
src/
  core/        基础类型、日志、时间与 JSON 工具
  models/      Node / Source / Settings 及其序列化
  db/          SQLite 封装、连接池、按源分库、表结构与查询
  net/         HTTP 客户端（分段超时、代理链路）
  parser/      分享链接、Clash YAML、混合文本、指纹去重
  collector/   订阅拉取、Gist 发现、Telegram 频道
  export/      JSON / Clash YAML / URI 导出与质量筛选
  geo/ ip/     GeoSite / GeoIP 库与 IP 风险评估
  kernel/      三种内核的检测、下载、版本管理
  proxy/       内核配置生成、进程编排、生命周期
  proxy_probe/ 内核控制器与基准站点探测
  speedtest/   握手测速与 TCP 预筛
  scheduler/   定时拉取与自动测速
  router/      两层选路、断路器、备选序列
  sysproxy/    Windows 系统代理读写
  platform/    单实例、托盘、UAC 提权、防火墙规则、作业对象
  api/         本地 HTTP API（29 条路由）
  gui/         侧栏 + 控制台/节点池/订阅源/系统设置/外部接口五个页面 + 主题 + 自绘控件
  app/         应用外壳、主窗口、故障切换驱动
tests/         按层分目录，每层一个 gtest 可执行目标
tools/         CJK 文案码点生成、颜色守卫、打包与诊断脚本
cmake/         运行时部署与 Inno Setup 脚本
```

## 已知限制

- 只在 Windows 上开发验证。`tools/linux-check.sh` 检查的是「CMake 有没有 Windows 硬依赖」，不等于可用的 Linux 构建。
- 出站 HTTP 请求的 TLS 证书校验尚未与上游对齐（当前不校验），这是有记录的待办而非疏漏。
- 「在内核里手动切换代理组」与自动选路的撤除态还没接完：控制器侧的数据已采集，界面未接。

## 说明

本项目用于学习与研究代理客户端的实现方式。使用者需自行确保其网络行为符合所在地区的法律法规，并只连接你有权使用的节点与订阅。
