# 常用场景演示

一条命令启动真实 Server + Agent + C++ HTTP 后端，并制造对应场景：

```bash
python3 examples/scenarios/demo.py --scenario rpc --keep
python3 examples/scenarios/demo.py --scenario slow --keep
python3 examples/scenarios/demo.py --scenario error --keep
python3 examples/scenarios/demo.py --scenario logstorm --keep
python3 examples/scenarios/demo.py --scenario slo --keep
python3 examples/scenarios/demo.py --scenario crash --keep
python3 examples/scenarios/demo.py --scenario all --keep
```

`--keep` 会保留进程与临时目录，脚本会打印 Web UI 地址、令牌、trace_id 和
request_id；按 Ctrl+C 结束并清理。

每个场景的目的、观察顺序与产出见
[docs/scenario-cookbook.md](../../docs/scenario-cookbook.md)。
快速定位方法见 [docs/quick-diagnosis.md](../../docs/quick-diagnosis.md)。

## 依赖

- 已构建 `build/`；
- Python 3 标准库；
- `clients/python/vmon_trace.py`（仓库内，无需安装）。
