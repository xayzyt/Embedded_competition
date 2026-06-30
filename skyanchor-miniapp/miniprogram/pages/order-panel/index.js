const api = require('../../services/api.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { defaultServiceStatus, formatClockTime, showServiceDiagnostics: showDiagnostics, syncServiceStatus } = require('../../utils/service-status.js');
const { APRILTAG_OPTIONS, isAssignedApriltag, formatApriltagValue } = require('../../utils/apriltag.js');
const { formatOrderName } = require('../../utils/order-display.js');
const {
  callDispatcher,
  getDispatcherContactName,
  hasDispatcherPhoneNumber,
  markDeliveryCompleteModalShown,
  shouldShowDeliveryCompleteModal
} = require('../../utils/delivery-notice.js');

const ACTIVE_STATUSES = ['created', 'pending_start', 'delivering', 'tag_matched', 'acting'];
const BUSY_STATUSES = ['pending_start', 'delivering', 'tag_matched', 'acting'];
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

function parseTimestamp(value) {
  if (value === undefined || value === null || value === '') {
    return 0;
  }

  const numeric = Number(value);
  if (Number.isFinite(numeric) && numeric > 0) {
    return numeric;
  }

  const parsed = Date.parse(value);
  return Number.isFinite(parsed) ? parsed : 0;
}

function formatDuration(startValue, endValue) {
  const start = parseTimestamp(startValue);
  const end = parseTimestamp(endValue);
  if (!start || !end || end <= start) {
    return '-';
  }

  const totalSeconds = Math.max(1, Math.round((end - start) / 1000));
  if (totalSeconds < 60) {
    return `${totalSeconds} 秒`;
  }

  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  if (minutes < 60) {
    return seconds ? `${minutes} 分 ${seconds} 秒` : `${minutes} 分`;
  }

  const hours = Math.floor(minutes / 60);
  const restMinutes = minutes % 60;
  return restMinutes ? `${hours} 小时 ${restMinutes} 分` : `${hours} 小时`;
}

function buildReport(order, noteText, deviceStateText) {
  if (!TERMINAL_STATUSES.includes(order.status)) {
    return {
      show: false
    };
  }

  const endTime = order.delivered_at || order.failed_at || order.updated_at;
  const durationText = formatDuration(order.created_at, endTime);
  const base = {
    show: true,
    duration_text: durationText,
    tag_text: formatApriltagValue(order.target_id),
    device_text: deviceStateText || '-',
    note_text: noteText || '-',
    class_name: order.status
  };

  if (order.status === 'delivered') {
    return {
      ...base,
      title: '配送报告',
      result_text: '已送达',
      summary_text: '识别、Tag、接驳闭环完成。'
    };
  }

  if (order.status === 'failed') {
    return {
      ...base,
      title: '异常报告',
      result_text: '失败',
      summary_text: noteText || '未送达，已进入失败状态。'
    };
  }

  return {
    ...base,
    title: '取消报告',
    result_text: '已取消',
    summary_text: noteText || '已取消，不再推进。'
  };
}

function buildDetailStatusTiles(status) {
  return [
    {
      key: 'cloud',
      label: '云端',
      value: status.serviceOnline ? '在线' : '离线',
      state: status.serviceOnline ? 'ok' : 'warn'
    },
    {
      key: 'mqtt',
      label: 'MQTT',
      value: status.serviceMqttReady ? '已连接' : '未就绪',
      state: status.serviceMqttReady ? 'ok' : 'warn'
    },
    {
      key: 'device',
      label: '板端',
      value: status.serviceDeviceStateReady ? status.serviceDeviceStateText : '未上报',
      state: status.serviceDeviceStateReady ? 'ok' : 'warn'
    },
    {
      key: 'policy',
      label: '策略',
      value: status.serviceWeatherBlocked ? '天气保护' : (status.serviceAcceptOrders ? '可接单' : '忙碌保护'),
      state: status.serviceWeatherBlocked ? 'warn' : 'ok'
    }
  ];
}

function buildSafetyBadges(order, status) {
  const active = order && BUSY_STATUSES.includes(order.status);
  return [
    {
      key: 'weather',
      label: '天气',
      value: status.serviceWeatherBlocked ? '保护中' : '正常',
      state: status.serviceWeatherBlocked ? 'warn' : 'ok'
    },
    {
      key: 'busy',
      label: '忙碌',
      value: active ? '锁定本单' : '空闲',
      state: active ? 'warn' : 'ok'
    },
    {
      key: 'cancel',
      label: '取消',
      value: order && order.can_cancel ? '可用' : '终态',
      state: order && order.can_cancel ? 'ok' : 'muted'
    },
    {
      key: 'retract',
      label: '回收',
      value: order && order.can_manual_retract ? '可用' : '待接驳',
      state: order && order.can_manual_retract ? 'ok' : 'muted'
    }
  ];
}

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
  const report = buildReport(order, noteText, deviceStateText);

  return {
    ...order,
    order_name_text: formatOrderName(order),
    status_text: statusLabel(order.status),
    apriltag_text: formatApriltagValue(order.target_id),
    note_text: noteText,
    note_label: order.status === 'failed' ? '失败原因' : '备注',
    last_device_state_text: deviceStateText,
    report,
    show_report: report.show,
    flow_steps: buildFlowSteps(order.status),
    is_active: ACTIVE_STATUSES.includes(order.status),
    can_manual_retract: order.status === 'acting',
    can_cancel: !TERMINAL_STATUSES.includes(order.status),
    show_contact_dispatcher: order.status === 'delivered',
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
    const status = String(data.status || '').trim();
    if (status === 'pending_start') {
      return '云端任务已下发';
    }
    if (status === 'delivering') {
      return 'AI / Tag 识别中';
    }
    if (status === 'tag_matched') {
      return 'AprilTag 已确认';
    }
    if (status === 'acting') {
      return '接驳执行中';
    }
    if (status === 'delivered') {
      return '配送完成';
    }
    if (status === 'failed') {
      return '安全失败';
    }
    if (status === 'cancelled') {
      return '任务取消';
    }
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
    .slice()
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
    detailStatusTiles: [],
    safetyBadges: [],
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
      const order = decorateOrder(data.order || null);

      this.setData({
        order,
        events: decorateEvents(data.events || []),
        detailStatusTiles: buildDetailStatusTiles(serviceStatus),
        safetyBadges: buildSafetyBadges(order, serviceStatus),
        loadError: '',
        hasLoaded: true,
        loading: false
      });
      this.maybeShowDeliveryCompleteModal(order);
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

  maybeShowDeliveryCompleteModal(order) {
    if (!this.data.isReceiverView || !order || order.status !== 'delivered') {
      return;
    }

    if (!shouldShowDeliveryCompleteModal(order.order_id)) {
      return;
    }

    markDeliveryCompleteModalShown(order.order_id);
    const canCallDispatcher = hasDispatcherPhoneNumber();
    wx.showModal({
      title: '配送已完成',
      content: `您的 ${order.order_name_text} 已送达，请前往取件。${canCallDispatcher ? `如需协助可联系${getDispatcherContactName()}。` : ''}`,
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

  contactDispatcher() {
    callDispatcher();
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
