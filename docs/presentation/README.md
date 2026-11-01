# 项目介绍 PPT

面向不了解本项目的听众（会一点 C++、不熟悉三方库），用 20 页讲清：

- 为什么需要它、和竞品差别在哪；
- 四层架构与框架原理（事件模型、关联键、SDK/Agent/Server 分工、调用链还原、降级机制）；
- 六块核心能力与四个高频使用场景；
- 如何接入、如何上手、当前边界与下一步。

## 文件

| 文件 | 说明 |
|---|---|
| `vmon-intro.pptx` | 可直接编辑/放映的演示文稿（16:9） |
| `build_deck.py` | 生成脚本，改文案/配色后重新运行即可 |
| `requirements.txt` | 唯一依赖 `python-pptx` |

## 重新生成

```bash
python3 -m venv .venv
.venv/bin/pip install -r docs/presentation/requirements.txt
.venv/bin/python docs/presentation/build_deck.py
```

脚本会同时做结构自检：页数、形状越界、明显文字溢出。

## 渲染与预览（可选）

安装 LibreOffice 后可导出 PDF 逐页检查：

```bash
/Applications/LibreOffice.app/Contents/MacOS/soffice \
  --headless --convert-to pdf --outdir /tmp/vmon-ppt \
  docs/presentation/vmon-intro.pptx
```

## 讲稿

每页的备注（演讲者视图）已写入 `notes`，包含该页要强调的一句话。
