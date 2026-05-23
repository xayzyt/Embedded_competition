const api = require('../../services/api.js');
const { getDemoProfile } = require('../../utils/demo-profile.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { defaultServiceStatus, syncServiceStatus } = require('../../utils/service-status.js');
const { formatApriltagValue } = require('../../utils/apriltag.js');

const POLL_INTERVAL_MS = 3000;
const HIDDEN_STATUSES = ['cancelled'];

function decorateOrder(order) {
  return {
    ...order,
    status_text: statusLabel(order.status),
    apriltag_text: formatApriltagValue(order && order.target_id)
  };
}

function filterVisibleOrders(orders) {
  return (orders || []).filter((item) => !HIDDEN_STATUSES.includes(item.status));
}

Page({
  data: Object.assign({
    receiverId: '',
    orders: [],
    creating: false,
    loading: false,
    loadError: '',
    hasLoaded: false
  }, defaultServiceStatus()),

  onLoad(query) {
    const app = getApp();
    const demoProfile = app.globalData.demoProfile || getDemoProfile();
    const receiverId = query.receiver_id || app.globalData.receiverId || wx.getStorageSync('skyanchor_receiver_id') || demoProfile.receiverId;

    this._skipNextOnShow = true;
    this._pageVisible = false;
    this._pollTimer = null;
    this._polling = false;
    this._confirmingCreate = false;

    this.setData({ receiverId });
    this.initializePage(!!receiverId);
  },

  async onShow() {
    this._pageVisible = true;

    if (this._skipNextOnShow) {
      this._skipNextOnShow = false;
      this.syncPollingState();
      return;
    }

    if (this.data.receiverId.trim()) {
      await this.fetchOrders();
      return;
    }

    await syncServiceStatus(this, true);
    this.syncPollingState();
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

  async initializePage(shouldLoad) {
    if (shouldLoad) {
      await this.fetchOrders();
      return;
    }

    await syncServiceStatus(this, true);
  },

  handleInput(e) {
    this.setData({
      receiverId: e.detail.value
    });
  },

  async saveReceiver() {
    const receiverId = this.data.receiverId.trim();
    if (!receiverId) {
      wx.showModal({
        title: '信息不完整',
        content: '请先输入用户 ID。',
        showCancel: false
      });
      return;
    }

    const app = getApp();
    app.globalData.receiverId = receiverId;
    wx.setStorageSync('skyanchor_receiver_id', receiverId);
    this.setData({ receiverId });

    wx.showToast({
      title: '已保存',
      icon: 'success'
    });

    await this.fetchOrders();
  },

  async openCreateOrder() {
    const receiverId = this.data.receiverId.trim();
    if (!receiverId) {
      wx.showModal({
        title: '信息不完整',
        content: '请先保存用户 ID，再去下单。',
        showCancel: false
      });
      return;
    }

    const app = getApp();
    app.globalData.receiverId = receiverId;
    wx.setStorageSync('skyanchor_receiver_id', receiverId);

    if (this.data.creating || this._confirmingCreate) {
      return;
    }

    this._confirmingCreate = true;
    const confirmed = await this.confirmCreateOrder();
    this._confirmingCreate = false;
    if (!confirmed) {
      return;
    }

    const serviceStatus = await syncServiceStatus(this, true);
    if (!serviceStatus.serviceOnline) {
      wx.showModal({
        // 云开发版改为提示云端服务状态，避免继续引导启动本地服务。
        title: '云端服务未连接',
        content: serviceStatus.serviceMessage,
        showCancel: false
      });
      return;
    }

    if (serviceStatus.serviceWeatherBlocked) {
      wx.showModal({
        title: '恶劣天气管制',
        content: serviceStatus.serviceMessage,
        showCancel: false
      });
      return;
    }

    this.setData({ creating: true });
    wx.showLoading({ title: '提交订单中' });

    try {
      const order = await api.createOrder({
        receiver_id: receiverId
      });

      wx.hideLoading();
      wx.showToast({
        title: '订单已提交',
        icon: 'success'
      });

      wx.navigateTo({
        url: `/pages/order-panel/index?order_id=${encodeURIComponent(order.order_id)}&role=receiver`
      });
    } catch (err) {
      await syncServiceStatus(this, true);
      wx.hideLoading();
      wx.showModal({
        title: '提交失败',
        content: err.message || '无法创建订单',
        showCancel: false
      });
    } finally {
      this.setData({ creating: false });
    }
  },

  confirmCreateOrder() {
    return new Promise((resolve) => {
      wx.showModal({
        title: '确认下单',
        content: '确定现在提交一笔新的配送订单吗？',
        confirmText: '确定',
        cancelText: '取消',
        success: (res) => resolve(!!res.confirm),
        fail: () => resolve(false)
      });
    });
  },

  async fetchOrders(options = {}) {
    const receiverId = this.data.receiverId.trim();
    const silent = !!options.silent;

    if (!receiverId) {
      this.stopPolling();
      this.setData({
        receiverId: '',
        orders: [],
        loading: false,
        loadError: '',
        hasLoaded: false
      });
      return;
    }

    const app = getApp();
    app.globalData.receiverId = receiverId;
    wx.setStorageSync('skyanchor_receiver_id', receiverId);
    this.setData({
      receiverId,
      loading: silent ? this.data.loading : true,
      loadError: silent ? this.data.loadError : ''
    });

    const serviceStatus = await syncServiceStatus(this, true);
    if (!serviceStatus.serviceOnline) {
      this.setData({
        orders: [],
        loadError: serviceStatus.serviceMessage,
        hasLoaded: true,
        loading: false
      });
      this.syncPollingState();
      return;
    }

    try {
      const orders = await api.listOrders({
        role: 'receiver',
        userId: receiverId
      });

      this.setData({
        orders: filterVisibleOrders(orders.map(decorateOrder)),
        loadError: '',
        hasLoaded: true,
        loading: false
      });
    } catch (err) {
      const nextStatus = await syncServiceStatus(this, true);
      this.setData({
        orders: [],
        loadError: nextStatus.serviceOnline
          ? (err.message || '无法获取订单列表')
          : nextStatus.serviceMessage,
        hasLoaded: true,
        loading: false
      });
    }

    this.syncPollingState();
  },

  startPolling() {
    if (this._pollTimer || !this._pageVisible || !this.data.receiverId.trim()) {
      return;
    }

    this._pollTimer = setInterval(() => {
      if (this._polling || !this.data.receiverId.trim()) {
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
    if (this._pageVisible && this.data.receiverId.trim()) {
      this.startPolling();
      return;
    }

    this.stopPolling();
  },

  openOrder(e) {
    const orderId = e.currentTarget.dataset.orderId;
    wx.navigateTo({
      url: `/pages/order-panel/index?order_id=${encodeURIComponent(orderId)}&role=receiver`
    });
  }
});
