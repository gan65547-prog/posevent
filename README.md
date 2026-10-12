# posevent — 独立区域事件监测



## 项目介绍

Posevent 是一个独立的“视频输入 → 人体检测 → 区域事件判断 → HDMI/RTSP 输出”项目。
它把区域业务从旧系统中独立出来，核心只接收统一的归一化检测结果，不依赖 `clib`、
`mlfw-d`、SIC、旧事件路由或飞书通知。

项目分为四层：

- `cpp/`：区域几何、类别/置信度过滤、进入/离开防抖、事件和 overlay 状态。
- `pipeline/`：RTSP 解码、ONNX/RKNN 模型适配、OSD、Wayland/framebuffer 输出。
- `region_server.py` 与 `web/`：区域抓帧、编辑、校验、原子保存和 reload 请求。
- `tools/`：构建后的启动、停止、watchdog、配置校验和板端打包工具。

设备驱动和板端 SDK 不属于源码仓库内容；当前板端使用的 `models/yolopose.rknn`
已纳入本项目部署包，并在 `MANIFEST.txt` 与 `SHA256SUMS` 中记录文件名和校验值。

本项目是独立的区域事件业务核心：区域配置、归一化检测框、检测框底部中心点判定、3 帧进入/5 帧离开防抖和可编辑区域。它不连接任何旧项目控制面，不发送外部通知。

当前实现由 `cpp/` 业务核心、`pipeline/` 输入/模型/显示适配层以及 `region_server.py` 区域管理服务组成。模型推理可以是单模型或多模型，但必须以统一的检测结果边界接入；模型 SDK 只能位于 `pipeline/adapters/model/`。

`posevent` 核心不拥有摄像头、RTSP、HDMI 或板端运行时。核心输入是归一化检测结果，负责区域解析、进入/离开防抖和事件输出；板端采集、模型推理适配和显示由外部 pipeline 适配层接入。

## 环境要求

- CMake 3.16+、C++17 编译器、Make 或 Ninja
- Python 3
- 区域服务抓取 RTSP/V4L2/文件快照需要 `ffmpeg`
- ONNX/RKNN live pipeline 需要对应模型运行时、OpenCV、设备驱动和显示权限

## 构建与测试

```bash
cmake -S . -B build-core
cmake --build build-core --parallel 4
ctest --test-dir build-core --output-on-failure
python3 tools/validate-regions.py configs/regions.json
```

## 运行

项目统一入口是构建产物 `posevent`。运维只需要先选择业务模式，不直接猜测
底层 runner：

```bash
./build-core/posevent help
./build-core/posevent web --host 0.0.0.0 --port 9191 \
  --source rtsp --url 'rtsp://camera/stream' \
  --regions configs/regions.json
./build-core/posevent pipeline ./build-rknn/pipeline/posevent_rknn_live \
  'rtsp://camera/stream' /opt/models/person.rknn /dev/fb0 configs/regions.json
./build-core/posevent replay ./build-core/pipeline/posevent_replay \
  fixtures/detections/zone-entry.jsonl /tmp/results.jsonl
```

其中 `web` 是区域配置服务，`pipeline` 后面才是具体模型 runner，`replay` 是离线
回放。`posevent` 负责统一分发参数和进程退出码；模型 SDK、RTSP 和显示实现仍留在
各自适配器中。生产环境建议使用 `tools/run-posevent-pipeline` 管理 PID、日志和
watchdog，但底层命令也可以通过统一入口显式执行。

`main web` 默认从当前工作目录执行 `region_server.py`；运行包也可以显式指定脚本：

```bash
POSEVENT_REGION_SERVER=/opt/posevent/region_server.py \
  /opt/posevent/bin/posevent web --host 0.0.0.0 --port 9191 \
  --regions /opt/posevent/configs/regions.json
```

打开 `http://<board-ip>:9191/` 后可以抓取 RTSP 背景、拉取/修改/推送
`configs/regions.json`。独立 pipeline 适配层提供 RTSP 输入、模型 adapter、
检测框/区域 OSD、Framebuffer 和 annotated RTSP 输出；启动入口会读取
`configs/pipeline.json`：

