const api = require('../../services/api.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { defaultServiceStatus, formatClockTime, showServiceDiagnostics: showDiagnostics, syncServiceStatus } = require('../../utils/service-status.js');
const { APRILTAG_OPTIONS, isAssignedApriltag, formatApriltagValue } = require('../../utils/apriltag.js');
const { formatOrderName } = require('../../utils/order-display.js');

const ACTIVE_STATUSES = ['created', 'pending_start', 'delivering', 'tag_matched', 'acting'];
const TERMINAL_STATUSES = ['delivered', 'failed', 'cancelled'];
const POLL_INTERVAL_MS = 2000;
const ORDER_FLOW_STEPS = [
  { key: 'created', label: '调度' },
  { key: 'pending_start', label: '响应' },
  { key: 'delivering', label: '识别' },
  { key: 'acting', label: '执行' },
  { key: 'delivered', label: '送达' }
];
const FLOW_INDEX_BY_STATUS = {
  created: 0,
  pending_start: 1,
  delivering: 2,
  tag_matched: 2,
  acting: 3,
  delivered: 4,
  failed: 3,
  cancelled: 0
};
const EVENT_TYPE_LABELS = {
  created: '订单创建',
  dispatch_assigned: '分配 Tag',
  start_requested: '开始配送',
  manual_retract_requested: '手动回收',
  status_changed: '状态更新',
  device_state: '板端上报'
};

function buildFlowSteps(status) {
  const currentIndex = FLOW_INDEX_BY_STATUS[status] || 0;
  const terminalDone = status === 'delivered';
  const stopped = status === 'failed' || status === 'cancelled';

  return ORDER_FLOW_STEPS.map((step, index) => {
    let state = 'todo';
    if (index < currentIndex || terminalDone) {
      state = 'done';
    } else if (index === currentIndex) {
      state = stopped ? 'stop' : 'active';
    }

    return {
      ...step,
      num: index + 1,
      state
    };
  });
}

function buildPrimaryAction(order) {
  const tagAssigned = isAssignedApriltag(order && order.target_id);

  if (order.status === 'created' && !tagAssigned) {
    return {
      type: 'assign',
      text: '选 Tag',
      disabled: false
    };
  }

  if (order.status === 'created' && tagAssigned) {
    return {
      type: 'start',
      text: '开始配送',
      disabled: false
    };
  }

  return {
    type: 'none',
    text: statusLabel(order.status),
    disabled: true
  };
}

function decorateOrder(order) {
  if (!order) {
    return null;
  }

  const primaryAction = buildPrimaryAction(order);
  const noteText = String(order.note || '').trim();
  const deviceStateText = String(order.last_device_state || '').trim();

  return {
    ...order,
    order_name_text: formatOrderName(order),
    status_text: statusLabel(order.status),
    apriltag_text: formatApriltagValue(order.target_id),
    note_text: noteText,
    note_label: order.status === 'failed' ? '失败原因' : '备注',
    last_device_state_text: deviceStateText,
    flow_steps: buildFlowSteps(order.status),
    is_active: ACTIVE_STATUSES.includes(order.status),
    can_manual_retract: order.status === 'acting',
    can_cancel: !TERMINAL_STATUSES.includes(order.status),
    primary_action: primaryAction.type,
    primary_action_text: primaryAction.text,
    primary_action_disabled: primaryAction.disabled
  };
}

function buildEventTitle(event) {
  const data = event && event.event_data || {};
  const eventType = event && event.event_type || '';

  if (eventType === 'dispatch_assigned') {
    return data.target_id !== undefined ? `分配 Tag ${data.target_id}` : '分配 Tag';
  }

  if (eventType === 'status_changed') {
    return `状态更新为 ${statusLabel(data.status)}`;
  }

  if (eventType === 'manual_retract_requested') {
    return '已请求手动回收托盘';
  }

  if (eventType === 'device_state') {
    const payload = data.payload || {};
    const state = String(payload.state || data.last_device_state || '').trim();
    return state ? `板端上报 ${state}` : '板端上报';
  }

  return EVENT_TYPE_LABELS[eventType] || eventType || '事件';
}

function buildEventDetail(event) {
  const data = event && event.event_data || {};
  const eventType = event && event.event_type || '';

  if (eventType === 'created') {
    return data.order_name ? `订单 ${data.order_name}` : '订单已写入云端';
  }

  if (eventType === 'dispatch_assigned') {
    return `设备 ${data.device_name || '-'} · Tag ${data.target_id}`;
  }

  if (eventType === 'start_requested') {
    return `设备 ${data.device_name || '-'} · request ${data.request_id || '-'}`;
  }

  if (eventType === 'manual_retract_requested') {
    return `设备 ${data.device_name || '-'} · request ${data.request_id || '-'}`;
  }

  if (eventType === 'status_changed') {
    return data.note || data.last_device_state || '状态已同步';
  }

  if (eventType === 'device_state') {
    const payload = data.payload || {};
    const state = String(payload.state || '').trim() || '-';
    const fault = Number(payload.fault || 0);
    const cargo = Number(payload.cargo_received || 0);
    return `state=${state} · fault=${fault} · cargo=${cargo}`;
  }

  return '';
}

function decorateEvents(events) {
  return (events || [])
    .slice(-8)
    .reverse()
    .map((event) => ({
      ...event,
      time_text: formatClockTime(event.created_at),
      title_text: buildEventTitle(event),
      detail_text: buildEventDetail(event)
    }));
}

Page({
  data: Object.assign({
    orderId: '',
    viewRole: 'sender',
    isReceiverView: false,
    order: null,
    events: [],
    actionPending: false,
    loading: false,
    loadError: '',
    hasLoaded: false
  }, defaultServiceStatus()),

  onLoad(query) {
    const viewRole = query.role === 'receiver' ? 'receiver' : 'sender';

    this.setData({
      orderId: query.order_id || '',
      viewRole,
      isReceiverView: viewRole === 'receiver'
    });

    this._skipNextOnShow = true;
    this._pageVisible = false;
    this._pollTimer = null;
    this._polling = false;
    this._selectingTag = false;

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
        hasLoaded: true,
        loading: false
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
        loadError: serviceStatus.serviceMessage,
        hasLoaded: true,
        loading: false
      });
      this.syncPollingState();
      return;
    }

    try {
      const data = await api.getOrder(this.data.orderId);

      this.setData({
        order: decorateOrder(data.order || null),
        events: decorateEvents(data.events || []),
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
    if (this._pollTimer || !this._pageVisible || !this.data.order || !this.data.order.is_active) {
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

  showServiceDiagnostics() {
    showDiagnostics(this.data);
  },

  async ensureMessagingReady(actionLabel) {
    const serviceStatus = await syncServiceStatus(this, true);

    if (!serviceStatus.serviceOnline) {
      wx.showModal({
        title: serviceStatus.serviceBlockTitle || '云端服务未连接',
        content: serviceStatus.serviceBlockAdvice || serviceStatus.serviceMessage,
        showCancel: false
      });
      return false;
    }

    if (!serviceStatus.serviceMqttReady) {
      wx.showModal({
        title: serviceStatus.serviceBlockTitle || '云端调度未就绪',
        content: `${serviceStatus.serviceBlockAdvice || '请检查 MQTT 与板端状态。'}\n\n当前暂时无法${actionLabel}。`,
        showCancel: false
      });
      return false;
    }

    if (!serviceStatus.serviceDeviceStateReady) {
      wx.showModal({
        title: serviceStatus.serviceBlockTitle || '未收到板端状态',
        content: `${serviceStatus.serviceBlockAdvice || '请确认 ESP32 已联网并连接 MQTT。'}\n\n当前暂时无法${actionLabel}。`,
        showCancel: false
      });
      return false;
    }

    if (serviceStatus.serviceWeatherBlocked) {
      wx.showModal({
        title: serviceStatus.serviceBlockTitle || '服务暂不可用',
        content: `${serviceStatus.serviceBlockAdvice || serviceStatus.serviceMessage}\n\n当前暂时无法${actionLabel}。`,
        showCancel: false
      });
      return false;
    }

    if (!serviceStatus.serviceAcceptOrders) {
      wx.showModal({
        title: serviceStatus.serviceBlockTitle || '设备未就绪',
        content: `${serviceStatus.serviceBlockAdvice || serviceStatus.serviceMessage}\n\n当前暂时无法${actionLabel}。`,
        showCancel: false
      });
      return false;
    }

    return true;
  },

  async ensureDeviceCommandReady(actionLabel) {
    const serviceStatus = await syncServiceStatus(this, true);

    if (!serviceStatus.serviceOnline) {
      wx.showModal({
        title: serviceStatus.serviceBlockTitle || '云端服务未连接',
        content: serviceStatus.serviceBlockAdvice || serviceStatus.serviceMessage,
        showCancel: false
      });
      return false;
    }

    if (!serviceStatus.serviceMqttReady) {
      wx.showModal({
        title: serviceStatus.serviceBlockTitle || '云端调度未就绪',
        content: `${serviceStatus.serviceBlockAdvice || '请检查 MQTT 与板端状态。'}\n\n当前暂时无法${actionLabel}。`,
        showCancel: false
      });
      return false;
    }

    if (!serviceStatus.serviceDeviceStateReady) {
      wx.showModal({
        title: serviceStatus.serviceBlockTitle || '未收到板端状态',
        content: `${serviceStatus.serviceBlockAdvice || '请确认 ESP32 已联网并连接 MQTT。'}\n\n当前暂时无法${actionLabel}。`,
        showCancel: false
      });
      return false;
    }

    return true;
  },

  async assignApriltag() {
    const order = this.data.order;
    if (!order || order.primary_action !== 'assign' || this.data.actionPending || this._selectingTag) {
      return;
    }

    this._selectingTag = true;
    const selectedTag = await this.selectApriltag();
    this._selectingTag = false;
    if (selectedTag === null) {
      return;
    }

    await this.runAction(
      () => api.assignOrder(this.data.orderId, { target_id: selectedTag }),
      `已分配 AprilTag ${selectedTag}`
    );
  },

  selectApriltag() {
    return new Promise((resolve) => {
      wx.showActionSheet({
        itemList: APRILTAG_OPTIONS.map((item) => item.label),
        success: (res) => {
          const selected = APRILTAG_OPTIONS[res.tapIndex];
          resolve(selected ? selected.id : null);
        },
        fail: () => resolve(null)
      });
    });
  },

  async startOrder() {
    const order = this.data.order;
    if (!order || order.primary_action !== 'start' || this.data.actionPending) {
      return;
    }

    const ready = await this.ensureMessagingReady('开始配送');
    if (!ready) {
      return;
    }

    await this.runAction(() => api.startOrder(this.data.orderId), '已开始配送');
  },

  async handlePrimaryAction() {
    const order = this.data.order;
    if (!order || order.primary_action_disabled || this.data.actionPending) {
      return;
    }

    if (order.primary_action === 'assign') {
      await this.assignApriltag();
      return;
    }

    if (order.primary_action === 'start') {
      await this.startOrder();
    }
  },

  async cancelOrder() {
    const order = this.data.order;
    if (!order || !order.can_cancel || this.data.isReceiverView || this.data.actionPending) {
      return;
    }

    if (order.status !== 'created') {
      const ready = await this.ensureMessagingReady('取消订单');
      if (!ready) {
        return;
      }
    }

    await this.runAction(() => api.cancelOrder(this.data.orderId), '已取消订单');
  },

  async manualRetractTray() {
    const order = this.data.order;
    if (!order || !order.can_manual_retract || this.data.actionPending) {
      return;
    }

    const ready = await this.ensureDeviceCommandReady('手动回收托盘');
    if (!ready) {
      return;
    }

    const confirmed = await new Promise((resolve) => {
      wx.showModal({
        title: '手动回收托盘',
        content: '请确认货物已放好，再回收托盘并关闭舱门。',
        confirmText: '回收',
        cancelText: '取消',
        success: (res) => resolve(!!res.confirm),
        fail: () => resolve(false)
      });
    });

    if (!confirmed) {
      return;
    }

    await this.runAction(() => api.manualRetractOrder(this.data.orderId), '已请求回收');
  },

  async runAction(action, toastTitle) {
    if (!this.data.orderId || this.data.actionPending) {
      return;
    }

    this.setData({ actionPending: true });
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
    } finally {
      this.setData({ actionPending: false });
    }
  }
});
