/**
 * graphAdapter.js
 * Converts SSE node data (Raft consensus) into vis-network nodes/edges format.
 * No dependencies on vis-network — pure data transformation.
 */
const GraphAdapter = (() => {
  const COLORS = {
    LEADER:    { fill: '#10b981', stroke: '#047857', border: '#34d399' },
    FOLLOWER:  { fill: '#2563eb', stroke: '#1d4ed8', border: '#60a5fa' },
    CANDIDATE: { fill: '#f59e0b', stroke: '#b45309', border: '#fbbf24' },
    OFFLINE:   { fill: '#1e293b', stroke: '#0f172a', border: '#475569' },
  };

  const ROLE_ORDER = { LEADER: 0, CANDIDATE: 1, FOLLOWER: 2, OFFLINE: 3 };

  // Fixed layout parameters — nodes never move once placed
  const SPACING_X = 190;
  const BASE_Y = 280;

  function color(role) {
    return COLORS[role] || COLORS.OFFLINE;
  }

  function makeTooltip(n) {
    const c = color(n.role);
    const lines = [
      `<b style="color:${c.fill}">${n.label}</b> <span style="color:#64748b;font-size:10px">:${n.port}</span>`,
      `Role: <span style="color:${c.fill}">${n.role}</span>`,
      `Term: ${n.term}`,
      `Blocks: ${n.blocks}`,
    ];
    if (n.id_short) lines.push(`Node ID: ${n.id_short}`);
    lines.push(`Status: <span style="color:${n.alive ? '#10b981' : '#f43f5e'}">${n.alive ? 'Online' : 'Offline'}</span>`);
    return lines.join('<br>');
  }

  function _computePositions(nodes) {
    const sorted = [...new Set(nodes.map(n => n.port))].sort((a, b) => a - b);
    const total = sorted.length;
    const startX = Math.max(400, total * SPACING_X / 2 + 80);
    return sorted.map((port, idx) => ({
      port,
      x: startX + (idx - (total - 1) / 2) * SPACING_X,
      y: BASE_Y + (idx % 2 === 0 ? -35 : 35),
    }));
  }

  /**
   * Convert a raw SSE node array to vis-network nodes (DataSet-compatible).
   * Every node gets a fixed position — no physics, no movement.
   */
  function toVisNodes(nodes) {
    const positions = _computePositions(nodes);
    return nodes.map(n => {
      const c = color(n.role);
      const isLeader = n.role === 'LEADER';
      const alive = n.alive;
      const pos = positions.find(p => p.port === n.port) || { x: 400, y: 280 };
      return {
        id: n.port,
        label: n.label,
        title: makeTooltip(n),
        x: pos.x,
        y: pos.y,
        size: isLeader ? 30 : 22,
        borderWidth: isLeader ? 3 : 1.5,
        borderWidthSelected: isLeader ? 4 : 3,
        color: {
          background: alive ? c.fill : COLORS.OFFLINE.fill,
          border: alive ? c.border : COLORS.OFFLINE.border,
          highlight: {
            background: alive ? c.fill : COLORS.OFFLINE.fill,
            border: alive ? '#f0f4ff' : COLORS.OFFLINE.border,
          },
        },
        font: {
          color: alive ? '#e2e8f0' : '#475569',
          size: 10,
          face: 'Inter, sans-serif',
          strokeWidth: 0,
        },
        shape: 'circle',
        shadow: isLeader ? { enabled: true, color: 'rgba(16,185,129,0.6)', size: 32, x: 0, y: 0 } : false,
        physics: false,
        // Custom data kept for edge filtering and particle spawning
        _role: n.role,
        _alive: n.alive,
        _term: n.term,
        _blocks: n.blocks,
        _label: n.label,
      };
    });
  }

  /**
   * Build edges from leader to each other alive node.
   * Returns vis-network edge objects.
   */
  function toVisEdges(nodes) {
    const alive = nodes.filter(n => n.alive);
    const leader = alive.find(n => n.role === 'LEADER');
    if (!leader) return [];
    return alive
      .filter(n => n.port !== leader.port)
      .map(n => ({
        id: `${leader.port}-${n.port}`,
        from: leader.port,
        to: n.port,
        color: { color: '#10b981', highlight: '#34d399', hover: '#10b981' },
        width: 2,
        widthSelectionIncrease: 1,
        dashes: false,
        smooth: { enabled: true, type: 'curvedCW', roundness: 0.1 },
        arrowStrikethrough: false,
        arrows: {
          to: { enabled: true, scaleFactor: 0.8 },
        },
        _alive: true,
      }));
  }

  /**
   * Build edges that also include offline nodes (shown as dashed red).
   * Used for showing disconnected nodes.
   */
  function toAllEdges(nodes) {
    const alive = nodes.filter(n => n.alive);
    const leader = alive.find(n => n.role === 'LEADER');
    if (!leader) return [];
    return nodes
      .filter(n => n.port !== leader.port)
      .map(n => {
        const isAlive = n.alive;
        return {
          id: `${leader.port}-${n.port}`,
          from: leader.port,
          to: n.port,
          color: {
            color: isAlive ? '#10b981' : '#f43f5e',
            highlight: isAlive ? '#34d399' : '#fb7185',
            hover: isAlive ? '#10b981' : '#f43f5e',
            opacity: isAlive ? 0.85 : 0.4,
          },
          width: isAlive ? 2 : 1,
          dashes: isAlive ? false : [4, 4],
          smooth: { enabled: true, type: 'curvedCW', roundness: 0.1 },
          arrowStrikethrough: false,
          arrows: {
            to: { enabled: true, scaleFactor: 0.7 },
          },
          _alive: isAlive,
        };
      });
  }

  /**
   * Convert a single node port → vis-network node (for incremental add).
   */
  function toVisNode(n) {
    return toVisNodes([n])[0];
  }

  return {
    COLORS,
    ROLE_ORDER,
    color,
    makeTooltip,
    toVisNodes,
    toVisEdges,
    toAllEdges,
    toVisNode,
  };
})();
