const api = require('../../services/api.js');
const { getDemoProfile } = require('../../utils/demo-profile.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { buildWeatherSummary, defaultServiceStatus, showServiceDiagnostics: showDiagnostics, syncServiceStatus } = require('../../utils/service-status.js');
const { formatApriltagValue } = require('../../utils/apriltag.js');
const { buildOrderInsight, formatOrderName } = require('../../utils/order-display.js');
const {
  callDispatcher,
  getDispatcherContactName,
  hasDispatcherPhoneNumber,
  markDeliveryCompleteModalShown,
  shouldShowDeliveryCompleteModal
} = require('../../utils/delivery-notice.js');

const POLL_INTERVAL_MS = 3000;
const WEATHER_CHECK_INTERVAL_MS = 30000;
const HIDDEN_STATUSES = ['cancelled'];
const TERMINAL_STATUSES = ['delivered', 'failed', 'cancelled'];
const CLEARABLE_STATUSES = ['delivered', 'failed', 'cancelled'];

function requestDeliveryNoticeSubscription(templateId) {
  const nextTemplateId = String(templateId || '').trim();
  if (!nextTemplateId || !wx.requestSubscribeMessage) {
    return Promise.resolve({
      subscribed: false,
      templateId: nextTemplateId
    });
  }

  return new Promise((resolve) => {
    wx.requestSubscribeMessage({
      tmplIds: [nextTemplateId],
      success: (res) => {
        resolve({
          subscribed: res && res[nextTemplateId] === 'accept',
          templateId: nextTemplateId
        });
      },
      fail: () => {
        resolve({
          subscribed: false,
          templateId: nextTemplateId
        });
      }
    });
  });
}

function canClearOrder(order) {
  return CLEARABLE_STATUSES.includes(order && order.status);
}

function canCancelOrder(order) {
  return order && !TERMINAL_STATUSES.includes(order.status);
}

function buildFailureBrief(order, insight) {
  const faultCode = String(order && order.fault_code || '').trim();
  const note = String(order && order.note || '').trim();
  const reason = String(insight && insight.reason_text || '').trim();
  const source = `${faultCode} ${note} ${reason}`.toLowerCase();

  if (source.includes('weather') || source.includes('天气')) {
    return '天气保护';
  }
  if (source.includes('camera') || source.includes('vision') || source.includes('tag') || source.includes('识别')) {
    return '识别异常';
  }
  if (source.includes('ch32') || source.includes('safe_close') || source.includes('机械')) {
    return '机械异常';
  }
  if (source.includes('manual_retract') || source.includes('回收')) {
    return '托盘回收';
  }

  return reason.replace(/[，,。.!！].*$/, '').slice(0, 8) || '未完成';
}

function decorateOrder(order) {
  const noteText = String(order && order.note || '').trim();
  const insight = buildOrderInsight(order);

  return {
    ...order,
    order_name_text: formatOrderName(order),
    status_text: statusLabel(order.status),
    apriltag_text: formatApriltagValue(order && order.target_id),
    note_text: noteText,
    is_failed: order && order.status === 'failed',
    show_failure: !!insight.show,
    failure_brief_text: buildFailureBrief(order, insight),
    failure_reason_text: insight.reason_text,
    failure_advice_text: insight.advice_text,
    can_cancel: canCancelOrder(order),
    can_clear: canClearOrder(order)
  };
}

function filterVisibleOrders(orders) {
  return (orders || []).filter((item) => {
    if (HIDDEN_STATUSES.includes(item.status)) {
      return false;
    }

    return true;
  });
}

Page({
  data: Object.assign({
    receiverId: '',
    orders: [],
    creating: false,
    loading: false,
    clearingOrderId: '',
    cancellingOrderId: '',
    loadError: '',
    hasLoaded: false,
    voiceSwitchPending: false,
    weatherSummary: buildWeatherSummary()
  }, defaultServiceStatus()),

  onLoad(query) {
    const app = getApp();
    const demoProfile = app.globalData.demoProfile || getDemoProfile();
    const receiverId = query.receiver_id || app.globalData.receiverId || wx.getStorageSync('skyanchor_receiver_id') || demoProfile.receiverId;

    this._skipNextOnShow = true;
    this._pageVisible = false;
    this._pollTimer = null;
    this._weatherTimer = null;
    this._polling = false;
    this._weatherChecking = false;

    this.setData({ receiverId });
    this.initializePage(!!receiverId);
  },

  async onShow() {
    this._pageVisible = true;
    this.syncWeatherPollingState();

    if (this._skipNextOnShow) {
      this._skipNextOnShow = false;
      this.syncPollingState();
      return;
    }

    if (this.data.receiverId.trim()) {
      await this.fetchOrders({ forceStatus: true });
      return;
    }

    const serviceStatus = await syncServiceStatus(this, true);
    this.setData({ weatherSummary: buildWeatherSummary(serviceStatus) });
    this.syncPollingState();
  },

  onHide() {
    this._pageVisible = false;
    this.stopPolling();
    this.stopWeatherPolling();
  },

  onUnload() {
    this._pageVisible = false;
    this.stopPolling();
    this.stopWeatherPolling();
  },

  onPullDownRefresh() {
    this.fetchOrders({ forceStatus: true }).finally(() => wx.stopPullDownRefresh());
  },

  async initializePage(shouldLoad) {
    if (shouldLoad) {
      await this.fetchOrders({ forceStatus: true });
      return;
    }

    const serviceStatus = await syncServiceStatus(this, true);
    this.setData({ weatherSummary: buildWeatherSummary(serviceStatus) });
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

  async toggleVoice(e) {
    if (this.data.voiceSwitchPending) {
      return;
    }

    const enabled = !!(e && e.detail && e.detail.value);
    const previous = !!this.data.serviceVoiceEnabled;

    if (!this.data.serviceOnline || !this.data.serviceMqttReady || !this.data.serviceDeviceStateReady) {
      this.setData({ serviceVoiceEnabled: previous });
      wx.showModal({
        title: '暂时不能切换',
        content: '请先确认云端、MQTT 和板端状态正常。',
        showCancel: false
      });
      return;
    }

    this.setData({
      voiceSwitchPending: true,
      serviceVoiceEnabled: enabled
    });

    try {
      await api.setVoiceEnabled(enabled);
      const serviceStatus = await syncServiceStatus(this, true);
      this.setData({
        weatherSummary: buildWeatherSummary(serviceStatus),
        voiceSwitchPending: false
      });
      wx.showToast({
        title: enabled ? '语音已开' : '语音已关',
        icon: 'success'
      });
    } catch (err) {
      const serviceStatus = await syncServiceStatus(this, true);
      this.setData({
        weatherSummary: buildWeatherSummary(serviceStatus),
        voiceSwitchPending: false
      });
      wx.showModal({
        title: '切换失败',
        content: err.message || '语音开关未更新成功',
        showCancel: false
      });
    }
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

    if (this.data.creating) {
      return;
    }

    this.setData({ creating: true });
    const serviceStatus = await syncServiceStatus(this, true);
    this.setData({ weatherSummary: buildWeatherSummary(serviceStatus) });
    if (!serviceStatus.serviceOnline) {
      this.setData({ creating: false });
      wx.showModal({
        title: serviceStatus.serviceBlockTitle || '云端服务未连接',
        content: serviceStatus.serviceBlockAdvice || serviceStatus.serviceMessage,
        showCancel: false
      });
      return;
    }

    if (serviceStatus.serviceActionBlocked) {
      this.setData({ creating: false });
      wx.showModal({
        title: serviceStatus.serviceBlockTitle || '服务暂不可用',
        content: serviceStatus.serviceBlockAdvice || serviceStatus.serviceMessage,
        showCancel: false
      });
      return;
    }

    const templateId = serviceStatus.serviceDeliveryNoticeTemplateId ||
      app.globalData.deliveryNoticeTemplateId ||
      this.data.serviceDeliveryNoticeTemplateId;
    const deliveryNotice = await requestDeliveryNoticeSubscription(templateId);

    wx.showLoading({ title: '提交订单中' });

    try {
      const order = await api.createOrder({
        receiver_id: receiverId,
        delivery_notice_subscribed: deliveryNotice.subscribed,
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
      const nextStatus = await syncServiceStatus(this, true);
      this.setData({ weatherSummary: buildWeatherSummary(nextStatus) });
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

  confirmCancelOrder(orderName) {
    return new Promise((resolve) => {
      wx.showModal({
        title: '取消订单',
        content: `确定取消 ${orderName || '该订单'} 吗？已开始配送的订单会同步通知板端停止任务。`,
        confirmText: '取消订单',
        cancelText: '返回',
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

    const serviceStatus = await syncServiceStatus(this, !!options.forceStatus);
    this.setData({ weatherSummary: buildWeatherSummary(serviceStatus) });
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

      const visibleOrders = filterVisibleOrders(orders.map(decorateOrder));
      this.setData({
        orders: visibleOrders,
        loadError: '',
        hasLoaded: true,
        loading: false
      });
      this.maybeShowDeliveredOrderModal(visibleOrders);
    } catch (err) {
      const nextStatus = await syncServiceStatus(this, !!options.forceStatus);
      this.setData({
        orders: [],
        weatherSummary: buildWeatherSummary(nextStatus),
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

  async refreshWeatherSummary(options = {}) {
    if (this._weatherChecking) {
      return;
    }

    this._weatherChecking = true;
    try {
      const serviceStatus = await syncServiceStatus(this, !!options.force);
      if (!this._pageVisible) {
        return;
      }
      this.setData({ weatherSummary: buildWeatherSummary(serviceStatus) });
    } finally {
      this._weatherChecking = false;
    }
  },

  startWeatherPolling() {
    if (this._weatherTimer || !this._pageVisible) {
      return;
    }

    this._weatherTimer = setInterval(() => {
      this.refreshWeatherSummary({ force: true });
    }, WEATHER_CHECK_INTERVAL_MS);
  },

  stopWeatherPolling() {
    if (this._weatherTimer) {
      clearInterval(this._weatherTimer);
      this._weatherTimer = null;
    }
  },

  syncWeatherPollingState() {
    if (this._pageVisible) {
      this.startWeatherPolling();
      return;
    }

    this.stopWeatherPolling();
  },

  syncPollingState() {
    if (this._pageVisible && this.data.receiverId.trim()) {
      this.startPolling();
      return;
    }

    this.stopPolling();
  },

  maybeShowDeliveredOrderModal(orders) {
    const deliveredOrder = (orders || []).find((item) => {
      return item && item.status === 'delivered' && shouldShowDeliveryCompleteModal(item.order_id);
    });
    if (!deliveredOrder) {
      return;
    }

    markDeliveryCompleteModalShown(deliveredOrder.order_id);
    const canCallDispatcher = hasDispatcherPhoneNumber();
    wx.showModal({
      title: '配送已完成',
      content: `您的 ${deliveredOrder.order_name_text} 已送达，请前往取件。${canCallDispatcher ? `如需协助可联系${getDispatcherContactName()}。` : ''}`,
      confirmText: canCallDispatcher ? '联系配送员' : '我知道了',
      cancelText: '稍后',
      showCancel: canCallDispatcher,
      success: (res) => {
        if (res.confirm && canCallDispatcher) {
          callDispatcher();
        }
      }
    });
  },

  async cancelOrder(e) {
    const orderId = e.currentTarget.dataset.orderId;
    const orderName = e.currentTarget.dataset.orderName;
    if (!orderId || this.data.cancellingOrderId) {
      return;
    }

    const confirmed = await this.confirmCancelOrder(orderName);
    if (!confirmed) {
      return;
    }

    this.setData({ cancellingOrderId: orderId });
    wx.showLoading({ title: '取消中' });

    try {
      await api.cancelOrder(orderId);
      this.setData({
        orders: this.data.orders.filter((item) => item.order_id !== orderId)
      });
      wx.hideLoading();
      wx.showToast({
        title: '已取消',
        icon: 'success'
      });
      this.fetchOrders({ silent: true }).catch(() => {});
    } catch (err) {
      wx.hideLoading();
      wx.showModal({
        title: '取消失败',
        content: err.message || '无法取消订单',
        showCancel: false
      });
    } finally {
      this.setData({ cancellingOrderId: '' });
    }
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
