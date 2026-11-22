#!/usr/bin/env python3
"""在确定性证据之上接一层 AI 诊断（可选）。

两种模式：
  1) 有 OpenAI 兼容端点（Ollama / vLLM / 内网网关）：
       python3 ai_diagnose.py --server URL --run-id RUN \
         --endpoint http://127.0.0.1:11434/v1 --model qwen2.5:7b
  2) 没有端点：输出「证据包 + 提示词」，交给任意 Agent / 人工复核：
       python3 ai_diagnose.py --server URL --run-id RUN --evidence-only

设计原则（见 docs/data-lineage-design.md）：
  - 确定性证据（闸门/差分/候选）先算好，AI 只做综合与解释；
  - AI 的每条结论必须引用 evidence_id，引用不上就标 unknown；
  - 离线优先，模型可插拔，不把遥测发到公网。
"""

import argparse
import json
import sys
import urllib.request

SYSTEM_PROMPT = """你是工业控制系统的排障专家。你会收到一份结构化的血缘证据包，
里面包含前置闸门（状态/计数/身份/时序）、候选、偏离基线和值传播边。

规则：
1. 只能依据证据推理，不得臆造字段或服务；
2. 每条结论必须引用证据编号（如 E1/E2），引用不上就写进 unknowns；
3. 区分「根因」与「症状」：下游报错不等于根因；
4. 输出严格 JSON：
   {"root_cause": "...", "confidence": 0.0-1.0,
    "chain": ["服务/字段 → 服务/字段", ...],
    "evidence_ids": ["E1", "E2"], "unknowns": ["..."],
    "next_checks": ["..."]}
"""


def http_get(url, timeout=10):
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8") or "{}")


def build_evidence(base, run_id, iteration_id):
    gates = http_get(base + "/api/lineage/gates?run_id=" + run_id)
    rca = http_get(
        base + "/api/lineage/rca?run_id=" + run_id
        + ("&iteration_id=%d" % iteration_id if iteration_id else "")
    )
    graph = http_get(
        base + "/api/lineage/graph?run_id=" + run_id
        + "&iteration_id=%d" % (rca.get("iteration_id") or 0)
    )

    evidence = []
    idx = 0

    def add(kind, text):
        nonlocal idx
        idx += 1
        evidence.append({"id": "E%d" % idx, "kind": kind, "text": text})
        return "E%d" % idx

    summary = gates.get("summary", {})
    for gate in ("G0", "G1", "G2", "G3"):
        info = summary.get(gate, {})
        add("gate",
            "%s 失败迭代=%s（%s）" % (
                gate, info.get("failed_iterations", 0),
                "通过" if info.get("passed") else "命中"))
    for it in gates.get("iterations", []):
        if not it.get("first_failed"):
            continue
        for g in it.get("gates", []):
            if not g.get("passed"):
                add("gate_detail", "%s %s — %s" % (g["gate"], g["check"], g["detail"]))
        break

    for d in rca.get("deviations", []):
        add("baseline_diff",
            "%s/%s = %s（基线占比 %.0f%%，共 %d 次迭代）" % (
                d.get("object"), d.get("field"), d.get("value"),
                (d.get("baseline_ratio") or 0) * 100, d.get("total", 0)))

    for e in graph.get("edges", []):
        add("value_edge",
            "%s.%s → %s.%s (%s)" % (
                e.get("from_service"), e.get("from_field"),
                e.get("to_service"), e.get("to_field"), e.get("relation")))

    pack = {
        "run_id": run_id,
        "iteration_id": rca.get("iteration_id"),
        "deterministic": {
            "verdict": rca.get("verdict"),
            "confidence": rca.get("confidence"),
            "candidates": rca.get("candidates", []),
            "narrative": rca.get("narrative"),
            "unknowns": rca.get("unknowns", []),
        },
        "evidence": evidence,
    }
    return pack


def call_model(endpoint, model, api_key, pack):
    url = endpoint.rstrip("/") + "/chat/completions"
    body = {
        "model": model,
        "temperature": 0.1,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": json.dumps(pack, ensure_ascii=False)},
        ],
    }
    headers = {"Content-Type": "application/json"}
    if api_key:
        headers["Authorization"] = "Bearer " + api_key
    req = urllib.request.Request(
        url, data=json.dumps(body).encode("utf-8"), headers=headers, method="POST")
    with urllib.request.urlopen(req, timeout=120) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    return data["choices"][0]["message"]["content"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", default="http://127.0.0.1:18428")
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--iteration-id", type=int, default=0)
    parser.add_argument("--endpoint", default="", help="OpenAI 兼容端点，如 http://127.0.0.1:11434/v1")
    parser.add_argument("--model", default="", help="模型名，如 qwen2.5:7b")
    parser.add_argument("--api-key", default="")
    parser.add_argument("--evidence-only", action="store_true",
                        help="只输出证据包与提示词，不调用模型")
    args = parser.parse_args()

    base = args.server.rstrip("/")
    pack = build_evidence(base, args.run_id, args.iteration_id)

    print("=" * 78)
    print("确定性结果（无 AI）")
    print("=" * 78)
    print(json.dumps(pack["deterministic"], ensure_ascii=False, indent=2))

    print("\n" + "=" * 78)
    print("证据包（%d 条）" % len(pack["evidence"]))
    print("=" * 78)
    for e in pack["evidence"]:
        print("  [%s] %-14s %s" % (e["id"], e["kind"], e["text"]))

    if args.evidence_only or not args.endpoint or not args.model:
        print("\n" + "=" * 78)
        print("未配置模型端点：把下面内容交给任意 Agent 即可得到 AI 诊断")
        print("=" * 78)
        print("SYSTEM:\n" + SYSTEM_PROMPT)
        print("USER:\n" + json.dumps(pack, ensure_ascii=False, indent=2))
        return 0

    print("\n" + "=" * 78)
    print("AI 诊断（%s @ %s）" % (args.model, args.endpoint))
    print("=" * 78)
    try:
        content = call_model(args.endpoint, args.model, args.api_key, pack)
        print(content)
        try:
            parsed = json.loads(content)
            print("\n--- 结构化 ---")
            print(json.dumps(parsed, ensure_ascii=False, indent=2))
        except json.JSONDecodeError:
            pass
    except Exception as exc:  # noqa: BLE001
        print("模型调用失败：%s" % exc)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
