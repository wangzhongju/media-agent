# TODO

- Skills 总览：`SKILL.md`
- Pipeline 模块文档：`modules/pipeline.md`
- RTSP 模块文档：`modules/rtsp.md`

## 高优先级

- [ ] 为 `StreamBuffer::canPublishLocked()` 的严格发布阻塞增加超时或降级策略，避免检测线程卡住后整路流永久阻塞。
- [ ] 为 packet/frame 对齐增加运行时诊断：匹配失败或漂移时记录 packet 的 `frame_id` / `pts`、命中的推理结果 `pts`，以及 pending-frame 回填数量。
- [ ] 如果上游时间戳抖动继续增大，需要在当前 `pts` 容差兜底之外，进一步增强 packet/result 的关联策略。
- [ ] 在高负载下验证多路流行为，并按 stream 调整缓存推理结果的 TTL，不要继续依赖固定超时预算。

## 中优先级

- [ ] 如果队列深度继续增长，需要减少 `StreamBuffer` 中缓存结果查找和最早阻塞解码帧查找的线性扫描。
- [ ] 如果 detector 吞吐成为瓶颈，需要重新评估当前“每个 stream 只允许一个 inflight 推理任务”的调度模型。
- [ ] 在长队列和多路流场景下，为 `StreamBuffer` 增加锁竞争和端到端时延监控。
- [ ] 评估严格发布等待对音视频时延的影响，特别是在推理阻塞或解码完成延迟时。
- [ ] 观察 `decoded_image == nullptr` 的占位帧是否会在异常流下持续积压，并按需补清理或指标统计。
- [ ] 在异常流场景下验证 packet/frame 匹配鲁棒性，确认 `pts/dts` 启发式是否仍会持续漂移。

## 低优先级

- [ ] 补充针对 packet/frame 对齐、占位帧回填、缓存结果消费，以及 `Done` / `Dropped` 状态切换附近发布阻塞行为的专项测试。
- [ ] 在开发文档中补充 90kHz 时间基下 `pts` 匹配语义，以及当前 ±100ms 匹配窗口的说明。