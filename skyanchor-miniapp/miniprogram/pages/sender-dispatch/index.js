const api = require('../../services/api.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { buildWeatherSummary, defaultServiceStatus, formatClockTime, showServiceDiagnostics: showDiagnostics, syncServiceStatus } = require('../../utils/service-status.js');
const { formatApriltagValue } = require('../../utils/apriltag.js');
const { formatOrderName } = require('../../utils/order-display.js');

const ACTIVE_STATUSES = ['pending_start', 'delivering', 'tag_matched', 'acting'];
const POLL_INTERVAL_MS = 5000;
const WEATHER_CHECK_INTERVAL_MS = 30000;

function decorateOrder(order) {
  return {
    ...order,
    order_name_text: formatOrderName(order),
    status_text: statusLabel(order.status),
    apriltag_text: formatApriltagValue(order && order.target_id)
  };
}

Page({
  data: Object.assign({
    pendingOrders: [],
    activeOrders: [],
    loading: false,
    loadError: '',
    hasLoaded: false,
    voiceSwitchPending: false,
    lastRefreshText: '未刷新',
    weatherSummary: buildWeatherSummary()
  }, defaultServiceStatus()),

  onLoad() {
    this._skipNextOnShow = true;
    this._pageVisible = false;
    this._pollTimer = null;
    this._weatherTimer = null;
    this._polling = false;
    this._weatherChecking = false;
    this.initializePage();
  },

  async onShow() {
    this._pageVisible = true;
    this.syncWeatherPollingState();

    if (this._skipNextOnShow) {
      this._skipNextOnShow = false;
      this.syncPollingState();
      return;
    }

    await this.fetchOrders({ forceStatus: true });
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

  async initializePage() {
    await this.fetchOrders({ forceStatus: true });
  },

  async fetchOrders(options = {}) {
    const silent = !!options.silent;

    this.setData({
      loading: silent ? this.data.loading : true,
      loadError: silent ? this.data.loadError : ''
    });

    const serviceStatus = await syncServiceStatus(this, !!options.forceStatus);
    this.setData({ weatherSummary: buildWeatherSummary(serviceStatus) });
    if (!serviceStatus.serviceOnline) {
      this.setData({
        pendingOrders: [],
        activeOrders: [],
        loadError: serviceStatus.serviceMessage,
        hasLoaded: true,
        loading: false,
        lastRefreshText: formatClockTime(Date.now())
      });
      this.syncPollingState();
      return;
    }

    try {
      const orders = await api.listOrders({});
      const decoratedOrders = orders.map(decorateOrder);

      this.setData({
        pendingOrders: decoratedOrders.filter((item) => item.status === 'created'),
        activeOrders: decoratedOrders.filter((item) => ACTIVE_STATUSES.includes(item.status)),
        loadError: '',
        hasLoaded: true,
        loading: false,
        lastRefreshText: formatClockTime(Date.now())
      });
    } catch (err) {
      const nextStatus = await syncServiceStatus(this, !!options.forceStatus);
      this.setData({
        pendingOrders: [],
        activeOrders: [],
        weatherSummary: buildWeatherSummary(nextStatus),
        loadError: nextStatus.serviceOnline
          ? (err.message || '无法获取订单列表')
          : nextStatus.serviceMessage,
        hasLoaded: true,
        loading: false,
        lastRefreshText: formatClockTime(Date.now())
      });
    }

    this.syncPollingState();
  },

  startPolling() {
    if (this._pollTimer || !this._pageVisible) {
      return;
    }

    this._pollTimer = setInterval(() => {
      if (this._polling) {
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
    if (this._pageVisible) {
      this.startPolling();
      return;
    }

    this.stopPolling();
  },

  showServiceDiagnostics() {
    showDiagnostics(this.data);
  },

  async runPreflightCheck() {
    const serviceStatus = await syncServiceStatus(this, true);
    this.setData({ weatherSummary: buildWeatherSummary(serviceStatus) });
    showDiagnostics(serviceStatus, '演示自检');
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

  openDetail(e) {
    const orderId = e.currentTarget.dataset.orderId;
    wx.navigateTo({
      url: `/pages/order-panel/index?order_id=${encodeURIComponent(orderId)}&role=sender`
    });
  }
});
