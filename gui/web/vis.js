const COLORS = {
  LEADER:   { fill: "#10b981", stroke: "#047857", pulse: "rgba(16, 185, 129, 0.25)", ring: "#10b981" },
  FOLLOWER: { fill: "#3b82f6", stroke: "#1d4ed8", pulse: "rgba(59, 130, 246, 0.15)", ring: "#3b82f6" },
  CANDIDATE:{ fill: "#f59e0b", stroke: "#b45309", pulse: "rgba(245, 158, 11, 0.15)", ring: "#f59e0b" },
  OFFLINE:  { fill: "#1e293b", stroke: "#0f172a", pulse: "transparent", ring: "#334155" },
};

// Badge DOM helpers ─ update only the text child, preserve the pill-dot
function _setBadge(sel, label) {
  const el = document.querySelector(sel);
  if (!el) return;
  // preserve pill-dot; update the pill-label span (or last text node)
  let lbl = el.querySelector(".pill-label");
  if (lbl) { lbl.textContent = label; return; }
  // fallback: just overwrite
  const dot = el.querySelector(".pill-dot");
  el.innerHTML = "";
  if (dot) el.appendChild(dot);
  const t = document.createElement("span");
  t.textContent = label;
  el.appendChild(t);
}
function _setBlockBadge(count) {
  const el = document.querySelector(".block-badge");
  if (!el) return;
  let sp = el.querySelector("span:last-child");
  if (sp && !sp.querySelector("svg")) { sp.textContent = `${count} BLOCKS`; }
  else {
    // build fresh - keep SVG intact
    const svg = el.querySelector("svg");
    el.innerHTML = "";
    if (svg) el.appendChild(svg);
    const t = document.createElement("span");
    t.textContent = `${count} BLOCKS`;
    el.appendChild(t);
  }
}


let data = { nodes: [], logs: {}, blockchain: { blocks: [] } };
let activeLog = "all";
let prevBlocks = 0;
let firstLoad = true;
let prevAliveCount = 0;
let waitingForNodes = true;
let graphBuilt = false;

const svg = d3.select("#vis").append("svg").attr("width","100%").attr("height","100%");

function dims() {
  const rect = document.getElementById("vis").getBoundingClientRect();
  return { w: rect.width, h: rect.height, cx: rect.width/2, cy: rect.height/2 };
}

const defs = svg.append("defs");
defs.html(`
  <filter id="glow"><feGaussianBlur stdDeviation="3" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
  <filter id="glowStrong"><feGaussianBlur stdDeviation="5" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
  <filter id="shadow"><feDropShadow dx="0" dy="2" stdDeviation="6" flood-color="#000" flood-opacity="0.5"/></filter>
  <filter id="linkGlow"><feGaussianBlur stdDeviation="2" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
  <marker id="arrow" viewBox="0 0 10 10" refX="24" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
    <path d="M 0 1.5 L 8 5 L 0 8.5 z" fill="#10b981"/>
  </marker>
  <marker id="arrowOff" viewBox="0 0 10 10" refX="20" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
    <path d="M 0 1.5 L 8 5 L 0 8.5 z" fill="#475569"/>
  </marker>
  <radialGradient id="nodeBg" cx="40%" cy="35%" r="60%">
    <stop offset="0%" stop-color="#fff" stop-opacity="0.15"/>
    <stop offset="100%" stop-color="#fff" stop-opacity="0"/>
  </radialGradient>
  <pattern id="dotGrid" width="30" height="30" patternUnits="userSpaceOnUse">
    <circle cx="1.5" cy="1.5" r="1" fill="#1e293b" opacity="0.6"/>
  </pattern>
  <style>
    @keyframes cable-flow {
      0% { stroke-dashoffset: 0; }
      100% { stroke-dashoffset: -12; }
    }
    @keyframes cable-flow-bg {
      0% { stroke-dashoffset: 0; }
      100% { stroke-dashoffset: -12; }
    }
    .animated-link {
      animation: cable-flow 1.2s linear infinite;
      filter: url(#linkGlow);
      stroke-linecap: round;
      stroke-linejoin: round;
    }
    .animated-link-bg {
      animation: cable-flow-bg 1.2s linear infinite;
      stroke-linecap: round;
      stroke-linejoin: round;
    }
  </style>
`);

// Static dark background behind everything
svg.append("rect").attr("class","bg").attr("width","100%").attr("height","100%").attr("fill","#040810");

