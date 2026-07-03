const api = require('../../services/api.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { buildWeatherSummary, defaultServiceStatus, formatClockTime, showServiceDiagnostics: showDiagnostics, syncServiceStatus } = require('../../utils/service-status.js');
const { APRILTAG_OPTIONS, isAssignedApriltag, formatApriltagValue } = require('../../utils/apriltag.js');
const {
  buildOrderInsight,
  formatDeviceState,
  formatOrderName,
  formatVoiceNodeFromDeviceState,
  formatVoiceNodeFromStatus
} = require('../../utils/order-display.js');
const {
  callDispatcher,
  hasDispatcherPhoneNumber,
  markDeliveryCompleteModalShown,
  shouldShowDeliveryCompleteModal
} = require('../../utils/delivery-notice.js');

const ACTIVE_STATUSES = ['created', 'pending_start', 'delivering', 'tag_matched', 'acting'];
const TERMINAL_STATUSES = ['delivered', 'failed', 'cancelled'];
const DEMO_RESET_STATUSES = ['pending_start', 'delivering', 'tag_matched', 'acting'];
const POLL_INTERVAL_MS = 2000;
const WEATHER_CHECK_INTERVAL_MS = 30000;
const DELIVERY_PHOTO_LOCAL_PATH_PREFIX = 'delivery_photo_local_path:';
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
  manual_retract_requested: '回收',
  demo_reset_requested: '演示复位',
  delivery_photo_uploaded: '送达照片',
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

function buildReport(order, deviceStateText, insight) {
  if (!TERMINAL_STATUSES.includes(order.status)) {
    return {
      show: false
    };
  }

  const endTime = order.delivered_at || order.failed_at || order.updated_at;
  const durationText = formatDuration(order.created_at, endTime);
  const finishedText = formatClockTime(endTime);
  const stateText = formatDeviceState(deviceStateText);
  const base = {
    show: true,
    duration_text: durationText,
    finished_text: finishedText,
    tag_text: formatApriltagValue(order.target_id),
    device_text: stateText,
    reason_text: insight && insight.reason_text || '',
    advice_text: insight && insight.advice_text || '',
    class_name: order.status
  };

  if (order.status === 'delivered') {
    return {
      ...base,
      title: '完成',
      result_text: '送达',
      summary_text: '语音播报、订单时间线和板端状态已完成闭环。'
    };
  }

  if (order.status === 'failed') {
    return {
      ...base,
      title: '异常',
      result_text: '失败',
      summary_text: insight && insight.reason_text || '异常结束，任务已停止。'
    };
  }

  return {
    ...base,
    title: '取消',
    result_text: '取消',
    summary_text: insight && insight.reason_text || '已停止，不再推进。'
  };
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
  const deviceStateText = String(order.last_device_state || '').trim();
  const issue = buildOrderInsight(order);
  const report = buildReport(order, deviceStateText, issue);
  const photoFileId = String(order.delivery_photo_file_id || '').trim();
  const photoLocalPath = String(order.delivery_photo_local_path || getCachedDeliveryPhotoPath(order.order_id)).trim();
  const photoStatus = String(order.delivery_photo_status || '').trim();
  const photoEmpty = !photoStatus || photoStatus === 'none';
  const photoReady = order.status === 'delivered';
  const photoText = photoReady
    ? '查看照片'
    : (photoEmpty ? '暂无照片' : '照片待同步');

  return {
    ...order,
    order_name_text: formatOrderName(order),
    status_text: statusLabel(order.status),
    apriltag_text: formatApriltagValue(order.target_id),
    last_device_state_text: deviceStateText ? formatDeviceState(deviceStateText) : '',
    failure_reason_text: issue.reason_text,
    failure_advice_text: issue.advice_text,
    issue,
    report,
    show_report: report.show,
    delivery_photo_file_id: photoFileId,
    delivery_photo_local_path: photoLocalPath,
    delivery_photo_visible: order.status === 'delivered',
    delivery_photo_ready: photoReady,
    delivery_photo_text: photoText,
    flow_steps: buildFlowSteps(order.status),
    is_active: ACTIVE_STATUSES.includes(order.status),
    can_manual_retract: order.status === 'acting',
    can_demo_reset: DEMO_RESET_STATUSES.includes(order.status),
    can_cancel: order.status === 'created',
    show_contact_dispatcher: order.status === 'delivered' && hasDispatcherPhoneNumber(),
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
    const voiceText = formatVoiceNodeFromStatus(status);
    if (voiceText) {
      return voiceText;
    }
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
    return '播报：托盘回收中';
  }

  if (eventType === 'demo_reset_requested') {
    return '播报：演示复位';
  }

  if (eventType === 'delivery_photo_uploaded') {
    return '送达照片已上传';
  }

  if (eventType === 'delivery_notice_sent') {
    return '完成通知已发送';
  }

  if (eventType === 'delivery_notice_failed') {
    return '完成通知发送失败';
  }

  if (eventType === 'device_state') {
    const payload = data.payload || {};
    const state = String(payload.state || data.last_device_state || '').trim();
    const voiceText = formatVoiceNodeFromDeviceState(payload, state);
    return voiceText || (state ? `板端上报：${formatDeviceState(state)}` : '板端上报');
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

  if (eventType === 'demo_reset_requested') {
    return `设备 ${data.device_name || '-'} · request ${data.request_id || '-'}`;
  }

  if (eventType === 'delivery_photo_uploaded') {
    return data.size ? `照片 ${Math.round(Number(data.size) / 1024)} KB` : '照片已存入云端';
  }

  if (eventType === 'delivery_notice_sent') {
    return '微信订阅消息已推送';
  }

  if (eventType === 'delivery_notice_failed') {
    return data.message ? String(data.message).slice(0, 80) : '订阅消息未发送成功';
  }

  if (eventType === 'status_changed') {
    const stateText = formatDeviceState(data.last_device_state);
    return data.note || (stateText !== '-' ? `状态已同步：${stateText}` : '状态已同步');
  }

  if (eventType === 'device_state') {
    const payload = data.payload || {};
    const state = String(payload.state || '').trim() || '-';
    const fault = Number(payload.fault || 0);
    const cargo = Number(payload.cargo_received || 0);
    const faultCode = String(payload.fault_code || '').trim();
    const note = String(payload.note || '').trim();
    const parts = [`状态 ${formatDeviceState(state)}`];
    if (fault) {
      parts.push('故障标记');
    }
    if (faultCode) {
      parts.push(`原因码 ${faultCode}`);
    }
    if (cargo) {
      parts.push('货物已接收');
    }
    if (note) {
      parts.push(note);
    }
    return parts.join(' · ');
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

function buildDeliveryReportModal(order, canContactDispatcher) {
  const report = order && order.report || {};
  return {
      order_name_text: order ? order.order_name_text : '',
      result_text: report.result_text || '送达',
      summary_text: report.summary_text || '闭环完成',
      tag_text: report.tag_text || '-',
      duration_text: report.duration_text || '-',
      finished_text: report.finished_text || '-',
      device_text: report.device_text || '-',
      photo_file_id: order && order.delivery_photo_file_id || '',
      photo_local_path: order && order.delivery_photo_local_path || '',
      photo_text: order && order.delivery_photo_text || '',
      can_contact_dispatcher: !!canContactDispatcher
  };
}

function getDeliveryPhotoLocalPathKey(orderId) {
  const text = String(orderId || '').trim();
  return `${DELIVERY_PHOTO_LOCAL_PATH_PREFIX}${text}`;
}

function getCachedDeliveryPhotoPath(orderId) {
  if (!orderId || !wx.getStorageSync) {
    return '';
  }
  try {
    return String(wx.getStorageSync(getDeliveryPhotoLocalPathKey(orderId)) || '').trim();
  } catch (error) {
    return '';
  }
}

function setCachedDeliveryPhotoPath(orderId, filePath) {
  if (!orderId || !filePath || !wx.setStorageSync) {
    return;
  }
  try {
    wx.setStorageSync(getDeliveryPhotoLocalPathKey(orderId), filePath);
  } catch (error) {
    // 本地路径缓存失败不影响本次预览。
  }
}

function cleanupDeliveryPhotoTempFiles(fs) {
  if (!fs || !wx.env || !wx.env.USER_DATA_PATH) {
    return Promise.resolve();
  }

  return new Promise((resolve) => {
    fs.readdir({
      dirPath: wx.env.USER_DATA_PATH,
      success: (res) => {
        const files = (res && res.files || [])
          .filter((name) => {
            const text = String(name || '');
            return /^skyanchor-delivery-photo-.*\.jpg$/.test(text) ||
              /^ORD-.*-photo\.jpg$/.test(text);
          });
        if (!files.length) {
          resolve();
          return;
        }

        let pending = files.length;
        const done = () => {
          pending -= 1;
          if (pending <= 0) {
            resolve();
          }
        };

        files.forEach((name) => {
          fs.unlink({
            filePath: `${wx.env.USER_DATA_PATH}/${name}`,
            success: done,
            fail: done
          });
        });
      },
      fail: () => resolve()
    });
  });
}

function writeBase64File(fs, filePath, base64Data) {
  return new Promise((resolve, reject) => {
    fs.writeFile({
      filePath,
      data: base64Data,
      encoding: 'base64',
      success: () => resolve(filePath),
      fail: (err) => reject(new Error(err && err.errMsg || '照片临时文件写入失败'))
    });
  });
}

async function writeDeliveryPhotoTempFile(orderId, base64Data) {
  const b64 = String(base64Data || '').trim();
  if (!b64 || !wx.getFileSystemManager || !wx.env || !wx.env.USER_DATA_PATH) {
    return Promise.reject(new Error('照片临时文件不可用'));
  }

  const safeName = String(orderId || 'delivery')
    .replace(/[^0-9A-Za-z_-]/g, '_')
    .slice(0, 64);
  const filePath = `${wx.env.USER_DATA_PATH}/skyanchor-delivery-photo-${safeName || 'delivery'}.jpg`;
  const fs = wx.getFileSystemManager();

  await cleanupDeliveryPhotoTempFiles(fs);
  try {
    return await writeBase64File(fs, filePath, b64);
  } catch (error) {
    await cleanupDeliveryPhotoTempFiles(fs);
    try {
      return await writeBase64File(fs, filePath, b64);
    } catch (retryError) {
      const message = String(retryError && retryError.message || error && error.message || '').toLowerCase();
      if (message.includes('maximum size') || message.includes('storage limit')) {
        throw new Error('微信本地缓存已满，请清理小程序缓存后重试');
      }
      throw retryError;
    }
  }
}

Page({
  data: Object.assign({
    orderId: '',
    viewRole: 'sender',
    isReceiverView: false,
    order: null,
    events: [],
    runtimeSummary: buildWeatherSummary(),
    actionPending: false,
    loading: false,
    loadError: '',
    hasLoaded: false,
    showDeliveryReportModal: false,
    deliveryReportModal: null
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
    this._weatherTimer = null;
    this._polling = false;
    this._weatherChecking = false;
    this._selectingTag = false;

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

    if (this.data.orderId) {
      await this.fetchDetail({ forceStatus: true });
      return;
    }

    const serviceStatus = await syncServiceStatus(this, true);
    this.setData({ runtimeSummary: buildWeatherSummary(serviceStatus) });
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
    this.fetchDetail({ forceStatus: true }).finally(() => wx.stopPullDownRefresh());
  },

  async initializePage() {
    if (this.data.orderId) {
      await this.fetchDetail({ forceStatus: true });
      return;
    }

    const serviceStatus = await syncServiceStatus(this, true);
    this.setData({ runtimeSummary: buildWeatherSummary(serviceStatus) });
  },

  async fetchDetail(options = {}) {
    const silent = !!options.silent;

    if (!this.data.orderId) {
      this.stopPolling();
      this.setData({
        order: null,
        events: [],
        loadError: '缺少订单，无法查看。',
        hasLoaded: true,
        loading: false
      });
      return;
    }

    this.setData({
      loading: silent ? this.data.loading : true,
      loadError: silent ? this.data.loadError : ''
    });

    const serviceStatus = await syncServiceStatus(this, !!options.forceStatus);
    if (!serviceStatus.serviceOnline) {
      this.setData({
        runtimeSummary: buildWeatherSummary(serviceStatus),
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
        runtimeSummary: buildWeatherSummary(serviceStatus),
        loadError: '',
        hasLoaded: true,
        loading: false
      });
      this.maybeShowDeliveryReport(order);
    } catch (err) {
      const nextStatus = await syncServiceStatus(this, !!options.forceStatus);
      this.setData({
        order: null,
        events: [],
        runtimeSummary: buildWeatherSummary(nextStatus),
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
      this.setData({ runtimeSummary: buildWeatherSummary(serviceStatus) });
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
    if (this._pageVisible && this.data.order && this.data.order.is_active) {
      this.startPolling();
      return;
    }

    this.stopPolling();
  },

  showServiceDiagnostics() {
    showDiagnostics(this.data);
  },

  maybeShowDeliveryReport(order) {
    if (!this.data.isReceiverView || !order || order.status !== 'delivered') {
      return;
    }

    if (!shouldShowDeliveryCompleteModal(order.order_id)) {
      return;
    }

    markDeliveryCompleteModalShown(order.order_id);
    this.setData({
      showDeliveryReportModal: true,
      deliveryReportModal: buildDeliveryReportModal(order, this.data.isReceiverView && order.show_contact_dispatcher)
    });
  },

  closeDeliveryReport() {
    this.setData({
      showDeliveryReportModal: false
    });
  },

  noop() {},

  contactDispatcher() {
    if (this.data.showDeliveryReportModal) {
      this.setData({
        showDeliveryReportModal: false
      });
    }
    callDispatcher();
  },

  async previewDeliveryPhoto() {
    const localPath = String(
      (this.data.deliveryReportModal && this.data.deliveryReportModal.photo_local_path) ||
      (this.data.order && this.data.order.delivery_photo_local_path) ||
      ''
    ).trim();
    if (localPath) {
      wx.previewImage({
        current: localPath,
        urls: [localPath]
      });
      return;
    }

    let fileID = String(
      (this.data.deliveryReportModal && this.data.deliveryReportModal.photo_file_id) ||
      (this.data.order && this.data.order.delivery_photo_file_id) ||
      ''
    ).trim();
    if (!fileID) {
      const orderId = String(this.data.orderId || this.data.order && this.data.order.order_id || '').trim();
      if (!orderId) {
        wx.showToast({
          title: this.data.order && this.data.order.delivery_photo_text || '照片待同步',
          icon: 'none'
        });
        return;
      }

      let syncedPhotoError = '';
      let inlinePhotoB64 = '';
      try {
        wx.showLoading({ title: '同步照片' });
        const data = await api.syncDeliveryPhoto(orderId);
        const order = decorateOrder(data.order || null);
        syncedPhotoError = String(data.photo_error || order && order.delivery_photo_error || '').trim();
        inlinePhotoB64 = String(data.photo_inline_b64 || '').trim();
        this.setData({
          order,
          events: decorateEvents(data.events || [])
        });
        fileID = String(order && order.delivery_photo_file_id || data.photo_file_id || '').trim();
      } catch (err) {
        wx.hideLoading();
        wx.showToast({
          title: err.message || '照片待同步',
          icon: 'none'
        });
        return;
      }
      wx.hideLoading();

      if (!fileID) {
        if (inlinePhotoB64) {
          try {
            wx.showLoading({ title: '打开照片' });
            const filePath = await writeDeliveryPhotoTempFile(orderId, inlinePhotoB64);
            setCachedDeliveryPhotoPath(orderId, filePath);
            const nextOrder = decorateOrder({
              ...(this.data.order || {}),
              delivery_photo_local_path: filePath
            });
            this.setData({
              order: nextOrder,
              deliveryReportModal: this.data.deliveryReportModal
                ? buildDeliveryReportModal(nextOrder, this.data.isReceiverView && nextOrder.show_contact_dispatcher)
                : this.data.deliveryReportModal
            });
            wx.previewImage({
              current: filePath,
              urls: [filePath]
            });
          } catch (err) {
            wx.showToast({
              title: err.message || '照片打开失败',
              icon: 'none'
            });
          } finally {
            wx.hideLoading();
          }
          return;
        }
        const photoError = syncedPhotoError || String(this.data.order && this.data.order.delivery_photo_error || '').trim();
        wx.showToast({
          title: photoError || '照片同步失败',
          icon: 'none'
        });
        return;
      }
    }
    if (!wx.cloud || typeof wx.cloud.getTempFileURL !== 'function') {
      wx.showToast({
        title: '云存储不可用',
        icon: 'none'
      });
      return;
    }
    wx.showLoading({ title: '打开照片' });
    try {
      const result = await wx.cloud.getTempFileURL({
        fileList: [fileID]
      });
      const item = result && result.fileList && result.fileList[0];
      const url = item && item.tempFileURL;
      if (!url) {
        throw new Error('未获取到照片地址');
      }
      wx.previewImage({
        current: url,
        urls: [url]
      });
    } catch (err) {
      wx.showToast({
        title: err.message || '照片打开失败',
        icon: 'none'
      });
    } finally {
      wx.hideLoading();
    }
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

  async ensureCancelCommandReady(actionLabel) {
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
        content: `${serviceStatus.serviceBlockAdvice || '请检查 MQTT 与板端连接。'}\n\n当前暂时无法${actionLabel}。`,
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
      const ready = await this.ensureCancelCommandReady('取消订单');
      if (!ready) {
        return;
      }
    }

    await this.runAction(() => api.cancelOrder(this.data.orderId), '已取消订单');
  },

  async demoResetOrder() {
    const order = this.data.order;
    if (!order || !order.can_demo_reset || this.data.isReceiverView || this.data.actionPending) {
      return;
    }

    const ready = await this.ensureCancelCommandReady('演示复位');
    if (!ready) {
      return;
    }

    await this.runAction(() => api.demoResetOrder(this.data.orderId), '已触发复位');
  },

  async manualRetractTray() {
    const order = this.data.order;
    if (!order || !order.can_manual_retract || this.data.actionPending) {
      return;
    }

    const ready = await this.ensureDeviceCommandReady('回收托盘');
    if (!ready) {
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
