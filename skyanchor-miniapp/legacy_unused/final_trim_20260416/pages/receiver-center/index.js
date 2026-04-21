/*
const api = require('../../services/api.js');
const { defaultServiceStatus, syncServiceStatus } = require('../../utils/service-status.js');

const NOTIFICATION_TYPE_LABELS = {
  order_delivering: '\u914d\u9001\u4e2d',
  order_delivered: '送达通知',
  order_failed: '失败通知',
  order_cancelled: '取消通知'
};

function notificationTypeLabel(type) {
  return NOTIFICATION_TYPE_LABELS[type] || type || '-';
}

Page({
  data: Object.assign({
    receiverId: '',
    unreadOnly: false,
    notifications: [],
    loading: false,
    loadError: '',
    hasLoaded: false
  }, defaultServiceStatus()),

  onLoad(query) {
    const app = getApp();
    const receiverId = query.receiver_id || app.globalData.receiverId || '';
    this._skipNextOnShow = true;
    this.setData({ receiverId });
    this.initializePage(!!receiverId);
  },

  async onShow() {
    if (this._skipNextOnShow) {
      this._skipNextOnShow = false;
      return;
    }

    if (this.data.receiverId.trim()) {
      await this.fetchNotifications();
      return;
    }

    await syncServiceStatus(this, true);
  },

  onPullDownRefresh() {
    this.fetchNotifications().finally(() => wx.stopPullDownRefresh());
  },

  async initializePage(shouldLoad) {
    if (shouldLoad) {
      await this.fetchNotifications();
      return;
    }

    await syncServiceStatus(this, true);
  },

  handleInput(e) {
    this.setData({
      receiverId: e.detail.value
    });
  },

  saveReceiver() {
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
  },

  toggleUnreadOnly(e) {
    this.setData(
      {
        unreadOnly: !!e.detail.value
      },
      () => {
        if (this.data.receiverId.trim()) {
          this.fetchNotifications();
        }
      }
    );
  },

  async fetchNotifications() {
    const receiverId = this.data.receiverId.trim();
    if (!receiverId) {
      this.setData({
        receiverId: '',
        notifications: [],
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
      loading: true,
      loadError: ''
    });

    const serviceStatus = await syncServiceStatus(this, true);
    if (!serviceStatus.serviceOnline) {
      this.setData({
        notifications: [],
        loadError: serviceStatus.serviceMessage,
        hasLoaded: true,
        loading: false
      });
      return;
    }

    try {
      const notifications = await api.listNotifications(receiverId, this.data.unreadOnly);
      this.setData({
        notifications: notifications.map((item) => ({
          ...item,
          type_text: notificationTypeLabel(item.type)
        })),
        loadError: '',
        hasLoaded: true
      });
    } catch (err) {
      const serviceStatus = await syncServiceStatus(this, true);
      this.setData({
        notifications: [],
        loadError: serviceStatus.serviceOnline
          ? (err.message || '无法获取通知列表')
          : '本地服务未启动，请先运行 skyanchor-server。',
        hasLoaded: true
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
        title: '已标记',
        icon: 'success'
      });
      this.fetchNotifications();
    } catch (err) {
      wx.showModal({
        title: '请求失败',
        content: err.message || '无法标记为已读',
        showCancel: false
      });
    }
  },

  openOrder(e) {
    const orderId = e.currentTarget.dataset.orderId;
    wx.navigateTo({
      url: `/pages/order-center/index?order_id=${encodeURIComponent(orderId)}&role=receiver`
    });
  }
});
*/

const api = require('../../services/api.js');
const { getDemoProfile } = require('../../utils/demo-profile.js');
const { defaultServiceStatus, syncServiceStatus } = require('../../utils/service-status.js');

const NOTIFICATION_TYPE_LABELS = {
  order_delivered: '送达通知',
  order_failed: '失败通知',
  order_cancelled: '取消通知'
};

const POLL_INTERVAL_MS = 3000;

function notificationTypeLabel(type) {
  return NOTIFICATION_TYPE_LABELS[type] || type || '-';
}

Page({
  data: Object.assign({
    receiverId: '',
    unreadOnly: false,
    notifications: [],
    loading: false,
    loadError: '',
    hasLoaded: false
  }, defaultServiceStatus()),

  onLoad(query) {
    const app = getApp();
    const demoProfile = app.globalData.demoProfile || getDemoProfile();
    const receiverId = query.receiver_id || app.globalData.receiverId || demoProfile.receiverId;
    this._skipNextOnShow = true;
    this._pageVisible = false;
    this._pollTimer = null;
    this._polling = false;
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
      await this.fetchNotifications();
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
    this.fetchNotifications().finally(() => wx.stopPullDownRefresh());
  },

  async initializePage(shouldLoad) {
    if (shouldLoad) {
      await this.fetchNotifications();
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

    await this.fetchNotifications();
  },

  toggleUnreadOnly(e) {
    this.setData(
      {
        unreadOnly: !!e.detail.value
      },
      () => {
        if (this.data.receiverId.trim()) {
          this.fetchNotifications();
        }
      }
    );
  },

  async fetchNotifications(options = {}) {
    const receiverId = this.data.receiverId.trim();
    const silent = !!options.silent;

    if (!receiverId) {
      this.stopPolling();
      this.setData({
        receiverId: '',
        notifications: [],
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
        notifications: [],
        loadError: serviceStatus.serviceMessage,
        hasLoaded: true,
        loading: false
      });
      this.syncPollingState();
      return;
    }

    try {
      const notifications = await api.listNotifications(receiverId, this.data.unreadOnly);
      this.setData({
        notifications: notifications.map((item) => ({
          ...item,
          type_text: notificationTypeLabel(item.type)
        })),
        loadError: '',
        hasLoaded: true,
        loading: false
      });
    } catch (err) {
      const nextStatus = await syncServiceStatus(this, true);
      this.setData({
        notifications: [],
        loadError: nextStatus.serviceOnline
          ? (err.message || '无法获取通知列表')
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
      this.fetchNotifications({ silent: true }).finally(() => {
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

  async markRead(e) {
    const notificationId = Number(e.currentTarget.dataset.notificationId);
    try {
      await api.markNotificationRead(notificationId);
      wx.showToast({
        title: '已标记',
        icon: 'success'
      });
      await this.fetchNotifications();
    } catch (err) {
      wx.showModal({
        title: '请求失败',
        content: err.message || '无法标记为已读',
        showCancel: false
      });
    }
  },

  openOrder(e) {
    const orderId = e.currentTarget.dataset.orderId;
    wx.navigateTo({
      url: `/pages/order-center/index?order_id=${encodeURIComponent(orderId)}&role=receiver`
    });
  }
});
