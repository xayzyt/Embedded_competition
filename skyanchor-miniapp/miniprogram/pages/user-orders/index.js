const api = require('../../services/api.js');
const { getDemoProfile } = require('../../utils/demo-profile.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { defaultServiceStatus, showServiceDiagnostics: showDiagnostics, syncServiceStatus } = require('../../utils/service-status.js');
const { formatApriltagValue } = require('../../utils/apriltag.js');
const { formatOrderName } = require('../../utils/order-display.js');
const { requestDeliveryCompleteSubscription } = require('../../utils/delivery-notice.js');

const POLL_INTERVAL_MS = 3000;
const HIDDEN_STATUSES = ['cancelled'];
const CLEARABLE_STATUSES = ['created', 'delivered', 'failed', 'cancelled'];
const WEATHER_FAILURE_KEYWORDS = ['恶劣天气', '天气管制', '天气保护', '接收保护'];

function canClearOrder(order) {
  return CLEARABLE_STATUSES.includes(order && order.status);
}

function decorateOrder(order) {
  const noteText = String(order && order.note || '').trim();

  return {
    ...order,
    order_name_text: formatOrderName(order),
    status_text: statusLabel(order.status),
    apriltag_text: formatApriltagValue(order && order.target_id),
    note_text: noteText,
    is_failed: order && order.status === 'failed',
    can_clear: canClearOrder(order)
  };
}

function filterVisibleOrders(orders) {
  return (orders || []).filter((item) => {
    if (HIDDEN_STATUSES.includes(item.status)) {
      return false;
    }

    const noteText = String(item && item.note_text || item && item.note || '').trim();
    const weatherFailure = item.status === 'failed' && WEATHER_FAILURE_KEYWORDS.some((keyword) => noteText.includes(keyword));
    return !weatherFailure;
  });
}

Page({
  data: Object.assign({
    receiverId: '',
    orders: [],
    creating: false,
    loading: false,
    clearingOrderId: '',
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

  showServiceDiagnostics() {
    showDiagnostics(this.data);
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
    const deliveryNotice = await requestDeliveryCompleteSubscription();
    const confirmed = await this.confirmCreateOrder();
    this._confirmingCreate = false;
    if (!confirmed) {
      return;
    }

    const serviceStatus = await syncServiceStatus(this, true);
    if (!serviceStatus.serviceOnline) {
      wx.showModal({
        title: serviceStatus.serviceBlockTitle || '云端服务未连接',
        content: serviceStatus.serviceBlockAdvice || serviceStatus.serviceMessage,
        showCancel: false
      });
      return;
    }

    if (serviceStatus.serviceActionBlocked) {
      wx.showModal({
        title: serviceStatus.serviceBlockTitle || '服务暂不可用',
        content: serviceStatus.serviceBlockAdvice || serviceStatus.serviceMessage,
        showCancel: false
      });
      return;
    }

    this.setData({ creating: true });
    wx.showLoading({ title: '提交订单中' });

    try {
      const order = await api.createOrder({
        receiver_id: receiverId,
        delivery_notice_subscribed: deliveryNotice.accepted,
        delivery_notice_template_id: deliveryNotice.templateId
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

  confirmClearOrder(orderName) {
    return new Promise((resolve) => {
      wx.showModal({
        title: '清除订单',
        content: `确定从后台删除 ${orderName || '该订单'} 吗？删除后不能恢复。`,
        confirmText: '清除',
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

  async clearOrder(e) {
    const orderId = e.currentTarget.dataset.orderId;
    const orderName = e.currentTarget.dataset.orderName;
    if (!orderId || this.data.clearingOrderId) {
      return;
    }

    const confirmed = await this.confirmClearOrder(orderName);
    if (!confirmed) {
      return;
    }

    this.setData({ clearingOrderId: orderId });
    wx.showLoading({ title: '清除中' });

    try {
      await api.deleteOrder(orderId);
      this.setData({
        orders: this.data.orders.filter((item) => item.order_id !== orderId)
      });
      wx.hideLoading();
      wx.showToast({
        title: '已清除',
        icon: 'success'
      });
      this.fetchOrders({ silent: true }).catch(() => {});
    } catch (err) {
      wx.hideLoading();
      wx.showModal({
        title: '清除失败',
        content: err.message || '无法清除订单',
        showCancel: false
      });
    } finally {
      this.setData({ clearingOrderId: '' });
    }
  },

  openOrder(e) {
    const orderId = e.currentTarget.dataset.orderId;
    wx.navigateTo({
      url: `/pages/order-panel/index?order_id=${encodeURIComponent(orderId)}&role=receiver`
    });
  }
});
