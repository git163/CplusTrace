# 三方源码目录

本目录用于离线构建：

| 目录 | 是否入库 | 说明 |
|---|---|---|
| `src/` | 是 | 已解压的三方源码/头文件，构建时直接引用 |
| `downloads/` | 否 | 下载的压缩包与校验和，体积大，不入 git |
| `bin/` | 否 | 预编译二进制（VictoriaMetrics/Logs 等），按平台下载，不入 git |
| `deps.tsv` | 是 | 依赖版本、许可证、下载地址与校验和清单 |

## 已入库源码

- CLI11 2.7.2（BSD-3-Clause）
- backward-cpp 1.6（MIT）
- concurrentqueue 1.0.5（BSD-2-Clause / Apache-2.0）
- cpp-httplib 0.64.0（MIT）
- cpptrace 1.0.4（Apache-2.0 / MIT）
- fmt 12.2.0（MIT）
- google-benchmark 1.10.0（Apache-2.0）
- googletest 1.12.1（BSD-3-Clause）
- libbpf 1.8.0（LGPL-2.1 / BSD-2-Clause 双许可；仅作为可选源码，默认不构建）
- nlohmann/json 3.12.0（MIT）
- re2 2025-11-05（BSD-3-Clause）
- tomlplusplus 3.4.0（MIT）
- xxHash 0.8.4（BSD-2-Clause）
- yaml-cpp 0.9.0（MIT）
- zlib 1.3.2（zlib）
- zstd 1.5.7（BSD-3-Clause / GPL-2.0 双许可；使用 BSD 条款）

许可证全文在各源码目录内；完整清单与下载方式见
[scripts/download-third-party.sh](../scripts/download-third-party.sh) 与
[docs/third-party-deps.md](../docs/third-party-deps.md)。

## 更新方式

```bash
./scripts/download-third-party.sh --proxy http://127.0.0.1:7890
./scripts/gen-deps.sh
```

`src/` 变更后需要重新提交；`downloads/`、`bin/` 不进入 git。
