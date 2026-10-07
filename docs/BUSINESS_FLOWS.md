# posevent 业务流程

## 1. 首次部署

```text
准备板端依赖 → 配置 camera/model/regions → 构建 runner
→ 启动 region server（可选）→ 启动 pipeline
→ 检查日志和画面 → 用 replay/测试用例验收事件边界
```

先安装 C++17/CMake/Python/ffmpeg 和板端模型运行时，修改 `configs/pipeline.json`，执行区域校验，再运行 `tools/run-posevent-pipeline` 并检查 `artifacts/runtime/pipeline.log`。

## 2. 单帧到事件

```text
输入帧 → 解码 → 模型推理 → 坐标归一化
→ 类别/置信度过滤 → 底部中心点命中
→ 每个区域更新防抖状态机 → events + overlays → OSD/sink
```

当检测框底部中心点连续达到进入帧数时产生一次 `zone_occupied` 并标红；连续离开达到离开帧数时产生一次 `zone_empty` 并恢复。不同区域分别维护状态，一个检测可以命中多个区域，事件不会每帧重复。`onEnter` 和 `onLost` 只是内部防抖过渡，不是对外业务事件。

事件载荷包括区域 id、区域名称、触发帧号和检测数量：占用事件的 `detectionCount` 是确认帧命中的检测数，清空事件的数量固定为 0。帧号是输入序号，不是毫秒时间。

## 3. 区域编辑与重载

```text
浏览器拉取 snapshot/regions → 编辑 → POST /regions.json
→ schema 校验 → 失败则原文件不变
→ 成功则备份并原子替换 → POST /reload 或 watcher 检测 mtime
→ runner 构造新 engine → 成功切换 / 失败保留旧 engine
```

配置切换会重建区域引擎，区域状态会重新开始计数；不支持热加载的 runner 保存后需要重启。保存成功只代表配置文件写入成功，必须同时检查 pipeline 日志确认 reload 已应用，否则仍可能使用旧配置。

## 4. 启停与异常

启动脚本检查配置和 runner，创建 runtime 目录并写 PID/日志；重复启动保持幂等。停止脚本终止 pipeline 并清理 PID。RTSP 断流由输入适配器有限次重连，不能把断流伪装成 `zone_empty`；当前核心没有独立的输入失效事件，运维侧应结合进程、日志和重连状态判断健康。

## 5. 离线验证

```bash
cmake --build build-core --parallel 4
build-core/pipeline/posevent_replay fixtures/detections/zone-entry.jsonl /tmp/results.jsonl
rg 'zone_occupied|zone_empty|active' /tmp/results.jsonl
```

replay 与 live runner 共享 `zone_core`，用于验证业务状态机，不替代真实摄像头、模型和显示链路验收。

## 6. 运维检查表

| 检查项 | 通过标准 |
|---|---|
| 配置 | 校验器输出 `valid` |
| 核心测试 | ctest 全部通过 |
| 输入/模型 | 能持续读帧且模型加载成功 |
| 事件 | 只出现 `zone_occupied` / `zone_empty`，且在防抖确认后出现 |
| 画面 | OSD 状态与事件一致 |
| 配置修改 | applied；失败时旧配置仍工作 |
| 进程 | PID 与存活进程一致，停止后清理 |

## 7. 已知限制

当前管理面无认证；live runner 的配置解析以单矩形闭环为主；`cooldown_frames` 虽在公共配置中，具体 runner 是否完整执行需以对应 adapter 测试为准；Python JSONL 监测器仅作辅助，生产结论以 C++ `zone_core` 为准。

## 8. 核心业务流程与代码跳转

本节按一次输入帧的实际执行顺序描述代码。阅读代码时建议从 `PipelineRunner::process` 开始，而不是从模型 adapter 开始：前者是业务管线和核心边界的稳定入口。

### 8.1 启动与对象初始化

```text
run-posevent-pipeline
  → 读取 configs/pipeline.json
  → 选择 POSEVENT_PIPELINE_BIN
  → 启动 onnx_live / rknn_live
  → runner 创建 ModelAdapter、ZoneDefinition、PipelineRunner
  → loadModel()
  → RtspSource::open()
```

代码入口：

- 启动脚本：[`tools/run-posevent-pipeline`](../tools/run-posevent-pipeline)，配置读取和 runner 选择见约第 10-58 行，进程启动和 PID/log 管理见约第 70-97 行。
- ONNX runner：[`pipeline/adapters/model/onnx/onnx_live_runner.cpp`](../pipeline/adapters/model/onnx/onnx_live_runner.cpp)，`main` 约第 94 行。
- RKNN runner：[`pipeline/adapters/model/rknn/rknn_live_runner.cpp`](../pipeline/adapters/model/rknn/rknn_live_runner.cpp)，`main` 约第 39 行。
- 业务 runner：[`pipeline/src/pipeline_runner.cpp`](../pipeline/src/pipeline_runner.cpp)，构造约第 3 行、模型加载约第 8 行。

`PipelineRunner` 构造时立即创建 `PoseventEngine`，而 `ZoneDispatch` 构造时将每个区域同时注册到空间解析器和状态机；见 [`cpp/src/zone_dispatch.cpp`](../cpp/src/zone_dispatch.cpp) 第 6-13 行。因此正常配置下，区域在第一帧前已经处于 `idle`，不会等待第一次命中才注册。

### 8.2 输入帧和模型推理

```text
RtspSource::read(VideoFrame)
  → ModelAdapter::infer(VideoFrame)
  → ModelInference.ok
       ├─ false：PipelineRunner 返回错误，不生成业务结果
       └─ true ：取 inference.frame 进入 PoseventEngine
```

