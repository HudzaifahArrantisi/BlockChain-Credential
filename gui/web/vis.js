const COLORS = {
  LEADER:   { fill: "#10b981", stroke: "#047857", pulse: "rgba(16, 185, 129, 0.25)", ring: "#10b981" },
  FOLLOWER: { fill: "#3b82f6", stroke: "#1d4ed8", pulse: "rgba(59, 130, 246, 0.15)", ring: "#3b82f6" },
  CANDIDATE:{ fill: "#f59e0b", stroke: "#b45309", pulse: "rgba(245, 158, 11, 0.15)", ring: "#f59e0b" },
  OFFLINE:  { fill: "#1e293b", stroke: "#0f172a", pulse: "transparent", ring: "#334155" },
};

let data = { nodes: [], logs: {}, blockchain: { blocks: [] } };
let activeLog = "all";
let prevBlocks = 0;
let firstLoad = true;

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

const linkG = gMain.append("g");
const linkLabelG = gMain.append("g");
const particleG = gMain.append("g");
const nodeG = gMain.append("g");

let simulation = null;
let currentNodes = [];
let showEdgeLabels = true;

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
  const flw = alive.filter(n => n !== leader);
  const dead = nodes.filter(n => !n.alive);

  // Preserve positions across polls
  const graphNodes = nodes.map(n => {
    const prev = currentNodes.find(p => p.port === n.port);
    return {
      ...n,
      x: prev ? prev.x : dims().cx + (Math.random() - 0.5) * 200,
      y: prev ? prev.y : dims().cy + (Math.random() - 0.5) * 200,
      vx: prev ? prev.vx : 0,
      vy: prev ? prev.vy : 0,
      fx: prev ? prev.fx : null,
      fy: prev ? prev.fy : null,
    };
  });

  const graphLinks = [];
  if (leader) {
    flw.forEach(f => {
      graphLinks.push({
        source: leader.port,
        target: f.port,
        type: "leads",
        label: "LEADS",
      });
    });
  }
  dead.forEach(d => {
    alive.forEach(a => {
      graphLinks.push({
        source: a.port,
        target: d.port,
        type: "disconnected",
        label: "LOST",
      });
    });
  });

  return { graphNodes, graphLinks, leader, flw, dead };
}

let particles = [];
let pid = 0;

