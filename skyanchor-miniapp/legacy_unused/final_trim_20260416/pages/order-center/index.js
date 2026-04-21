/*
const api = require('../../services/api.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { defaultServiceStatus, syncServiceStatus } = require('../../utils/service-status.js');

const EVENT_LABELS = {
  created: '订单创建',
  start_requested: '已请求启动',
  status_changed: '状态更新'
};

const DEVICE_STATE_LABELS = {
  docking: '对接中',
  completed: '已完成',
  fault: '故障'
};

function eventLabel(eventType) {
  return EVENT_LABELS[eventType] || eventType || '-';
}

function deviceStateLabel(state) {
  return DEVICE_STATE_LABELS[state] || state || '-';
}

function parseEventData(eventData) {
  if (!eventData) {
    return {};
  }

  try {
    return JSON.parse(eventData);
  } catch (err) {
    return { raw: String(eventData) };
  }
}

function formatEventData(eventType, eventData) {
  const data = parseEventData(eventData);

  if (data.raw) {
    return data.raw;
  }

  if (eventType === 'created') {
    const parts = [];
    if (data.target_id !== undefined) parts.push(`目标 ID：${data.target_id}`);
    if (data.device_name) parts.push(`设备：${data.device_name}`);
    return parts.join('，') || '订单已创建';
  }

  if (eventType === 'start_requested') {
    return data.note || '已向设备发送启动指令';
  }

  if (eventType === 'status_changed') {
    const parts = [];
    if (data.status) parts.push(`状态：${statusLabel(data.status)}`);
    if (data.last_device_state) parts.push(`设备状态：${deviceStateLabel(data.last_device_state)}`);
    if (data.matched_tag_id) parts.push(`匹配标签：${data.matched_tag_id}`);
    if (data.note) parts.push(`备注：${data.note}`);
    return parts.join('，') || '状态已更新';
  }

  const pairs = Object.keys(data).map((key) => `${key}：${data[key]}`);
  return pairs.join('，') || '{}';
}

function decorateOrder(order) {
  if (!order) {
    return null;
  }

  const canStart = ['created', 'pending_start'].includes(order.status);
  const canCancel = !['delivered', 'failed', 'cancelled'].includes(order.status);

  return {
    ...order,
    status_text: statusLabel(order.status),
    can_start: canStart,
    can_cancel: canCancel
  };
}

Page({
  data: Object.assign({
    orderId: '',
    order: null,
    events: [],
    loading: false,
    loadError: '',
    hasLoaded: false
  }, defaultServiceStatus()),

  onLoad(query) {
    if (query.order_id) {
      this.setData({ orderId: query.order_id });
    }
    this._skipNextOnShow = true;
    this.initializePage();
  },

  async onShow() {
    if (this._skipNextOnShow) {
      this._skipNextOnShow = false;
      return;
    }

    if (this.data.orderId) {
      await this.fetchDetail();
      return;
    }

    await syncServiceStatus(this, true);
  },

  onPullDownRefresh() {
    this.fetchDetail().finally(() => wx.stopPullDownRefresh());
  },

  async initializePage() {
    if (this.data.orderId) {
      await this.fetchDetail();
      return;
    }

    await syncServiceStatus(this, true);
  },

  async fetchDetail() {
    if (!this.data.orderId) {
      this.setData({
        order: this.data.order,
        events: this.data.events,
        loadError: '缺少订单编号，无法查看详情。',
        hasLoaded: true
      });
      return;
    }

    this.setData({
      loading: true,
      loadError: ''
    });

    const serviceStatus = await syncServiceStatus(this, true);
    if (!serviceStatus.serviceOnline) {
      this.setData({
        order: null,
        events: [],
        loadError: serviceStatus.serviceMessage,
        hasLoaded: true,
        loading: false
      });
      return;
    }

    try {
      const data = await api.getOrder(this.data.orderId);
      const order = data.order || null;
      const events = (data.events || []).map((item) => ({
        ...item,
        event_label: eventLabel(item.event_type),
        event_text: formatEventData(item.event_type, item.event_data)
      }));

      this.setData({
        order: decorateOrder(order),
        events,
        loadError: '',
        hasLoaded: true
      });
    } catch (err) {
      const serviceStatus = await syncServiceStatus(this, true);
      this.setData({
        order: null,
        events: [],
        loadError: serviceStatus.serviceOnline
          ? (err.message || '无法获取订单详情')
          : '本地服务未启动，请先运行 skyanchor-server。',
        hasLoaded: true
      });
    } finally {
      this.setData({ loading: false });
    }
  },

  async startOrder() {
    if (!this.data.order || !this.data.order.can_start) {
      return;
    }
    await this.runAction(() => api.startOrder(this.data.orderId), '已启动');
  },

  async cancelOrder() {
    if (!this.data.order || !this.data.order.can_cancel) {
      return;
    }
    await this.runAction(() => api.cancelOrder(this.data.orderId), '已取消');
  },

  async mockActing() {
    await this.runAction(
      () =>
        api.mockOrderState(this.data.orderId, {
          status: 'acting',
          note: '模拟执行中',
          last_device_state: 'docking'
        }),
      '已模拟执行中'
    );
  },

  async mockDelivered() {
    const targetId = this.data.order ? Number(this.data.order.target_id) : 0;
    await this.runAction(
      () =>
        api.mockOrderState(this.data.orderId, {
          status: 'delivered',
          note: '模拟送达完成',
          matched_tag_id: targetId || 1,
          last_device_state: 'completed'
        }),
      '已模拟送达'
    );
  },

  async mockFailed() {
    await this.runAction(
      () =>
        api.mockOrderState(this.data.orderId, {
          status: 'failed',
          note: '模拟失败',
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
      await this.fetchDetail();
    } catch (err) {
      await syncServiceStatus(this, true);
      wx.hideLoading();
      wx.showModal({
        title: '请求失败',
        content: err.message || '无法处理当前请求',
        showCancel: false
      });
    }
  }
});
*/

