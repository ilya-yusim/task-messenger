// tm-worker-farm UI — vanilla JS, no framework, no build step.

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

const POLL_MS = 1000;

const els = {
  form: $("#spawn-form"),
  host: $("#host"),
  hostStatus: $("#host-status"),
  hostStatusText: $("#host-status-text"),
  bootstrapBtn: $("#bootstrap-btn"),
  count: $("#count"),
  args: $("#args"),
  spawnBtn: $("#spawn-btn"),
  stopAllBtn: $("#stop-all-btn"),
  spawnStatus: $("#spawn-status"),
  rows: $("#worker-rows"),
  workerCount: $("#worker-count"),
  modal: $("#log-modal"),
  logTitle: $("#log-title"),
  logMeta: $("#log-meta"),
  logPre: $("#log-pre"),
  logClose: $("#log-close"),
  logRefresh: $("#log-refresh"),
  logAuto: $("#log-auto"),
  quarantineSection: $("#quarantine"),
  quarantineRows: $("#quarantine-rows"),
  quarantineCount: $("#quarantine-count"),
};

let activeStream = null; // { es: EventSource, id: string }
let logPollTimer = null; // setInterval handle for codespace tail polling
let activeLogWorker = null; // {id, host, state} of the worker shown in the modal
const LOG_POLL_MS = 2000;
const LOG_TAIL_BYTES = 65536;
const LOG_TAIL_LINES = 500; // codespace endpoint takes a line count

function fmtTime(iso) {
  if (!iso) return "—";
  return new Date(iso).toLocaleTimeString();
}

function fmtUptime(startedAt, stoppedAt) {
  if (!startedAt) return "—";
  const start = new Date(startedAt).getTime();
  const end = stoppedAt ? new Date(stoppedAt).getTime() : Date.now();
  let s = Math.max(0, Math.floor((end - start) / 1000));
  const h = Math.floor(s / 3600); s -= h * 3600;
  const m = Math.floor(s / 60); s -= m * 60;
  if (h > 0) return `${h}h${String(m).padStart(2, "0")}m`;
  if (m > 0) return `${m}m${String(s).padStart(2, "0")}s`;
  return `${s}s`;
}

function stateClass(state) {
  return `state-${state || "exited"}`;
}

function fmtExit(w) {
  if (w.state !== "exited") return "—";
  if (w.last_error) return `err: ${w.last_error}`;
  if (w.exit_code === undefined || w.exit_code === null) return "—";
  return String(w.exit_code);
}

