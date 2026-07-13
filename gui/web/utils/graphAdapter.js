const GraphAdapter = (() => {
  const COLORS = {
    LEADER:    { fill: '#10b981', stroke: '#047857', border: '#34d399', glow: 'rgba(16,185,129,0.35)' },
    FOLLOWER:  { fill: '#2563eb', stroke: '#1d4ed8', border: '#60a5fa', glow: 'rgba(37,99,235,0.2)' },
    CANDIDATE: { fill: '#f59e0b', stroke: '#b45309', border: '#fbbf24', glow: 'rgba(245,158,11,0.25)' },
    OFFLINE:   { fill: '#1e293b', stroke: '#0f172a', border: '#475569', glow: 'transparent' },
  };

  const ROLE_ORDER = { LEADER: 0, CANDIDATE: 1, FOLLOWER: 2, OFFLINE: 3 };

  function color(role) {
    return COLORS[role] || COLORS.OFFLINE;
  }

  function makeTooltip(n) {
    const c = color(n.role);
    const roleBadge = `<span style="display:inline-block;padding:1px 7px;border-radius:3px;font-size:9px;font-weight:700;letter-spacing:.6px;text-transform:uppercase;background:${c.fill}22;color:${c.fill};border:1px solid ${c.fill}44">${n.role}</span>`;
    const statusIcon = n.alive ? '\u25CF' : '\u25CB';
    const statusColor = n.alive ? '#10b981' : '#f43f5e';
    const lines = [
      `<div style="display:flex;align-items:center;gap:8px;margin-bottom:6px">`,
      `<span style="font-weight:700;font-size:13px;color:#f0f4ff">${n.label}</span>${roleBadge}`,
      `</div>`,
      `<div style="display:grid;grid-template-columns:auto 1fr;gap:2px 12px;font-size:11px;line-height:1.8">`,
      `<span style="color:#64748b">Term</span><span style="color:#e2e8f0;font-weight:600">${n.term ?? '\u2014'}</span>`,
      `<span style="color:#64748b">Blocks</span><span style="color:#e2e8f0;font-weight:600">${n.blocks ?? '\u2014'}</span>`,
      `<span style="color:#64748b">Port</span><span style="color:#94a3b8">${n.port}</span>`,
      n.id_short ? `<span style="color:#64748b">Node ID</span><span style="color:#94a3b8;font-family:monospace;font-size:10px">${n.id_short}</span>` : '',
      `<span style="color:#64748b">Status</span><span style="color:${statusColor}">${statusIcon} ${n.alive ? 'Online' : 'Offline'}</span>`,
      `</div>`,
    ].join('');
    return lines;
  }

  function toVisNodes(nodes) {
    return nodes.map(n => {
      const c = color(n.role);
      const isLeader = n.role === 'LEADER';
      const alive = n.alive;
      const size = isLeader ? 32 : n.role === 'CANDIDATE' ? 26 : 24;
      const borderWidth = isLeader ? 3 : 1.5;
      return {
        id: n.port,
        label: n.label,
        title: makeTooltip(n),
        size,
        borderWidth,
        borderWidthSelected: borderWidth + 1.5,
        color: {
          background: alive ? c.fill : COLORS.OFFLINE.fill,
          border: alive ? c.border : COLORS.OFFLINE.border,
          highlight: {
            background: alive ? c.fill : COLORS.OFFLINE.fill,
            border: '#f0f4ff',
          },
          hover: {
            background: alive ? c.fill : COLORS.OFFLINE.fill,
            border: '#f0f4ff',
          },
        },
        font: {
          color: alive ? '#e2e8f0' : '#475569',
          size: 10,
          face: 'Inter, sans-serif',
          strokeWidth: 0,
        },
        shape: isLeader ? 'star' : 'dot',
        shadow: {
          enabled: true,
          color: alive ? c.glow : 'transparent',
          size: isLeader ? 40 : 20,
          x: 0,
          y: 0,
        },
        physics: true,
        mass: isLeader ? 3 : n.role === 'CANDIDATE' ? 2 : 1.5,
        _role: n.role,
        _alive: n.alive,
        _term: n.term,
        _blocks: n.blocks,
        _label: n.label,
        _port: n.port,
      };
    });
  }

  function toVisEdges(nodes) {
    const alive = nodes.filter(n => n.alive);
    const leader = alive.find(n => n.role === 'LEADER');
    if (!leader) return [];
    return alive
      .filter(n => n.port !== leader.port)
      .map((n, i) => ({
        id: `${leader.port}-${n.port}`,
        from: leader.port,
        to: n.port,
        color: {
          color: '#10b981',
          highlight: '#34d399',
          hover: '#10b981',
          opacity: 0.7,
        },
        width: 2,
        widthSelectionIncrease: 1,
        dashes: false,
        smooth: {
          enabled: true,
          type: 'curvedCW',
          roundness: 0.08 + (i % 3) * 0.03,
        },
        arrowStrikethrough: false,
        arrows: {
          to: { enabled: true, scaleFactor: 0.7 },
        },
        _alive: true,
        _edgeType: 'heartbeat',
      }));
  }

  function toAllEdges(nodes) {
    const alive = nodes.filter(n => n.alive);
    const leader = alive.find(n => n.role === 'LEADER');
    if (!leader) return [];
    return nodes
      .filter(n => n.port !== leader.port)
      .map((n, i) => {
        const isAlive = n.alive;
        return {
          id: `${leader.port}-${n.port}`,
          from: leader.port,
          to: n.port,
          color: {
            color: isAlive ? '#10b981' : '#f43f5e',
            highlight: isAlive ? '#34d399' : '#fb7185',
            hover: isAlive ? '#10b981' : '#f43f5e',
            opacity: isAlive ? 0.7 : 0.3,
          },
          width: isAlive ? 2 : 1,
          dashes: isAlive ? false : [5, 5],
          smooth: {
            enabled: true,
            type: 'curvedCW',
            roundness: 0.08 + (i % 3) * 0.03,
          },
          arrowStrikethrough: false,
          arrows: {
            to: { enabled: true, scaleFactor: 0.7 },
          },
          _alive: isAlive,
          _edgeType: isAlive ? 'heartbeat' : 'disconnected',
        };
      });
  }

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
