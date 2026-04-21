const api = require('../../services/api.js');
const { getDemoProfile } = require('../../utils/demo-profile.js');
const { defaultServiceStatus, syncServiceStatus } = require('../../utils/service-status.js');

Page({
  data: Object.assign({
    receiverId: '',
    creating: false
  }, defaultServiceStatus()),

  onLoad() {
    const app = getApp();
    const demoProfile = app.globalData.demoProfile || getDemoProfile();
    const receiverId = app.globalData.receiverId || wx.getStorageSync('skyanchor_receiver_id') || demoProfile.receiverId;

    this.setData({
      receiverId
    });

    syncServiceStatus(this, true);
  },

  onShow() {
    syncServiceStatus(this, true);
  },

  async submitOrder() {
    if (this.data.creating) {
      return;
    }

    const receiverId = String(this.data.receiverId || '').trim();
    if (!receiverId) {
      wx.showModal({
        title: '缺少用户信息',
        content: '请先返回用户界面保存用户 ID。',
        showCancel: false
      });
      return;
    }

    const serviceStatus = await syncServiceStatus(this, true);
    if (!serviceStatus.serviceOnline) {
      wx.showModal({
        title: '本地服务未连接',
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

      wx.redirectTo({
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
  }
});
