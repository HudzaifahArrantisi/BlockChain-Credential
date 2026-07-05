// ── SecureChain Live D3.js Visualization ───────────────────────────────

const COLORS = {
  LEADER:   { fill: "#4ade80", stroke: "#22c55e", pulse: "rgba(74,222,128,0.4)" },
  FOLLOWER: { fill: "#60a5fa", stroke: "#3b82f6", pulse: "rgba(96,165,250,0.3)" },
  CANDIDATE:{ fill: "#fbbf24", stroke: "#f59e0b", pulse: "rgba(251,191,36,0.3)" },
  OFFLINE:  { fill: "#1e293b", stroke: "#334155", pulse: "transparent" },
};

let data = { nodes: [], logs: {}, blockchain: { blocks: [] } };
let activeLog = "all";
let prevBlocks = 0;

// ── Init D3 ────────────────────────────────────────────────────────────

const svg = d3.select("#vis").append("svg").attr("width","100%").attr("height","100%");

function dims() {
  const rect = document.getElementById("vis").getBoundingClientRect();
  return { w: rect.width, h: rect.height, cx: rect.width/2, cy: rect.height/2 };
}

// ── Bg + grid ──────────────────────────────────────────────────────────

svg.append("rect").attr("class","bg").attr("width","100%").attr("height","100%").attr("fill","#0a0e17");

let gridDots;
function drawGrid() {
  if (gridDots) gridDots.remove();
  const d = dims();
  const pts = [];
  for (let x = 0; x < d.w; x += 40) for (let y = 0; y < d.h; y += 40) pts.push({x,y});
  gridDots = svg.append("g").selectAll("circle").data(pts).join("circle")
    .attr("cx", p => p.x).attr("cy", p => p.y).attr("r", 1).attr("fill","#1e293b");
}
drawGrid();

// ── Layers ─────────────────────────────────────────────────────────────

const linkG = svg.append("g");
const particleG = svg.append("g");
const nodeG = svg.append("g");
const labelG = svg.append("g");

// ── Layout ─────────────────────────────────────────────────────────────

function layout(nodes) {
  const d = dims();
  const R = Math.min(d.w, d.h) * 0.32;
  const map = {};
  const alive = nodes.filter(n => n.alive);
  const leader = alive.find(n => n.role === "LEADER");
  const flw = alive.filter(n => n !== leader);
  const dead = nodes.filter(n => !n.alive);

  if (leader) map[leader.port] = { x: d.cx, y: d.cy, leader: true };

  flw.forEach((n, i) => {
    const a = (2 * Math.PI * i) / Math.max(flw.length,1) - Math.PI/2;
    map[n.port] = { x: d.cx + R * Math.cos(a), y: d.cy + R * Math.sin(a), leader: false };
  });

  dead.forEach((n, i) => {
    map[n.port] = { x: 60 + i * 75, y: d.h - 35, leader: false };
  });

  return map;
}

// ── Arc ────────────────────────────────────────────────────────────────

function arc(d) {
  const dx = d.tx - d.sx, dy = d.ty - d.sy;
  const dr = Math.sqrt(dx*dx + dy*dy) * 0.4;
  return `M${d.sx},${d.sy}A${dr},${dr} 0 0,1 ${d.tx},${d.ty}`;
}

// ── Particles ──────────────────────────────────────────────────────────

let particles = [];
let pid = 0;

function emitParticle(sx, sy, tx, ty) {
  particles.push({ id: pid++, sx, sy, tx, ty, p: 0 });
  setTimeout(() => { particles = particles.filter(x => x.id !== pid-1); }, 1500);
}

// ── Render ─────────────────────────────────────────────────────────────

