# 项目介绍 PPT

面向不了解本项目的听众（会一点 C++、不熟悉三方库），用 26 页讲清：

- 为什么需要它、和竞品差别在哪；
- 四层架构、模块设计、系统调度与框架原理（事件模型、关联键、SDK/Agent/Server
  分工、调用链还原、降级机制）；
- 六块核心能力、四个高频场景、快速诊断三步法与客户端接入；
- 如何接入、如何上手、当前边界与下一步。

更细的类图、时序图与框架原理图见 [../module-design.md](../module-design.md)；
链路时序的计算细节见 [../principles.md](../principles.md)。

## 文件

| 文件 | 说明 |
|---|---|
| `vmon-intro.pptx` | 项目总体介绍（26 页，16:9） |
| `trace-timeline.pptx` | 链路时序还原专题（15 页，进程间 + 进程内） |
| `build_deck.py` | 项目介绍生成脚本 |
| `build_trace_deck.py` | 时序专题生成脚本 |
| `requirements.txt` | 唯一依赖 `python-pptx` |

## 重新生成

```bash
python3 -m venv .venv
.venv/bin/pip install -r docs/presentation/requirements.txt
.venv/bin/python docs/presentation/build_deck.py
.venv/bin/python docs/presentation/build_trace_deck.py
```

脚本会同时做结构自检：页数、形状越界、明显文字溢出。

## 预览（不依赖 Office）

仓库自带一个近似渲染脚本，把每页导出为 PNG，适合快速检查中文、排版与溢出：

```bash
.venv/bin/python docs/presentation/render_preview.py \
  docs/presentation/vmon-intro.pptx -o /tmp/vmon-preview
```

它只支持本项目用到的矩形、圆角矩形、椭圆、箭头、文本框与表格，属于“够用”
的预览，最终效果以 PowerPoint / WPS 打开为准。

如果本机已安装 LibreOffice，也可以导出 PDF：

```bash
/Applications/LibreOffice.app/Contents/MacOS/soffice \
  --headless --convert-to pdf --outdir /tmp/vmon-ppt \
  docs/presentation/vmon-intro.pptx
```

> 注意：部分 macOS 环境的 LibreOffice 无中文字体渲染能力，PDF 中可能丢字；
> 此时优先用上面的 `render_preview.py`，或用 WPS/PowerPoint 打开 PPTX。

## 讲稿

每页的备注（演讲者视图）已写入 `notes`，包含该页要强调的一句话。