// Main canvas-group which is zoomable and pannable
const gMain = svg.append("g").attr("class", "canvas-group");

// Infinite dot grid inside the zoomable group so it pans/scales naturally
gMain.append("rect")
  .attr("x", -20000)
  .attr("y", -20000)
  .attr("width", 40000)
  .attr("height", 40000)
  .attr("fill", "url(#dotGrid)")
  .attr("pointer-events", "none");

const linkBgG = gMain.append("g");  // Background glow layer
const linkG = gMain.append("g");    // Main link layer
const linkLabelG = gMain.append("g");
const particleG = gMain.append("g");
const nodeG = gMain.append("g");

let simulation = null;
let currentNodes = [];
let showEdgeLabels = true;
let linkPath = null;  // Global reference for tick function
let linkBgPath = null;  // Global reference for tick function
let linkLabelText = null;  // Global reference for tick function

// ── Zoom behavior ──
const zoomBehavior = d3.zoom()
  .scaleExtent([0.15, 4])
  .on("zoom", (event) => {
    gMain.attr("transform", event.transform);
  });
svg.call(zoomBehavior);

// Center & scale graph dynamically
function fitView() {
  if (!currentNodes || !currentNodes.length) return;
  const d = dims();
  
  let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
  currentNodes.forEach(n => {
    if (n.x < minX) minX = n.x;
    if (n.x > maxX) maxX = n.x;
    if (n.y < minY) minY = n.y;
    if (n.y > maxY) maxY = n.y;
  });
  
  const margin = 100;
  minX -= margin; maxX += margin;
  minY -= margin; maxY += margin;
  
  const dx = maxX - minX;
  const dy = maxY - minY;
  const cx = (minX + maxX) / 2;
  const cy = (minY + maxY) / 2;
  
  let scale = Math.min(d.w / dx, d.h / dy);
  scale = Math.max(0.3, Math.min(1.5, scale));
  
  const tx = d.w / 2 - scale * cx;
  const ty = d.h / 2 - scale * cy;
  
  svg.transition().duration(750).ease(d3.easeCubicOut).call(
    zoomBehavior.transform,
    d3.zoomIdentity.translate(tx, ty).scale(scale)
  );
}

// ── Drag behavior ──
function drag(sim) {
  return d3.drag()
    .on("start", (event, d) => {
      if (!event.active) sim.alphaTarget(0.2).restart();
      d.fx = d.x;
      d.fy = d.y;
    })
    .on("drag", (event, d) => {
      d.fx = event.x;
      d.fy = event.y;
    })
    .on("end", (event, d) => {
      if (!event.active) sim.alphaTarget(0);
      d.fx = null;
      d.fy = null;
    });
}

function buildGraph(nodes) {
  const alive = nodes.filter(n => n.alive);
  const leader = alive.find(n => n.role === "LEADER");
  const center = leader || (alive.length > 0 ? alive[0] : null);

  // Preserve positions across polls
  const graphNodes = nodes.map(n => {
    const prev = currentNodes.find(p => p.port === n.port);
    return {
      ...n,
      x: prev ? prev.x : dims().cx + (Math.random() - 0.5) * 400,
      y: prev ? prev.y : dims().cy + (Math.random() - 0.5) * 400,
      vx: prev ? prev.vx : 0,
      vy: prev ? prev.vy : 0,
      fx: prev ? prev.fx : null,
      fy: prev ? prev.fy : null,
    };
  });

  // Every node (alive or dead) connects to the leader/center
  const graphLinks = [];
  if (center) {
    graphNodes.forEach(n => {
      if (n.port !== center.port) {
        graphLinks.push({
          source: center.port,
          target: n.port,
          alive: n.alive,
        });
      }
    });
  }

  return { graphNodes, graphLinks, leader };
}

let particles = [];
let pid = 0;