function render(ev) {
  data = ev;
  const { nodes, logs, blockchain } = ev;
  const d = dims();
  const pos = layout(nodes);
  const leader = nodes.find(n => n.role === "LEADER");

  // ── Header ──
  const cL = nodes.filter(n => n.role === "LEADER").length;
  const cF = nodes.filter(n => n.role === "FOLLOWER").length;
  const cO = nodes.filter(n => !n.alive).length;
  const cB = (blockchain.blocks || []).length;
  document.querySelector(".leader-badge").textContent = `${cL} LEADER`;
  document.querySelector(".follower-badge").textContent = `${cF} FOLLOWER`;
  document.querySelector(".offline-badge").textContent = `${cO} OFFLINE`;
  document.querySelector(".block-badge").textContent = `${cB} BLOCKS`;

  // ── Links ──
  const links = [];
  const linkKeys = new Set();
  nodes.filter(n => n.alive).forEach(n => {
    const p = pos[n.port];
    if (!p) return;
    if (n.role === "LEADER") {
      nodes.filter(x => x.alive && x.role !== "LEADER").forEach(x => {
        const tp = pos[x.port];
        if (!tp) return;
        const k = `${p.x},${p.y}-${tp.x},${tp.y}`;
        const rk = `${tp.x},${tp.y}-${p.x},${p.y}`;
        if (!linkKeys.has(k) && !linkKeys.has(rk)) {
          linkKeys.add(k);
          links.push({ sx: p.x, sy: p.y, tx: tp.x, ty: tp.y });
        }
      });
    } else if (leader && pos[leader.port]) {
      const lp = pos[leader.port];
      links.push({ sx: lp.x, sy: lp.y, tx: p.x, ty: p.y });
    }
  });

  linkG.selectAll("path").data(links).join("path")
    .attr("d", arc)
    .attr("stroke", d => d.sx === pos[leader?.port]?.x ? "#2d4a3a" : "#1e293b")
    .attr("stroke-width", 1.5).attr("fill", "none").attr("opacity", 0.7);

  // ── Particles ──
  particles.forEach(p => { p.p += 0.04; });
  particleG.selectAll("circle").data(particles, p => p.id).join("circle")
    .attr("r", 3.5)
    .attr("fill", "#4ade80")
    .attr("opacity", p => Math.max(0, 1 - p.p * 1.2))
    .attr("cx", p => p.sx + (p.tx - p.sx) * p.p)
    .attr("cy", p => p.sy + (p.ty - p.sy) * p.p);

  // ── Nodes ──
  const items = nodes.map(n => ({ ...n, ...pos[n.port], r: n.role === "LEADER" ? 26 : 18 }));

  const grp = nodeG.selectAll("g.n").data(items, n => n.port).join("g").attr("class","n");

  // pulse ring
  grp.selectAll("circle.pulse").data(d => [d]).join("circle").attr("class","pulse")
    .attr("r", d => d.role === "LEADER" ? 34 : 24)
    .attr("fill", d => COLORS[d.role]?.pulse || "transparent")
    .attr("opacity", d => d.role === "LEADER" && d.alive ? 0.3 : 0);

  // body
  grp.selectAll("circle.body").data(d => [d]).join("circle").attr("class","body")
    .attr("r", d => d.r)
    .attr("fill", d => d.alive ? (COLORS[d.role]?.fill || "#1e293b") : "#1e293b")
    .attr("stroke", d => d.alive ? (COLORS[d.role]?.stroke || "#334155") : "#334155")
    .attr("stroke-width", 2);

  // label (icon)
  grp.selectAll("text.icon").data(d => [d]).join("text").attr("class","icon")
    .attr("text-anchor","middle").attr("dy","0.35em")
    .attr("fill", d => d.alive ? "#fff" : "#475569")
    .attr("font-size", d => d.role === "LEADER" ? 14 : 11)
    .attr("font-weight", 700)
    .text(d => d.role === "LEADER" ? "★" : d.label.replace("Node ",""));

  // sub label
  grp.selectAll("text.sub").data(d => [d]).join("text").attr("class","sub")
    .attr("text-anchor","middle")
    .attr("dy", d => d.role === "LEADER" ? 40 : 30)
    .attr("fill","#64748b").attr("font-size", 10)
    .text(d => d.role === "LEADER" ? `Leader · term ${d.term}` : d.role);

  // position
  grp.attr("transform", d => `translate(${d.x},${d.y})`);

  // ── Pulse animation ──
  grp.select(".pulse").each(function(d) {
    if (d.role === "LEADER" && d.alive) {
      d3.select(this)
        .attr("opacity", 0.3).attr("r", d.role === "LEADER" ? 34 : 24)
        .transition().duration(1500).ease(d3.easeCubicOut)
        .attr("r", 48).attr("opacity", 0)
        .on("end", function() { d3.select(this).attr("r", 34).attr("opacity", 0.3); });
    }
  });

  // ── Tooltip ──
  grp.on("mouseenter", function(e, d) {
    const tip = document.getElementById("vis-tooltip");
    tip.innerHTML = `<b>${d.label}</b> (${d.port})<br>
      Role: ${d.role}<br>
      Term: ${d.term}<br>
      Blocks: ${d.blocks}<br>
      ${d.id_short ? `ID: ${d.id_short}` : ''}`;
    tip.style.display = "block";
    tip.style.left = "0px";
    tip.style.top = "0px";
    const b = tip.getBoundingClientRect();
    const vis = document.getElementById("vis").getBoundingClientRect();
    tip.style.left = (e.clientX - vis.left + 12) + "px";
    tip.style.top = (e.clientY - vis.top - 10) + "px";
  }).on("mouseleave", function() {
    document.getElementById("vis-tooltip").style.display = "none";
  });

  // ── Blockchain ──
  renderChain(blockchain);

  // ── Logs ──
  renderLogs(logs, nodes);
}