业务边界在 [`pipeline/src/pipeline_runner.cpp`](../pipeline/src/pipeline_runner.cpp) 第 16-29 行：模型失败不会被转换为空检测。模型专有的预处理、推理和后处理位于 [`pipeline/adapters/model/`](../pipeline/adapters/model/)，输出统一 `DetectionFrame`；核心不读取 SDK 类型。

### 8.3 检测框转区域占用

`ZoneDispatch::process` 是核心业务的第一个关键跳转：

1. 第 20-22 行遍历检测并计算底部中心点 `x+w/2`、`y+h`。
2. 第 23 行调用 `regionResolver::resolvePoint`，一个点可以命中多个重叠区域。
3. 第 24-27 行按区域 id 找配置，并调用 `accepts`。
4. 第 52-56 行执行 `confidence >= minConfidence` 和类别白名单判断。
5. 第 33-34 行去重，区域只要有一个有效检测就算本帧 occupied；`counts` 保留该区域的有效检测数量。

代码：[`cpp/src/zone_dispatch.cpp`](../cpp/src/zone_dispatch.cpp)。几何实现：[`cpp/src/region_resolver.cpp`](../cpp/src/region_resolver.cpp)；矩形边界包含边界点，多边形使用 ray-casting。

### 8.4 状态机和防抖

`ZoneDispatch` 将去重后的 occupied 区域列表交给 [`regionRouter::tick`](../cpp/src/region_router.cpp) 第 13-52 行。状态机逐区域执行 `step`，见同文件第 100-163 行：

```text
idle
  └─ occupied → preDetect（onEnter）
preDetect
  ├─ 连续达到 confirmFrames → active（onConfirm）
  └─ empty → idle（onFalse）
active
  └─ empty → postDetect（onLost）
postDetect
  ├─ occupied → active（onReturn）
  └─ 连续达到 lostFrames → cooldown（onLeave）
cooldown
  └─ 达到 cooldownFrames → idle（onReady）
```

注意：状态机按帧计数。进入 `preDetect` 时计数置为 1；因此默认 `confirmFrames=3` 在第 3 个连续 occupied 帧确认。离开同理，默认 `lostFrames=5` 在第 5 个连续 empty 帧确认。

### 8.5 事件映射

`ZoneDispatch::process` 第 36-49 行读取 router 的 `lastEvent`：

- `onConfirm` → `zone_occupied`，携带区域 id、名称、确认帧号和有效检测数量。
- `onLeave` → `zone_empty`，携带区域 id、名称、帧号，检测数量为 0。
- `onEnter`、`onFalse`、`onLost`、`onReturn`、`onReady` 不对外发 `ZoneEvent`。

这一步是业务系统消费事件的唯一推荐入口；不要根据 overlay 颜色或 router 内部枚举自行重复推导事件。

### 8.6 Overlay 输出和画面行为

`PoseventEngine::process` 见 [`cpp/src/posevent_engine.cpp`](../cpp/src/posevent_engine.cpp) 第 9-29 行：

- 第 13-15 行保留帧号、原始 detections 并附加业务事件。
- 第 16-26 行为每个区域生成 overlay。
- `active` 或 `postDetect` 都显示 active，因此离开防抖期间区域继续保持红色，避免短暂漏检导致闪烁。
- `DetectionFrame` 重载第 31-37 行额外复制 timestamp、width、height。

随后由 [`pipeline/src/osd_renderer.cpp`](../pipeline/src/osd_renderer.cpp)、framebuffer/Wayland sink 或 annotated RTSP sink 消费结果。renderer 不应重新执行区域判定。

### 8.7 区域配置更新流程

```text
浏览器 POST /regions.json
  → region_server.py 解析 JSON
  → tools/validate-regions.py 校验
  → 写临时文件 + fsync
  → 备份旧 regions.json 为 .bak
  → os.replace 原子替换
  → POST /reload 更新 mtime
  → runner 检测 mtime
  → loadRegionsConfig
  → PipelineRunner::reloadZones
  → 新 engine 替换旧 engine，状态重新开始
```

服务端代码：[`region_server.py`](../region_server.py) 第 235-271 行；reload 端点第 273-281 行。ONNX runner 的 mtime 检测和加载见 [`onnx_live_runner.cpp`](../pipeline/adapters/model/onnx/onnx_live_runner.cpp) 约第 127-149 行；原子替换业务边界见 [`pipeline/src/pipeline_runner.cpp`](../pipeline/src/pipeline_runner.cpp) 第 31-42 行。

`/reload` 的响应只表示 mtime 已更新，不代表 runner 一定应用成功；应以 pipeline 日志中的 `regions reload applied` 为准。

### 8.8 异常分支

| 异常 | 代码行为 | 业务含义 |
|---|---|---|
| 模型加载失败 | `loadModel` 返回 false，runner 退出 | 不把系统误报为无人 |
| 单帧推理失败 | `PipelineRunner::process` 返回 false | 当前帧无业务结论 |
| 配置为空 | reload 拒绝 | 继续使用旧 engine |
| 配置语法/几何错误 | loader/validator 拒绝 | 继续使用旧配置 |
| RTSP 断流 | `RtspSource` 负责重连 | 不自动产生 `zone_empty` |
| sink 打开失败 | runner 启动失败 | 先修复输出权限/地址 |
| PID 失效 | stop 脚本需人工检查 | 不能只看 PID 文件判断健康 |