const api = require('../../services/api.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { defaultServiceStatus, syncServiceStatus } = require('../../utils/service-status.js');

const EVENT_LABELS = {
  created: '订单创建',
  start_requested: '已请求启动',
  status_changed: '状态更新',
  mqtt_ack: 'MQTT 应答',
  device_state: '设备状态'
};

const DEVICE_STATE_LABELS = {
  configured: '已配置',
  wait_approach: '前往目标中',
  auth_passed: '身份校验通过',
  docking: '对接执行中',
  completed: '已完成',
  fault: '故障'
};

const ACTIVE_STATUSES = ['created', 'pending_start', 'delivering', 'tag_matched', 'acting'];
const POLL_INTERVAL_MS = 2000;

function eventLabel(eventType) {
  return EVENT_LABELS[eventType] || eventType || '-';
}

function deviceStateLabel(state) {
  return DEVICE_STATE_LABELS[state] || state || '-';
}

function parseEventData(eventData) {
  if (!eventData) {
    return {};
  }

  try {
    return JSON.parse(eventData);
  } catch (err) {
    return { raw: String(eventData) };
  }
}

function formatEventData(eventType, eventData) {
  const data = parseEventData(eventData);

  if (data.raw) {
    return data.raw;
  }

  if (eventType === 'created') {
    const parts = [];
    if (data.target_id !== undefined) parts.push(`目标 ID：${data.target_id}`);
    if (data.device_name) parts.push(`设备：${data.device_name}`);
    return parts.join('，') || '订单已创建';
  }

  if (eventType === 'start_requested') {
    return data.note || '已向设备发送启动指令';
  }

  if (eventType === 'mqtt_ack') {
    const payload = data.payload || {};
    const parts = [];
    if (payload.cmd) parts.push(`指令：${payload.cmd}`);
    if (payload.code !== undefined) parts.push(`结果码：${payload.code}`);
    if (payload.msg) parts.push(`说明：${payload.msg}`);
    return parts.join('，') || '收到 MQTT 应答';
  }

  if (eventType === 'device_state') {
    const payload = data.payload || {};
    const parts = [];
    if (payload.state) parts.push(`设备状态：${deviceStateLabel(payload.state)}`);
    if (payload.matched_tag_id) parts.push(`匹配标签：${payload.matched_tag_id}`);
    if (payload.note) parts.push(`说明：${payload.note}`);
    return parts.join('，') || '收到设备状态';
  }

  if (eventType === 'status_changed') {
    const parts = [];
    if (data.status) parts.push(`订单状态：${statusLabel(data.status)}`);
    if (data.last_device_state) parts.push(`设备状态：${deviceStateLabel(data.last_device_state)}`);
    if (data.matched_tag_id) parts.push(`匹配标签：${data.matched_tag_id}`);
    if (data.note) parts.push(`说明：${data.note}`);
    return parts.join('，') || '订单状态已更新';
  }

  const pairs = Object.keys(data).map((key) => `${key}：${data[key]}`);
  return pairs.join('，') || '{}';
}

function decorateOrder(order) {
  if (!order) {
    return null;
  }

  const isActive = ACTIVE_STATUSES.includes(order.status);
  const canStart = order.status === 'created';
  const canCancel = !['delivered', 'failed', 'cancelled'].includes(order.status);
  const noteText = order.status === 'pending_start' && order.note === 'accepted'
    ? '\u5df2\u4e0b\u53d1\u542f\u52a8\u6307\u4ee4\uff0c\u7b49\u5f85\u8bbe\u5907\u56de\u4f20\u72b6\u6001'
    : (order.note || '-');

  return {
    ...order,
    status_text: statusLabel(order.status),
    note_text: noteText,
    is_active: isActive,
    can_start: canStart,
    can_cancel: canCancel
  };
}

Page({
  data: Object.assign({
    orderId: '',
    viewRole: 'sender',
    isReceiverView: false,
    order: null,
    events: [],
    loading: false,
    loadError: '',
    hasLoaded: false
  }, defaultServiceStatus()),

  onLoad(query) {
    const viewRole = query.role === 'receiver' ? 'receiver' : 'sender';

    if (query.order_id) {
      this.setData({
        orderId: query.order_id,
        viewRole,
        isReceiverView: viewRole === 'receiver'
      });
    } else {
      this.setData({
        viewRole,
        isReceiverView: viewRole === 'receiver'
      });
    }

    this._skipNextOnShow = true;
    this._pageVisible = false;
    this._pollTimer = null;
    this._polling = false;
    this.initializePage();
  },

  async onShow() {
    this._pageVisible = true;

    if (this._skipNextOnShow) {
      this._skipNextOnShow = false;
      this.syncPollingState();
      return;
    }

    if (this.data.orderId) {
      await this.fetchDetail();
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
    this.fetchDetail().finally(() => wx.stopPullDownRefresh());
  },

  async initializePage() {
    if (this.data.orderId) {
      await this.fetchDetail();
      return;
    }

    await syncServiceStatus(this, true);
  },

  async fetchDetail(options = {}) {
    const silent = !!options.silent;

    if (!this.data.orderId) {
      this.stopPolling();
      this.setData({
        order: null,
        events: [],
        loadError: '缺少订单编号，无法查看详情。',
        hasLoaded: true
      });
      return;
    }

    this.setData({
      loading: silent ? this.data.loading : true,
      loadError: silent ? this.data.loadError : ''
    });

    const serviceStatus = await syncServiceStatus(this, true);
    if (!serviceStatus.serviceOnline) {
      this.setData({
        order: this.data.order,
        events: this.data.events,
        loadError: serviceStatus.serviceMessage,
        hasLoaded: true,
        loading: false
      });
      this.syncPollingState();
      return;
    }

    try {
      const data = await api.getOrder(this.data.orderId);
      const order = decorateOrder(data.order || null);
      const events = (data.events || []).map((item) => ({
        ...item,
        event_label: eventLabel(item.event_type),
        event_text: formatEventData(item.event_type, item.event_data)
      }));

      this.setData({
        order,
        events,
        loadError: '',
        hasLoaded: true,
        loading: false
      });
    } catch (err) {
      const nextStatus = await syncServiceStatus(this, true);
      this.setData({
        order: null,
        events: [],
        loadError: nextStatus.serviceOnline
          ? (err.message || '无法获取订单详情')
          : nextStatus.serviceMessage,
        hasLoaded: true,
        loading: false
      });
    }

    this.syncPollingState();
  },

  startPolling() {
    if (this._pollTimer || !this._pageVisible) {
      return;
    }

    if (!this.data.order || !this.data.order.is_active) {
      return;
    }

    this._pollTimer = setInterval(() => {
      if (this._polling || !this.data.orderId) {
        return;
      }

      this._polling = true;
      this.fetchDetail({ silent: true }).finally(() => {
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
    if (this._pageVisible && this.data.order && this.data.order.is_active) {
      this.startPolling();
      return;
    }

    this.stopPolling();
  },

  async ensureMessagingReady(actionLabel) {
    const serviceStatus = await syncServiceStatus(this, true);

    if (!serviceStatus.serviceOnline) {
      wx.showModal({
        title: '服务未连接',
        content: serviceStatus.serviceMessage,
        showCancel: false
      });
      return false;
    }

    if (!serviceStatus.serviceMqttReady) {
      wx.showModal({
        title: '消息通道未连接',
        content: `MQTT 当前未就绪，无法${actionLabel}。请先确认 skyanchor-server 已成功连接消息通道。`,
        showCancel: false
      });
      return false;
    }

    return true;
  },

  async startOrder() {
    if (!this.data.order || !this.data.order.can_start) {
      return;
    }

    const ready = await this.ensureMessagingReady('开始配送');
    if (!ready) {
      return;
    }

    await this.runAction(() => api.startOrder(this.data.orderId), '已启动');
  },

  async cancelOrder() {
    if (!this.data.order || !this.data.order.can_cancel) {
      return;
    }

    const ready = await this.ensureMessagingReady('取消订单');
    if (!ready) {
      return;
    }

    await this.runAction(() => api.cancelOrder(this.data.orderId), '已取消');
  },

  async mockActing() {
    await this.runAction(
      () =>
        api.mockOrderState(this.data.orderId, {
          status: 'acting',
          note: '模拟执行中',
          last_device_state: 'docking'
        }),
      '已模拟执行中'
    );
  },

  async mockDelivered() {
    const targetId = this.data.order ? Number(this.data.order.target_id) : 0;
    await this.runAction(
      () =>
        api.mockOrderState(this.data.orderId, {
          status: 'delivered',
          note: '模拟送达完成',
          matched_tag_id: targetId || 1,
          last_device_state: 'completed'
        }),
      '已模拟送达'
    );
  },

  async mockFailed() {
    await this.runAction(
      () =>
        api.mockOrderState(this.data.orderId, {
          status: 'failed',
          note: '模拟失败',
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
      await this.fetchDetail();
    } catch (err) {
      await syncServiceStatus(this, true);
      wx.hideLoading();
      wx.showModal({
        title: '请求失败',
        content: err.message || '无法处理当前请求',
        showCancel: false
      });
    }
  }
});
