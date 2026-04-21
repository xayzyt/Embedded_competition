const api = require('../../services/api.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { defaultServiceStatus, syncServiceStatus } = require('../../utils/service-status.js');
const { formatApriltagValue } = require('../../utils/apriltag.js');

const ACTIVE_STATUSES = ['pending_start', 'delivering', 'tag_matched', 'acting'];

function decorateOrder(order) {
  return {
    ...order,
    status_text: statusLabel(order.status),
    apriltag_text: formatApriltagValue(order && order.target_id)
  };
}

Page({
  data: Object.assign({
    pendingOrders: [],
    activeOrders: [],
    loading: false,
    loadError: '',
    hasLoaded: false
  }, defaultServiceStatus()),

  onLoad() {
    this._skipNextOnShow = true;
    this.initializePage();
  },

  async onShow() {
    if (this._skipNextOnShow) {
      this._skipNextOnShow = false;
      return;
    }

    await this.fetchOrders();
  },

  onPullDownRefresh() {
    this.fetchOrders().finally(() => wx.stopPullDownRefresh());
  },

  async initializePage() {
    await this.fetchOrders();
  },

  async fetchOrders() {
    this.setData({
      loading: true,
      loadError: ''
    });

    const serviceStatus = await syncServiceStatus(this, true);
    if (!serviceStatus.serviceOnline) {
      this.setData({
        pendingOrders: [],
        activeOrders: [],
        loadError: serviceStatus.serviceMessage,
        hasLoaded: true,
        loading: false
      });
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
        loading: false
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
        loading: false
      });
    }
  },

  openDetail(e) {
    const orderId = e.currentTarget.dataset.orderId;
    wx.navigateTo({
      url: `/pages/order-panel/index?order_id=${encodeURIComponent(orderId)}&role=sender`
    });
  }
});
