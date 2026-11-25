# 示例图示（SVG）

> 这里存放示例文档用的原理框架、拓扑与时序图。SVG 可离线查看、可插入 PPT，
> 也便于代码评审时直接预览。
>
> 所有图由 [build_diagrams.py](build_diagrams.py) 生成，只依赖 Python 标准库。

## 1. 图清单

| 文件 | 内容 | 使用位置 |
|---|---|---|
| [overall-data-flow.svg](overall-data-flow.svg) | 统一采集数据流：业务进程 → Agent → Server → Web/CLI/证据包 | [examples/README.md](../README.md) |
| [hello-span-tree.svg](hello-span-tree.svg) | `demo-request-1` 的 Span 树与错误传播 | [hello/README.md](../hello/README.md) |
| [http-backend-sequence.svg](http-backend-sequence.svg) | Python/浏览器 → BFF → C++ 后端 → Agent → Server 时序 | [http_backend/README.md](../http_backend/README.md) |
| [faultlab-topology.svg](faultlab-topology.svg) | faultlab 六角色拓扑与故障注入点 | [faultlab/README.md](../faultlab/README.md) |
| [industrial-framework.svg](industrial-framework.svg) | 工业闭环：7 进程 → G0–G3 闸门 → RCA → Web | [industrial/README.md](../industrial/README.md) |

## 2. 重新生成

```bash
python3 examples/diagrams/build_diagrams.py
```

脚本会覆盖同目录下的 SVG。修改配色、文案或布局时，直接改
[build_diagrams.py](build_diagrams.py) 里的 `COLORS` / `node()` / `arrow()`
等辅助函数，然后重新运行。

## 3. 约定

- 画布使用固定 `viewBox`，根节点带 `max-width:100%;height:auto`，窄屏会等比缩放；
- 文字统一使用系统中文字体回退链，GitHub / 浏览器 / VS Code 预览均可显示；
- 箭头颜色语义：蓝 = 请求/入口，青 = Agent/采集，绿 = Server/存储，
  橙 = 风险/慢，红 = 错误/崩溃，紫 = 客户端/输出，灰 = 普通关联；
- 文档中同时保留 Mermaid 源码（折叠在 `<details>` 里），改结构时优先改 Mermaid，
  需要静态图时再同步 SVG。
