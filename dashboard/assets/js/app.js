function monitoringDashboard() {
  return {
    listenHost: window.location.hostname || "127.0.0.1",
    // Empty until the first /api/monitor snapshot arrives. We deliberately
    // do NOT fall back to a hardcoded port here — when the page is served
    // over 80/443 (e.g. behind a reverse proxy), window.location.port is ""
    // and inventing a port like "9090" would mislead the user into thinking
    // the dashboard is reachable on that port.
    listenPort: window.location.port || "",
    pollIntervalMs: 1000,
    lastUpdateText: "never",
    errorMessage: "",
    consecutiveFailures: 0,
    pollHandle: null,
    inFlight: false,
    lastSuccessTimestampMs: null,
    monitorApiPath: "/api/monitor",

    kpi: {
      worker_count: null,
      generator_status: null,
      task_queue_size: null,
      workers_waiting: null,
      uptime_seconds: null,
      avg_roundtrip_ms: null,
      failure_rate_pct: null,
    },

    workers: [],
    recentDisconnects: [],
    disconnectHistoryExpanded: true,
    table: null,

    init() {
      this.initWorkersTable();
      this.fetchSnapshot();
      this.pollHandle = window.setInterval(() => {
        this.fetchSnapshot();
      }, this.pollIntervalMs);

      window.addEventListener("beforeunload", () => {
        if (this.pollHandle !== null) {
          window.clearInterval(this.pollHandle);
          this.pollHandle = null;
        }
      });
    },

    initWorkersTable() {
      this.table = new Tabulator("#workers-table", {
        layout: "fitDataStretch",
        placeholder: "No worker sessions yet",
        // Stable row identity — Tabulator reconciles updates instead of
        // tearing the table down on every poll, which was causing rows to
        // flicker/vanish between /api/monitor ticks.
        index: "session_id",
        data: this.workers,
        rowFormatter: (row) => {
          const data = row.getData();
          const el = row.getElement();
          if (data && data.dispatcher_fresh === false) {
            el.classList.add("worker-row-stale");
          } else {
            el.classList.remove("worker-row-stale");
          }
        },
        columns: [
          {
            title: "Worker<br>Node",
            field: "worker_node_id",
            minWidth: 170,
            formatter: (cell) => {
              const raw = this.toStringValue(cell.getValue(), "unknown");
              const normalized = this.trimLeadingZerosHex(raw);
              const escaped = this.escapeHtml(normalized);
              const compact = normalized.length > 12
                ? `${normalized.slice(0, 6)}...${normalized.slice(-4)}`
                : normalized;
              return `<span title="${escaped}">${this.escapeHtml(compact)}</span>`;
            },
          },
          {
            title: "State",
            field: "worker_state",
            minWidth: 160,
            headerFilter: "list",
            headerFilterParams: {
              values: {
                "": "all",
                initializing: "initializing",
                waiting_for_task: "waiting_for_task",
                processing_task: "processing_task",
                active: "active",
                completing: "completing",
                terminated: "terminated",
                error_state: "error_state",
                unknown: "unknown",
              },
            },
            headerFilterFunc: (headerValue, rowValue) => {
              if (!headerValue) {
                return true;
              }
              return String(rowValue) === String(headerValue);
            },
            formatter: (cell) => {
              const state = this.toStringValue(cell.getValue(), "unknown");
              const badgeClass = this.stateClassFor(state);
              return `<span class="state-badge ${badgeClass}">${this.escapeHtml(state)}</span>`;
            },
          },
          {
            title: "Fresh",
            field: "dispatcher_fresh",
            hozAlign: "center",
            width: 100,
            headerFilter: "tickCross",
            headerFilterParams: {
              tristate: true,
              indeterminateValue: null,
            },
            headerFilterFunc: (headerValue, rowValue) => {
              if (headerValue === null || headerValue === "") {
                return true;
              }
              return Boolean(rowValue) === Boolean(headerValue);
            },
            formatter: (cell) => {
              const fresh = Boolean(cell.getValue());
              if (fresh) {
                return '<span class="fresh-indicator fresh-indicator-ok" title="fresh">✓</span>';
              }
              return '<span class="fresh-indicator fresh-indicator-stale" title="stale">⚠</span>';
            },
          },
          { title: "Sent", field: "tasks_sent", hozAlign: "right", width: 90 },
          { title: "Completed", field: "tasks_completed", hozAlign: "right", minWidth: 110 },
          { title: "Failed", field: "tasks_failed", hozAlign: "right", minWidth: 90 },
          {
            title: "Avg<br>Roundtrip",
            field: "avg_roundtrip_ms",
            hozAlign: "right",
            minWidth: 130,
            formatter: (cell) => {
              const value = this.toNumber(cell.getValue(), null);
              if (value === null) {
                return "--";
              }
              return `${value.toFixed(1)} ms`;
            },
          },
          {
            title: "Session",
            field: "session_duration_s",
            hozAlign: "right",
            width: 110,
            formatter: (cell) => this.formatMinutesSeconds(cell.getValue()),
          },
          {
            title: "Bytes<br>Sent",
            field: "bytes_sent",
            hozAlign: "right",
            minWidth: 115,
            formatter: (cell) => this.formatBytes(cell.getValue()),
          },
          {
            title: "Bytes<br>Received",
            field: "bytes_received",
            hozAlign: "right",
            minWidth: 135,
            formatter: (cell) => this.formatBytes(cell.getValue()),
          },
          { title: "Remote<br>Endpoint", field: "remote_endpoint", minWidth: 190, widthGrow: 2 },
        ],
      });
    },

    async fetchSnapshot() {
      if (this.inFlight) {
        return;
      }

      this.inFlight = true;
      try {
        const response = await fetch(this.monitorApiPath, {
          method: "GET",
          headers: {
            Accept: "application/json",
          },
          cache: "no-store",
        });

        // 503 with {"error":"no snapshot available"} is the rendezvous
        // server's expected response when no dispatcher has uploaded a
        // snapshot yet. Treat it as a benign "waiting" state instead of
        // surfacing it as an error to the user.
        if (response.status === 503) {
          this.consecutiveFailures = 0;
          this.errorMessage = "";
          this.lastUpdateText = "waiting for dispatcher snapshot…";
          return;
        }

        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }

        const payload = await response.json();
        const normalized = this.normalizeSnapshot(payload);
        this.applySnapshot(normalized);

        this.consecutiveFailures = 0;
        this.errorMessage = "";
      } catch (error) {
        this.consecutiveFailures += 1;
        const reason = error instanceof Error ? error.message : "unknown error";
        this.errorMessage = `Request failed (${this.consecutiveFailures}): ${reason}`;
      } finally {
        this.inFlight = false;
      }
    },

    normalizeSnapshot(payload) {
      const p = payload && typeof payload === "object" ? payload : {};
      const workersRaw = Array.isArray(p.workers) ? p.workers : [];

      const workers = workersRaw.map((row) => this.normalizeWorker(row));

      const completedWorkers = workers.filter((w) => w.tasks_completed > 0);
      const totalCompleted = workers.reduce((sum, w) => sum + w.tasks_completed, 0);
      const totalFailed = workers.reduce((sum, w) => sum + w.tasks_failed, 0);

      const avgRoundtripMs = completedWorkers.length > 0
        ? completedWorkers.reduce((sum, w) => sum + w.avg_roundtrip_ms, 0) / completedWorkers.length
        : null;

      const totalSettled = totalCompleted + totalFailed;
      const failureRatePct = totalSettled > 0
        ? (totalFailed / totalSettled) * 100
        : null;

      const snapshotTimestampMs = this.toNumber(p.snapshot_timestamp_ms, null);

      return {
        listen_host: this.toStringValue(p.listen_host, this.listenHost),
        listen_port: this.toNumber(p.listen_port, this.listenPort),
        snapshot_timestamp_ms: snapshotTimestampMs,
        generator_status: this.toStringValue(p.generator_status, "unknown"),
        worker_count: this.toNumber(p.worker_count, workers.length),
        task_queue_size: this.toNumber(p.task_queue_size, null),
        workers_waiting: this.toNumber(p.workers_waiting, 0),
        uptime_seconds: this.toNumber(p.uptime_seconds, null),
        avg_roundtrip_ms: avgRoundtripMs,
        failure_rate_pct: failureRatePct,
        workers,
        recent_disconnects: this.normalizeRecentDisconnects(p.recent_disconnects),
      };
    },

    normalizeRecentDisconnects(rows) {
      if (!Array.isArray(rows)) {
        return [];
      }
      const normalized = rows.map((row) => {
        const r = row && typeof row === "object" ? row : {};
        return {
          worker_node_id: this.trimLeadingZerosHex(this.toStringValue(r.worker_node_id, "unknown")),
          session_id: this.toNumber(r.session_id, 0),
          remote_endpoint: this.toStringValue(r.remote_endpoint, "unknown"),
          reason: this.toStringValue(r.reason, "unknown"),
          disconnected_ts_ms: this.toNumber(r.disconnected_ts_ms, 0),
        };
      });
      return normalized;
    },

    normalizeWorker(row) {
      const w = row && typeof row === "object" ? row : {};
      return {
        worker_node_id: this.toStringValue(w.worker_node_id, "unknown"),
        session_id: this.toStringValue(w.session_id, ""),
        remote_endpoint: this.toStringValue(w.remote_endpoint, ""),
        worker_state: this.toStringValue(w.worker_state, "unknown"),
        dispatcher_fresh: this.toBoolean(w.dispatcher_fresh, false),
        tasks_sent: this.toNumber(w.tasks_sent, 0),
        tasks_completed: this.toNumber(w.tasks_completed, 0),
        tasks_failed: this.toNumber(w.tasks_failed, 0),
        bytes_sent: this.toNumber(w.bytes_sent, 0),
        bytes_received: this.toNumber(w.bytes_received, 0),
        avg_roundtrip_ms: this.toNumber(w.avg_roundtrip_ms, 0),
        session_duration_s: this.toNumber(w.session_duration_s, 0),
        last_seen_dispatcher_ts_ms: this.toNumber(w.last_seen_dispatcher_ts_ms, 0),
      };
    },

    applySnapshot(snapshot) {
      this.listenHost = snapshot.listen_host;
      this.listenPort = snapshot.listen_port;

      this.kpi.worker_count = snapshot.worker_count;
      this.kpi.generator_status = this.formatGeneratorStatus(snapshot.generator_status);
      this.kpi.task_queue_size = snapshot.task_queue_size;
      this.kpi.workers_waiting = snapshot.workers_waiting;
      this.kpi.uptime_seconds = this.formatDurationSeconds(snapshot.uptime_seconds);
      this.kpi.avg_roundtrip_ms = snapshot.avg_roundtrip_ms === null
        ? null
        : `${snapshot.avg_roundtrip_ms.toFixed(1)} ms`;
      this.kpi.failure_rate_pct = snapshot.failure_rate_pct === null
        ? null
        : `${snapshot.failure_rate_pct.toFixed(1)}%`;

      this.workers = snapshot.workers;
      this.recentDisconnects = snapshot.recent_disconnects;
      if (this.table) {
        // updateOrAddData keeps existing rows in place (reconciled by the
        // configured index) and only mutates changed cells, then we remove
        // rows whose session_id is no longer in the snapshot. This avoids
        // the full teardown/rebuild that replaceData() performs, which was
        // making worker rows briefly disappear between polls.
        const keepIds = new Set(this.workers.map((w) => w.session_id));
        this.table.updateOrAddData(this.workers).then(() => {
          const toRemove = this.table
            .getRows()
            .filter((row) => !keepIds.has(row.getData().session_id))
            .map((row) => row.getData().session_id);
          if (toRemove.length > 0) {
            this.table.deleteRow(toRemove).catch(() => {});
          }
        }).catch(() => {});
      }

      this.lastSuccessTimestampMs = snapshot.snapshot_timestamp_ms ?? Date.now();
      this.lastUpdateText = this.formatTimestamp(this.lastSuccessTimestampMs);
    },

    toNumber(value, fallback) {
      if (value === null || value === undefined || value === "") {
        return fallback;
      }
      const parsed = Number(value);
      return Number.isFinite(parsed) ? parsed : fallback;
    },

    toBoolean(value, fallback) {
      if (typeof value === "boolean") {
        return value;
      }
      if (value === "true") {
        return true;
      }
      if (value === "false") {
        return false;
      }
      return fallback;
    },

    toStringValue(value, fallback) {
      if (value === null || value === undefined) {
        return fallback;
      }
      const text = String(value);
      return text.length > 0 ? text : fallback;
    },

    formatTimestamp(timestampMs) {
      if (!Number.isFinite(timestampMs) || timestampMs <= 0) {
        return "never";
      }
      return new Date(timestampMs).toLocaleTimeString();
    },

    formatTimeAgo(timestampMs) {
      if (!Number.isFinite(timestampMs) || timestampMs <= 0) {
        return "unknown";
      }
      const ageSec = Math.max(0, Math.floor((Date.now() - timestampMs) / 1000));
      if (ageSec < 60) {
        return `${ageSec}s ago`;
      }
      const mins = Math.floor(ageSec / 60);
      const rem = ageSec % 60;
      return `${mins}m ${rem}s ago`;
    },

    formatDurationSeconds(totalSeconds) {
      if (!Number.isFinite(totalSeconds) || totalSeconds < 0) {
        return null;
      }
      const seconds = Math.floor(totalSeconds);
      const hours = Math.floor(seconds / 3600);
      const minutes = Math.floor((seconds % 3600) / 60);
      const remaining = seconds % 60;
      return `${hours}h ${minutes}m ${remaining}s`;
    },

    formatMinutesSeconds(totalSeconds) {
      const seconds = this.toNumber(totalSeconds, null);
      if (seconds === null || seconds < 0) {
        return "--";
      }
      const s = Math.floor(seconds);
      const minutes = Math.floor(s / 60);
      const remaining = String(s % 60).padStart(2, "0");
      return `${minutes}:${remaining}`;
    },

    formatBytes(byteCount) {
      const bytes = this.toNumber(byteCount, null);
      if (bytes === null || bytes < 0) {
        return "--";
      }
      if (bytes < 1024) {
        return `${bytes} B`;
      }
      if (bytes < 1024 * 1024) {
        return `${(bytes / 1024).toFixed(1)} KB`;
      }
      return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
    },

    stateClassFor(state) {
      switch (state) {
        case "processing_task":
          return "state-processing-task";
        case "active":
          return "state-active";
        case "waiting_for_task":
          return "state-waiting-for-task";
        case "initializing":
          return "state-initializing";
        case "completing":
          return "state-completing";
        case "terminated":
          return "state-terminated";
        case "error_state":
          return "state-error-state";
        case "unknown":
        default:
          return "state-unknown";
      }
    },

    escapeHtml(text) {
      return String(text)
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#39;");
    },

    trimLeadingZerosHex(value) {
      if (!/^[0-9a-fA-F]+$/.test(value)) {
        return value;
      }
      const trimmed = value.replace(/^0+/, "");
      return trimmed.length > 0 ? trimmed.toLowerCase() : "0";
    },

    formatNullable(value) {
      if (value === null || value === undefined || value === "") {
        return "--";
      }
      return String(value);
    },

    formatGeneratorStatus(status) {
      switch (String(status || "")) {
        case "no_tasks":
          return "No tasks";
        case "no_workers":
          return "No workers";
        case "starting":
          return "Starting";
        case "running":
          return "Running";
        case "stopping":
          return "Stopping";
        case "stopped":
          return "Stopped";
        case "error":
          return "Error";
        default:
          return status;
      }
    },
  };
}
