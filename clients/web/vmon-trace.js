/*!
 * vmon-trace.js — 浏览器端 trace 起点与传播（无依赖，UMD）。
 *
 * 用法：
 *   <script src="/vmon-trace.js"></script>
 *   <script>
 *     const tracedFetch = vmonTrace.instrumentFetch(window.fetch, {
 *       service: "web-console",
 *       requestId: "REQ-1",
 *       report: (event) => navigator.sendBeacon("/bff/vmon/events", JSON.stringify(event))
 *     });
 *     tracedFetch("/api/orders");
 *   </script>
 *
 * 重要：浏览器不要直接持有 vmon-server 的写 token；事件上报给 BFF/网关，
 * 由 BFF 统一鉴权、采样并转发到 Agent 或 Server。
 */
(function (root, factory) {
  "use strict";
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
  } else {
    root.vmonTrace = factory();
  }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  var HEX = "0123456789abcdef";

  function randomHex(size) {
    var bytes = new Uint8Array(size);
    var cryptoObj = typeof crypto !== "undefined" ? crypto : null;
    if (cryptoObj && cryptoObj.getRandomValues) {
      cryptoObj.getRandomValues(bytes);
    } else {
      for (var i = 0; i < size; ++i) bytes[i] = Math.floor(Math.random() * 256);
    }
    var out = "";
    for (var j = 0; j < bytes.length; ++j) {
      out += HEX[(bytes[j] >> 4) & 0xf] + HEX[bytes[j] & 0xf];
    }
    return out;
  }

  function isHex(value) {
    return typeof value === "string" && /^[0-9a-fA-F]+$/.test(value);
  }

  function newContext(parent) {
    if (parent && parent.traceId) {
      return {
        traceId: parent.traceId,
        spanId: randomHex(8),
        sampled: parent.sampled !== false
      };
    }
    return { traceId: randomHex(16), spanId: randomHex(8), sampled: true };
  }

  function parseTraceparent(value) {
    if (typeof value !== "string" || value.length !== 55) return null;
    if (value[2] !== "-" || value[35] !== "-" || value[52] !== "-") return null;
    if (value.slice(0, 2) !== "00") return null;
    var traceId = value.slice(3, 35).toLowerCase();
    var spanId = value.slice(36, 52).toLowerCase();
    var flags = value.slice(53, 55);
    if (!isHex(traceId) || !isHex(spanId) || !isHex(flags)) return null;
    if (/^0+$/.test(traceId) || /^0+$/.test(spanId)) return null;
    return {
      traceId: traceId,
      spanId: spanId,
      sampled: parseInt(flags, 16) !== 0
    };
  }

  function makeTraceparent(context, sampled) {
    var flag = (sampled === undefined ? context.sampled !== false : sampled)
      ? "01"
      : "00";
    return "00-" + context.traceId + "-" + context.spanId + "-" + flag;
  }

  function headers(context, ids) {
    ids = ids || {};
    var out = { traceparent: makeTraceparent(context) };
    if (ids.requestId) out["x-request-id"] = ids.requestId;
    if (ids.taskId) out["x-task-id"] = ids.taskId;
    return out;
  }

  function nowNs() {
    if (typeof performance !== "undefined" && performance.now) {
      return Math.round(performance.now() * 1e6);
    }
    return Date.now() * 1e6;
  }

  function mergeHeaders(source, extra) {
    var out = {};
    var key;
    if (source && typeof source.forEach === "function" && !Array.isArray(source)) {
      // Headers 对象
      source.forEach(function (value, name) {
        out[name] = value;
      });
    } else {
      for (key in source || {}) {
        if (Object.prototype.hasOwnProperty.call(source, key)) {
          out[key] = source[key];
        }
      }
    }
    for (key in extra) {
      if (Object.prototype.hasOwnProperty.call(extra, key)) out[key] = extra[key];
    }
    return out;
  }

  function reportEvent(report, event) {
    if (!report) return;
    try {
      if (typeof report === "function") {
        report(event);
      } else if (typeof report === "string") {
        var body = JSON.stringify(event);
        if (typeof navigator !== "undefined" && navigator.sendBeacon) {
          navigator.sendBeacon(report, body);
        } else if (typeof fetch === "function") {
          fetch(report, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: body,
            keepalive: true
          });
        }
      }
    } catch (error) {
      // fail-open：上报失败绝不影响业务请求
    }
  }

  /**
   * 包装 fetch：自动注入 traceparent / request-id / task-id，并上报一次调用事件。
   * options:
   *   service     服务名（默认 web-client）
   *   requestId   业务请求 ID（可选）
   *   taskId      任务 ID（可选）
   *   name        事件名（默认请求 URL）
   *   traceparent 显式父上下文（可选）
   *   sample      采样率 0..1（默认 1）
   *   report      function(event) 或 BFF 上报 URL
   */
  function instrumentFetch(fetchImpl, options) {
    options = options || {};
    if (typeof fetchImpl !== "function") {
      throw new Error("vmonTrace.instrumentFetch 需要传入 fetch");
    }
    var sample = options.sample === undefined ? 1 : Number(options.sample);

    return function tracedFetch(input, init) {
      init = init || {};
      var sampled = Math.random() < sample;
      var parent = options.traceparent
        ? parseTraceparent(options.traceparent)
        : options.parent || null;
      var context = newContext(parent);
      context.sampled = sampled;
      var url = typeof input === "string" ? input : (input && input.url) || "";
      var name = options.name || (init.method || "GET") + " " + url;
      var extra = headers(context, {
        requestId: options.requestId,
        taskId: options.taskId
      });
      if (!sampled) {
        // 未命中采样仍透传业务 ID，但不注入 traceparent、不上报事件。
        delete extra.traceparent;
      }
      var merged = mergeHeaders(init.headers, extra);
      var requestInit = {};
      for (var key in init) {
        if (Object.prototype.hasOwnProperty.call(init, key)) requestInit[key] = init[key];
      }
      requestInit.headers = merged;

      var start = nowNs();
      var baseEvent = {
        service: options.service || "web-client",
        name: name,
        trace_id: context.traceId,
        span_id: context.spanId,
        parent_span_id: parent ? parent.spanId : "",
        request_id: options.requestId || "",
        task_id: options.taskId || "",
        kind: "client"
      };

      return Promise.resolve(fetchImpl(input, requestInit)).then(
        function (response) {
          var event = clone(baseEvent);
          event.duration_ns = Math.max(0, nowNs() - start);
          event.http_status = response && response.status ? response.status : 0;
          event.status = event.http_status >= 500 ? "error" : "ok";
          event.error_code = event.http_status >= 500
            ? "E_HTTP_" + event.http_status
            : "";
          if (sampled) reportEvent(options.report, event);
          return response;
        },
        function (error) {
          var event = clone(baseEvent);
          event.duration_ns = Math.max(0, nowNs() - start);
          event.status = "error";
          event.error_code = "E_FETCH";
          event.message = String((error && error.message) || error);
          if (sampled) reportEvent(options.report, event);
          throw error;
        }
      );
    };
  }

  function clone(value) {
    var out = {};
    for (var key in value) {
      if (Object.prototype.hasOwnProperty.call(value, key)) out[key] = value[key];
    }
    return out;
  }

  return {
    newContext: newContext,
    parseTraceparent: parseTraceparent,
    makeTraceparent: makeTraceparent,
    headers: headers,
    instrumentFetch: instrumentFetch
  };
});
