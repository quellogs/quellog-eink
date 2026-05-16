# 本地构建 Release 指导

本文说明如何在本机或 Docker 中构建 Quellog 固件 Release 包。

目标产物：

- `firmware/dist/release/v<version>/quellog-firmware_v<version>.zip`
- `firmware/dist/release/v<version>/merged-binary.bin`
- `firmware/dist/release/v<version>/flash_args`

## 1. 构建前确认

### 版本号

Release 包版本号来自 [CMakeLists.txt](/home/anonysoul/Workspace/quellog/firmware/CMakeLists.txt:3) 的 `PROJECT_VER`：

```cmake
set(PROJECT_VER "0.1.0")
```

发版前先确认这里是否已经改成目标版本。

### 本机构建依赖

如果你要直接在宿主机执行 `python3 scripts/build_release.py`，需要先准备：

- Node.js 20 或更高版本
- `npm`
- Python 3
- ESP-IDF 5.2.x
- 已加载 ESP-IDF 环境，即当前 shell 可以直接执行 `idf.py`

建议先检查：

```bash
node -v
npm -v
python3 --version
idf.py --version
```

已验证通过的 Docker 内 Node 版本是 `20.20.2`。不建议使用 Ubuntu 22.04 自带的 Node 12，因为 `device-config-web` 使用的 Vite 5 会报语法错误。

典型报错如下：

```text
SyntaxError: Unexpected token '?'
```

这通常说明 Node 版本过低。

## 2. 宿主机构建

在仓库根目录执行：

```bash
cd firmware
python3 scripts/build_release.py
```

脚本会依次执行：

1. 构建 `device-config-web` 前端产物
2. 执行 `idf.py set-target esp32s3`
3. 执行 `idf.py build`
4. 执行 `esptool merge_bin`
5. 生成 release 目录和 zip 包

构建成功后，终端最后会输出 zip 文件路径。

## 3. Docker 构建

如果本机没有稳定的 ESP-IDF 环境，优先用 Docker。

在仓库根目录执行：

```bash
docker run --rm \
  -v "$(pwd):/workspace" \
  -w /workspace/firmware \
  espressif/idf:release-v5.2 \
  bash -lc '
    apt-get update &&
    apt-get install -y ca-certificates curl gnupg &&
    mkdir -p /etc/apt/keyrings &&
    curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key |
      gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg &&
    echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_20.x nodistro main" \
      > /etc/apt/sources.list.d/nodesource.list &&
    apt-get update &&
    apt-get install -y nodejs &&
    python3 scripts/build_release.py
  '
```

说明：

- 挂载整个仓库到 `/workspace`，而不是只挂 `firmware/`
- 这样脚本可以同时访问 `firmware/` 和根目录下的 `device-config-web/`
- Node 20 在容器内临时安装，仅用于当前构建

## 4. 产物说明

成功后会生成目录：

```bash
firmware/dist/release/v<version>/
```

其中主要文件：

- `quellog-firmware_v<version>.zip`：最终发布包
- `merged-binary.bin`：单文件整包镜像
- `flash_args`：多文件烧录参数
- `flasher_args.json`：烧录配置描述
- `bootloader.bin`
- `partition-table.bin`
- `quellog_firmware.bin`
- `font_partition.bin`
- `FLASHING.md`
- `manifest.json`

注意：

- `font_partition.bin` 会单独放在 release 目录中
- 当前脚本不会把 `font_partition.bin` 打进 zip，只会放在解压目录里
- 如果你的分发方式必须只靠 zip，需要先确认下游是否接受这个行为

## 5. 常见问题

### Node 版本过低

现象：

```text
SyntaxError: Unexpected token '?'
```

原因：

- `device-config-web` 构建依赖的 Vite 版本要求更高的 Node 运行时

处理：

- 升级到 Node 20+
- 或直接使用本文的 Docker 命令

### `idf.py` 不存在

现象：

```text
ESP-IDF environment not found. Run source export.sh first.
```

处理：

- 先执行 ESP-IDF 对应版本的 `export.sh`
- 或改用 Docker 构建

### `sdkconfig` 被改写

`scripts/build_release.py` 内部会执行：

```bash
idf.py set-target esp32s3
```

这一步可能会：

- 重写 `firmware/sdkconfig`
- 把原文件改名为 `firmware/sdkconfig.old`

如果你在 `sdkconfig` 里做过手工调整，构建后要检查是否需要从 `sdkconfig.old` 恢复。

### 前端 `dist/` 目录问题

如果本机构建时在 `device-config-web/dist` 清理或写入阶段失败：

- 先检查目录权限
- 再检查是否有其他进程占用文件
- 如果仍然不稳定，直接使用 Docker 构建

## 6. 可选操作

### 只打包，不重新编译

如果 `firmware/build/` 里的产物已经完整存在，可以使用：

```bash
cd firmware
python3 scripts/build_release.py --skip-build
```

脚本会直接复用已有的：

- `build/merged-binary.bin`
- `build/flasher_args.json`
- `build/font_partition.bin`

适用于你已经执行过完整构建，只想重打包的场景。

### 校验 tag 与版本号一致

```bash
cd firmware
python3 scripts/build_release.py --tag v0.1.0
```

如果 `--tag` 与 `PROJECT_VER` 不一致，脚本会直接失败。

## 7. 推荐流程

日常本地发包，建议固定使用以下顺序：

1. 修改 `firmware/CMakeLists.txt` 中的 `PROJECT_VER`
2. 执行 Docker Release 构建
3. 检查 `firmware/dist/release/v<version>/`
4. 抽查 zip 内容、`merged-binary.bin` 和 `flash_args`
5. 用 release 目录中的文件做一次实际烧录验证

如果只是个人开发调试，不出包：

1. `cd firmware`
2. `./build.sh` 或 `idf.py build`