function renderRows(workers) {
  els.workerCount.textContent = workers.length ? `(${workers.length})` : "";
  if (!workers.length) {
    els.rows.innerHTML = `<tr><td colspan="8" class="muted">No workers. Use the form above to spawn some.</td></tr>`;
    return;
  }
  const frag = document.createDocumentFragment();
  for (const w of workers) {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td class="id" title="${w.id}">${w.id}</td>
      <td>${w.host || "local"}</td>
      <td><span class="${stateClass(w.state)}">${w.state}${w.adopted && w.state === 'exited' ? ' (orphan)' : ''}</span></td>
      <td>${w.pid || "—"}</td>
      <td>${fmtTime(w.started_at)}</td>
      <td>${fmtUptime(w.started_at, w.stopped_at)}</td>
      <td>${fmtExit(w)}</td>
      <td class="actions"></td>
    `;
    const actions = tr.querySelector("td.actions");

    const logBtn = document.createElement("button");
    logBtn.type = "button";
    logBtn.textContent = "Logs";
    logBtn.addEventListener("click", () => openLogs(w));
    actions.appendChild(logBtn);

    if (w.state === "running" || w.state === "starting") {
      const stopBtn = document.createElement("button");
      stopBtn.type = "button";
      stopBtn.className = "row-stop";
      stopBtn.textContent = "Stop";
      stopBtn.addEventListener("click", () => stopWorker(w.id, stopBtn));
      actions.appendChild(stopBtn);
    } else if (w.state === "exited") {
      const purgeBtn = document.createElement("button");
      purgeBtn.type = "button";
      purgeBtn.className = "row-purge";
      purgeBtn.textContent = "Purge";
      purgeBtn.title = "Delete log + sentinel + pidfile and remove this row";
      purgeBtn.addEventListener("click", () => purgeWorker(w.id, purgeBtn));
      actions.appendChild(purgeBtn);
    }
    frag.appendChild(tr);
  }
  els.rows.replaceChildren(frag);
}

async function poll() {
  try {
    const res = await fetch("/workers", { cache: "no-store" });
    if (!res.ok) throw new Error(`GET /workers -> ${res.status}`);
    const data = await res.json();
    renderRows(Array.isArray(data) ? data : []);
  } catch (err) {
    els.rows.innerHTML = `<tr><td colspan="8" class="muted">Error loading workers: ${err.message}</td></tr>`;
  }
}

async function spawn(count, args, hostID) {
  els.spawnBtn.disabled = true;
  els.spawnStatus.textContent = `Starting ${count} on ${hostID}…`;
  try {
    const res = await fetch("/workers", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ count, args, host_id: hostID }),
    });
    if (!res.ok && res.status !== 207) {
      const txt = await res.text();
      throw new Error(`spawn -> ${res.status}: ${txt.trim()}`);
    }
    const body = await res.json().catch(() => ({}));
    const workers = body.workers || [];
    const ok = workers.filter((w) => w.ok).length;
    const failed = workers.filter((w) => !w.ok);
    if (failed.length) {
      const detail = failed.map((f) => f.error).join("; ");
      els.spawnStatus.textContent = `${ok}/${workers.length} started; failures: ${detail}`;
    } else {
      els.spawnStatus.textContent = `${ok} worker(s) started.`;
    }
  } catch (err) {
    els.spawnStatus.textContent = `Spawn failed: ${err.message}`;
  } finally {
    els.spawnBtn.disabled = false;
    poll();
  }
}

async function stopWorker(id, btn) {
  if (btn) btn.disabled = true;
  try {
    const res = await fetch(`/workers/${encodeURIComponent(id)}/stop`, { method: "POST" });
    if (!res.ok) throw new Error(`stop -> ${res.status}`);
  } catch (err) {
    els.spawnStatus.textContent = `Stop failed: ${err.message}`;
  } finally {
    poll();
  }
}

async function purgeWorker(id, btn) {
  if (btn) btn.disabled = true;
  try {
    const res = await fetch(`/workers/${encodeURIComponent(id)}/purge`, { method: "POST" });
    if (!res.ok) {
      const txt = await res.text();
      throw new Error(`purge -> ${res.status}: ${txt.trim()}`);
    }
  } catch (err) {
    els.spawnStatus.textContent = `Purge failed: ${err.message}`;
  } finally {
    poll();
  }
}

async function stopAll() {
  if (!confirm("Stop all running workers?")) return;
  els.stopAllBtn.disabled = true;
  try {
    const res = await fetch("/workers/stop-all", { method: "POST" });
    if (!res.ok) throw new Error(`stop-all -> ${res.status}`);
    els.spawnStatus.textContent = "Stop-all sent.";
  } catch (err) {
    els.spawnStatus.textContent = `Stop-all failed: ${err.message}`;
  } finally {
    els.stopAllBtn.disabled = false;
    poll();
  }
}

function openLogs(w) {
  els.logTitle.textContent = `${w.id} — ${w.state}`;
  els.logMeta.textContent = w.host ? `host: ${w.host}` : "";
  els.logPre.textContent = "Loading…";
  els.modal.showModal();

  // Tear down anything left over from a previous open.
  closeStream();
  activeLogWorker = { id: w.id, host: w.host, state: w.state };

  // Initial fetch + decide refresh strategy. Codespace workers
  // can't use SSE (the server returns 501 for them, see
  // handleWorkerLogStream) so we poll the tail endpoint instead.
  // For local workers we still use SSE for true tail-f behaviour
  // and fall back to a manual Refresh button if it dies.
  refreshLog().then(() => {
    const isCodespace = w.host && w.host !== "local";
    const isLive = w.state === "running" || w.state === "starting" || w.state === "stopping";
    if (!isLive) return;
    if (isCodespace) {
      startLogPoll();
    } else {
      const es = new EventSource(`/workers/${encodeURIComponent(w.id)}/log/stream`);
      es.onmessage = (ev) => {
        els.logPre.textContent += ev.data + "\n";
        els.logPre.scrollTop = els.logPre.scrollHeight;
      };
      // If the stream errors out (e.g. server doesn't actually
      // support SSE for this worker), silently downgrade to
      // periodic polling so the operator still sees fresh output.
      es.onerror = () => {
        es.close();
        if (activeStream && activeStream.es === es) {
          activeStream = null;
          startLogPoll();
        }
      };
      activeStream = { es, id: w.id };
    }
  });
}

// refreshLog re-fetches the tail of the active log worker's log file
// and replaces the pre's contents (auto-scrolled to the bottom).
// Returns a promise so callers can chain post-load behaviour.
async function refreshLog() {
  if (!activeLogWorker) return;
  const w = activeLogWorker;
  const isCodespace = w.host && w.host !== "local";
  const url = isCodespace
    ? `/workers/${encodeURIComponent(w.id)}/log?tail=${LOG_TAIL_LINES}`
    : `/workers/${encodeURIComponent(w.id)}/log?tail=${LOG_TAIL_BYTES}`;
  try {
    const res = await fetch(url, { cache: "no-store" });
    if (!res.ok) throw new Error(`log -> ${res.status}`);
    const text = await res.text();
    // Preserve user's scroll position only if they've scrolled away
    // from the bottom (>40 px from the floor). Otherwise stay glued
    // to the tail.
    const stuckToBottom =
      els.logPre.scrollHeight - els.logPre.scrollTop - els.logPre.clientHeight < 40;
    els.logPre.textContent = text;
    if (stuckToBottom) {
      els.logPre.scrollTop = els.logPre.scrollHeight;
    }
  } catch (err) {
    els.logPre.textContent = `Error: ${err.message}`;
  }
}

function startLogPoll() {
  stopLogPoll();
  if (!els.logAuto.checked) return;
  logPollTimer = setInterval(() => { refreshLog(); }, LOG_POLL_MS);
}

function stopLogPoll() {
  if (logPollTimer) {
    clearInterval(logPollTimer);
    logPollTimer = null;
  }
}

function closeStream() {
  if (activeStream) {
    activeStream.es.close();
    activeStream = null;
  }
  stopLogPoll();
  activeLogWorker = null;
}

function parseArgs(text) {
  // Naive whitespace-split. Operator can pass through quoted args via
  // the JSON API directly if they need spaces in a single token.
  return text.trim().length ? text.trim().split(/\s+/) : [];
}

els.form.addEventListener("submit", (e) => {
  e.preventDefault();
  const count = parseInt(els.count.value, 10);
  if (!Number.isFinite(count) || count < 1) return;
  spawn(count, parseArgs(els.args.value), els.host.value || "local");
});

els.stopAllBtn.addEventListener("click", stopAll);
els.host.addEventListener("change", refreshHostStatus);
els.bootstrapBtn.addEventListener("click", (e) => bootstrapHost(e.currentTarget.dataset.hostId, e.currentTarget));

els.logClose.addEventListener("click", () => {
  closeStream();
  els.modal.close();
});
els.modal.addEventListener("close", closeStream);
els.logRefresh.addEventListener("click", refreshLog);
els.logAuto.addEventListener("change", () => {
  if (!activeLogWorker) return;
  const isCodespace = activeLogWorker.host && activeLogWorker.host !== "local";
  // Auto-refresh toggle controls the polling timer. SSE for local
  // workers is unaffected — its whole point is that it doesn't poll.
  if (els.logAuto.checked) {
    if (isCodespace) startLogPoll();
  } else {
    stopLogPoll();
  }
});

poll();
setInterval(poll, POLL_MS);
loadHosts();

// Hosts -------------------------------------------------------------

async function loadHosts() {
  try {
    const res = await fetch("/hosts", { cache: "no-store" });
    if (!res.ok) throw new Error(`GET /hosts -> ${res.status}`);
    const hosts = await res.json();
    renderHosts(Array.isArray(hosts) ? hosts : []);
  } catch (err) {
    // Fallback: single local entry so the form still works.
    renderHosts([{ id: "local", backend: "local", supported: true }]);
  }
}

function renderHosts(hosts) {
  const sel = els.host;
  sel.replaceChildren();
  for (const h of hosts) {
    const opt = document.createElement("option");
    opt.value = h.id;
    opt.textContent = h.supported ? `${h.id} (${h.backend})` : `${h.id} (${h.backend}, not yet supported)`;
    if (!h.supported) opt.disabled = true;
    sel.appendChild(opt);
  }
  // Pick the first supported host by default.
  const firstSupported = hosts.find((h) => h.supported);
  if (firstSupported) sel.value = firstSupported.id;
  refreshHostStatus();
}

async function refreshHostStatus() {
  const id = els.host.value;
  if (!id) {
    els.hostStatusText.textContent = "";
    els.bootstrapBtn.hidden = true;
    return;
  }
  els.hostStatusText.textContent = `Checking ${id}…`;
  els.bootstrapBtn.hidden = true;
  try {
    const res = await fetch(`/hosts/${encodeURIComponent(id)}/status`, { cache: "no-store" });
    if (!res.ok) throw new Error(`status -> ${res.status}`);
    const s = await res.json();
    els.hostStatusText.textContent = formatHostStatus(s);
    // Bootstrap is meaningful for codespace hosts that are reachable
    // (gh authed + codespace running). "ok" today only means gh+cs
    // are healthy; tm-worker may or may not be installed. Surface
    // the button so the operator can install on demand.
    els.bootstrapBtn.hidden = !(s.backend === "codespace" && s.status === "ok");
    els.bootstrapBtn.dataset.hostId = id;
  } catch (err) {
    els.hostStatusText.textContent = `${id}: status check failed: ${err.message}`;
    els.bootstrapBtn.hidden = true;
  }
}

function formatHostStatus(s) {
  const head = `${s.id} (${s.backend}): ${s.status}`;
  const tail = [s.detail, s.hint && `→ ${s.hint}`].filter(Boolean).join(" — ");
  return tail ? `${head} — ${tail}` : head;
}

async function bootstrapHost(id, btn) {
  if (!id) return;
  if (!confirm(`Install tm-worker on ${id}? This downloads the latest release locally and uploads it via gh codespace cp.`)) return;
  btn.disabled = true;
  const prev = btn.textContent;
  btn.textContent = "Bootstrapping…";
  els.hostStatusText.textContent = `${id}: bootstrapping (this can take a minute)…`;
  try {
    const res = await fetch(`/hosts/${encodeURIComponent(id)}/bootstrap`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: "{}",
    });
    const txt = await res.text();
    if (!res.ok) throw new Error(txt.trim() || `bootstrap -> ${res.status}`);
    let body;
    try { body = JSON.parse(txt); } catch { body = {}; }
    els.hostStatusText.textContent = `${id}: bootstrap ok (tag=${body.tag || "?"}, asset=${body.asset_name || "?"})`;
  } catch (err) {
    els.hostStatusText.textContent = `${id}: bootstrap failed: ${err.message}`;
  } finally {
    btn.disabled = false;
    btn.textContent = prev;
  }
}

// Quarantine ----------------------------------------------------------

async function pollQuarantine() {
  try {
    const res = await fetch("/quarantine", { cache: "no-store" });
    if (!res.ok) throw new Error(`GET /quarantine -> ${res.status}`);
    const data = await res.json();
    renderQuarantine(Array.isArray(data) ? data : []);
  } catch (err) {
    // Soft-fail; the section just stays hidden if the endpoint is down.
    els.quarantineSection.hidden = true;
  }
}

function renderQuarantine(items) {
  if (!items.length) {
    els.quarantineSection.hidden = true;
    els.quarantineRows.replaceChildren();
    return;
  }
  els.quarantineSection.hidden = false;
  els.quarantineCount.textContent = `(${items.length})`;
  const frag = document.createDocumentFragment();
  for (const it of items) {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>${it.run_id}</td>
      <td>${String(it.slot).padStart(2, "0")}</td>
      <td>${it.pid}</td>
      <td title="${it.controller_id || ''}">${(it.controller_id || '').slice(0, 12)}…</td>
      <td>${it.alive ? "yes" : "no"}</td>
      <td class="actions"></td>
    `;
    const actions = tr.querySelector("td.actions");
    for (const action of ["adopt", "kill", "ignore"]) {
      const b = document.createElement("button");
      b.type = "button";
      b.textContent = action[0].toUpperCase() + action.slice(1);
      b.addEventListener("click", () => quarantineAct(it, action, b));
      actions.appendChild(b);
    }
    frag.appendChild(tr);
  }
  els.quarantineRows.replaceChildren(frag);
}

async function quarantineAct(item, action, btn) {
  if (action === "kill" && !confirm(`Kill PID ${item.pid} (run ${item.run_id} slot ${item.slot})?`)) return;
  btn.disabled = true;
  try {
    const url = `/quarantine/${encodeURIComponent(item.run_id)}/${String(item.slot).padStart(2, "0")}/${action}`;
    const res = await fetch(url, { method: "POST" });
    if (!res.ok) {
      const txt = await res.text();
      throw new Error(`${action} -> ${res.status}: ${txt.trim()}`);
    }
  } catch (err) {
    els.spawnStatus.textContent = `Quarantine ${action} failed: ${err.message}`;
  } finally {
    pollQuarantine();
    poll();
  }
}

pollQuarantine();
setInterval(pollQuarantine, POLL_MS * 3);