```bash
POSEVENT_PIPELINE_BIN=/path/to/posevent_onnx_live \
  tools/run-posevent-pipeline
tools/stop-posevent-pipeline
```

`POSEVENT_PIPELINE_BIN` 可以替换为 RKNN 或 ONNX 独立 runner，核心业务不依赖
模型 SDK 类型。若 runner 提供配置重载命令，可同时设置
`POSEVENT_RELOAD_COMMAND`，启动脚本会管理同生命周期的区域 mtime watcher。

启动脚本默认设置 `POSEVENT_MAX_FRAMES=0`，即持续运行。需要冒烟测试时才显式
设置正数帧数（例如 `POSEVENT_MAX_FRAMES=30`）。板端如果存在 RTSP 解码或
NPU runtime 偶发卡死，可启用受控 watchdog：

```bash
POSEVENT_PIPELINE_BIN=/opt/posevent/bin/posevent_rknn_live \
POSEVENT_ENTRYPOINT=/opt/posevent/bin/posevent \
POSEVENT_INPUT_URL='rtsp://192.168.0.108:8554/vislbox-business' \
POSEVENT_MODEL_PATH=/opt/models/person.rknn \
POSEVENT_REGIONS_CONFIG=/opt/posevent/configs/regions.json \
POSEVENT_FRAMEBUFFER=/dev/fb0 \
POSEVENT_MAX_FRAMES=0 \
POSEVENT_WATCHDOG=1 \
POSEVENT_RUN_TIMEOUT=45 \
tools/run-posevent-pipeline
```

watchdog 每轮启动一个 runner；runner 正常退出或达到超时时间后自动重启，
日志统一写入 `artifacts/runtime/pipeline.log`。它是运行保护措施，不等价于
修复底层 RTSP/RKNN 驱动卡死；如果日志持续出现 `Broken pipe`、`decode_slice_header`
或固定帧数停止，应继续修复设备运行时，而不是把重启次数当作验收证据。

`detections.jsonl` 每行格式：

```json
{"seq": 1, "timestamp_ms": 1, "detections": [{"class": "person", "score": 0.95, "box": [0.2, 0.3, 0.1, 0.3]}]}
```

`box` 使用归一化 `[x, y, w, h]`（左上角 + 宽高）；区域配置可直接编辑 `configs/regions.json`，编辑器静态页面位于 `web/region-editor.html`。

## 文档与排障

- [架构设计](docs/ARCHITECTURE.md)：模块边界、数据协议、部署拓扑和错误策略。
- [业务流程](docs/BUSINESS_FLOWS.md)：部署、推理、配置重载、事件和运维流程。

常用命令：

```bash
build-core/pipeline/posevent_replay fixtures/detections/zone-entry.jsonl /tmp/results.jsonl
tail -f artifacts/runtime/pipeline.log
tools/validate-dependencies.sh
```

当前区域服务没有认证，默认允许跨域，只应部署在受信任局域网或认证网关之后。当前 live runner 的配置加载闭环主要覆盖单矩形区域；部署多区域或多边形前，应确认所选 runner 的解析器支持该能力。

## 测试手册

### 1. 核心单元测试和 pipeline 契约测试

```bash
cmake -S . -B build-core
cmake --build build-core --parallel 4
ctest --test-dir build-core --output-on-failure
```

测试覆盖区域几何、区域分发、FSM 防抖、公开 API、fake model adapter、OSD、RTSP 参数校验、runner、配置重载、模型合并、显示 sink 和 framebuffer renderer。测试失败时先保留完整输出，不要只重跑单个二进制。

### 2. 配置测试

```bash
python3 tools/validate-regions.py configs/regions.json
python3 tools/validate-regions.py fixtures/regions.json  # 若存在自定义 fixture
```

校验器关注版本、非空区域、重复 id、矩形/多边形坐标、类别和置信度。Web 提交和 pipeline reload 都必须使用合法配置；`regions.json.bak` 可作为回滚源。

### 3. 离线业务回放

