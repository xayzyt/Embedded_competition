const api = require('../../services/api.js');

Page({
  data: {
    receiverId: 'receiver_003',
    unreadOnly: false,
    notifications: [],
    loading: false
  },

  onLoad(query) {
    const app = getApp();
    this.setData({
      receiverId: query.receiver_id || app.globalData.receiverId
    });
    this.fetchNotifications();
  },

  onPullDownRefresh() {
    this.fetchNotifications().finally(() => wx.stopPullDownRefresh());
  },

  handleInput(e) {
    this.setData({
      receiverId: e.detail.value
    });
  },

  toggleUnreadOnly(e) {
    this.setData({
      unreadOnly: e.detail.value
    }, () => this.fetchNotifications());
  },

  async fetchNotifications() {
    this.setData({ loading: true });
    try {
      const notifications = await api.listNotifications(this.data.receiverId, this.data.unreadOnly);
      this.setData({ notifications });
    } catch (err) {
      wx.showModal({
        title: '加载失败',
        content: err.message || '无法获取通知',
        showCancel: false
      });
    } finally {
      this.setData({ loading: false });
    }
  },

  async markRead(e) {
    const notificationId = Number(e.currentTarget.dataset.notificationId);
    try {
      await api.markNotificationRead(notificationId);
      wx.showToast({
        title: '已读',
        icon: 'success'
      });
      this.fetchNotifications();
    } catch (err) {
      wx.showModal({
        title: '操作失败',
        content: err.message || '无法标记已读',
        showCancel: false
      });
    }
  },

  openOrder(e) {
    const orderId = e.currentTarget.dataset.orderId;
    wx.navigateTo({
      url: `/pages/order-detail/index?order_id=${encodeURIComponent(orderId)}`
    });
  }
});
