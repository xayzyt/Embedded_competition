const api = require('../../services/api.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { getDemoProfile } = require('../../utils/demo-profile.js');
const { defaultServiceStatus, syncServiceStatus } = require('../../utils/service-status.js');

Page({
  data: Object.assign({
    senderId: '',
    orders: [],
    loading: false,
    loadError: '',
    hasLoaded: false
  }, defaultServiceStatus()),

  onLoad(query) {
    const app = getApp();
    const senderId = query.sender_id || app.globalData.senderId || getDemoProfile().senderId;
    this._skipNextOnShow = true;
    this.setData({ senderId });
    this.initializePage(!!senderId);
  },

  async onShow() {
    if (this._skipNextOnShow) {
      this._skipNextOnShow = false;
      return;
    }

    if (this.data.senderId.trim()) {
      await this.fetchOrders();
      return;
    }

    await syncServiceStatus(this, true);
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
      senderId: e.detail.value
    });
  },

  saveSender() {
    const senderId = this.data.senderId.trim();
    if (!senderId) {
      wx.showModal({
        title: '信息不完整',
        content: '请先输入配送员 ID。',
        showCancel: false
      });
      return;
    }

    const app = getApp();
    app.globalData.senderId = senderId;
    wx.setStorageSync('skyanchor_sender_id', senderId);
    this.setData({ senderId });

    wx.showToast({
      title: '已保存',
      icon: 'success'
    });

    this.fetchOrders();
  },

  openCreate() {
    const senderId = this.data.senderId.trim();
    if (!senderId) {
      wx.showModal({
        title: '信息不完整',
        content: '请先输入并保存配送员 ID。',
        showCancel: false
      });
      return;
    }

    const app = getApp();
    app.globalData.senderId = senderId;
    wx.setStorageSync('skyanchor_sender_id', senderId);

    wx.navigateTo({
      url: `/pages/sender-create/index?sender_id=${encodeURIComponent(senderId)}`
    });
  },

  async fetchOrders() {
    const senderId = this.data.senderId.trim();
    if (!senderId) {
      this.setData({
        senderId: '',
        orders: [],
        loading: false,
        loadError: '',
        hasLoaded: false
      });
      return;
    }

    const app = getApp();
    app.globalData.senderId = senderId;
    wx.setStorageSync('skyanchor_sender_id', senderId);
    this.setData({
      senderId,
      loading: true,
      loadError: ''
    });

    const serviceStatus = await syncServiceStatus(this, true);
    if (!serviceStatus.serviceOnline) {
      this.setData({
        orders: [],
        loadError: serviceStatus.serviceMessage,
        hasLoaded: true,
        loading: false
      });
      return;
    }

    try {
      const orders = await api.listOrders({
        role: 'sender',
        userId: senderId
      });
      this.setData({
        orders: orders.map((item) => ({
          ...item,
          status_text: statusLabel(item.status)
        })),
        loadError: '',
        hasLoaded: true
      });
    } catch (err) {
      const serviceStatus = await syncServiceStatus(this, true);
      this.setData({
        orders: [],
        loadError: serviceStatus.serviceOnline
          ? (err.message || '无法获取订单列表')
          : '本地服务未启动，请先运行 skyanchor-server。',
        hasLoaded: true
      });
    } finally {
      this.setData({ loading: false });
    }
  },

  openDetail(e) {
    const orderId = e.currentTarget.dataset.orderId;
    wx.navigateTo({
      url: `/pages/order-center/index?order_id=${encodeURIComponent(orderId)}&role=sender`
    });
  }
});