```bash
build-core/pipeline/posevent_replay \
  fixtures/detections/zone-entry.jsonl \
  /tmp/posevent-results.jsonl
cat /tmp/posevent-results.jsonl
```

JSONL 每行至少包含 `seq` 或 `frame_seq`，检测包含 `class`、`score` 和归一化 `box=[x,y,w,h]`。检查输出中的 `zone_occupied`、`zone_empty` 和 `active`，不要用 `zone_enter`/`zone_leave` 作为当前协议名称。

### 4. 区域服务接口冒烟测试

启动服务：

```bash
python3 region_server.py --host 127.0.0.1 --port 9191 \
  --source file --path ./sample.mp4 \
  --regions /tmp/posevent-regions.json
```

另开终端执行：

```bash
curl -fsS http://127.0.0.1:9191/
curl -fsS http://127.0.0.1:9191/regions.json
curl -fLo /tmp/snapshot.jpg http://127.0.0.1:9191/snapshot.jpg
curl -fsS -X POST -H 'Content-Type: application/json' \
  --data-binary @configs/regions.json \
  http://127.0.0.1:9191/regions.json
curl -fsS -X POST http://127.0.0.1:9191/reload
```

确认 POST 返回 `status=ok`、旧配置生成 `.bak`，并检查 pipeline 日志确认 reload 真正应用。

## 板端部署手册

### 部署前检查

```bash
uname -a
cmake --version
python3 --version
ffmpeg -version
test -r /dev/video41                 # 使用 V4L2 时
test -w /dev/fb0                     # 使用 framebuffer 时
test -r /opt/models/person.onnx      # 按实际模型路径替换
```

同时用 ffmpeg 单独确认摄像头链路：

```bash
ffmpeg -rtsp_transport tcp -i 'rtsp://user:password@camera/stream1' \
  -frames:v 1 -y /tmp/camera.jpg
```

### ONNX 开发机构建

```bash
cmake -S . -B build-onnx -DPOSEVENT_ONNX_ROOT=/opt/onnxruntime
cmake --build build-onnx --parallel 4
POSEVENT_PIPELINE_BIN="$PWD/build-onnx/pipeline/posevent_onnx_live" \
POSEVENT_INPUT_URL='rtsp://user:password@camera/stream1' \
POSEVENT_MODEL_PATH=/opt/models/person.onnx \
POSEVENT_REGIONS_CONFIG="$PWD/configs/regions.json" \
POSEVENT_MAX_FRAMES=0 \
tools/run-posevent-pipeline
```

### RKNN 开发机构建

```bash
cmake -S . -B build-rknn -DPOSEVENT_RKNN_ROOT=/opt/rknn_runtime
cmake --build build-rknn --parallel 4
POSEVENT_PIPELINE_BIN="$PWD/build-rknn/pipeline/posevent_rknn_live" \
POSEVENT_INPUT_URL='rtsp://user:password@camera/stream1' \
POSEVENT_MODEL_PATH=/opt/models/person.rknn \
POSEVENT_REGIONS_CONFIG="$PWD/configs/regions.json" \
POSEVENT_MAX_FRAMES=0 \
tools/run-posevent-pipeline
```

以上两节是开发机具备对应 SDK 时的构建方式。板端没有完整源码或交叉编译 SDK 时，
使用下面的“板端运行包部署”，不要在板端重复执行构建。

`POSEVENT_MAX_FRAMES=0` 表示持续运行，也是启动脚本的默认值；设置正数帧数时 runner 会在达到该帧数后退出，适合冒烟测试。使用 framebuffer 时设置 `POSEVENT_FRAMEBUFFER=/dev/fb0`，脚本可能需要免密 sudo 才能访问设备。

### HDMI 显示路径

108 板端使用 GNOME/Wayland 时，桌面合成器控制 HDMI，直接写 `/dev/fb0` 即使成功也
不一定会出现在物理显示器上。此时应使用 Wayland 显示路径：

```bash
POSEVENT_DISPLAY_MODE=wayland \
POSEVENT_WAYLAND_RUNTIME_DIR=/run/user/<display-uid> \
POSEVENT_WAYLAND_DISPLAY=wayland-0 \
POSEVENT_MAX_FRAMES=0 \
tools/run-posevent-pipeline
```

