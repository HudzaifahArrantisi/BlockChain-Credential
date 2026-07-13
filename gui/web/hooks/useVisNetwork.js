const UseVisNetwork = (() => {

  const PHYSICS_CONFIG = {
    enabled: true,
    solver: 'forceAtlas2Based',
    forceAtlas2Based: {
      gravitationalConstant: -48,
      centralGravity: 0.008,
      springLength: 180,
      springConstant: 0.04,
      damping: 0.6,
      avoidOverlap: 0.5,
    },
    minVelocity: 1.5,
    maxVelocity: 30,
    stabilization: {
      enabled: true,
      iterations: 200,
      updateInterval: 25,
      onlyDynamicEdges: false,
      fit: true,
    },
    adaptiveTimestep: true,
    barnesHut: {
      theta: 0.6,
      gravitationalConstant: -3000,
      centralGravity: 0.02,
      springLength: 160,
      springConstant: 0.03,
      damping: 0.7,
    },
  };

  const DEFAULTS = {
    physics: PHYSICS_CONFIG,
    edges: {
      smooth: { enabled: true, type: 'curvedCW', roundness: 0.12 },
      arrowStrikethrough: false,
      color: { opacity: 0.65 },
    },
    nodes: {
      font: {
        face: 'Inter, sans-serif',
        size: 10,
        color: '#e2e8f0',
        strokeWidth: 0,
      },
      scaling: { label: { enabled: false } },
      borderWidth: 1.5,
      shadow: { enabled: true, size: 12 },
    },
    interaction: {
      hover: true,
      tooltipDelay: 150,
      navigationButtons: false,
      keyboard: false,
      dragNodes: true,
      dragView: true,
      zoomView: true,
      hoverConnectedEdges: true,
      selectConnectedEdges: true,
      tooltip: {
        font: { size: 0 },
      },
    },
    manipulation: { enabled: false },
    layout: {
      improvedLayout: false,
      randomSeed: 42,
    },
    configure: {
      enabled: false,
    },
  };

  function mergeOptions(...overrides) {
    return deepMerge({}, DEFAULTS, ...overrides);
  }

  function deepMerge(target, ...sources) {
    for (const src of sources) {
      for (const key in src) {
        if (src[key] && typeof src[key] === 'object' && !Array.isArray(src[key])) {
          if (!target[key]) target[key] = {};
          deepMerge(target[key], src[key]);
        } else {
          target[key] = src[key];
        }
      }
    }
    return target;
  }

  function create(container, nodes, edges, options = {}) {
    if (!container) throw new Error('UseVisNetwork: container element required');
    const opts = mergeOptions(options);
    const data = {
      nodes: new vis.DataSet(nodes || []),
      edges: new vis.DataSet(edges || []),
    };
    const network = new vis.Network(container, data, opts);
    return { network, nodes: data.nodes, edges: data.edges };
  }

  function setNodes(dataSet, nodeList) {
    const ids = dataSet.getIds();
    const newIds = nodeList.map(n => n.id);
    const toRemove = ids.filter(id => !newIds.includes(id));
    if (toRemove.length) dataSet.remove(toRemove);
    const existing = ids.filter(id => newIds.includes(id));
    if (existing.length) {
      dataSet.update(nodeList.filter(n => existing.includes(n.id)));
    }
    const toAdd = nodeList.filter(n => !existing.includes(n.id));
    if (toAdd.length) dataSet.add(toAdd);
    return { added: toAdd.length, removed: toRemove.length };
  }

  function setEdges(dataSet, edgeList) {
    const ids = dataSet.getIds();
    const edgeIds = edgeList.map(e => e.id);
    const toRemove = ids.filter(id => !edgeIds.includes(id));
    if (toRemove.length) dataSet.remove(toRemove);
    const existing = ids.filter(id => edgeIds.includes(id));
    if (existing.length) {
      dataSet.update(edgeList.filter(e => existing.includes(e.id)));
    }
    const toAdd = edgeList.filter(e => !ids.includes(e.id));
    if (toAdd.length) dataSet.add(toAdd);
    return { added: toAdd.length, removed: toRemove.length };
  }

  function fitView(network, duration = 600) {
    if (!network) return;
    network.fit({ animation: { duration, easingFunction: 'easeInOutQuad' } });
  }

  function selectNode(network, nodeId) {
    if (!network) return;
    network.selectNodes([nodeId], false);
  }

  function focusNode(network, nodeId, scale = 1.8) {
    if (!network) return;
    network.focus(nodeId, {
      scale,
      animation: { duration: 500, easingFunction: 'easeInOutQuad' },
    });
  }

  function destroy(network) {
    if (!network) return;
    network.destroy();
  }

  function startPhysics(network) {
    if (!network) return;
    network.setOptions({
      physics: { enabled: true, stabilization: { enabled: true, iterations: 100, fit: true } },
    });
  }

  function stopPhysics(network) {
    if (!network) return;
    network.setOptions({ physics: { enabled: false } });
  }

  function stabilize(network, iterations = 150) {
    if (!network) return;
    network.setOptions({
      physics: {
        enabled: true,
        stabilization: { enabled: true, iterations, fit: false },
      },
    });
    network.once('stabilizationIterationsDone', () => {
      network.setOptions({ physics: { enabled: true } });
    });
  }

  function enableDrag(network) {
    if (!network) return;
    network.setOptions({ interaction: { dragNodes: true } });
  }

  function disableDrag(network) {
    if (!network) return;
    network.setOptions({ interaction: { dragNodes: false } });
  }

  function wireEvents(network, callbacks = {}) {
    if (!network) return;

    const {
      onClick,
      onDoubleClick,
      onHover,
      onBlur,
      onDragEnd,
      onSelect,
      onDeselect,
      onStabilized,
    } = callbacks;

    if (onClick) {
      network.on('click', params => onClick(params));
    }
    if (onDoubleClick) {
      network.on('doubleClick', params => onDoubleClick(params));
    }
    if (onHover) {
      network.on('hoverNode', params => onHover(params));
    }
    if (onBlur) {
      network.on('blurNode', params => onBlur(params));
    }
    if (onDragEnd) {
      network.on('dragEnd', params => onDragEnd(params));
    }
    if (onSelect) {
      network.on('select', params => onSelect(params));
    }
    if (onDeselect) {
      network.on('deselectNode', params => onDeselect(params));
    }
    if (onStabilized) {
      network.once('stabilizationIterationsDone', () => onStabilized());
    }
  }

  function getNodePosition(network, nodeId) {
    if (!network) return null;
    return network.getPosition(nodeId);
  }

  function getBoundingBox(network, nodeId) {
    if (!network) return null;
    return network.getBoundingBox(nodeId);
  }

  return {
    PHYSICS_CONFIG,
    DEFAULTS,
    mergeOptions,
    create,
    setNodes,
    setEdges,
    fitView,
    selectNode,
    focusNode,
    destroy,
    startPhysics,
    stopPhysics,
    stabilize,
    enableDrag,
    disableDrag,
    wireEvents,
    getNodePosition,
    getBoundingBox,
  };
})();