function render(ev) {
  data = ev;
  const { nodes, logs, blockchain } = ev;
  const d = dims();

  // ── Detect active nodes ──────────────────────────────────────────
  const aliveNodes = nodes.filter(n => n.alive);
  const offlineCount = nodes.length - aliveNodes.length;

  if (aliveNodes.length > prevAliveCount) waitingForNodes = false;
  prevAliveCount = aliveNodes.length;

  // ── Waiting overlay ──────────────────────────────────────────────
  const waitingEl = document.getElementById("waiting-overlay");
  if (aliveNodes.length === 0) {
    document.getElementById("vis").style.display = "none";
    if (waitingEl) waitingEl.style.display = "flex";
    _setBadge(".leader-badge",  "0 LEADER");
    _setBadge(".follower-badge","0 FOLLOWER");
    _setBadge(".offline-badge", "0 OFFLINE");
    _setBlockBadge((blockchain.blocks||[]).length);
    renderChain(blockchain);
    renderLogs(logs, nodes);
    return;
  }
  document.getElementById("vis").style.display = "block";
  if (waitingEl) waitingEl.style.display = "none";

  // ── Update badges (every tick) ───────────────────────────────────
  const cL = aliveNodes.filter(n => n.role === "LEADER").length;
  const cF = aliveNodes.filter(n => n.role === "FOLLOWER").length;
  const cB = (blockchain.blocks || []).length;
  _setBadge(".leader-badge",  `${cL} LEADER`);
  _setBadge(".follower-badge",`${cF} FOLLOWER`);
  _setBadge(".offline-badge", `${offlineCount} OFFLINE`);
  _setBlockBadge(cB);
  const activeNum = document.getElementById("active-num");
  if (activeNum) activeNum.textContent = aliveNodes.length;

  // ── Build graph ONCE (first tick with nodes) ────────────────────
  if (!graphBuilt) {
    graphBuilt = true;
    const { graphNodes, graphLinks, leader } = buildGraph(aliveNodes);
    currentNodes = graphNodes;
    firstLoad = true;

    // Empty link layers (filled every tick)
    linkBgPath = linkBgG.selectAll("path").data([], d => d).join("path");
    linkPath = linkG.selectAll("path").data([], d => d).join("path");
    linkLabelText = linkLabelG.selectAll("text").data([], d => d).join("text");

    // Nodes
    const is10Node = graphNodes.length >= 8;
    const leaderR = 26;
    const fR = is10Node ? 16 : 20;
    const offs = is10Node ? 4 : 6;

    const items = graphNodes.map(n => ({
      ...n,
      r: n.role === "LEADER" ? leaderR : fR,
      iconSize: n.role === "LEADER" ? 14 : (is10Node ? 10 : 12),
      nameSize: n.role === "LEADER" ? 12 : (is10Node ? 10 : 11),
      roleSize: n.role === "LEADER" ? 9 : 8,
      nameDy: n.role === "LEADER" ? leaderR + offs + 14 : fR + offs + 12,
      roleDy: n.role === "LEADER" ? leaderR + offs + 28 : fR + offs + 24,
    }));

    const grp = nodeG.selectAll("g.n").data(items, n => n.port).join("g").attr("class","n");
    grp.selectAll("circle.ring").data(d => [d]).join("circle").attr("class","ring");
    grp.selectAll("circle.pulse").data(d => [d]).join("circle").attr("class","pulse");
    grp.selectAll("circle.body").data(d => [d]).join("circle").attr("class","body");
    grp.selectAll("circle.hl").data(d => [d]).join("circle").attr("class","hl");
    grp.selectAll("text.icon").data(d => [d]).join("text").attr("class","icon");
    grp.selectAll("text.name").data(d => [d]).join("text").attr("class","name");
    grp.selectAll("text.role").data(d => [d]).join("text").attr("class","role");

    // Tooltip
    grp.on("mouseenter", function(e, d) {
      const tip = document.getElementById("vis-tooltip");
      const c = COLORS[d.role] || COLORS.OFFLINE;
      tip.innerHTML = `<div style="display:flex;align-items:center;gap:8px;margin-bottom:6px">
        <span style="font-size:18px">${d.role === "LEADER" ? "★" : "●"}</span>
        <b style="font-size:14px">${d.label}</b>
        <span style="color:#64748b;font-size:11px">:${d.port}</span></div>
        <div style="display:grid;grid-template-columns:auto auto;gap:3px 16px;font-size:12px">
        <span style="color:#64748b">Role</span><span style="color:${c.fill||'#64748b'};font-weight:600">${d.role}</span>
        <span style="color:#64748b">Term</span><span>${d.term}</span>
        <span style="color:#64748b">Blocks</span><span>${d.blocks}</span>
        ${d.id_short ? `<span style="color:#64748b">Node ID</span><span style="font-family:monospace">${d.id_short}</span>` : ''}
        <span style="color:#64748b">Status</span><span style="color:${d.alive ? '#10b981' : '#f43f5e'}">${d.alive ? 'Online' : 'Offline'}</span>
      </div>`;
      const vr = document.getElementById("vis").getBoundingClientRect();
      tip.style.display = "block"; tip.style.left = "0px"; tip.style.top = "0px";
      const tb = tip.getBoundingClientRect();
      let tx = e.clientX - vr.left + 16, ty = e.clientY - vr.top - tb.height - 10;
      if (ty < 4) ty = e.clientY - vr.top + 20;
      if (tx + tb.width > vr.width - 10) tx = vr.width - tb.width - 10;
      tip.style.left = tx + "px"; tip.style.top = ty + "px";
      setTimeout(() => { tip.style.opacity = "1"; }, 10);
    }).on("mouseleave", function() {
      const tip = document.getElementById("vis-tooltip");
      tip.style.opacity = "0"; setTimeout(() => { tip.style.display = "none"; }, 200);
    });

    // Force simulation
    simulation = d3.forceSimulation(items)
      .force("center", d3.forceCenter(d.cx, d.cy).strength(0.06))
      .force("charge", d3.forceManyBody().strength(it => it.role === "LEADER" ? -1200 : -700))
      .force("link", d3.forceLink(graphLinks).id(l => l.port).distance(180).strength(0.35))
      .force("collision", d3.forceCollide().radius(it => it.r + 55))
      .alphaDecay(0.022)
      .on("tick", () => {
        grp.attr("transform", d => `translate(${d.x},${d.y})`);

        const pathFunc = l => {
          if (!l.source || !l.target) return "";
          const dx = l.target.x - l.source.x, dy = l.target.y - l.source.y;
          const dr = Math.sqrt(dx*dx + dy*dy) * 1.2;
          return `M${l.source.x},${l.source.y}A${dr},${dr} 0 0,1 ${l.target.x},${l.target.y}`;
        };

        linkBgPath.attr("d", pathFunc);
        linkPath.attr("d", pathFunc);
      });

    grp.call(drag(simulation));
    setTimeout(fitView, 300);
    firstLoad = false;
  }

  // ── Rebuild links EVERY tick (leader/node changes) ──────────────
  const getPort = (x) => (typeof x === "object" ? x.port : x);
  const { graphLinks } = buildGraph(nodes);
  // Update simulation links
  if (simulation) {
    simulation.force("link").links(graphLinks);
    simulation.alpha(0.1).restart();
  }

  // Alive links: green glow + solid cable
  linkBgPath = linkBgG.selectAll("path")
    .data(graphLinks.filter(l => l.alive), l => `${getPort(l.source)}-${getPort(l.target)}`)
    .join("path")
    .attr("stroke", "#10b981")
    .attr("stroke-width", 8)
    .attr("stroke-dasharray", "8 4")
    .attr("fill", "none")
    .attr("opacity", 0.2)
    .attr("class", "animated-link-bg");

  linkPath = linkG.selectAll("path")
    .data(graphLinks, l => `${getPort(l.source)}-${getPort(l.target)}`)
    .join("path")
    .attr("id", l => `link-${getPort(l.source)}-${getPort(l.target)}`)
    .attr("stroke", l => l.alive ? "#10b981" : "#f43f5e")
    .attr("stroke-width", l => l.alive ? 3 : 2)
    .attr("stroke-dasharray", l => l.alive ? "8 4" : "4 4")
    .attr("fill", "none")
    .attr("opacity", l => l.alive ? 0.85 : 0.5)
    .attr("marker-end", l => l.alive ? "url(#arrow)" : "url(#arrowOff)")
    .attr("class", l => l.alive ? "animated-link" : "");

  // ── Update node colors/text IN-PLACE every tick ─────────────────
  const simItems = simulation ? simulation.nodes() : [];
  nodeG.selectAll("g.n")
    .attr("transform", d => {
      const live = nodes.find(n => n.port === d.port);
      if (live) { d.role = live.role; d.term = live.term; d.alive = live.alive; d.blocks = live.blocks; }
      return `translate(${d.x},${d.y})`;
    })
    .each(function(d) {
      const g = d3.select(this);
      const roleColor = COLORS[d.role] || COLORS.OFFLINE;

      g.select("circle.ring")
        .attr("r", d.r + 4)
        .attr("stroke", d.alive ? (roleColor.ring || "#334155") : "#334155")
        .attr("stroke-width", d.role === "LEADER" ? 2 : 1.5)
        .attr("opacity", d.alive && d.role === "LEADER" ? 0.8 : 0.3)
        .attr("filter", d.role === "LEADER" ? "url(#glow)" : null);

      g.select("circle.pulse")
        .attr("r", d.role === "LEADER" ? d.r + 6 : d.r + 4)
        .attr("fill", roleColor.pulse || "transparent");

      g.select("circle.body")
        .attr("r", d.r)
        .attr("fill", d.alive ? (roleColor.fill || "#1e293b") : "#1e293b")
        .attr("opacity", d.alive ? 0.95 : 0.4);

      g.select("text.icon")
        .attr("fill", d.alive ? "#fff" : "#475569")
        .text(d.role === "LEADER" ? "★" : (d.role === "CANDIDATE" ? "⚡" : "●"));

      g.select("text.name")
        .attr("dy", d.nameDy)
        .attr("fill", d.alive ? "#e2e8f0" : "#475569")
        .text(d.label);

      g.select("text.role")
        .attr("dy", d.roleDy)
        .attr("fill", d.alive ? (d.role === "LEADER" ? "#10b981" : (d.role === "CANDIDATE" ? "#f59e0b" : "#60a5fa")) : "#475569")
        .text(d.role === "LEADER" ? `Leader T${d.term}` : (d.role === "CANDIDATE" ? "CANDIDATE" : "Follower"));
    });

  // ── Particles (every tick) ──
  const now = Date.now();
  const leaderNd = nodes.find(n => n.role === "LEADER" && n.alive);
  if (leaderNd) {
    const followers = nodes.filter(n => n !== leaderNd && n.alive);
    followers.forEach(f => {
      if (Math.random() < 0.12) {
        particles.push({ id: pid++, source: leaderNd.port, target: f.port, p: 0, born: now });
      }
    });
  }
  particles = particles.filter(p => now - p.born < 1600);
  particles.forEach(p => { p.p = (now - p.born) / 1600; });
  particleG.selectAll("circle").data(particles, p => p.id).join("circle")
    .attr("r", 3).attr("fill", "#10b981").attr("filter", "url(#glow)")
    .attr("opacity", p => Math.max(0, 1 - p.p * 1.5))
    .each(function(p) {
      const el = document.getElementById(`link-${p.source}-${p.target}`);
      let cx = 0, cy = 0;
      if (el) { try { const pt = el.getPointAtLength(p.p * el.getTotalLength()); cx = pt.x; cy = pt.y; } catch(e) {} }
      d3.select(this).attr("cx", cx).attr("cy", cy);
    });

  // ── Leader pulse animation (every tick) ──
  nodeG.selectAll("g.n").each(function(d) {
    if (d.role === "LEADER" && d.alive) {
      const p = d3.select(this).select("circle.pulse");
      if (p.size()) {
        p.attr("opacity", 0.25).attr("r", d.r + 6)
          .transition().duration(1600).ease(d3.easeCubicOut)
          .attr("r", d.r + 32).attr("opacity", 0)
          .on("end", function() { d3.select(this).attr("r", d.r + 6).attr("opacity", 0.25); });
      }
    }
  });

  renderChain(blockchain);
  renderLogs(logs, nodes);
}