启动脚本会自动为 runner 补上显示参数；runner 使用 `ffplay` 在当前 Wayland 会话创建
全屏画面。验证时应同时看到 `ffplay` 进程和持续增长的 `frame=` 日志。不要把
`/dev/fb0` 内容变化作为 HDMI 可见性的唯一证据。

### 启动区域编辑服务

建议先启动服务，再启动 pipeline：

```bash
POSEVENT_REGION_SERVER="$PWD/region_server.py" \
./build-core/posevent web \
  --host 0.0.0.0 --port 9191 \
  --source rtsp --url 'rtsp://user:password@camera/stream1' \
  --regions "$PWD/configs/regions.json"
```

浏览器访问 `http://<board-ip>:9191/`，点击“拉取”加载当前配置，抓帧后绘制区域，
点击“推送”。推送成功后必须检查配置文件和 Pipeline 日志；HTTP `200` 只代表 Web
服务接受了配置，不代表 runner 已经应用配置。若 runner 没有启用 mtime reload，
执行 `tools/stop-posevent-pipeline` 后再按启动命令启动。

推送闭环检查：

```bash
curl -fsS http://<board-ip>:9191/regions.json
curl -fsS -X POST -H 'Content-Type: application/json' \
  --data-binary @configs/regions.json \
  http://<board-ip>:9191/regions.json
tail -f artifacts/runtime/pipeline.log
```

必须同时确认配置文件修改时间变化、日志出现 `regions reload applied`，以及
下一帧 overlay 位置变化。HTTP `200` 只代表 Web 服务接受了配置，不代表 Pipeline
已经应用配置。

## 板端运行包部署

板端没有完整源码树或交叉编译 SDK 时，不在板端执行 `cmake -S .`。应在开发机
构建后部署运行包：

```text
/opt/posevent/bin/posevent
/opt/posevent/bin/posevent_rknn_live
/opt/posevent/region_server.py
/opt/posevent/web/region-editor.html
/opt/posevent/configs/pipeline.json
/opt/posevent/configs/regions.json
/opt/posevent/tools/run-posevent-pipeline
/opt/posevent/tools/stop-posevent-pipeline
/opt/posevent/tools/posevent-watchdog
/opt/posevent/models/<model>.rknn
```

部署后先验证统一入口：

```bash
cd /opt/posevent
./bin/posevent help
POSEVENT_REGION_SERVER=/opt/posevent/region_server.py \
  nohup ./bin/posevent web --host 0.0.0.0 --port 9191 \
  --source rtsp --url 'rtsp://192.168.0.108:8554/vislbox-business' \
  --regions /opt/posevent/configs/regions.json \
  >/opt/posevent/artifacts/runtime/web.log 2>&1 & echo $! \
  >/opt/posevent/artifacts/runtime/web.pid
```

Wayland HDMI Pipeline：

```bash
POSEVENT_ENTRYPOINT=/opt/posevent/bin/posevent \
POSEVENT_PIPELINE_BIN=/opt/posevent/bin/posevent_rknn_live \
POSEVENT_INPUT_URL='rtsp://192.168.0.108:8554/vislbox-business' \
POSEVENT_MODEL_PATH=/opt/posevent/models/<model>.rknn \
POSEVENT_REGIONS_CONFIG=/opt/posevent/configs/regions.json \
POSEVENT_DISPLAY_MODE=wayland \
POSEVENT_WAYLAND_RUNTIME_DIR=/run/user/<display-uid> \
POSEVENT_WAYLAND_DISPLAY=wayland-0 \
POSEVENT_MAX_FRAMES=0 \
tools/run-posevent-pipeline

```

停止服务：

```bash
tools/stop-posevent-pipeline
kill "$(cat /opt/posevent/artifacts/runtime/web.pid)" 2>/dev/null || true
```

验收必须同时看到持续增长的 `frame=` 日志和 Wayland `ffplay` 进程；只看到
`/dev/fb0` 内容变化不代表 HDMI 已显示。