function render(ev) {
  data = ev;
  const { nodes, logs, blockchain } = ev;
  const d = dims();

  const cL = nodes.filter(n => n.role === "LEADER").length;
  const cF = nodes.filter(n => n.role === "FOLLOWER").length;
  const cO = nodes.filter(n => !n.alive).length;
  const cB = (blockchain.blocks || []).length;
  document.querySelector(".leader-badge").innerHTML = `<span class="dot"></span>${cL} LEADER`;
  document.querySelector(".follower-badge").innerHTML = `<span class="dot"></span>${cF} FOLLOWER`;
  document.querySelector(".offline-badge").innerHTML = `<span class="dot"></span>${cO} OFFLINE`;
  document.querySelector(".block-badge").innerHTML = `<span class="icon">⬡</span>${cB} BLOCKS`;

  const { graphNodes, graphLinks, leader } = buildGraph(nodes);
  currentNodes = graphNodes;

  const getPort = (x) => (typeof x === "object" ? x.port : x);

  // ── Links ──
  const linkPath = linkG.selectAll("path").data(graphLinks, l => `${getPort(l.source)}-${getPort(l.target)}`).join("path");
  linkPath
    .attr("id", l => `link-${getPort(l.source)}-${getPort(l.target)}`)
    .attr("stroke", l => l.type === "leads" ? "#10b981" : "#f43f5e")
    .attr("stroke-width", l => l.type === "leads" ? 1.5 : 1)
    .attr("stroke-dasharray", l => l.type === "disconnected" ? "3 3" : "none")
    .attr("fill", "none")
    .attr("opacity", l => l.type === "leads" ? 0.4 : 0.15)
    .attr("marker-end", l => l.type === "leads" ? "url(#arrow)" : "url(#arrowOff)");

  // ── Link labels ──
  const linkLabelText = linkLabelG.selectAll("text").data(graphLinks.filter(l => l.type === "leads"), l => `${getPort(l.source)}-${getPort(l.target)}`).join("text")
    .attr("text-anchor","middle")
    .attr("fill","#64748b")
    .attr("font-size", 8)
    .attr("font-weight", 600)
    .attr("opacity", 0.7)
    .style("display", showEdgeLabels ? "block" : "none")
    .text(l => l.label);

  // ── Nodes ──
  const is10Node = graphNodes.length >= 8;
  const leaderR = 26;
  const fR = is10Node ? 16 : 20;
  const offset = is10Node ? 4 : 6;

  const items = graphNodes.map(n => ({
    ...n,
    r: n.role === "LEADER" ? leaderR : fR,
    iconSize: n.role === "LEADER" ? 14 : (is10Node ? 10 : 12),
    nameSize: n.role === "LEADER" ? 12 : (is10Node ? 10 : 11),
    roleSize: n.role === "LEADER" ? 9 : 8,
    nameDy: n.role === "LEADER" ? leaderR + offset + 14 : fR + offset + 12,
    roleDy: n.role === "LEADER" ? leaderR + offset + 28 : fR + offset + 24,
  }));

  const grp = nodeG.selectAll("g.n").data(items, n => n.port).join("g").attr("class","n");

  // Outer ring
  grp.selectAll("circle.ring").data(d => [d]).join("circle").attr("class","ring")
    .attr("r", d => d.r + 4)
    .attr("fill", "none")
    .attr("stroke", d => d.alive ? COLORS[d.role]?.ring || "#334155" : "#334155")
    .attr("stroke-width", d => d.role === "LEADER" ? 2 : 1.5)
    .attr("opacity", d => d.alive && d.role === "LEADER" ? 0.8 : 0.3)
    .attr("filter", d => d.role === "LEADER" ? "url(#glow)" : null);

  // Pulse (Leader only glows dynamically)
  grp.selectAll("circle.pulse").data(d => [d]).join("circle").attr("class","pulse")
    .attr("r", d => d.role === "LEADER" ? d.r + 6 : d.r + 4)
    .attr("fill", d => COLORS[d.role]?.pulse || "transparent");

  // Body
  const body = grp.selectAll("circle.body").data(d => [d]).join("circle").attr("class","body");
  body.attr("r", d => d.r)
    .attr("fill", d => d.alive ? COLORS[d.role]?.fill || "#1e293b" : "#1e293b")
    .attr("stroke", "rgba(255, 255, 255, 0.15)")
    .attr("stroke-width", 2)
    .attr("filter", "url(#shadow)");
  body.filter(d => d.alive)
    .attr("opacity", 0.95);

  // Highlight
  grp.selectAll("circle.hl").data(d => [d]).join("circle").attr("class","hl")
    .attr("r", d => d.r - 2)
    .attr("fill", "url(#nodeBg)")
    .attr("opacity", 0.5);

  // Icon
  grp.selectAll("text.icon").data(d => [d]).join("text").attr("class","icon")
    .attr("text-anchor","middle").attr("dy","0.35em")
    .attr("fill", d => d.alive ? "#fff" : "#475569")
    .attr("font-size", d => d.iconSize)
    .attr("font-weight", 700)
    .text(d => d.role === "LEADER" ? "★" : (d.role === "CANDIDATE" ? "⚡" : "●"));

  // Name
  grp.selectAll("text.name").data(d => [d]).join("text").attr("class","name")
    .attr("text-anchor","middle")
    .attr("dy", d => d.nameDy)
    .attr("fill", d => d.alive ? "#e2e8f0" : "#475569")
    .attr("font-size", d => d.nameSize)
    .attr("font-weight", 600)
    .attr("font-family", "'Outfit', sans-serif")
    .text(d => d.label);

  // Role
  grp.selectAll("text.role").data(d => [d]).join("text").attr("class","role")
    .attr("text-anchor","middle")
    .attr("dy", d => d.roleDy)
    .attr("fill","#64748b")
    .attr("font-size", d => d.roleSize)
    .text(d => d.role === "LEADER" ? `Leader T${d.term}` : (d.role === "CANDIDATE" ? "CANDIDATE" : "Follower"));

  // Tooltip
  grp.on("mouseenter", function(e, d) {
    const tip = document.getElementById("vis-tooltip");
    tip.innerHTML = `
      <div style="display:flex;align-items:center;gap:8px;margin-bottom:6px">
        <span style="font-size:18px">${d.role === "LEADER" ? "★" : "●"}</span>
        <b style="font-size:14px">${d.label}</b>
        <span style="color:#64748b;font-size:11px">:${d.port}</span>
      </div>
      <div style="display:grid;grid-template-columns:auto auto;gap:3px 16px;font-size:12px">
        <span style="color:#64748b">Role</span><span style="color:${COLORS[d.role]?.fill||'#64748b'};font-weight:600">${d.role}</span>
        <span style="color:#64748b">Term</span><span>${d.term}</span>
        <span style="color:#64748b">Blocks</span><span>${d.blocks}</span>
        ${d.id_short ? `<span style="color:#64748b">Node ID</span><span style="font-family:monospace">${d.id_short}</span>` : ''}
        <span style="color:#64748b">Status</span><span style="color:${d.alive ? '#10b981' : '#f43f5e'}">${d.alive ? 'Online' : 'Offline'}</span>
      </div>`;
    const vr = document.getElementById("vis").getBoundingClientRect();
    tip.style.display = "block";
    tip.style.left = "0px";
    tip.style.top = "0px";
    const tb = tip.getBoundingClientRect();
    let tx = e.clientX - vr.left + 16;
    let ty = e.clientY - vr.top - tb.height - 10;
    if (ty < 4) ty = e.clientY - vr.top + 20;
    if (tx + tb.width > vr.width - 10) tx = vr.width - tb.width - 10;
    tip.style.left = tx + "px";
    tip.style.top = ty + "px";
    setTimeout(() => { tip.style.opacity = "1"; }, 10);
  }).on("mouseleave", function() {
    const tip = document.getElementById("vis-tooltip");
    tip.style.opacity = "0";
    setTimeout(() => { tip.style.display = "none"; }, 200);
  });

  // ── Force simulation ──
  if (simulation) simulation.stop();

  const centerX = d.cx;
  const centerY = d.cy;

  simulation = d3.forceSimulation(items)
    .force("center", d3.forceCenter(centerX, centerY).strength(0.06))
    .force("charge", d3.forceManyBody().strength(d => d.role === "LEADER" ? -1200 : -700))
    .force("link", d3.forceLink(graphLinks).id(l => l.port).distance(180).strength(0.35))
    .force("collision", d3.forceCollide().radius(d => d.r + 55))
    .alphaDecay(0.022)
    .on("tick", () => {
      grp.attr("transform", d => {
        // Keep nodes slightly within margins but let them float organically
        return `translate(${d.x},${d.y})`;
      });

      // Curved link equations
      linkPath.attr("d", l => {
        if (!l.source || !l.target) return "";
        const sx = l.source.x, sy = l.source.y;
        const tx = l.target.x, ty = l.target.y;
        const dx = tx - sx, dy = ty - sy;
        const dr = Math.sqrt(dx*dx + dy*dy) * 1.2; // organic Mirofish arc sweep
        return `M${sx},${sy}A${dr},${dr} 0 0,1 ${tx},${ty}`;
      });

      // Align link labels along the arcs and rotate them
      linkLabelText.attr("transform", l => {
        if (!l.source || !l.target) return "";
        const sx = l.source.x, sy = l.source.y;
        const tx = l.target.x, ty = l.target.y;
        const mx = (sx + tx) / 2;
        const my = (sy + ty) / 2;
        
        let angle = Math.atan2(ty - sy, tx - sx) * 180 / Math.PI;
        if (angle > 90 || angle < -90) angle += 180; // Keep text readable upright
        
        const dx = tx - sx, dy = ty - sy;
        const len = Math.sqrt(dx*dx + dy*dy);
        if (len === 0) return `translate(${mx},${my})`;
        const px = -dy / len;
        const py = dx / len;
        const offset = len * 0.08; // perpendicular offset to sit above curve
        
        return `translate(${mx + px * offset},${my + py * offset}) rotate(${angle})`;
      });
    });

  // Enable dragging on nodes
  grp.call(drag(simulation));

  // ── Particles flowing from leader to followers ──
  const now = Date.now();
  if (leader) {
    const leaderNode = items.find(n => n.port === leader.port);
    if (leaderNode && graphLinks.length) {
      graphLinks.filter(l => l.type === "leads").forEach(l => {
        if (Math.random() < 0.12) {
          const tgt = items.find(n => n.port === getPort(l.target));
          if (tgt) {
            particles.push({
              id: pid++,
              source: getPort(l.source),
              target: getPort(l.target),
              p: 0, born: now,
            });
          }
        }
      });
    }
  }

  // Filter out dead particles
  particles = particles.filter(p => now - p.born < 1600);
  particles.forEach(p => { p.p = (now - p.born) / 1600; });

  // Update particles positions along the curved SVG path lines
  particleG.selectAll("circle").data(particles, p => p.id).join("circle")
    .attr("r", 3)
    .attr("fill", "#10b981")
    .attr("filter", "url(#glow)")
    .attr("opacity", p => Math.max(0, 1 - p.p * 1.5))
    .each(function(p) {
      const pathEl = document.getElementById(`link-${p.source}-${p.target}`);
      let cx = 0, cy = 0;
      if (pathEl) {
        try {
          const totalLen = pathEl.getTotalLength();
          const pt = pathEl.getPointAtLength(p.p * totalLen);
          cx = pt.x;
          cy = pt.y;
        } catch (err) {
          const srcNode = items.find(n => n.port === p.source);
          const tgtNode = items.find(n => n.port === p.target);
          if (srcNode && tgtNode) {
            cx = srcNode.x + (tgtNode.x - srcNode.x) * p.p;
            cy = srcNode.y + (tgtNode.y - srcNode.y) * p.p;
          }
        }
      }
      d3.select(this).attr("cx", cx).attr("cy", cy);
    });

  // Leader dynamic pulsing animation
  grp.select(".pulse").each(function(d) {
    if (d.role === "LEADER" && d.alive) {
      d3.select(this)
        .attr("opacity", 0.25).attr("r", d.r + 6)
        .transition().duration(1600).ease(d3.easeCubicOut)
        .attr("r", d.r + 32).attr("opacity", 0)
        .on("end", function() {
          d3.select(this).attr("r", d.r + 6).attr("opacity", 0.25);
        });
    }
  });

  renderChain(blockchain);
  renderLogs(logs, nodes);

  // Trigger fitView once on first load
  if (firstLoad && graphNodes.length > 0) {
    setTimeout(() => {
      fitView();
    }, 200);
    firstLoad = false;
  }
}

function renderChain(bc) {
  const blocks = bc.blocks || [];
  const el = document.getElementById("chain-container");
  if (!blocks.length) {
    el.innerHTML = '<div style="text-align:center;color:#475569;padding:50px;font-size:12px">⛓️ Waiting for blocks...</div>';
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
      <div class="student">${b.student_name||'?'} <span style="color:#64748b;font-weight:400">(${b.student_id||'?'})</span></div>
      <div class="hash">${hsh} <span style="color:#334155">←</span> ${prev}</div>
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
  nodes.forEach(n => {
    const c = n.alive ? (COLORS[n.role]?.fill || "#94a3b8") : "#f43f5e";
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
    return `<div class="log-line ${cls}"><span class="ts">${ts}</span><span style="color:#475569">[${l.label}]</span> ${rest}</div>`;
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
