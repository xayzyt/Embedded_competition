Page({
  data: {
    senderId: 'sender_001',
    receiverId: 'receiver_003'
  },

  onLoad() {
    const app = getApp();
    this.setData({
      senderId: app.globalData.senderId,
      receiverId: app.globalData.receiverId
    });
  },

  handleInput(e) {
    const key = e.currentTarget.dataset.key;
    this.setData({
      [key]: e.detail.value
    });
  },

  saveDefaults() {
    const app = getApp();
    app.globalData.senderId = this.data.senderId.trim() || 'sender_001';
    app.globalData.receiverId = this.data.receiverId.trim() || 'receiver_003';
    this.setData({
      senderId: app.globalData.senderId,
      receiverId: app.globalData.receiverId
    });
    wx.showToast({
      title: '已保存',
      icon: 'success'
    });
  },

  openCreateOrder() {
    this.saveDefaults();
    wx.navigateTo({
      url: `/pages/create-order/index?sender_id=${encodeURIComponent(this.data.senderId)}&receiver_id=${encodeURIComponent(this.data.receiverId)}`
    });
  },

  openOrderList() {
    this.saveDefaults();
    wx.navigateTo({
      url: `/pages/order-list/index?sender_id=${encodeURIComponent(this.data.senderId)}`
    });
  },

  openNotifications() {
    this.saveDefaults();
    wx.navigateTo({
      url: `/pages/receiver-center/index?receiver_id=${encodeURIComponent(this.data.receiverId)}`
    });
  }
});