function renderChain(bc) {
  const blocks = bc.blocks || [];
  const el = document.getElementById("chain-container");

  // Update header meta count
  const meta = document.querySelector(".chain-length");
  if (meta) meta.textContent = `${blocks.length} block${blocks.length !== 1 ? 's' : ''}`;

  if (!blocks.length) {
    el.innerHTML = '<div style="text-align:center;color:#2d3a50;padding:40px 20px;font-size:11px;font-family:\'JetBrains Mono\',monospace;letter-spacing:0.5px">— waiting for blocks —</div>';
    prevBlocks = 0; return;
  }
  const isNew = blocks.length > prevBlocks;
  let html = "";
  [...blocks].reverse().forEach((b, i) => {
    const fresh = isNew && i === 0;
    const genesis = b.index === "0" || i === blocks.length - 1;
    const hsh = (b.block_hash || "").substring(0, 16) + "…";
    const prev = (b.previous_hash || "").substring(0, 12) + "…";
    html += `<div class="block-card${fresh?' new':''}${genesis?' genesis':''}">
      <div class="idx">Block #${b.index}${genesis ? ' · GENESIS' : ''}</div>
      <div class="student">${b.student_name||'?'} <span style="color:var(--c-muted);font-weight:400;font-size:11px">(${b.student_id||'?'})</span></div>
      <div class="hash">${hsh} <span style="color:var(--c-dim)">←</span> ${prev}</div>
      <div class="time">${b.timestamp||''}</div>
    </div>`;
  });
  el.innerHTML = html;
  el.scrollTop = 0;
  prevBlocks = blocks.length;
}


