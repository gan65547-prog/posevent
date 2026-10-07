# 代码与业务流程审计

## 审计范围

本次审计覆盖 `cpp/` 核心、`pipeline/` runner/adapter、`region_server.py`、区域校验器、启停脚本和测试。结论以当前源代码行为为准。

## 已确认主链路

1. live runner 从 RTSP 读取 `VideoFrame`。
2. model adapter 返回 `ModelInference`；成功时携带 `DetectionFrame`，失败时返回错误。
3. `PipelineRunner` 将检测帧交给 `PoseventEngine`。
4. `ZoneDispatch` 计算检测框底部中心点，解析命中区域，再做类别/置信度过滤。
5. `regionRouter` 对已注册区域逐帧 tick。
6. `PoseventEngine` 汇总事件和 overlay；`postDetect` 期间 overlay 仍保持 active，避免短暂漏检导致闪烁。
7. renderer/sink 使用当前帧结果输出 OSD、framebuffer 或 annotated RTSP。

## 业务正确性结论

- 区域支持矩形和多边形；一个检测可以命中多个重叠区域。
- 类别和最低置信度过滤发生在占用判定之前。
- 区域状态按 id 独立维护，进入/离开/冷却/短时漏检均由 FSM 处理。
- 配置重载先构造替代引擎，失败时保留旧引擎。
- 对外事件是 `zone_occupied` 和 `zone_empty`，不是 `zone_enter` / `zone_leave`。
- `onEnter`、`onLost` 等只是内部状态过渡。
- 防抖按帧数而非毫秒计时；帧率变化会改变实际时间窗口。
- 简化的 `process(detections, frame)` 不设置 timestamp、width、height；只有 `DetectionFrame` 重载保留这些元数据。

## 配置与 live runner 风险

校验器支持矩形、多边形和多个区域，但当前 ONNX/RKNN live runner 的解析闭环主要按矩形区域读取。因此 Web 保存成功不等于所有 runner 都能完整执行该配置；部署多区域、多边形、类别和阈值字段前必须用目标 runner 验证。

Web 的 `/reload` 只更新区域文件 mtime，真正应用动作由 watcher 或 runner 完成；没有 reload consumer 时，接口成功不会改变运行中的 engine。

## 运维与安全风险

- 服务默认监听 `0.0.0.0`，无认证且允许跨域，只能放在可信局域网或认证反向代理之后。
- PID 文件和日志位于 `artifacts/runtime`，不能只凭 PID 文件判断健康，应同时检查进程、日志和输入帧。
- pipeline 依赖模型库、设备驱动、ffmpeg 和显示权限，这些不由核心构建保证。
- `POSEVENT_RELOAD_COMMAND` 是 shell 命令字符串，部署时不得拼接不可信输入。

## 文档维护规则

修改事件类型、状态机阈值、配置 schema、runner CLI 或 API 端点时，应同步更新架构文档、业务流程、README，并补充对应回归测试。
