const api = require('../../services/api.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { defaultServiceStatus, formatClockTime, showServiceDiagnostics: showDiagnostics, syncServiceStatus } = require('../../utils/service-status.js');
const { formatApriltagValue } = require('../../utils/apriltag.js');
const { formatOrderName } = require('../../utils/order-display.js');

const ACTIVE_STATUSES = ['pending_start', 'delivering', 'tag_matched', 'acting'];
const POLL_INTERVAL_MS = 5000;

function buildOrderHint(order) {
  const status = order && order.status;
  if (status === 'created') {
    return '云端已收单，等待调度员分配 AprilTag';
  }
  if (status === 'pending_start') {
    return '任务已下发，等待板端确认';
  }
  if (status === 'delivering') {
    return 'AI 正在等待无人机进入识别区';
  }
  if (status === 'tag_matched') {
    return 'AprilTag 已确认，准备接驳';
  }
  if (status === 'acting') {
    return 'CH32 正在执行接驳动作';
  }
  return '云端状态已同步';
}

function decorateOrder(order) {
  return {
    ...order,
    order_name_text: formatOrderName(order),
    status_text: statusLabel(order.status),
    apriltag_text: formatApriltagValue(order && order.target_id),
    stage_hint: buildOrderHint(order)
  };
}

Page({
  data: Object.assign({
    pendingOrders: [],
    activeOrders: [],
    loading: false,
    loadError: '',
    hasLoaded: false,
    lastRefreshText: '未刷新'
  }, defaultServiceStatus()),

  onLoad() {
    this._skipNextOnShow = true;
    this._pageVisible = false;
    this._pollTimer = null;
    this._polling = false;
    this.initializePage();
  },

  async onShow() {
    this._pageVisible = true;

    if (this._skipNextOnShow) {
      this._skipNextOnShow = false;
      this.syncPollingState();
      return;
    }

    await this.fetchOrders();
  },

  onHide() {
    this._pageVisible = false;
    this.stopPolling();
  },

  onUnload() {
    this._pageVisible = false;
    this.stopPolling();
  },

  onPullDownRefresh() {
    this.fetchOrders().finally(() => wx.stopPullDownRefresh());
  },

  async initializePage() {
    await this.fetchOrders();
  },

  async fetchOrders(options = {}) {
    const silent = !!options.silent;

    this.setData({
      loading: silent ? this.data.loading : true,
      loadError: silent ? this.data.loadError : ''
    });

    const serviceStatus = await syncServiceStatus(this, true);
    if (!serviceStatus.serviceOnline) {
      this.setData({
        pendingOrders: [],
        activeOrders: [],
        loadError: serviceStatus.serviceMessage,
        hasLoaded: true,
        loading: false,
        lastRefreshText: formatClockTime(Date.now())
      });
      this.syncPollingState();
      return;
    }

    try {
      const orders = await api.listOrders({});
      const decoratedOrders = orders.map(decorateOrder);

      this.setData({
        pendingOrders: decoratedOrders.filter((item) => item.status === 'created'),
        activeOrders: decoratedOrders.filter((item) => ACTIVE_STATUSES.includes(item.status)),
        loadError: '',
        hasLoaded: true,
        loading: false,
        lastRefreshText: formatClockTime(Date.now())
      });
    } catch (err) {
      const nextStatus = await syncServiceStatus(this, true);
      this.setData({
        pendingOrders: [],
        activeOrders: [],
        loadError: nextStatus.serviceOnline
          ? (err.message || '无法获取订单列表')
          : nextStatus.serviceMessage,
        hasLoaded: true,
        loading: false,
        lastRefreshText: formatClockTime(Date.now())
      });
    }

    this.syncPollingState();
  },

  startPolling() {
    if (this._pollTimer || !this._pageVisible) {
      return;
    }

    this._pollTimer = setInterval(() => {
      if (this._polling) {
        return;
      }

      this._polling = true;
      this.fetchOrders({ silent: true }).finally(() => {
        this._polling = false;
      });
    }, POLL_INTERVAL_MS);
  },

  stopPolling() {
    if (this._pollTimer) {
      clearInterval(this._pollTimer);
      this._pollTimer = null;
    }
  },

  syncPollingState() {
    if (this._pageVisible) {
      this.startPolling();
      return;
    }

    this.stopPolling();
  },

  showServiceDiagnostics() {
    showDiagnostics(this.data);
  },

  async runPreflightCheck() {
    const serviceStatus = await syncServiceStatus(this, true);
    showDiagnostics(serviceStatus, '演示自检');
  },

  openDetail(e) {
    const orderId = e.currentTarget.dataset.orderId;
    wx.navigateTo({
      url: `/pages/order-panel/index?order_id=${encodeURIComponent(orderId)}&role=sender`
    });
  }
});
