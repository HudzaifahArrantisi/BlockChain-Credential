// ── SecureChain Dev Dashboard ─────────────────────────────────────────────
// Live logs + live actions from blockchain nodes via SSE

(function () {
  "use strict";

  const MAX_LOG_LINES = 500;
  const MAX_ACTIONS = 100;
  let paused = false;
  let activeFilter = "all";
  let logLines = [];
  let actions = [];
  let prevNodeState = {};
  let prevBlockchainLen = 0;

  // ── Elements ──────────────────────────────────────────────────────────

  const $ = (s) => document.querySelector(s);
  const $$ = (s) => document.querySelectorAll(s);

  const connDot = $("#conn-dot");
  const connText = $("#conn-text");
  const nodeCards = $("#node-cards");
  const logConsole = $("#log-console");
  const logFilter = $("#log-filter");
  const logLineCount = $("#log-line-count");
  const actionFeed = $("#action-feed");
  const actionCount = $("#action-count");
  const blockCount = $("#block-count");
  const chainFeed = $("#chain-feed");
  const nodeCount = $("#node-count");

  // ── Clock ─────────────────────────────────────────────────────────────

  function updateClock() {
    const now = new Date();
    const hh = String(now.getHours()).padStart(2, "0");
    const mm = String(now.getMinutes()).padStart(2, "0");
    const ss = String(now.getSeconds()).padStart(2, "0");
    $("#clock").textContent = `${hh}:${mm}:${ss}`;
  }
  setInterval(updateClock, 1000);
  updateClock();

  // ── SSE Connection ────────────────────────────────────────────────────

  let evtSource = null;
  let reconnectTimer = null;

  function connectSSE() {
    if (evtSource) {
      try { evtSource.close(); } catch (_) {}
    }

    evtSource = new EventSource("/events");

    evtSource.onopen = () => {
      connDot.className = "conn-dot connected";
      connText.textContent = "Connected";
    };

    evtSource.onmessage = (e) => {
      if (paused) return;
      try {
        const data = JSON.parse(e.data);
        processData(data);
      } catch (_) {}
    };

    evtSource.onerror = () => {
      connDot.className = "conn-dot disconnected";
      connText.textContent = "Reconnecting...";
      evtSource.close();
      reconnectTimer = setTimeout(connectSSE, 3000);
    };
  }

  // ── Process SSE Data ──────────────────────────────────────────────────

  function processData(ev) {
    const { nodes, logs, blockchain } = ev;

    // Detect actions (role changes, new blocks, elections)
    detectActions(nodes, blockchain);

    // Render node cards
    renderNodeCards(nodes);

    // Render logs
    renderLogs(logs, nodes);

    // Render blockchain
    renderChain(blockchain);

    // Update counts
    nodeCount.textContent = nodes.length;

    // Save state for next diff
    prevNodeState = {};
    nodes.forEach((n) => {
      prevNodeState[n.port] = { role: n.role, alive: n.alive, blocks: n.blocks };
    });
    if (blockchain.blocks) {
      prevBlockchainLen = blockchain.blocks.length;
    }
  }

  // ── Detect Actions ────────────────────────────────────────────────────

  function detectActions(nodes, blockchain) {
    const ts = new Date().toLocaleTimeString("en-GB");

    nodes.forEach((n) => {
      const prev = prevNodeState[n.port];

      if (!prev) {
        // First time seeing this node
        if (n.alive) {
          addAction(ts, "NODE_ONLINE", n.label, `Node came online (${n.role})`, n.role);
        }
        return;
      }

      // Role change
      if (prev.role !== n.role) {
        if (n.role === "LEADER") {
          addAction(ts, "ELECTED", n.label, `Elected as LEADER (term ${n.term})`, "LEADER");
        } else if (n.role === "CANDIDATE") {
          addAction(ts, "CANDIDATE", n.label, `Started election (term ${n.term})`, "CANDIDATE");
        } else if (n.role === "FOLLOWER" && prev.role === "LEADER") {
          addAction(ts, "STEP_DOWN", n.label, `Stepped down from LEADER`, "FOLLOWER");
        }
      }

      // Online/offline
      if (prev.alive && !n.alive) {
        addAction(ts, "NODE_OFFLINE", n.label, "Node went offline", "OFFLINE");
      } else if (!prev.alive && n.alive) {
        addAction(ts, "NODE_ONLINE", n.label, "Node came online", n.role);
      }
    });

    // New blocks
    if (blockchain.blocks && blockchain.blocks.length > prevBlockchainLen) {
      const newBlocks = blockchain.blocks.slice(prevBlockchainLen);
      newBlocks.forEach((b) => {
        const name = b.student_name || "?";
        const sid = b.student_id || "?";
        addAction(
          ts,
          "NEW_BLOCK",
          `Block #${b.index}`,
          `New block committed: ${name} (${sid})`,
          "LEADER"
        );
      });
    }
  }

  function addAction(ts, type, source, message, role) {
    actions.unshift({ ts, type, source, message, role, id: Date.now() + Math.random() });
    if (actions.length > MAX_ACTIONS) actions.pop();
    renderActions();
  }

  // ── Render Node Cards ─────────────────────────────────────────────────

  function renderNodeCards(nodes) {
    let html = "";
    nodes.forEach((n) => {
      const roleClass = n.role ? n.role.toLowerCase() : "offline";
      const statusDot = n.alive
        ? `<span class="ndot ${roleClass}"></span>`
        : `<span class="ndot offline"></span>`;
      const idShort = n.id_short || "—";
      const leaderInfo = n.role === "LEADER" ? `<div class="nc-leader">LEADER</div>` : "";

      html += `
        <div class="node-card ${roleClass} ${!n.alive ? "dead" : ""}" data-port="${n.port}">
          <div class="nc-top">
            ${statusDot}
            <span class="nc-label">${n.label}</span>
            <span class="nc-port">:${n.port}</span>
          </div>
          <div class="nc-mid">
            <div class="nc-role">${n.role}</div>
            ${leaderInfo}
          </div>
          <div class="nc-bottom">
            <span>Term: ${n.term}</span>
            <span>Blocks: ${n.blocks}</span>
          </div>
          <div class="nc-id">${idShort}</div>
        </div>`;
    });
    nodeCards.innerHTML = html;
  }

  // ── Render Logs ───────────────────────────────────────────────────────

  function renderLogs(logs, nodes) {
    // Build filter options if needed
    const existingPorts = new Set();
    logFilter.querySelectorAll("option").forEach((o) => {
      if (o.value !== "all") existingPorts.add(o.value);
    });
    nodes.forEach((n) => {
      const port = String(n.port);
      if (!existingPorts.has(port)) {
        const opt = document.createElement("option");
        opt.value = port;
        opt.textContent = `${n.label} (:${port})`;
        logFilter.appendChild(opt);
      }
    });

    // Collect log lines
    let lines = [];
    if (activeFilter === "all") {
      Object.entries(logs).forEach(([port, ls]) => {
        const n = nodes.find((x) => String(x.port) === port);
        ls.forEach((l) =>
          lines.push({ text: l, label: n?.label || port, role: n?.role, port })
        );
      });
    } else {
      const ls = logs[activeFilter] || [];
      const n = nodes.find((x) => String(x.port) === activeFilter);
      ls.forEach((l) =>
        lines.push({ text: l, label: n?.label || activeFilter, role: n?.role, port: activeFilter })
      );
    }

    // Deduplicate & keep order
    const seen = new Set();
    const unique = [];
    lines.forEach((l) => {
      const key = l.text + l.label;
      if (!seen.has(key)) {
        seen.add(key);
        unique.push(l);
      }
    });

    logLines = unique.slice(-MAX_LOG_LINES);
    logLineCount.textContent = `${logLines.length} lines`;

    // Render
    const html = logLines
      .map((l) => {
        const cls = l.role ? l.role.toLowerCase() : "";
        const tsMatch = l.text.match(/\[(\d{2}:\d{2}:\d{2})\]/);
        const ts = tsMatch ? tsMatch[1] : "";
        const rest = l.text.replace(/\[\d{2}:\d{2}:\d{2}\]\s*/, "");
        return `<div class="ll ${cls}"><span class="ts">${ts}</span><span class="src">[${l.label}]</span> ${escHtml(rest)}</div>`;
      })
      .join("");

    logConsole.innerHTML = html;
    logConsole.scrollTop = logConsole.scrollHeight;
  }

  // ── Render Actions ────────────────────────────────────────────────────

  function renderActions() {
    actionCount.textContent = actions.length;

    const html = actions
      .map((a) => {
        const icon = actionIcon(a.type);
        const cls = a.role ? a.role.toLowerCase() : "";
        return `<div class="af-item ${cls}">
          <span class="af-ts">${a.ts}</span>
          <span class="af-icon">${icon}</span>
          <span class="af-type">${a.type}</span>
          <span class="af-msg">${escHtml(a.message)}</span>
        </div>`;
      })
      .join("");

    actionFeed.innerHTML = html;
  }

  function actionIcon(type) {
    switch (type) {
      case "ELECTED": return "&#9733;";
      case "CANDIDATE": return "&#9889;";
      case "STEP_DOWN": return "&#8595;";
      case "NEW_BLOCK": return "&#9741;";
      case "NODE_ONLINE": return "&#9679;";
      case "NODE_OFFLINE": return "&#9675;";
      default: return "&#8226;";
    }
  }

  // ── Render Blockchain ─────────────────────────────────────────────────

  function renderChain(bc) {
    const blocks = bc.blocks || [];
    blockCount.textContent = blocks.length;

    if (!blocks.length) {
      chainFeed.innerHTML = `<div class="empty">Waiting for blocks...</div>`;
      return;
    }

    let html = "";
    [...blocks]
      .reverse()
      .forEach((b, i) => {
        const fresh = i === 0 && blocks.length > prevBlockchainLen;
        const genesis = b.index === "0" || i === blocks.length - 1;
        const hsh = (b.block_hash || "").substring(0, 16) + "...";
        html += `<div class="bf-card${fresh ? " fresh" : ""}${genesis ? " genesis" : ""}">
          <div class="bf-idx">#${b.index}${genesis ? " GENESIS" : ""}</div>
          <div class="bf-name">${b.student_name || "?"}</div>
          <div class="bf-id">${b.student_id || "?"}</div>
          <div class="bf-hash">${hsh}</div>
          <div class="bf-time">${b.timestamp || ""}</div>
        </div>`;
      });

    chainFeed.innerHTML = html;
  }

  // ── Helpers ───────────────────────────────────────────────────────────

  function escHtml(s) {
    const d = document.createElement("div");
    d.textContent = s;
    return d.innerHTML;
  }

  // ── Controls ──────────────────────────────────────────────────────────

  logFilter.addEventListener("change", () => {
    activeFilter = logFilter.value;
  });

  $("#btn-clear").addEventListener("click", () => {
    logLines = [];
    actions = [];
    logConsole.innerHTML = "";
    actionFeed.innerHTML = "";
    logLineCount.textContent = "0 lines";
    actionCount.textContent = "0";
  });

  $("#btn-pause").addEventListener("click", () => {
    paused = !paused;
    $("#btn-pause").textContent = paused ? "Resume" : "Pause";
    $("#btn-pause").classList.toggle("active", paused);
  });

  $("#btn-export").addEventListener("click", () => {
    const lines = logLines.map((l) => l.text).join("\n");
    const blob = new Blob([lines], { type: "text/plain" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `scdv-logs-${new Date().toISOString().slice(0, 19).replace(/:/g, "-")}.txt`;
    a.click();
  });

  // ── Boot ──────────────────────────────────────────────────────────────

  connectSSE();
})();