function renderLogs(logs, nodes) {
  const tabs = document.getElementById("log-tabs");
  const content = document.getElementById("log-content");
  let th = `<button class="log-tab ${activeLog==='all'?'active':''}" data-p="all"><span class="dot" style="background:#64748b"></span>All</button>`;
  const aliveNodes = nodes.filter(n => n.alive);
  aliveNodes.forEach(n => {
    const c = COLORS[n.role]?.fill || "#94a3b8";
    th += `<button class="log-tab ${activeLog==n.port?'active':''}" data-p="${n.port}">
      <span class="dot" style="background:${c}"></span>${n.label}</button>`;
  });
  // Reset filter if selected node went offline
  if (activeLog !== "all" && !aliveNodes.some(n => n.port == activeLog)) {
    activeLog = "all";
  }
  tabs.innerHTML = th;
  tabs.querySelectorAll(".log-tab").forEach(b => {
    b.onclick = () => { activeLog = b.dataset.p; renderLogs(logs, nodes); };
  });
  let lines = [];
  if (activeLog === "all") {
    Object.entries(logs).forEach(([port, ls]) => {
      const n = nodes.find(x => x.port == port);
      ls.forEach(l => lines.push({ text: l, label: n?.label||port, role: n?.role }));
    });
  } else {
    const ls = logs[activeLog] || [];
    const n = nodes.find(x => x.port == activeLog);
    ls.forEach(l => lines.push({ text: l, label: n?.label||activeLog, role: n?.role }));
  }
  content.innerHTML = lines.slice(-80).map(l => {
    const cls = l.role ? l.role.toLowerCase() : "";
    const ts = l.text.match(/\[(\d{2}:\d{2}:\d{2})\]/)?.[1] || "";
    const rest = l.text.replace(/\[\d{2}:\d{2}:\d{2}\] /, "");
    return `<div class="log-line ${cls}"><span class="ts">${ts}</span><span style="color:var(--c-dim)">[${l.label}]</span> ${rest}</div>`;
  }).join("");
  content.scrollTop = content.scrollHeight;
}

