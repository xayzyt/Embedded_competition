const api = require('../../services/api.js');
const { statusLabel } = require('../../utils/order-status-labels.js');

Page({
  data: {
    orderId: '',
    order: null,
    events: [],
    loading: false
  },

  onLoad(query) {
    if (query.order_id) {
      this.setData({ orderId: query.order_id });
      this.fetchDetail();
    }
  },

  onPullDownRefresh() {
    this.fetchDetail().finally(() => wx.stopPullDownRefresh());
  },

  async fetchDetail() {
    if (!this.data.orderId) {
      return;
    }

    this.setData({ loading: true });
    try {
      const data = await api.getOrder(this.data.orderId);
      const order = data.order || null;
      this.setData({
        order: order
          ? {
              ...order,
              status_text: statusLabel(order.status)
            }
          : null,
        events: data.events || []
      });
    } catch (err) {
      wx.showModal({
        title: '加载失败',
        content: err.message || '无法获取订单详情',
        showCancel: false
      });
    } finally {
      this.setData({ loading: false });
    }
  },

  async startOrder() {
    await this.runAction(() => api.startOrder(this.data.orderId), '订单已启动');
  },

  async cancelOrder() {
    await this.runAction(() => api.cancelOrder(this.data.orderId), '订单已取消');
  },

  async mockActing() {
    await this.runAction(
      () => api.mockOrderState(this.data.orderId, {
        status: 'acting',
        note: 'mock acting',
        last_device_state: 'docking'
      }),
      '已模拟 acting'
    );
  },

  async mockDelivered() {
    const targetId = this.data.order ? Number(this.data.order.target_id) : 0;
    await this.runAction(
      () => api.mockOrderState(this.data.orderId, {
        status: 'delivered',
        note: 'mock delivered',
        matched_tag_id: targetId || 1,
        last_device_state: 'completed'
      }),
      '已模拟送达'
    );
  },

  async mockFailed() {
    await this.runAction(
      () => api.mockOrderState(this.data.orderId, {
        status: 'failed',
        note: 'mock failed',
        last_device_state: 'fault'
      }),
      '已模拟失败'
    );
  },

  async runAction(action, toastTitle) {
    if (!this.data.orderId) {
      return;
    }

    wx.showLoading({ title: '处理中' });
    try {
      await action();
      wx.hideLoading();
      wx.showToast({
        title: toastTitle,
        icon: 'success'
      });
      this.fetchDetail();
    } catch (err) {
      wx.hideLoading();
      wx.showModal({
        title: '操作失败',
        content: err.message || '请求失败',
        showCancel: false
      });
    }
  }
});
