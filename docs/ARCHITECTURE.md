# posevent 架构设计

## 总体拓扑

```text
RTSP/V4L2/文件 → RtspSource → ModelAdapter(ONNX/RKNN/Fake)
                                  ↓ DetectionFrame
                         PoseventEngine / zone_core
                  ┌───────────────┼────────────────┐
                  ↓               ↓                ↓
             OSD renderer   Framebuffer/Wayland   Annotated RTSP

浏览器 → region_server.py → 校验、原子写 regions.json → watcher/runner 重载
```

## 分层边界

`cpp/` 中的 `zone_core` 是唯一的业务权威实现。`regionResolver` 负责矩形/多边形命中，`ZoneDispatch` 负责类别与置信度过滤及检测框底部中心点计算，`regionRouter` 负责进入/离开防抖，`PoseventEngine` 输出单帧完整结果。该层只依赖 C++ 标准库，不包含 FFmpeg、OpenCV、模型 SDK、DRM/KMS、Python 或网络代码。

`pipeline/` 将输入帧转换为 `VideoFrame`，通过 `ModelAdapter` 输出统一的 `ModelInference`，再由 `PipelineRunner` 调用核心。renderer/sink 只消费当前帧和 `PoseventFrameResult`，不能从隐式全局状态读取检测框。模型推理失败必须返回 `ok=false`，不能伪装为空检测。

模型 SDK、像素/letterbox 坐标转换和模型专有逻辑只能位于 `pipeline/adapters/model/`。当前 live runner 的配置加载闭环主要覆盖单矩形区域；多区域/多边形部署前应确认所选 runner 的解析器支持该能力。

`region_server.py` 只负责区域编辑、配置读写和快照，不负责推理、事件决策或通知。写配置前调用 `tools/validate-regions.py`，成功后备份旧文件并原子替换。当前服务无认证，必须限制在可信网络。

## 数据协议与业务规则

检测框为归一化左上角加宽高，区域命中点为：

```text
center_x = x + w / 2
bottom_y = y + h
```

`DetectionFrame` 包含帧序号、时间戳、尺寸、detections 和可选 poses；`PoseventFrameResult` 包含当前帧 detections、events 和 overlays。确认进入后 `active=true` 且 overlay 为红色，确认离开后恢复非活动颜色。

### 对外事件与内部状态

内部状态机有 `idle`、`preDetect`、`active`、`postDetect`、`cooldown` 五个状态。`onEnter`、`onConfirm`、`onLost`、`onReturn`、`onLeave`、`onReady` 是内部过渡；只有 `onConfirm` 和 `onLeave` 映射为对外事件：

| 内部过渡 | 对外 `ZoneEvent.type` | 说明 |
|---|---|---|
| `onEnter` | 无 | 首次看到目标，开始进入计数 |
| `onConfirm` | `zone_occupied` | 连续达到进入帧数 |
| `onLost` / `onReturn` | 无 | 离开防抖及其恢复 |
| `onLeave` | `zone_empty` | 连续达到离开帧数 |
| `onReady` | 无 | 冷却结束，重新进入 idle |

占用事件的 `detectionCount` 是确认帧命中的检测数量，清空事件的数量固定为 0。帧号是输入序号，不是毫秒时间。

`stateCounter` 在每次 tick 开始递增；因此 `confirmFrames=3` 在连续命中的第 3 帧确认，`lostFrames=5` 在连续空帧的第 5 帧确认。防抖按帧数而不是毫秒计时，帧率变化会改变实际时间窗口。

`regions.json` 必须包含 `version`、顶层 `debounce` 和非空 `regions`。每个区域包含 id/name/type，以及矩形的 x/y/w/h 或多边形 vertices，可选 classes 和 min_confidence。id 不可重复，坐标必须在 `[0,1]`，多边形至少 3 个点，未知字段会被校验器拒绝。核心默认连续 3 帧进入、5 帧离开。

## 错误与状态策略

- 模型加载失败：pipeline 启动失败，不进入“无人”状态。
- 单帧推理失败：返回错误并记录，不能当作合法空检测。
- 配置重载失败：保留上一份有效的 engine，新配置不得部分生效。
- 空区域/非法配置：拒绝，不能退化为全屏区域。
- RTSP 断流：由输入适配器有限次重连，业务核心不感知网络细节。
- 配置重载会整体替换 `PoseventEngine`，区域状态和防抖计数重新开始，这是避免旧状态与新几何区域混用的有意取舍。

## 构建、部署与验证

开发机/CI 用 CMake 构建核心、pipeline tests 和 replay；板端额外注入 ONNX Runtime 或 RKNN SDK、摄像头驱动和显示权限；管理端通过 HTTP 修改区域文件。模型库和设备驱动不应提交或链接到核心库。

验证入口：`ctest --test-dir build-core --output-on-failure`、`python3 tools/validate-regions.py configs/regions.json`、`posevent_replay`、`artifacts/runtime/pipeline.log` 和 `tools/validate-dependencies.sh`。历史边界见 [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)，代码与流程审计见 [CODE_FLOW_AUDIT.md](CODE_FLOW_AUDIT.md)。