### 生成板端部署包

### 一键构建并部署到 108

开发机上直接执行下面的命令。`runner` 必须是已经按板端 RKNN SDK 编译好的
`posevent_rknn_live`；模型使用项目内的 `models/yolopose.rknn`，不需要再手工指定：

```bash
chmod +x tools/build-and-deploy-board.sh
tools/build-and-deploy-board.sh \
  /path/to/posevent_rknn_live \
  gan@192.168.0.108 \
  /opt/posevent
```

这个入口依次执行：核心 C/C++ 构建、区域配置校验、部署包生成、上传、板端原生编译
`main.c`、停止旧进程、启动 Web 区域服务、启动 RKNN 推理/Wayland HDMI 流程，最后
检查模型校验值、Web 接口和推理 PID。板端只需要运行包，不需要执行 CMake。

在开发机完成构建并准备板端 runner 和模型后执行：

```bash
tools/package-board.sh \
  build-rknn/pipeline/posevent_rknn_live \
  /path/to/yolopose.rknn \
  /tmp/posevent-board-package
```

该命令会打包统一入口、runner、模型、108 配置模板、区域服务、编辑页面和运行脚本。
部署包中的 `models/yolopose.rknn` 是唯一板端模型来源。复制到
108 后，将整个目录部署到 `/opt/posevent`，再按上面的启动命令运行。

部署前建议生成校验清单：

```bash
find /tmp/posevent-board-package -type f -print0 | sort -z | xargs -0 shasum -a 256 \
  > /tmp/posevent-board-package/SHA256SUMS
```

## 运行验收

1. `ctest` 全部通过，区域校验输出 `valid`。
2. region server 的首页、配置读取和 snapshot 均返回成功。
3. pipeline 日志出现模型加载成功和持续读帧，没有连续 RTSP 重连。
4. 目标在区域内连续达到进入帧数后出现一次 `zone_occupied`，区域变红。
5. 目标连续离开达到离开帧数后出现一次 `zone_empty`，区域恢复非活动色。
6. 区域编辑后日志出现 `regions reload applied`；若出现 rejected，旧配置仍应继续工作。
7. 停止后 `artifacts/runtime/pipeline.pid` 和 watcher PID 被清理。

## 常用运行变量

| 变量 | 用途 |
|---|---|
| `POSEVENT_PIPELINE_BIN` | 指定 live runner 可执行文件 |
| `POSEVENT_ENTRYPOINT` | 可选统一入口；设为 `posevent` 时由 `posevent pipeline` 分发到底层 runner |
| `POSEVENT_PIPELINE_CONFIG` | 指定 pipeline JSON |
| `POSEVENT_INPUT_URL` | 覆盖配置中的 RTSP URL |
| `POSEVENT_MODEL_PATH` | 覆盖配置中的模型路径 |
| `POSEVENT_REGIONS_CONFIG` | 指定区域配置文件 |
| `POSEVENT_STATE_DIR` | 指定 PID/log 目录 |
| `POSEVENT_FRAMEBUFFER` | 指定 framebuffer 设备 |
| `POSEVENT_MAX_FRAMES` | runner 帧数上限，`0` 为持续运行 |
| `POSEVENT_RELOAD_COMMAND` | 启用外部区域 watcher reload 命令 |
| `POSEVENT_WATCHDOG` | 设为 `1` 启用 runner 自动重启保护 |
| `POSEVENT_RUN_TIMEOUT` | watchdog 单轮最长秒数，`0` 表示不限制 |
| `POSEVENT_RESTART_DELAY` | watchdog 重启间隔，默认 1 秒 |

## 发布前检查

```bash
cmake --build build-core --parallel 4
ctest --test-dir build-core --output-on-failure
python3 tools/validate-regions.py configs/regions.json
tools/validate-dependencies.sh
git diff --check
```

部署包应包含 runner、模型文件、`configs/regions.json`、`web/`、`tools/` 和必要的运行时库；不要把开发机 `build-*`、旧模型库或不匹配的设备驱动混入核心部署包。