// ── SSE ──
const evtSource = new EventSource("/events");
evtSource.onmessage = e => {
  try { render(JSON.parse(e.data)); } catch (x) { /* skip bad frames */ }
};
evtSource.onerror = () => {
  setTimeout(() => {
    fetch("/api/state").then(r => r.json()).then(render).catch(()=>{});
  }, 3000);
};

// ── Resize ──
function drawGrid() {} // Replaced by infinite SVG pattern grid, keeping empty to avoid resize errors

window.addEventListener("resize", () => {
  svg.attr("width", dims().w).attr("height", dims().h);
  if (data.nodes) render(data);
});

// ── Sidebar drag resize ──
const handle = document.getElementById("resize-handle");
const sidePanel = document.getElementById("side-panel");
let isDragging = false;

handle.addEventListener("mousedown", () => {
  isDragging = true;
  handle.classList.add("active");
  document.body.style.cursor = "col-resize";
  document.body.style.userSelect = "none";
});

document.addEventListener("mousemove", (e) => {
  if (!isDragging) return;
  const mainRect = document.getElementById("main").getBoundingClientRect();
  const mainWidth = mainRect.width;
  const handleWidth = 6;
  let panelWidth = mainWidth - (e.clientX - mainRect.left) - handleWidth;
  panelWidth = Math.max(280, Math.min(800, panelWidth));
  sidePanel.style.width = panelWidth + "px";
  sidePanel.style.maxWidth = "none";
});

document.addEventListener("mouseup", () => {
  if (isDragging) {
    isDragging = false;
    handle.classList.remove("active");
    document.body.style.cursor = "";
    document.body.style.userSelect = "";
    svg.attr("width", dims().w).attr("height", dims().h);
    if (data.nodes) render(data);
  }
});