// ── Blockchain ────────────────────────────────────────────────────────

function renderChain(bc) {
  const blocks = bc.blocks || [];
  const el = document.getElementById("chain-container");
  if (!blocks.length) {
    el.innerHTML = '<div style="text-align:center;color:#475569;padding:40px;font-size:13px">⛓️ Waiting for blocks...</div>';
    prevBlocks = 0; return;
  }
  const isNew = blocks.length > prevBlocks;
  let html = "";
  [...blocks].reverse().forEach((b, i) => {
    const fresh = isNew && i === 0;
    const genesis = b.index === "0" || i === blocks.length - 1;
    const hsh = (b.block_hash || "").substring(0, 14) + "...";
    const prev = (b.previous_hash || "").substring(0, 10) + "...";
    html += `<div class="block-card${fresh?' new':''}${genesis?' genesis':''}">
      <div class="idx">Block #${b.index} ${genesis ? '🌐 GENESIS' : ''}</div>
      <div class="student">${b.student_name||'?'} <span style="color:#94a3b8">(${b.student_id||'?'})</span></div>
      <div class="hash">${hsh} ← ${prev}</div>
      <div class="time">${b.timestamp||''}</div>
    </div>`;
  });
  el.innerHTML = html;
  el.scrollTop = 0;
  prevBlocks = blocks.length;
}

// ── Logs ──────────────────────────────────────────────────────────────

function renderLogs(logs, nodes) {
  const tabs = document.getElementById("log-tabs");
  const content = document.getElementById("log-content");

  let th = `<button class="log-tab ${activeLog==='all'?'active':''}" data-p="all">📋 All</button>`;
  nodes.forEach(n => {
    const c = n.alive ? (COLORS[n.role]?.fill || "#94a3b8") : "#f87171";
    th += `<button class="log-tab ${activeLog==n.port?'active':''} ${!n.alive?'offline':''}" data-p="${n.port}">
      <span class="dot" style="background:${c}"></span>${n.label}</button>`;
  });
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
    return `<div class="log-line ${cls}"><span class="ts">${ts}</span><span style="color:#64748b">[${l.label}]</span> ${rest}</div>`;
  }).join("");
  content.scrollTop = content.scrollHeight;
}

// ── SSE ───────────────────────────────────────────────────────────────

const evtSource = new EventSource("/events");
evtSource.onmessage = e => {
  try { render(JSON.parse(e.data)); } catch (x) { /* skip bad frames */ }
};
evtSource.onerror = () => {
  setTimeout(() => {
    fetch("/api/state").then(r => r.json()).then(render).catch(()=>{});
  }, 3000);
};

// ── Resize ────────────────────────────────────────────────────────────

window.addEventListener("resize", () => {
  svg.attr("width", dims().w).attr("height", dims().h);
  drawGrid();
  if (data.nodes) render(data);
});
