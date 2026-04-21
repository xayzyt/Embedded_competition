const api = require('../../services/api.js');

Page({
  data: {
    senderId: 'sender_001',
    receiverId: 'receiver_003',
    targetId: '3',
    deviceName: 'skyanchor-p4',
    note: '',
    creating: false
  },

  onLoad(query) {
    const app = getApp();
    this.setData({
      senderId: query.sender_id || app.globalData.senderId,
      receiverId: query.receiver_id || app.globalData.receiverId
    });
  },

  handleInput(e) {
    const key = e.currentTarget.dataset.key;
    this.setData({
      [key]: e.detail.value
    });
  },

  async createOnly() {
    await this.submit(false);
  },

  async createAndStart() {
    await this.submit(true);
  },

  async submit(autoStart) {
    if (this.data.creating) {
      return;
    }

    const senderId = this.data.senderId.trim();
    const receiverId = this.data.receiverId.trim();
    const deviceName = this.data.deviceName.trim();
    const targetId = Number(this.data.targetId);

    if (!senderId || !receiverId || !deviceName) {
      wx.showModal({
        title: '信息不完整',
        content: '配送员 ID、收货人 ID 和设备名称不能为空。',
        showCancel: false
      });
      return;
    }

    if (!Number.isInteger(targetId) || targetId < 1) {
      wx.showModal({
        title: '目标 ID 无效',
        content: '请输入大于 0 的整数目标 ID。',
        showCancel: false
      });
      return;
    }

    this.setData({ creating: true });
    wx.showLoading({ title: autoStart ? '创建并启动中' : '创建中' });

    try {
      const order = await api.createOrder({
        sender_id: senderId,
        receiver_id: receiverId,
        target_id: targetId,
        device_name: deviceName,
        note: this.data.note
      });

      if (autoStart) {
        await api.startOrder(order.order_id);
      }

      wx.hideLoading();
      wx.showToast({
        title: autoStart ? '已启动' : '已创建',
        icon: 'success'
      });

      wx.navigateTo({
        url: `/pages/order-center/index?order_id=${encodeURIComponent(order.order_id)}`
      });
    } catch (err) {
      wx.hideLoading();
      wx.showModal({
        title: '请求失败',
        content: err.message || '无法创建订单',
        showCancel: false
      });
    } finally {
      this.setData({ creating: false });
    }
  }
});
