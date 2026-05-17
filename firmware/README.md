# Quellog E-Ink Firmware

`firmware/` 是 Quellog 墨水屏客户端固件子工程。

当前版本提供墨水屏客户端固件骨架：

- 默认板型：`Zectrix S3 e-paper 4.2"`
- 分层：`Board` / `Display` / `Application` / `UiPage`
- 首页：统计
- 页面：统计、最近记录、设置
- 数据：本地占位数据
- 中文显示：内置 `SourceHanSansSC` 字库与 UTF-8 文本渲染
- Wi‑Fi：支持 STA 联网与 SoftAP Web 配网
- 配网：无已保存凭据时自动进入热点配网，设置页支持手动重新配网

## 目录

- `main/main.cc`：ESP-IDF 启动入口，只负责初始化 NVS 并启动应用
- `components/quellog_app/*`：应用状态机、输入分发、设备状态与页面切换
- `components/quellog_ui/*`：页面注册、页面模型构建与应用上下文类型
- `components/quellog_display/*`：显示抽象、顶部状态栏、图表/弹窗渲染与字体资源
- `components/quellog_board/*`：板级抽象、默认板型实现、按键/电池/蓝牙/存储适配
- `components/quellog_storage/*`：NVS 配置读写封装
- `components/quellog_wifi/*`：Wi‑Fi 管理、SoftAP 配网、DNS captive portal、凭据保存

## 配网说明

- 设备首次启动且没有已保存 Wi‑Fi 时，会自动开启开放热点。
- 热点名称格式：`Quellog-XXXX`
- 浏览器访问地址：`http://192.168.4.1`
- 设置页提供“重新配网”入口，可在已联网状态下重新进入热点模式。
- 配网成功后，设备会保存最多 5 组 Wi‑Fi 凭据，并自动退出热点。
- 设备在启动或断线后，会从当前扫描到的已保存 Wi‑Fi 中自动连接信号最强者。
- Web 配网页会展示已保存网络，并支持删除单条凭据。

## 构建

```bash
cd firmware
./build.sh
```

固件构建现在会自动先执行仓库根目录下 `device-config-web/` 前端构建，再把产出的 HTML 嵌入 ESP-IDF 固件。

前置要求：

- 已安装 Node.js 与 `npm`
- 已安装或已加载 ESP-IDF 环境

如果未加载 ESP-IDF 环境，请先执行对应版本的 `export.sh`。

也可以直接运行：

```bash
cd firmware
idf.py build
```

这同样会先触发 Web UI 构建。

## Web 配网页前端

设备设置 Web 前端源码位于仓库根目录 `device-config-web/`，采用 `Vite + Vanilla` 多页工程组织：

- `index.html`：配网页入口
- `done.html`：配网完成页
- `src/`：页面脚本、样式与 API 封装
- `dist/`：构建产物，供固件通过 `EMBED_TXTFILES` 嵌入

本地开发命令：

```bash
cd device-config-web
npm ci
npm run dev
```

开发模式默认启用本地 mock `/scan`、`/submit`、`/credentials`、`/credentials/delete`、`/exit` 接口。

若要代理到真实设备，可在启动前指定：

```bash
cd device-config-web
QUELLOG_WIFI_TARGET=http://192.168.4.1 npm run dev
```

生产构建命令：

```bash
cd device-config-web
npm run build
```

`idf.py build`、`./build.sh` 与 `python3 scripts/build_release.py` 都会自动执行这个步骤；如果机器上缺少 Node.js 或 `npm`，构建会在前置阶段直接失败。

## Docker 构建

如果本机未安装或未加载 ESP-IDF 环境，也可以直接使用固件构建镜像。

在仓库根目录执行：

```bash
docker run --rm \
  -v "$(pwd):/workspace" \
  -w /workspace/firmware \
  anonysoul/quellog-firmware-builder:latest \
  bash -lc 'idf.py set-target esp32s3 && idf.py build'
```

构建产物默认输出到 `firmware/build/` 目录。

说明：

- 中文字库已编进固件，不再需要额外字体分区或单独字库烧录。
- 默认 `idf.py flash` 可以直接烧录完整可显示中文的固件。

## Release 打包

详细步骤见 [LOCAL_RELEASE_BUILD.md](/home/anonysoul/Workspace/quellog/firmware/LOCAL_RELEASE_BUILD.md:1)。

本地打包命令：

```bash
cd firmware
python3 scripts/build_release.py
```

脚本会：

- 读取 `CMakeLists.txt` 中的 `PROJECT_VER`
- 构建 `esp32s3 / zectrix-s3-epaper-4.2`
- 执行 `idf.py merge-bin`
- 输出 `dist/release/v<version>/quellog-firmware_v<version>.zip`

压缩包内包含：

- `merged-binary.bin`
- `flash_args`
- `flasher_args.json`
- `bootloader.bin`
- `partition-table.bin`
- `quellog_firmware.bin`
- `manifest.json`
- `FLASHING.md`

版本号统一在 `firmware/CMakeLists.txt` 的 `PROJECT_VER` 修改。

## 烧录 Release 包

从 GitHub Release 下载并解压 zip 后，可使用以下两种方式烧录。

多文件烧录：

```bash
python -m esptool --chip esp32s3 -p <PORT> -b 460800 --before default_reset --after hard_reset write_flash @flash_args
```

单文件烧录：

```bash
python -m esptool --chip esp32s3 -p <PORT> -b 460800 --before default_reset --after hard_reset write_flash 0x0 merged-binary.bin
```
