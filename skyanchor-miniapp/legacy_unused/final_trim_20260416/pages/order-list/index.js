const api = require('../../services/api.js');
const { statusLabel } = require('../../utils/order-status-labels.js');

Page({
  data: {
    senderId: 'sender_001',
    orders: [],
    loading: false
  },

  onLoad(query) {
    const app = getApp();
    this.setData({
      senderId: query.sender_id || app.globalData.senderId
    });
    this.fetchOrders();
  },

  onPullDownRefresh() {
    this.fetchOrders().finally(() => wx.stopPullDownRefresh());
  },

  handleInput(e) {
    this.setData({
      senderId: e.detail.value
    });
  },

  async fetchOrders() {
    this.setData({ loading: true });
    try {
      const orders = await api.listOrders({
        role: 'sender',
        userId: this.data.senderId
      });
      this.setData({
        orders: orders.map((item) => ({
          ...item,
          status_text: statusLabel(item.status)
        }))
      });
    } catch (err) {
      wx.showModal({
        title: '加载失败',
        content: err.message || '无法获取订单列表',
        showCancel: false
      });
    } finally {
      this.setData({ loading: false });
    }
  },

  openDetail(e) {
    const orderId = e.currentTarget.dataset.orderId;
    wx.navigateTo({
      url: `/pages/order-center/index?order_id=${encodeURIComponent(orderId)}`
    });
  }
});
