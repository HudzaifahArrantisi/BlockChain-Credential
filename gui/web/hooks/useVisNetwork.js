/**
 * useVisNetwork.js
 * vis-network lifecycle manager.
 * Handles creation, updates, and event wiring.
 * Physics is disabled — nodes have fixed positions from GraphAdapter.
 *
 * Dependencies: vis-network (global `vis`), GraphAdapter
 */
const UseVisNetwork = (() => {

  /**
   * Default configuration — physics disabled, static layout only.
   */
  const DEFAULTS = {
    physics: {
      enabled: false,
    },
    edges: {
      smooth: { enabled: true, type: 'curvedCW', roundness: 0.15 },
      arrowStrikethrough: false,
    },
    nodes: {
      font: { face: 'Inter, sans-serif', size: 10, color: '#e2e8f0' },
      scaling: { label: { enabled: false } },
    },
    interaction: {
      hover: true,
      tooltipDelay: 200,
      navigationButtons: false,
      keyboard: false,
      dragNodes: true,
      dragView: true,
      zoomView: true,
      hoverConnectedEdges: true,
      selectConnectedEdges: true,
    },
    manipulation: { enabled: false },
    layout: {
      improvedLayout: false,
    },
  };

  /**
   * Merge user config with defaults.
   */
  function mergeOptions(...overrides) {
    return Object.assign({}, DEFAULTS, ...overrides);
  }

  /**
   * Create a new vis.Network in `container`.
   * Returns the network instance.
   */
  function create(container, nodes, edges, options = {}) {
    if (!container) throw new Error('NetworkGraph: container element required');
    const opts = mergeOptions(options);
    const data = {
      nodes: new vis.DataSet(nodes || []),
      edges: new vis.DataSet(edges || []),
    };
    const network = new vis.Network(container, data, opts);
    return { network, nodes: data.nodes, edges: data.edges };
  }

  /**
   * Replace all nodes in the DataSet.
   * Returns { added, removed } counts.
   */
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

  /**
   * Replace all edges in the DataSet.
   */
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

  /**
   * Fit the view to show all nodes.
   */
  function fitView(network, duration = 400) {
    if (!network) return;
    network.fit({ animation: { duration, easingFunction: 'easeInOutQuad' } });
  }

  /**
   * Select a node by id.
   */
  function selectNode(network, nodeId) {
    if (!network) return;
    network.selectNodes([nodeId], false);
  }

  /**
   * Focus on a specific node (center + zoom).
   */
  function focusNode(network, nodeId, scale = 1.5) {
    if (!network) return;
    network.focus(nodeId, { scale, animation: { duration: 400, easingFunction: 'easeInOutQuad' } });
  }

  /**
   * Destroy the network (cleanup).
   */
  function destroy(network) {
    if (!network) return;
    network.destroy();
  }

  return {
    DEFAULTS,
    mergeOptions,
    create,
    setNodes,
    setEdges,
    fitView,
    selectNode,
    focusNode,
    destroy,
  };
})();
