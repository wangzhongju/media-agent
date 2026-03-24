# 算法模块

## 关注文件

- `src/detector/IDetector.h`
- `src/detector/AlgoDetector.h`
- `src/detector/AlgoDetector.cpp`
- `src/detector/DetectorFactory.h`

## 当前职责

- `IDetector` 定义统一算法接口。
- `AlgoDetector` 是当前算法桩实现，也是后续真实算法库接入点。
- `DetectorFactory` 按 `cfg.algorithm_id()` 创建 detector。

## 当前接口

- `init(const AlgorithmConfig&)`
- `detect(const VideoFrame&, const AlgorithmConfig&) -> std::vector<DetectionObject>`
- `release()`

## 开发约束

- detector 只做算法推理，不做业务消息组装。
- 不要在 detector 中构造 `AlarmInfo` 或 `DetectionFrame`。
- 算法运行时参数直接从 `AlgorithmConfig` 读取。
- 支持多目标输出时，直接返回多个 `DetectionObject`。
- 如果算法支持零拷贝，优先使用 `frame.dma_buf->fd`。

## 常见改动

### 接入真实算法库

1. 在 `AlgoDetector.cpp` 中替换 stub 推理逻辑。
2. 在 `init()` 中完成模型加载和算法上下文初始化。
3. 在 `detect()` 中把算法输出转换成 `DetectionObject` 列表。
4. 在 `release()` 中完成算法资源释放。

### 增加算法参数

1. 在 `media-agent.proto` 的 `AlgorithmConfig` 中增加字段。
2. 构建生成新的 protobuf 代码。
3. 在 `AlgoDetector::init` 或 `AlgoDetector::detect` 中使用该字段。

### 增加新的 detector 类型

1. 新增一个 `IDetector` 实现。
2. 在 `DetectorFactory.h` 中增加按 `algorithm_id()` 的分发逻辑。

## 禁止事项

- 不要为 detector 输出重新定义本地结果结构。
- 不要把报警规则塞进算法层。
- 不要把 pipeline 业务逻辑复制到 detector。