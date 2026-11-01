/* vmon Web UI：无框架、离线可用，全部通过 /api 与中心服务交互。 */
(function () {
  "use strict";

  const state = {
    token: localStorage.getItem("vmon-token") || "",
    view: "overview",
    overviewTimer: null,
    selectedTrace: "",
    // 聚类下钻时先记录指纹，再由 showView("logs") 统一消费，
    // 避免 hash 路由触发的 loadLogs() 与指纹查询并发覆盖结果。
    pendingLogFingerprint: "",
  };

  const $ = (id) => document.getElementById(id);
  const esc = (value) =>
    String(value == null ? "" : value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");

  function toast(message) {
    const node = $("toast");
    node.textContent = message;
    node.classList.add("show");
    clearTimeout(node._timer);
    node._timer = setTimeout(() => node.classList.remove("show"), 2600);
  }

  async function api(path, options) {
    const opts = options || {};
    const headers = Object.assign({}, opts.headers || {});
    if (state.token) headers["X-Vmon-Token"] = state.token;
    if (opts.json !== undefined) {
      headers["Content-Type"] = "application/json";
      opts.body = JSON.stringify(opts.json);
      opts.method = opts.method || "POST";
    }
    let res;
    try {
      res = await fetch(path, { method: opts.method || "GET", headers, body: opts.body });
    } catch (err) {
      throw new Error("网络请求失败: " + err.message);
    }
    const text = await res.text();
    let data = null;
    try {
      data = text ? JSON.parse(text) : null;
    } catch (err) {
      data = null;
    }
    if (!res.ok) {
      const message = data && data.error ? data.error : text || res.statusText;
      if (res.status === 401) toast("未授权，请先填写访问令牌");
      throw new Error(message || "HTTP " + res.status);
    }
    return data;
  }

  function ms(ns) {
    if (!ns) return "0 ms";
    const value = Number(ns) / 1e6;
    if (value >= 1000) return (value / 1000).toFixed(2) + " s";
    return value.toFixed(value < 10 ? 1 : 0) + " ms";
  }

  // span 尚未结束时 end_ns 为 0、duration_ns 为 0；不要显示成负数。
  function spanMs(span) {
    if (!span) return "0 ms";
    const duration = Number(span.duration_ns || 0);
    if (duration > 0) return ms(duration);
    return Number(span.end_ns || 0) > 0 ? "0 ms" : "进行中";
  }

  function pct(value) {
    return (Number(value || 0) * 100).toFixed(2) + "%";
  }

  function badge(level) {
    return '<span class="badge ' + esc(level || "info") + '">' + esc(level || "-") + "</span>";
  }

  function empty(text) {
    return '<p class="muted">' + esc(text || "暂无数据") + "</p>";
  }

  function safeRun(fn) {
    return async function () {
      try {
        await fn.apply(null, arguments);
      } catch (err) {
        toast(err.message || String(err));
        console.error(err);
      }
    };
  }

  // ------------------------------------------------------------ 总览
  function cards(items) {
    return items
      .map(
        (item) =>
          '<div class="card"><div class="label">' +
          esc(item.label) +
          '</div><div class="value">' +
          esc(item.value) +
          '</div><div class="hint">' +
          esc(item.hint || "") +
          "</div></div>"
      )
      .join("");
  }

  function traceTable(traces, onClick) {
    if (!traces || !traces.length) return empty("暂无调用链");
    const rows = traces
      .map(
        (t) =>
          '<tr class="clickable" data-trace="' +
          esc(t.trace_id) +
          '"><td class="mono">' +
          esc(t.trace_id.slice(0, 12)) +
          '…</td><td>' +
          (t.has_error ? '<span class="error-text">错误</span>' : '<span class="ok-text">正常</span>') +
          "</td><td>" +
          esc(t.root_service || "-") +
          "</td><td>" +
          esc(t.root_name || "-") +
          "</td><td>" +
          ms(t.duration_ns) +
          "</td><td>" +
          esc(t.time || "") +
          "</td></tr>"
      )
      .join("");
    const html =
      '<table><thead><tr><th>Trace</th><th>状态</th><th>服务</th><th>入口</th><th>耗时</th><th>时间</th></tr></thead><tbody>' +
      rows +
      "</tbody></table>";
    if (onClick) {
      setTimeout(() => {
        document.querySelectorAll("tr[data-trace]").forEach((row) => {
          row.addEventListener("click", () => onClick(row.getAttribute("data-trace")));
        });
      }, 0);
    }
    return html;
  }

  const loadOverview = safeRun(async function () {
    const data = await api("/api/overview");
    $("overview-cards").innerHTML = cards([
      { label: "节点", value: data.node_count, hint: "在线 Agent" },
      { label: "服务", value: data.service_count, hint: data.instance_count + " 个实例" },
      { label: "5 分钟 QPS", value: Number(data.qps || 0).toFixed(2) },
      { label: "错误率", value: Number(data.error_rate || 0).toFixed(1) + "%", hint: data.error_count_5m + " 条错误链路" },
      { label: "P95", value: data.p95_ms.toFixed(1) + " ms" },
      { label: "P99", value: data.p99_ms.toFixed(1) + " ms" },
      { label: "链路缓存", value: data.trace_count },
      { label: "日志", value: data.log_count, hint: "采样 " + data.logs_sampled + " / 去重 " + data.logs_deduped + " / 噪声 " + data.logs_noise },
    ]);

    $("overview-slow").innerHTML = traceTable(data.slow_traces, openTrace);
    $("overview-errors").innerHTML = (data.top_errors || []).length
      ? '<table><thead><tr><th>次数</th><th>级别</th><th>模板</th><th>服务</th></tr></thead><tbody>' +
        data.top_errors
          .map(
            (c) =>
              '<tr class="clickable" data-cluster="' +
              esc(c.fingerprint) +
              '"><td>' +
              esc(c.count) +
              "</td><td>" +
              badge(c.level) +
              "</td><td class=\"mono truncate\">" +
              esc(c.template) +
              "</td><td>" +
              esc(c.service) +
              "</td></tr>"
          )
          .join("") +
        "</tbody></table>"
      : empty("没有错误日志模板");

    setTimeout(() => {
      document.querySelectorAll("[data-cluster]").forEach((row) => {
        row.addEventListener("click", () => {
          $("log-text").value = "";
          drillIntoCluster(row.getAttribute("data-cluster"));
        });
      });
    }, 0);

    const events = data.recent_events || [];
    $("overview-events").innerHTML = events.length
      ? events
          .map(
            (e) =>
              '<div class="log-line"><span class="meta">' +
              esc(e.time || "") +
              "</span>" +
              badge(e.severity) +
              " <strong>" +
              esc(e.title) +
              '</strong> <span class="muted">' +
              esc(e.service || "") +
              " " +
              esc(e.node || "") +
              "</span></div>"
          )
          .join("")
      : empty("暂无事件");
  });

  async function loadInspection() {
    const data = await api("/api/inspection");
    const rows = (data.checks || [])
      .map(
        (c) =>
          '<tr><td class="mono">' +
          esc(c.id) +
          "</td><td>" +
          '<span class="badge ' +
          esc(c.status === "fail" ? "error" : c.status === "warn" ? "warn" : "ok") +
          '">' +
          esc(c.status) +
          "</span></td><td>" +
          esc(c.name) +
          "</td><td>" +
          esc(c.detail) +
          "</td></tr>"
      )
      .join("");
    $("inspection-result").innerHTML =
      '<p class="muted">ok=' +
      data.summary.ok +
      " warn=" +
      data.summary.warn +
      " fail=" +
      data.summary.fail +
      "</p><table><thead><tr><th>ID</th><th>状态</th><th>检查项</th><th>详情</th></tr></thead><tbody>" +
      rows +
      "</tbody></table>";
  }

  // ------------------------------------------------------------ 调用链
  async function searchTraces() {
    const query = $("trace-query").value.trim();
    const filter = $("trace-filter").value;
    const minMs = Number($("trace-min-duration").value || 0);
    if (/^[0-9a-f]{16,32}$/i.test(query)) {
      openTrace(query);
      return;
    }
    const params = new URLSearchParams();
    params.set("limit", "100");
    // 统一搜索：trace_id / request_id / task_id / 服务名都交给服务端做子串匹配。
    if (query) params.set("q", query);
    if (filter === "error") params.set("error", "1");
    if (minMs > 0) params.set("min_duration_ns", String(Math.floor(minMs * 1e6)));
    const data = await api("/api/traces?" + params.toString());
    $("trace-list").innerHTML =
      '<h3>最近调用链 (' + data.count + ")</h3>" + traceTable(data.traces, openTrace);
  }

  async function openTrace(traceId) {
    state.selectedTrace = traceId;
    location.hash = "traces";
    const detail = await api("/api/traces/" + encodeURIComponent(traceId));
    const summary = detail.summary || {};
    const logs = detail.logs || [];
    const roots = detail.roots || [];
    const critical = detail.critical_path || [];

    const summaryCards = cards([
      { label: "耗时", value: ms(summary.duration_ns) },
      { label: "Span", value: summary.span_count },
      { label: "服务", value: summary.service_count },
      { label: "深度", value: summary.depth },
      { label: "状态", value: summary.has_error ? "错误" : "正常", hint: summary.error_code || "" },
    ]);

    $("trace-detail").innerHTML =
      '<h3 class="mono">' +
      esc(detail.trace_id) +
      "</h3>" +
      '<p class="muted">request_id=' +
      esc(detail.request_id || "-") +
      " task_id=" +
      esc(detail.task_id || "-") +
      "</p>" +
      '<div class="cards">' +
      summaryCards +
      "</div>" +
      (detail.first_error
        ? '<div class="panel" style="border-color:#663333"><h3>首个错误</h3><div class="mono">' +
          esc(detail.first_error.service) +
          " / " +
          esc(detail.first_error.name) +
          " / " +
          esc(detail.first_error.error_code || detail.first_error.status || "") +
          "</div><p>" +
          esc(detail.first_error.file || "") +
          (detail.first_error.line ? ":" + detail.first_error.line : "") +
          "</p></div>"
        : "") +
      '<div class="panel"><h3>Span 树</h3>' +
      renderTree(roots) +
      "</div>" +
      '<div class="panel"><h3>时间瀑布图</h3>' +
      renderWaterfall(roots, summary.start_ns, summary.duration_ns) +
      "</div>" +
      '<div class="panel"><h3>关键路径</h3><div class="crit-path">' +
      critical
        .map(
          (s) =>
            '<div class="step">' +
            esc(s.service) +
            " · " +
            esc(s.name) +
            " · " +
            spanMs(s) +
            (s.error_code ? ' · <span class="error-text">' + esc(s.error_code) + "</span>" : "") +
            "</div>"
        )
        .join("") +
      "</div></div>" +
      '<div class="panel"><h3>关联日志 (' +
      logs.length +
      ")</h3>" +
      renderLogLines(logs) +
      "</div>";
  }

  function renderTree(nodes) {
    if (!nodes || !nodes.length) return empty("没有 Span");
    function walk(list) {
      return (
        '<ul class="tree">' +
        list
          .map((node) => {
            const bad = node.status === "error" || node.error_code;
            return (
              '<li><div class="node' +
              (bad ? " bad" : "") +
              '"><span class="name">' +
              esc(node.service || "-") +
              " · " +
              esc(node.name || "-") +
              '</span><span class="meta">' +
              spanMs(node) +
              (node.kind ? " · " + esc(node.kind) : "") +
              (node.node ? " · " + esc(node.node) : "") +
              (node.error_code ? " · " + esc(node.error_code) : "") +
              '</span><span class="meta mono">' +
              esc((node.span_id || "").slice(0, 10)) +
              "</span></div>" +
              (node.children && node.children.length ? walk(node.children) : "") +
              "</li>"
            );
          })
          .join("") +
        "</ul>"
      );
    }
    return walk(nodes);
  }

  function renderWaterfall(roots, traceStartNs, traceDurationNs) {
    if (!roots || !roots.length) return empty("没有 Span");
    const rows = [];
    function walk(nodes, depth) {
      nodes.forEach((node) => {
        rows.push({ span: node, depth });
        if (node.children && node.children.length) walk(node.children, depth + 1);
      });
    }
    walk(roots, 0);
    if (!rows.length) return empty("没有 Span");

    let start = Number(traceStartNs || 0);
    let end = start;
    rows.forEach((row) => {
      const spanStart = Number(row.span.start_ns || 0);
      const spanEnd = Number(row.span.end_ns || 0);
      if (!start || spanStart < start) start = spanStart;
      if (spanEnd > end) end = spanEnd;
    });
    if (!start) start = Math.min.apply(null, rows.map((row) => Number(row.span.start_ns || 0)));
    let total = Number(traceDurationNs || 0);
    if (!total) total = Math.max(1, end - start);
    if (!total) total = 1;

    return '<div class="waterfall">' + rows
      .map((row) => {
        const span = row.span;
        const spanStart = Number(span.start_ns || start);
        const spanEnd = Number(span.end_ns || 0);
        let duration = Number(span.duration_ns || 0);
        if (duration <= 0) {
          // 未结束的 span 延伸到当前 trace 末尾；已结束但耗时不足 1ns 的按 0 处理。
          duration = spanEnd > spanStart ? spanEnd - spanStart
                                          : Math.max(0, end - spanStart);
        }
        const left = Math.max(0, Math.min(100, ((spanStart - start) / total) * 100));
        const width = Math.max(0.3, Math.min(100 - left, (duration / total) * 100));
        const bad = span.status === "error" || span.error_code;
        return (
          '<div class="waterfall-row"><span class="waterfall-label" style="padding-left:' +
          row.depth * 12 +
          'px" title="' +
          esc(span.span_id || "") +
          '">' +
          esc(span.service || "-") +
          " · " +
          esc(span.name || "-") +
          '</span><span class="waterfall-track"><span class="waterfall-bar' +
          (bad ? " bad" : "") +
          '" style="left:' +
          left.toFixed(3) +
          "%;width:" +
          width.toFixed(3) +
          '%"></span></span><span class="waterfall-time">' +
          ms(duration) +
          (spanEnd > 0 ? "" : "（进行中）") +
          "</span></div>"
        );
      })
      .join("") + "</div>";
  }

  function renderLogLines(logs) {
    if (!logs || !logs.length) return empty("暂无日志");
    return logs
      .map(
        (log) =>
          '<div class="log-line ' +
          (log.level === "error" || log.level === "fatal" ? "error" : "") +
          '"><span class="meta">' +
          esc(log.time || "") +
          "</span>" +
          badge(log.level) +
          ' <span class="meta">' +
          esc(log.service || "") +
          " " +
          esc(log.node || "") +
          '</span><span class="msg">' +
          esc(log.msg) +
          "</span></div>"
      )
      .join("");
  }

  // ------------------------------------------------------------ 服务拓扑
  function topologyLayout(edges) {
    const nodes = {};
    edges.forEach((edge) => {
      if (!nodes[edge.caller]) nodes[edge.caller] = { name: edge.caller, level: 0 };
      if (!nodes[edge.callee]) nodes[edge.callee] = { name: edge.callee, level: 0 };
    });
    const names = Object.keys(nodes);
    // 最多做 N 轮松弛，遇到环时停在有限层级，避免死循环。
    for (let round = 0; round < names.length; ++round) {
      let changed = false;
      edges.forEach((edge) => {
        const caller = nodes[edge.caller];
        const callee = nodes[edge.callee];
        if (caller && callee && callee.level <= caller.level) {
          callee.level = caller.level + 1;
          changed = true;
        }
      });
      if (!changed) break;
    }
    const byLevel = {};
    names.forEach((name) => {
      const level = Math.min(nodes[name].level, Math.max(0, names.length - 1));
      (byLevel[level] = byLevel[level] || []).push(nodes[name]);
    });
    const levels = Object.keys(byLevel).map(Number).sort((a, b) => a - b);
    const maxLevel = levels.length ? levels[levels.length - 1] : 0;
    const width = 980;
    const height = 320;
    const colWidth = (width - 80) / Math.max(1, maxLevel + 1);
    levels.forEach((level) => {
      const list = byLevel[level];
      list.forEach((node, index) => {
        node.x = 40 + level * colWidth + colWidth / 2;
        node.y = 40 + ((index + 1) * (height - 80)) / (list.length + 1);
      });
    });
    return { nodes, width, height };
  }

  function topologySvg(edges) {
    if (!edges.length) return empty("暂无拓扑边（需要跨服务调用链）");
    const layout = topologyLayout(edges);
    const ns = "http://www.w3.org/2000/svg";
    let svg =
      '<svg class="topo-svg" viewBox="0 0 ' + layout.width + " " + layout.height +
      '" xmlns="' + ns + '"><defs><marker id="arrow" markerWidth="8" markerHeight="8" refX="7" refY="3" orient="auto"><path d="M0,0 L0,6 L7,3 z" fill="#8494a8"/></marker></defs>';
    edges.forEach((edge) => {
      const a = layout.nodes[edge.caller];
      const b = layout.nodes[edge.callee];
      if (!a || !b) return;
      const bad = edge.error_count > 0;
      const cls = "topo-edge" + (edge.confidence === "weak" ? " weak" : "") + (bad ? " bad" : "");
      const strokeWidth = 1 + Math.min(4, Math.log((edge.count || 1) + 1));
      svg +=
        '<line class="' + cls + '" x1="' + (a.x + 76) + '" y1="' + a.y +
        '" x2="' + (b.x - 82) + '" y2="' + b.y + '" stroke-width="' +
        strokeWidth.toFixed(2) + '" marker-end="url(#arrow)"><title>' +
        esc(edge.caller + " → " + edge.callee + " " + edge.method + " ×" + edge.count) +
        "</title></line>";
    });
    Object.keys(layout.nodes).forEach((name) => {
      const node = layout.nodes[name];
      svg +=
        '<rect class="topo-node" x="' + (node.x - 76) + '" y="' + (node.y - 16) +
        '" width="152" height="32" rx="6"/><text class="topo-label" x="' + node.x +
        '" y="' + (node.y + 4) + '">' + esc(name) + "</text>";
    });
    return svg + "</svg>";
  }

  async function loadTopology() {
    const data = await api("/api/topology?limit=1000");
    const edges = data.edges || [];
    $("topology-graph").innerHTML = topologySvg(edges);
    $("topology-result").innerHTML = edges.length
      ? '<table><thead><tr><th>调用方</th><th>被调方</th><th>方法</th><th>次数</th><th>错误</th><th>平均</th><th>最大</th><th>置信度</th></tr></thead><tbody>' +
        edges
          .map(
            (edge) =>
              "<tr><td><strong>" + esc(edge.caller) + "</strong></td><td><strong>" +
              esc(edge.callee) + '</strong></td><td class="mono">' + esc(edge.method) +
              "</td><td>" + esc(edge.count) + '</td><td class="' +
              (edge.error_count ? "error-text" : "") + '">' + esc(edge.error_count) +
              "</td><td>" + ms(edge.avg_duration_ns) + "</td><td>" +
              ms(edge.max_duration_ns) + "</td><td>" + esc(edge.confidence) +
              "</td></tr>"
          )
          .join("") + "</tbody></table>"
      : empty("暂无拓扑边（需要跨服务调用链）");
  }

  // ------------------------------------------------------------ 日志
  function logQuery() {
    const params = new URLSearchParams();
    const add = (key, id) => {
      const value = $(id).value.trim();
      if (value) params.set(key, value);
    };
    add("trace_id", "log-trace");
    add("request_id", "log-request");
    add("task_id", "log-task");
    add("service", "log-service");
    add("node", "log-node");
    add("level", "log-level");
    add("text", "log-text");
    const windowSeconds = Number($("log-window").value || 0);
    if (windowSeconds > 0) {
      params.set("since_ns", String((Date.now() - windowSeconds * 1000) * 1e6));
    }
    params.set("limit", "500");
    if ($("log-errors").checked) params.set("errors_only", "1");
    return params;
  }

  async function loadLogs() {
    const data = await api("/api/logs?" + logQuery().toString());
    const logs = data.logs || [];
    $("log-result").innerHTML =
      '<p class="muted">共 ' +
      logs.length +
      " 条（最多返回 500 条；错误日志不做采样）</p>" +
      (logs.length
        ? '<table><thead><tr><th>时间</th><th>级别</th><th>服务/节点</th><th>正文</th><th>Trace</th><th>指纹</th></tr></thead><tbody>' +
          logs
            .map(
              (log) =>
                '<tr><td class="mono">' +
                esc(log.time || "") +
                "</td><td>" +
                badge(log.level) +
                '</td><td class="mono">' +
                esc(log.service || "-") +
                "<br/>" +
                esc(log.node || "-") +
                '</td><td class="mono">' +
                esc(log.msg) +
                (log.file ? '<div class="muted">' + esc(log.file) + ":" + esc(log.line) + "</div>" : "") +
                '</td><td class="mono">' +
                (log.trace_id
                  ? '<a href="#traces" data-trace="' + esc(log.trace_id) + '">' + esc(log.trace_id.slice(0, 12)) + "…</a>"
                  : "") +
                '</td><td class="mono">' +
                esc(log.fingerprint || "") +
                "</td></tr>"
            )
            .join("") +
          "</tbody></table>"
        : empty("没有匹配的日志"));
    bindTraceLinks();
  }

  async function loadLogsByFingerprint(fingerprint) {
    const data = await api("/api/logs?" + new URLSearchParams({ fingerprint, limit: "300" }).toString());
    const logs = data.logs || [];
    $("log-result").innerHTML =
      '<p class="muted">指纹 ' + esc(fingerprint) + "，共 " + logs.length +
      ' 条 <button id="log-fingerprint-clear">清除指纹过滤</button></p>' +
      renderLogLines(logs);
    const clear = $("log-fingerprint-clear");
    if (clear) clear.addEventListener("click", () => loadLogs());
    bindTraceLinks();
  }

  function drillIntoCluster(fingerprint) {
    if (!fingerprint) return;
    state.pendingLogFingerprint = fingerprint;
    if (state.view === "logs") {
      // 当前已在日志页：hash 不会变化，直接查询即可。
      state.pendingLogFingerprint = "";
      loadLogsByFingerprint(fingerprint).catch((err) => toast(err.message));
    } else {
      location.hash = "logs";
    }
  }

  function bindTraceLinks() {
    document.querySelectorAll("a[data-trace]").forEach((a) => {
      a.addEventListener("click", (event) => {
        event.preventDefault();
        openTrace(a.getAttribute("data-trace")).catch((err) => toast(err.message));
      });
    });
  }

  // ------------------------------------------------------------ 聚类
  async function loadClusters() {
    const params = new URLSearchParams({ limit: "300" });
    const service = $("cluster-service").value.trim();
    if (service) params.set("service", service);
    if ($("cluster-errors").checked) params.set("errors_only", "1");
    const data = await api("/api/logs/clusters?" + params.toString());
    const clusters = data.clusters || [];
    $("cluster-result").innerHTML = clusters.length
      ? '<table><thead><tr><th>次数</th><th>级别</th><th>模板</th><th>服务</th><th>节点</th><th>最近样本</th><th>最近时间</th></tr></thead><tbody>' +
        clusters
          .map(
            (c) =>
              '<tr class="clickable" data-cluster="' +
              esc(c.fingerprint) +
              '"><td>' +
              esc(c.count) +
              "</td><td>" +
              badge(c.level) +
              '</td><td class="mono truncate">' +
              esc(c.template) +
              "</td><td>" +
              esc(c.service) +
              "</td><td>" +
              esc((c.nodes || []).join(", ")) +
              '</td><td class="mono truncate">' +
              esc(c.last_sample) +
              "</td><td>" +
              esc(c.last_time) +
              "</td></tr>"
          )
          .join("") +
        "</tbody></table>"
      : empty("暂无聚类（可能还没有日志）");
    document.querySelectorAll("[data-cluster]").forEach((row) => {
      row.addEventListener("click", () => drillIntoCluster(row.getAttribute("data-cluster")));
    });
  }

  // ------------------------------------------------------------ 服务 / 节点
  async function loadServices() {
    const data = await api("/api/services");
    $("services-result").innerHTML = data.length
      ? '<table><thead><tr><th>服务</th><th>实例</th><th>存活</th><th>事件</th><th>错误</th><th>节点</th><th>版本</th><th>最近</th></tr></thead><tbody>' +
        data
          .map(
            (s) =>
              '<tr><td><strong>' +
              esc(s.service) +
              '</strong></td><td class="mono">' +
              esc((s.instance_details || []).map((i) => i.instance).join(", ")) +
              "</td><td>" +
              esc(s.alive_instances) +
              "</td><td>" +
              esc(s.event_count) +
              '</td><td class="' +
              (s.error_count ? "error-text" : "") +
              '">' +
              esc(s.error_count) +
              "</td><td>" +
              esc((s.nodes || []).join(", ")) +
              "</td><td>" +
              esc((s.versions || []).join(", ")) +
              "</td><td>" +
              esc(s.last_seen) +
              "</td></tr>"
          )
          .join("") +
        "</tbody></table>"
      : empty("暂未发现服务");
  }

  async function loadNodes() {
    const nodes = await api("/api/nodes");
    $("nodes-result").innerHTML = nodes.length
      ? nodes
          .map(
            (n) =>
              '<div class="panel"><h3>' +
              esc(n.name) +
              ' <span class="muted">' +
              esc(n.os) +
              " " +
              esc(n.arch) +
              " · kernel " +
              esc(n.kernel) +
              " · load " +
              Number(n.load1 || 0).toFixed(2) +
              " · 内存 " +
              Math.round((n.mem_total_kb || 0) / 1024) +
              " MB</span></h3>" +
              '<table><thead><tr><th>PID</th><th>进程</th><th>状态</th><th>RSS</th><th>线程</th><th>命令行</th></tr></thead><tbody>' +
              (n.processes || [])
                .map(
                  (p) =>
                    "<tr><td>" +
                    esc(p.pid) +
                    '</td><td class="mono">' +
                    esc(p.name) +
                    "</td><td>" +
                    esc(p.state) +
                    "</td><td>" +
                    Math.round((p.rss_kb || 0) / 1024) +
                    " MB</td><td>" +
                    esc(p.threads) +
                    '</td><td class="mono truncate">' +
                    esc(p.cmdline) +
                    "</td></tr>"
                )
                .join("") +
              "</tbody></table></div>"
          )
          .join("")
      : empty("暂无节点（检查 Agent 是否上报）");
  }

  // ------------------------------------------------------------ 事件
  async function loadEvents() {
    const params = new URLSearchParams({ limit: "300" });
    ["status", "severity", "type"].forEach((key) => {
      const value = $("event-" + key).value;
      if (value) params.set(key, value);
    });
    const service = $("event-service").value.trim();
    if (service) params.set("service", service);
    const data = await api("/api/events?" + params.toString());
    $("events-result").innerHTML = data.events.length
      ? '<table><thead><tr><th>时间</th><th>级别</th><th>类型</th><th>标题</th><th>详情</th><th>对象</th><th>状态</th><th>操作</th></tr></thead><tbody>' +
        data.events
          .map(
            (e) =>
              '<tr><td class="mono">' +
              esc(e.time || "") +
              "</td><td>" +
              badge(e.severity) +
              "</td><td>" +
              esc(e.type) +
              "</td><td>" +
              esc(e.title) +
              '</td><td class="mono truncate">' +
              esc(e.detail) +
              "</td><td>" +
              esc(e.service || "") +
              " " +
              esc(e.node || "") +
              "</td><td>" +
              esc(e.status) +
              "</td><td>" +
              (e.status === "open"
                ? '<button data-ack="' + esc(e.id) + '">确认</button> '
                : "") +
              (e.status !== "resolved"
                ? '<button data-resolve="' + esc(e.id) + '">解决</button>'
                : "") +
              "</td></tr>"
          )
          .join("") +
        "</tbody></table>"
      : empty("暂无事件");
    const bindAction = (attribute, action, message) => {
      document.querySelectorAll("[" + attribute + "]").forEach((button) => {
        button.addEventListener("click", async () => {
          try {
            await api(
              "/api/events/" + encodeURIComponent(button.getAttribute(attribute)) + "/" + action,
              { method: "POST" }
            );
            toast(message);
            loadEvents();
          } catch (err) {
            toast(err.message);
          }
        });
      });
    };
    bindAction("data-ack", "ack", "已确认");
    bindAction("data-resolve", "resolve", "已解决");
  }

  // ------------------------------------------------------------ 崩溃
  async function loadCrashes() {
    const data = await api("/api/crashes?limit=100");
    const crashes = data.crashes || [];
    $("crash-list").innerHTML = crashes.length
      ? '<h3>崩溃 (' + crashes.length + ')</h3>' + crashes
          .map(
            (c) =>
              '<div class="log-line clickable" data-crash="' +
              esc(c.id) +
              '"><span class="meta">' +
              esc(c.last || c.time || "") +
              "</span>" +
              badge("critical") +
              " <strong>" +
              esc(c.service || "-") +
              "</strong> " +
              esc(c.signal) +
              ' <span class="muted">×' +
              esc(c.count) +
              " " +
              esc(c.node || "") +
              "</span></div>"
          )
          .join("")
      : empty("暂无崩溃记录");
    document.querySelectorAll("[data-crash]").forEach((row) => {
      row.addEventListener("click", () => openCrash(row.getAttribute("data-crash")).catch((err) => toast(err.message)));
    });
  }

  async function openCrash(id) {
    const crash = await api("/api/crashes/" + encodeURIComponent(id));
    const frames = (crash.frames || [])
      .map(
        (f, index) =>
          '<div class="log-line"><span class="meta">#' +
          index +
          "</span>" +
          esc(f.function || f.address || "?") +
          ' <span class="muted">' +
          esc(f.file || "") +
          (f.line ? ":" + f.line : "") +
          "</span></div>"
      )
      .join("");
    $("crash-detail").innerHTML =
      "<h3 class=\"mono\">" +
      esc(crash.id) +
      "</h3><p>" +
      esc(crash.service) +
      " · " +
      esc(crash.node) +
      " · " +
      esc(crash.signal) +
      " · build_id=" +
      esc(crash.build_id || "-") +
      "</p><p class=\"muted\">二进制: " +
      esc(crash.binary || "-") +
      " · pid=" +
      esc(crash.pid) +
      " · 次数=" +
      esc(crash.count) +
      "</p><pre class=\"code\">" +
      esc(crash.reason || crash.report || "") +
      "</pre><div class=\"panel\"><h3>栈帧</h3>" +
      (frames || empty("没有栈帧")) +
      "</div>";
  }

  // ------------------------------------------------------------ 剖析
  async function loadProfiles() {
    const data = await api("/api/profiles?limit=50");
    const profiles = data.profiles || [];
    $("profiles-result").innerHTML = profiles.length
      ? profiles
          .map((p) => {
            const top = p.top_self || [];
            const max = Math.max.apply(null, top.map((t) => t.samples).concat([1]));
            return (
              '<div class="panel"><h3>' +
              esc(p.service || "-") +
              " · " +
              esc(p.kind) +
              " · " +
              esc(p.node || "") +
              ' <span class="muted">样本 ' +
              esc(p.total_samples) +
              " · " +
              esc(p.time || "") +
              '</span> <button data-profile="' +
              esc(p.id) +
              '">火焰图</button></h3>' +
              top
                .map(
                  (t) =>
                    '<div class="bar-row"><span class="fn" title="' +
                    esc(t.function) +
                    '">' +
                    esc(t.function) +
                    '</span><span class="bar" style="width:' +
                    Math.max(2, Math.round((t.samples / max) * 320)) +
                    'px"></span><span class="num">' +
                    esc(t.samples) +
                    "</span></div>"
                )
                .join("") +
              '<div id="flame-' +
              esc(p.id) +
              '"></div></div>'
            );
          })
          .join("")
      : empty("暂无剖析数据（通过 /v1/profile 或控制通道上报）");
    document.querySelectorAll("[data-profile]").forEach((button) => {
      button.addEventListener("click", async () => {
        try {
          const detail = await api("/api/profiles/" + encodeURIComponent(button.getAttribute("data-profile")));
          $("flame-" + detail.id).innerHTML =
            '<div class="panel"><h3>火焰图（样本数）</h3>' + flameTree(detail.flamegraph) + "</div>";
        } catch (err) {
          toast(err.message);
        }
      });
    });
  }

  function flameTree(node) {
    if (!node) return "";
    const children = (node.children || []).map(flameTree).join("");
    return (
      '<details open><summary>' +
      esc(node.name) +
      " · " +
      esc(node.value) +
      "</summary>" +
      (children ? '<div style="padding-left:14px">' + children + "</div>" : "") +
      "</details>"
    );
  }

  // ------------------------------------------------------------ 指标
  async function loadMetrics() {
    const name = $("metric-name").value.trim();
    const params = new URLSearchParams({ limit: "2000" });
    if (name) params.set("name", name);
    const points = await api("/api/metrics?" + params.toString());
    if (!points.length) {
      $("metrics-result").innerHTML = empty("暂无指标");
      return;
    }
    const groups = {};
    points.forEach((p) => {
      const key = p.name + " " + JSON.stringify(p.labels || {});
      (groups[key] = groups[key] || []).push(p);
    });
    const blocks = Object.keys(groups).map((key) => {
      const series = groups[key];
      const values = series.map((p) => Number(p.value));
      const min = Math.min.apply(null, values);
      const max = Math.max.apply(null, values);
      return (
        '<div class="panel"><h3>' +
        esc(key) +
        '</h3><p class="muted">' +
        series.length +
        " 点 · min " +
        min.toFixed(3) +
        " · max " +
        max.toFixed(3) +
        " · 最新 " +
        values[values.length - 1].toFixed(3) +
        "</p>" +
        sparkline(values) +
        "</div>"
      );
    });
    $("metrics-result").innerHTML = blocks.join("");
  }

  function sparkline(values) {
    const width = 720;
    const height = 90;
    const max = Math.max.apply(null, values);
    const min = Math.min.apply(null, values);
    const span = max - min || 1;
    const step = values.length > 1 ? width / (values.length - 1) : width;
    const path = values
      .map((v, i) => (i ? "L" : "M") + (i * step).toFixed(1) + "," + (height - ((v - min) / span) * (height - 8) - 4).toFixed(1))
      .join(" ");
    return (
      '<svg viewBox="0 0 ' +
      width +
      " " +
      height +
      '" style="width:100%;height:90px;background:#10161f;border:1px solid var(--line);border-radius:6px">' +
      '<path d="' +
      path +
      '" fill="none" stroke="#4da3ff" stroke-width="1.5"/></svg>'
    );
  }

  // ------------------------------------------------------------ SLO
  async function loadSlos() {
    const data = await api("/api/slos");
    const slos = data.slos || [];
    $("slos-result").innerHTML = slos.length
      ? '<table><thead><tr><th>名称</th><th>服务</th><th>类型</th><th>目标</th><th>状态</th><th>SLI</th><th>预算剩余</th><th>燃烧率</th><th>样本</th><th>窗口</th><th></th></tr></thead><tbody>' +
        slos
          .map(
            (slo) =>
              "<tr><td><strong>" +
              esc(slo.name) +
              '</strong><div class="muted mono">' +
              esc(slo.id) +
              "</div></td><td>" +
              esc(slo.service || "全部") +
              "</td><td>" +
              esc(slo.type === "latency" ? "延迟" : "可用性") +
              "</td><td>" +
              pct(slo.target) +
              '</td><td class="slo-' +
              esc(slo.status) +
              '"><strong>' +
              esc(slo.status) +
              "</strong></td><td>" +
              pct(slo.sli) +
              "</td><td>" +
              pct(slo.budget_remaining) +
              "</td><td>" +
              Number(slo.burn_rate || 0).toFixed(2) +
              "</td><td>" +
              esc(slo.total) +
              ' <span class="muted">/ 坏 ' +
              esc(slo.bad) +
              "</span></td><td>" +
              esc(slo.window_seconds) +
              " s</td><td>" +
              '<button data-slo-delete="' +
              esc(slo.id) +
              '">删除</button></td></tr>'
          )
          .join("") +
        "</tbody></table>"
      : empty("还没有 SLO，使用下方表单创建一个。");
    document.querySelectorAll("[data-slo-delete]").forEach((button) => {
      button.addEventListener("click", async () => {
        try {
          await api("/api/slos/" + encodeURIComponent(button.getAttribute("data-slo-delete")), { method: "DELETE" });
          toast("SLO 已删除");
          loadSlos();
        } catch (err) {
          toast(err.message);
        }
      });
    });
  }

  async function saveSlo() {
    const type = $("slo-type").value;
    const thresholdMs = Number($("slo-latency").value || 0);
    const body = {
      id: $("slo-id").value.trim(),
      name: $("slo-name").value.trim(),
      service: $("slo-service").value.trim(),
      type,
      target: Number($("slo-target").value || 0.99),
      window_seconds: Number($("slo-window").value || 3600),
      latency_threshold_ns: type === "latency" ? Math.round(thresholdMs * 1e6) : 0,
      description: $("slo-desc").value.trim(),
    };
    try {
      const data = await api("/api/slos", { json: body });
      $("slo-msg").textContent = "已保存 " + ((data.slo && data.slo.id) || body.id);
      loadSlos();
    } catch (err) {
      $("slo-msg").textContent = err.message;
    }
  }

  // ------------------------------------------------------------ 诊断 / 证据包 / 命令
  async function runDiagnosis() {
    const body = {
      trace_id: $("diag-trace").value.trim(),
      request_id: $("diag-request").value.trim(),
      service: $("diag-service").value.trim(),
      node: $("diag-node").value.trim(),
      pid: Number($("diag-pid").value || 0),
      collect_stack: $("diag-stack").checked,
    };
    $("diag-result").textContent = "诊断中…";
    const data = await api("/api/diagnose", { json: body });
    $("diag-result").textContent = JSON.stringify(data, null, 2);
  }

  async function createEvidence() {
    const hours = Number($("ev-window").value || 1);
    const body = {
      incident: $("ev-incident").value.trim() || "manual",
      service: $("ev-service").value.trim(),
      trace_id: $("ev-trace").value.trim(),
      request_id: $("ev-request").value.trim(),
      window_ns: Math.max(1, hours) * 3600 * 1e9,
    };
    $("ev-result").textContent = "生成中…";
    const data = await api("/api/evidence", { json: body });
    $("ev-result").textContent = JSON.stringify(data, null, 2);
  }

  async function setLogLevel() {
    const body = {
      level: $("ll-level").value,
      service: $("ll-service").value.trim(),
      node: $("ll-node").value.trim(),
      pid: Number($("ll-pid").value || 0),
      ttl_ms: Number($("ll-ttl").value || 300000),
    };
    const data = await api("/api/control/log-level", { json: body });
    $("ll-result").innerHTML = '<p class="ok-text">已下发：' + esc(JSON.stringify(data.commands)) + "</p>";
    loadCommands();
  }

  async function loadCommands() {
    const data = await api("/api/commands?limit=50");
    $("commands-result").innerHTML = data.commands.length
      ? '<table><thead><tr><th>ID</th><th>类型</th><th>目标</th><th>状态</th><th>结果</th></tr></thead><tbody>' +
        data.commands
          .map(
            (c) =>
              '<tr><td class="mono">' +
              esc(c.id) +
              "</td><td>" +
              esc(c.type) +
              "</td><td>" +
              esc(c.node || "") +
              " pid=" +
              esc(c.pid) +
              "</td><td>" +
              esc(c.status) +
              '</td><td class="mono truncate">' +
              esc(c.error || c.result || "") +
              "</td></tr>"
          )
          .join("") +
        "</tbody></table>"
      : empty("暂无命令");
  }

  // ------------------------------------------------------------ 规则
  async function loadRules() {
    const data = await api("/api/rules");
    const rules = data.rules || [];
    $("rules-result").innerHTML = rules.length
      ? '<table><thead><tr><th>ID</th><th>类型</th><th>对象</th><th>阈值</th><th>窗口</th><th>冷却</th><th>级别</th><th>启用</th><th></th></tr></thead><tbody>' +
        rules
          .map(
            (r) =>
              '<tr><td class="mono">' +
              esc(r.id) +
              "</td><td>" +
              esc(r.type) +
              "</td><td>" +
              esc(r.service || r.node || r.metric || "全局") +
              "</td><td>" +
              esc(r.threshold) +
              " " +
              esc(r.op || "") +
              "</td><td>" +
              Math.round((r.window_ns || 0) / 1e9) +
              " s</td><td>" +
              Math.round((r.cooldown_ns || 0) / 1e9) +
              " s</td><td>" +
              badge(r.severity) +
              "</td><td>" +
              (r.enabled ? "是" : "否") +
              "</td><td>" +
              (String(r.id).indexOf("default-") === 0
                ? '<span class="muted">默认</span>'
                : '<button data-rule-delete="' + esc(r.id) + '">删除</button>') +
              "</td></tr>"
          )
          .join("") +
        "</tbody></table>"
      : empty("暂无规则");
    document.querySelectorAll("[data-rule-delete]").forEach((button) => {
      button.addEventListener("click", async () => {
        try {
          await api("/api/rules/" + encodeURIComponent(button.getAttribute("data-rule-delete")), { method: "DELETE" });
          toast("已删除");
          loadRules();
        } catch (err) {
          toast(err.message);
        }
      });
    });
  }

  async function saveRuleForm() {
    const body = {
      id: $("rf-id").value.trim(),
      name: $("rf-name").value.trim(),
      type: $("rf-type").value,
      service: $("rf-service").value.trim(),
      node: $("rf-node").value.trim(),
      metric: $("rf-metric").value.trim(),
      op: $("rf-op").value,
      threshold: Number($("rf-threshold").value || 0),
      window_ns: Math.max(1, Number($("rf-window").value || 60)) * 1e9,
      cooldown_ns: Math.max(0, Number($("rf-cooldown").value || 300)) * 1e9,
      severity: $("rf-severity").value,
      enabled: $("rf-enabled").checked,
    };
    try {
      await api("/api/rules", { json: body });
      $("rf-msg").textContent = "已保存 " + (body.id || body.name);
      loadRules();
    } catch (err) {
      $("rf-msg").textContent = err.message;
    }
  }

  async function saveRule() {
    let body;
    try {
      body = JSON.parse($("rule-json").value);
    } catch (err) {
      $("rule-msg").textContent = "JSON 解析失败: " + err.message;
      return;
    }
    try {
      await api("/api/rules", { json: body });
      $("rule-msg").textContent = "已保存";
      loadRules();
    } catch (err) {
      $("rule-msg").textContent = err.message;
    }
  }

  // ------------------------------------------------------------ 路由与初始化
  function showView(name) {
    if (!document.getElementById("view-" + name)) name = "overview";
    state.view = name;
    document.querySelectorAll(".view").forEach((v) => v.classList.remove("active"));
    document.getElementById("view-" + name).classList.add("active");
    document.querySelectorAll("#nav button").forEach((b) => {
      b.classList.toggle("active", b.getAttribute("data-view") === name);
    });
    if (state.overviewTimer) {
      clearInterval(state.overviewTimer);
      state.overviewTimer = null;
    }
    const loaders = {
      overview: () => {
        loadOverview();
        if ($("overview-auto").checked) state.overviewTimer = setInterval(loadOverview, 5000);
      },
      traces: searchTraces,
      topology: loadTopology,
      logs: () => {
        const fingerprint = state.pendingLogFingerprint;
        state.pendingLogFingerprint = "";
        return fingerprint ? loadLogsByFingerprint(fingerprint) : loadLogs();
      },
      clusters: loadClusters,
      services: loadServices,
      nodes: loadNodes,
      events: loadEvents,
      crashes: loadCrashes,
      profiles: loadProfiles,
      metrics: loadMetrics,
      slos: loadSlos,
      diagnose: loadCommands,
      rules: loadRules,
    };
    if (loaders[name]) safeRun(loaders[name])();
  }

  function init() {
    $("token").value = state.token;
    $("save-token").addEventListener("click", () => {
      state.token = $("token").value.trim();
      localStorage.setItem("vmon-token", state.token);
      toast("令牌已保存");
      showView(state.view);
    });

    $("nav").addEventListener("click", (event) => {
      const button = event.target.closest("button[data-view]");
      if (button) location.hash = button.getAttribute("data-view");
    });
    window.addEventListener("hashchange", () => showView(location.hash.slice(1) || "overview"));

    $("overview-refresh").addEventListener("click", safeRun(loadOverview));
    $("inspection-run").addEventListener("click", safeRun(loadInspection));
    $("trace-search").addEventListener("click", safeRun(searchTraces));
    $("topology-refresh").addEventListener("click", safeRun(loadTopology));
    $("trace-query").addEventListener("keydown", (e) => {
      if (e.key === "Enter") safeRun(searchTraces)();
    });
    $("log-search").addEventListener("click", safeRun(loadLogs));
    $("cluster-search").addEventListener("click", safeRun(loadClusters));
    $("services-refresh").addEventListener("click", safeRun(loadServices));
    $("nodes-refresh").addEventListener("click", safeRun(loadNodes));
    $("events-refresh").addEventListener("click", safeRun(loadEvents));
    $("crashes-refresh").addEventListener("click", safeRun(loadCrashes));
    $("profiles-refresh").addEventListener("click", safeRun(loadProfiles));
    $("metrics-search").addEventListener("click", safeRun(loadMetrics));
    $("slos-refresh").addEventListener("click", safeRun(loadSlos));
    $("slo-save").addEventListener("click", saveSlo);
    $("diag-run").addEventListener("click", safeRun(runDiagnosis));
    $("ev-run").addEventListener("click", safeRun(createEvidence));
    $("ll-run").addEventListener("click", safeRun(setLogLevel));
    $("commands-refresh").addEventListener("click", safeRun(loadCommands));
    $("rules-refresh").addEventListener("click", safeRun(loadRules));
    $("rule-save").addEventListener("click", saveRule);
    $("rf-save").addEventListener("click", saveRuleForm);

    showView(location.hash.slice(1) || "overview");
  }

  document.addEventListener("DOMContentLoaded", init);
})();
