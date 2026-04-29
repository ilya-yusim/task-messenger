// tm-worker-farm UI — vanilla JS, no framework, no build step.

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

const POLL_MS = 1000;

const els = {
  form: $("#spawn-form"),
  count: $("#count"),
  args: $("#args"),
  spawnBtn: $("#spawn-btn"),
  stopAllBtn: $("#stop-all-btn"),
  spawnStatus: $("#spawn-status"),
  rows: $("#worker-rows"),
  workerCount: $("#worker-count"),
  modal: $("#log-modal"),
  logTitle: $("#log-title"),
  logPre: $("#log-pre"),
  logClose: $("#log-close"),
};

let activeStream = null; // { es: EventSource, id: string }

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
      <td><span class="${stateClass(w.state)}">${w.state}</span></td>
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

async function spawn(count, args) {
  els.spawnBtn.disabled = true;
  els.spawnStatus.textContent = `Starting ${count}…`;
  try {
    const res = await fetch("/workers", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ count, args }),
    });
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
  els.logPre.textContent = "Loading…";
  els.modal.showModal();

  // Close any prior stream first.
  closeStream();

  // Initial tail (last 64 KB) so the user immediately sees something.
  fetch(`/workers/${encodeURIComponent(w.id)}/log?tail=65536`, { cache: "no-store" })
    .then((r) => r.ok ? r.text() : Promise.reject(new Error(`log -> ${r.status}`)))
    .then((text) => {
      els.logPre.textContent = text;
      els.logPre.scrollTop = els.logPre.scrollHeight;
    })
    .catch((err) => { els.logPre.textContent = `Error: ${err.message}`; });

  // Live tail via SSE — only meaningful while the worker is running.
  if (w.state === "running" || w.state === "starting" || w.state === "stopping") {
    const es = new EventSource(`/workers/${encodeURIComponent(w.id)}/log/stream`);
    es.onmessage = (ev) => {
      els.logPre.textContent += ev.data + "\n";
      els.logPre.scrollTop = els.logPre.scrollHeight;
    };
    es.onerror = () => { /* server closed; that's fine */ };
    activeStream = { es, id: w.id };
  }
}

function closeStream() {
  if (activeStream) {
    activeStream.es.close();
    activeStream = null;
  }
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
  spawn(count, parseArgs(els.args.value));
});

els.stopAllBtn.addEventListener("click", stopAll);

els.logClose.addEventListener("click", () => {
  closeStream();
  els.modal.close();
});
els.modal.addEventListener("close", closeStream);

poll();
setInterval(poll, POLL_MS);