// ── Upload Handler (side panel) ──
const uplBtnSide = document.getElementById("btn-upload-side");
if (uplBtnSide) {
  uplBtnSide.addEventListener("click", async () => {
    const file = document.getElementById("upl-file-side").files[0];
    const kode = document.getElementById("upl-kode-side").value.trim();
    const nama = document.getElementById("upl-nama-side").value.trim();
    const nim = document.getElementById("upl-nim-side").value.trim();
    const statusEl = document.getElementById("upl-status-side");

    if (!file) { statusEl.textContent = "❌ Pilih file"; statusEl.className = "upl-status-side err"; return; }
    if (!kode || !nama || !nim) { statusEl.textContent = "❌ Isi semua data"; statusEl.className = "upl-status-side err"; return; }

    uplBtnSide.disabled = true;
    uplBtnSide.classList.add("loading");
    statusEl.textContent = "📤 Uploading...";
    statusEl.className = "upl-status-side";

    try {
      const b64 = await new Promise((resolve, reject) => {
        const r = new FileReader();
        r.onload = () => resolve(r.result.split(",")[1]);
        r.onerror = reject;
        r.readAsDataURL(file);
      });
      const resp = await fetch("/api/register", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ filename: file.name, file_data: b64, kode, nama, nim }),
      });
      const result = await resp.json();
      if (result.status === "OK") {
        statusEl.textContent = `✅ ${result.message}`;
        statusEl.className = "upl-status-side ok";
        document.getElementById("upl-file-side").value = "";
        document.getElementById("upl-kode-side").value = "";
        document.getElementById("upl-nama-side").value = "";
        document.getElementById("upl-nim-side").value = "";
        // reset drop zone
        const dz = document.getElementById("file-drop-zone");
        const dl = document.getElementById("file-drop-label");
        if (dz) dz.classList.remove("has-file");
        if (dl) dl.innerHTML = 'Drop PDF or <u>browse</u>';
      } else {
        statusEl.textContent = `❌ ${result.message}`;
        statusEl.className = "upl-status-side err";
      }
    } catch (err) {
      statusEl.textContent = `❌ ${err.message}`;
      statusEl.className = "upl-status-side err";
    } finally {
      uplBtnSide.disabled = false;
      uplBtnSide.classList.remove("loading");
    }
  });
}

// ── UI Control Panel Actions ──
document.getElementById("toggle-edge-labels").onchange = (e) => {
  showEdgeLabels = e.target.checked;
  linkLabelG.selectAll("text").style("display", showEdgeLabels ? "block" : "none");
};

document.getElementById("btn-fit").onclick = () => {
  fitView();
};

document.getElementById("btn-refresh").onclick = () => {
  const d = dims();
  currentNodes.forEach(n => {
    n.x = d.cx + (Math.random() - 0.5) * 200;
    n.y = d.cy + (Math.random() - 0.5) * 200;
    n.vx = 0;
    n.vy = 0;
    n.fx = null;
    n.fy = null;
  });
  if (simulation) {
    simulation.alpha(1).restart();
  }
};

// ── File drop zone UX ───────────────────────────────────────
(function () {
  const dropZone = document.getElementById("file-drop-zone");
  const fileInput = document.getElementById("upl-file-side");
  const dropLabel = document.getElementById("file-drop-label");
  if (!dropZone || !fileInput || !dropLabel) return;

  function updateLabel(file) {
    if (file) {
      dropLabel.textContent = file.name;
      dropZone.classList.add("has-file");
    } else {
      dropLabel.innerHTML = 'Drop PDF or <u>browse</u>';
      dropZone.classList.remove("has-file");
    }
  }

  fileInput.addEventListener("change", () => {
    updateLabel(fileInput.files[0] || null);
  });

  dropZone.addEventListener("dragover", (e) => {
    e.preventDefault();
    dropZone.classList.add("over");
  });
  dropZone.addEventListener("dragleave", () => {
    dropZone.classList.remove("over");
  });
  dropZone.addEventListener("drop", (e) => {
    e.preventDefault();
    dropZone.classList.remove("over");
    const f = e.dataTransfer.files[0];
    if (f && f.type === "application/pdf") {
      // Inject into file input via DataTransfer
      const dt = new DataTransfer();
      dt.items.add(f);
      fileInput.files = dt.files;
      updateLabel(f);
    }
  });
})();

