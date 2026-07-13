/**
 * NetworkGraph.js
 * Main graph component — ties vis-network, adapter, and SSE events together.
 * ForceAtlas2Based physics for organic node placement, then gentle damping.
 */
const NetworkGraph = (() => {

  // ── Internal state ──────────────────────────────────────────────
  let _network = null;
  let _nodeSet = null;
  let _edgeSet = null;
  let _container = null;
  let _visContainer = null;
  let _prevAliveCount = 0;
  let _particles = [];
  let _particleId = 0;
  let _particleAnimId = null;
  let _pulsePhase = 0;
  let _savedPositions = {};
  let _stabilized = false;

  // ── Config ──────────────────────────────────────────────────────
  const PARTICLE_SPEED = 0.008;
  const PARTICLE_SPAWN_RATE = 0.25;

  // ── Particle overlay (canvas on top of vis-network) ────────────
  let _pCanvas = null;
  let _pCtx = null;

  function _initParticleCanvas(container) {
    _pCanvas = document.createElement('canvas');
    _pCanvas.style.position = 'absolute';
    _pCanvas.style.top = '0';
    _pCanvas.style.left = '0';
    _pCanvas.style.width = '100%';
    _pCanvas.style.height = '100%';
    _pCanvas.style.pointerEvents = 'none';
    _pCanvas.style.zIndex = '2';
    container.appendChild(_pCanvas);
    _pCtx = _pCanvas.getContext('2d');
    _resizeParticleCanvas();
  }

  function _resizeParticleCanvas() {
    if (!_pCanvas || !_container) return;
    const rect = _container.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    _pCanvas.width = rect.width * dpr;
    _pCanvas.height = rect.height * dpr;
    _pCanvas.style.width = rect.width + 'px';
    _pCanvas.style.height = rect.height + 'px';
    _pCtx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }

  function _drawParticles() {
    if (!_pCtx || !_network) {
      _particleAnimId = null;
      return;
    }

    const canvasRect = _container.getBoundingClientRect();
    _pCtx.clearRect(0, 0, canvasRect.width, canvasRect.height);

    const scale = _network.getScale();

    // ── Leader radial pulse glow ──────────────────────────────────
    _pulsePhase += 0.02;
    const pulseIntensity = 0.5 + 0.5 * Math.sin(_pulsePhase);
    const leaderNode = _nodeSet.get().find(n => n._role === 'LEADER');
    if (leaderNode) {
      try {
        const pos = _network.getPositions([leaderNode.id]);
        if (pos && pos[leaderNode.id]) {
          const center = _network.canvasToDOM(pos[leaderNode.id]);
          const baseRadius = 28 * scale;
          const pulseRadius = baseRadius + pulseIntensity * 20 * scale;
          const gradient = _pCtx.createRadialGradient(
            center.x, center.y, baseRadius * 0.3,
            center.x, center.y, pulseRadius
          );
          gradient.addColorStop(0, `rgba(16,185,129,${0.2 * pulseIntensity})`);
          gradient.addColorStop(0.5, `rgba(16,185,129,${0.08 * pulseIntensity})`);
          gradient.addColorStop(1, 'rgba(16,185,129,0)');
          _pCtx.beginPath();
          _pCtx.arc(center.x, center.y, pulseRadius, 0, Math.PI * 2);
          _pCtx.fillStyle = gradient;
          _pCtx.fill();
        }
      } catch (e) { /* skip */ }
    }

    // ── Particles ─────────────────────────────────────────────────
    const aliveParticles = _particles.filter(p => p.progress < 1 && p.alive);

    for (const p of aliveParticles) {
      try {
        const fromPos = _network.getPositions([p.from]);
        const toPos = _network.getPositions([p.to]);
        if (!fromPos || !toPos || !fromPos[p.from] || !toPos[p.to]) {
          p.alive = false;
          continue;
        }

        const fx = fromPos[p.from].x;
        const fy = fromPos[p.from].y;
        const tx = toPos[p.to].x;
        const ty = toPos[p.to].y;

        const x = fx + (tx - fx) * p.progress;
        const y = fy + (ty - fy) * p.progress;

        const screenPos = _network.canvasToDOM({ x, y });

        const alpha = Math.max(0, 1 - p.progress * 1.5);
        const particleColors = [
          'rgba(16,185,129',
          'rgba(59,130,246',
          'rgba(250,204,21',
          'rgba(168,85,247',
        ];
        const baseColor = particleColors[p.colorIdx % particleColors.length];
        const r = Math.max(1.5, 2.5 * Math.sqrt(scale));
        _pCtx.beginPath();
        _pCtx.arc(screenPos.x, screenPos.y, r, 0, Math.PI * 2);
        _pCtx.fillStyle = `${baseColor}, ${alpha})`;
        _pCtx.fill();

        _pCtx.beginPath();
        _pCtx.arc(screenPos.x, screenPos.y, r * 2.5, 0, Math.PI * 2);
        _pCtx.fillStyle = `${baseColor}, ${alpha * 0.15})`;
        _pCtx.fill();
      } catch (e) { /* skip */ }
    }

    for (const p of _particles) {
      if (p.alive && p.progress < 1) p.progress += PARTICLE_SPEED;
    }
    _particles = _particles.filter(p => p.progress < 1 && p.alive);

    const hasWork = _particles.length > 0 || (leaderNode && leaderNode._role === 'LEADER');
    if (hasWork) {
      _particleAnimId = requestAnimationFrame(_drawParticles);
    } else {
      _particleAnimId = null;
    }
  }

  function _ensureParticleLoop() {
    if (!_particleAnimId) {
      _drawParticles();
    }
  }

  function _spawnParticles() {
    if (!_network) return;
    const nodes = _nodeSet.get();
    const alive = nodes.filter(n => n._alive);
    const leader = alive.find(n => n._role === 'LEADER');
    if (!leader) return;
    const followers = alive.filter(n => n.id !== leader.id);
    for (const f of followers) {
      if (Math.random() < PARTICLE_SPAWN_RATE) {
        _particles.push({
          id: _particleId++,
          from: leader.id,
          to: f.id,
          progress: 0,
          alive: true,
          colorIdx: Math.floor(Math.random() * 4),
        });
      }
    }
  }

  // ── Entry animation ─────────────────────────────────────────────
  function _applyEntryAnimation() {
    if (!_container) return;
    _container.style.opacity = '0';
    _container.style.transform = 'scale(0.96)';
    _container.style.transition = 'opacity 600ms ease-out, transform 600ms ease-out';
    requestAnimationFrame(() => {
      requestAnimationFrame(() => {
        _container.style.opacity = '1';
        _container.style.transform = 'scale(1)';
      });
    });
  }

  // ── Public API ──────────────────────────────────────────────────

  /**
   * Initialize the graph inside a DOM container.
   * @param {string|Element} containerOrId - The #vis container element or its ID.
   */
  function init(containerOrId) {
    _container = typeof containerOrId === 'string'
      ? document.getElementById(containerOrId)
      : containerOrId;

    if (!_container) throw new Error('NetworkGraph: container not found');

    _container.style.width = '100%';
    _container.style.height = '100%';
    _container.style.position = 'relative';

    _visContainer = document.createElement('div');
    _visContainer.style.width = '100%';
    _visContainer.style.height = '100%';
    _visContainer.style.position = 'absolute';
    _visContainer.style.top = '0';
    _visContainer.style.left = '0';
    _container.appendChild(_visContainer);

    // Create with physics-enabled layout (forceAtlas2Based) for organic feel
    const result = UseVisNetwork.create(
      _visContainer,
      [],
      [],
      {
        physics: {
          enabled: true,
          stabilization: { enabled: true, iterations: 250, fit: true },
          solver: 'forceAtlas2Based',
          forceAtlas2Based: {
            gravitationalConstant: -48,
            centralGravity: 0.006,
            springLength: 200,
            springConstant: 0.035,
            damping: 0.55,
            avoidOverlap: 0.4,
          },
          adaptiveTimestep: true,
        },
        interaction: { hover: true, tooltipDelay: 150 },
      }
    );

    _network = result.network;
    _nodeSet = result.nodes;
    _edgeSet = result.edges;

    _initParticleCanvas(_container);
    _applyEntryAnimation();

    // ── Stabilization: save positions when physics settles ─────────
    _network.once('stabilizationIterationsDone', () => {
      _stabilized = true;
      _saveAllPositions();
      // Keep physics running but with high damping for minor adjustments
      _network.setOptions({
        physics: {
          enabled: true,
          forceAtlas2Based: { damping: 0.7 },
          minVelocity: 0.5,
          stabilization: { enabled: false },
        },
      });
    });

    // ── Events ──────────────────────────────────────────────────
    _network.on('click', function (params) {
      if (params.nodes.length > 0) {
        const nodeId = params.nodes[0];
        UseVisNetwork.focusNode(_network, nodeId, 1.5);
      }
    });

    _network.on('doubleClick', function (params) {
      if (params.nodes.length > 0) {
        const nodeId = params.nodes[0];
        UseVisNetwork.focusNode(_network, nodeId, 2);
      } else {
        UseVisNetwork.fitView(_network, 500);
      }
    });

    // Save positions after drag — they persist across SSE updates
    _network.on('dragEnd', function () {
      _saveAllPositions();
    });

    // Resize handler
    window.addEventListener('resize', () => {
      _network.fit({ animation: false });
      _resizeParticleCanvas();
    });

    _ensureParticleLoop();

    console.log('[NetworkGraph] Initialized with ForceAtlas2 physics');
  }

  function _saveAllPositions() {
    if (!_network) return;
    try {
      const allIds = _nodeSet.getIds();
      if (allIds.length === 0) return;
      const allPos = _network.getPositions(allIds);
      for (const [id, pos] of Object.entries(allPos)) {
        if (pos) _savedPositions[id] = { x: pos.x, y: pos.y };
      }
    } catch (e) { /* skip */ }
  }

  /**
   * Update the graph with new data from SSE.
   * Only visual state (color, border, role) changes — nodes never move.
   * @param {object} data - { nodes, logs, blockchain }
   */
  function update(data) {
    if (!_network) return;
    const { nodes } = data;
    const alive = nodes.filter(n => n.alive);

    // ── Show/hide waiting overlay ────────────────────────────────
    if (alive.length > _prevAliveCount) {
      const overlay = document.getElementById('waiting-overlay');
      if (overlay) overlay.style.display = 'none';
    }
    _prevAliveCount = alive.length;

    const visEl = document.getElementById('vis');
    if (visEl) {
      visEl.style.display = alive.length ? 'block' : 'none';
      if (!alive.length) {
        const overlay = document.getElementById('waiting-overlay');
        if (overlay) overlay.style.display = 'flex';
      }
    }

    // ── Update vis-network nodes/edges ──────────────────────────
    const visNodes = GraphAdapter.toVisNodes(nodes);
    const currentDsNodes = _nodeSet.get();
    const existingIds = currentDsNodes.map(n => n.id);

    // Only fix positions for nodes that have already been placed.
    // New nodes (first appearance) intentionally get NO x/y so physics
    // settles them organically.
    for (const vn of visNodes) {
      const saved = _savedPositions[vn.id];
      if (saved) {
        vn.x = saved.x;
        vn.y = saved.y;
      }
      // else: brand-new node — let physics place it
    }

    const visEdges = GraphAdapter.toAllEdges(nodes);
    UseVisNetwork.setNodes(_nodeSet, visNodes);
    UseVisNetwork.setEdges(_edgeSet, visEdges);

    _spawnParticles();
    _ensureParticleLoop();
  }

  /**
   * Fit the graph to the viewport.
   */
  function fit() {
    if (!_network) return;
    UseVisNetwork.fitView(_network, 400);
  }

  function highlightNode(port, state) {
    if (!_nodeSet) return;
    const node = _nodeSet.get(port);
    if (!node) return;
    const role = node._role || 'OFFLINE';
    const c = GraphAdapter.COLORS[role] || GraphAdapter.COLORS.OFFLINE;
    const isLeader = role === 'LEADER';
    const updates = { id: port };
    if (state === 'active') {
      updates.color = { border: '#34d399', highlight: { border: '#6ee7b7' } };
      updates.borderWidth = isLeader ? 4 : 2.5;
      updates.shadow = { enabled: true, color: 'rgba(16,185,129,0.4)', size: 20, x: 0, y: 0 };
    } else if (state === 'done') {
      updates.color = { border: '#10b981', highlight: { border: '#10b981' } };
      updates.borderWidth = isLeader ? 3 : 1.5;
      updates.shadow = { enabled: true, color: 'rgba(16,185,129,0.15)', size: 12, x: 0, y: 0 };
    } else {
      const p = isLeader ? { shadow: { enabled: true, color: 'rgba(16,185,129,0.6)', size: 32, x: 0, y: 0 } } : { shadow: false };
      updates.color = { background: c.fill, border: c.border, highlight: { background: c.fill, border: '#f0f4ff' } };
      updates.borderWidth = isLeader ? 3 : 1.5;
      updates.shadow = p.shadow;
    }
    _nodeSet.update(updates);
  }

  function highlightEdge(edgeId, state) {
    if (!_edgeSet) return;
    const edge = _edgeSet.get(edgeId);
    if (!edge) return;
    const updates = { id: edgeId };
    if (state === 'active') {
      updates.color = { color: '#34d399', highlight: '#34d399', hover: '#34d399' };
      updates.width = 3;
      updates.dashes = false;
    } else if (state === 'done') {
      updates.color = { color: '#10b981', opacity: 0.35 };
      updates.width = 2;
      updates.dashes = false;
    } else {
      const isAlive = edge._alive;
      updates.color = { color: isAlive ? '#10b981' : '#f43f5e', highlight: isAlive ? '#34d399' : '#fb7185', hover: isAlive ? '#10b981' : '#f43f5e', opacity: isAlive ? 0.85 : 0.4 };
      updates.width = isAlive ? 2 : 1;
      updates.dashes = isAlive ? false : [4, 4];
    }
    _edgeSet.update(updates);
  }

  function resetHighlights() {
    if (!_nodeSet || !_edgeSet) return;
    if (window.data && window.data.nodes) {
      const visNodes = GraphAdapter.toVisNodes(window.data.nodes);
      const current = _nodeSet.get();
      for (const vn of visNodes) {
        const cur = current.find(n => n.id === vn.id);
        if (cur) { vn.x = cur.x; vn.y = cur.y; }
      }
      UseVisNetwork.setNodes(_nodeSet, visNodes);
      const visEdges = GraphAdapter.toAllEdges(window.data.nodes);
      UseVisNetwork.setEdges(_edgeSet, visEdges);
    }
  }

  function refresh() {
    if (!_network) return;
    _stabilized = false;
    _savedPositions = {};
    UseVisNetwork.stabilize(_network, 150);
    setTimeout(() => {
      if (_network) {
        _saveAllPositions();
        _stabilized = true;
        UseVisNetwork.fitView(_network, 400);
      }
    }, 3000);
  }

  function spawnParticlesBurst(fromPort, toPorts, count) {
    if (!_network || !_nodeSet) return;
    for (const toPort of toPorts) {
      for (let i = 0; i < (count || 6); i++) {
        _particles.push({
          id: _particleId++,
          from: fromPort,
          to: toPort,
          progress: Math.random() * 0.3,
          alive: true,
          colorIdx: Math.floor(Math.random() * 4),
        });
      }
    }
    _ensureParticleLoop();
  }

  /**
   * Clean up.
   */
  function destroy() {
    if (_particleAnimId) cancelAnimationFrame(_particleAnimId);
    UseVisNetwork.destroy(_network);
    _network = null;
    _nodeSet = null;
    _edgeSet = null;
    _particles = [];
    _particleAnimId = null;
    _savedPositions = {};
  }

  return {
    init,
    update,
    fit,
    refresh,
    destroy,
    highlightNode,
    highlightEdge,
    resetHighlights,
    spawnParticlesBurst,
  };
})();
