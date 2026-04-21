const api = require('../../services/api.js');
const { statusLabel } = require('../../utils/order-status-labels.js');
const { defaultServiceStatus, syncServiceStatus } = require('../../utils/service-status.js');
const { APRILTAG_OPTIONS, isAssignedApriltag, formatApriltagValue } = require('../../utils/apriltag.js');

const ACTIVE_STATUSES = ['created', 'pending_start', 'delivering', 'tag_matched', 'acting'];
const TERMINAL_STATUSES = ['delivered', 'failed', 'cancelled'];
const POLL_INTERVAL_MS = 2000;

function buildPrimaryAction(order) {
  const tagAssigned = isAssignedApriltag(order && order.target_id);

  if (order.status === 'created' && !tagAssigned) {
    return {
      type: 'assign',
      text: '选择 AprilTag',
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
    status_text: statusLabel(order.status),
    apriltag_text: formatApriltagValue(order.target_id),
    note_text: noteText,
    note_label: order.status === 'failed' ? '失败原因' : '备注',
    last_device_state_text: deviceStateText,
    is_active: ACTIVE_STATUSES.includes(order.status),
    can_cancel: !TERMINAL_STATUSES.includes(order.status),
    primary_action: primaryAction.type,
    primary_action_text: primaryAction.text,
    primary_action_disabled: primaryAction.disabled
  };
}

Page({
  data: Object.assign({
    orderId: '',
    viewRole: 'sender',
    isReceiverView: false,
    order: null,
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
        loadError: '',
        hasLoaded: true,
        loading: false
      });
    } catch (err) {
      const nextStatus = await syncServiceStatus(this, true);
      this.setData({
        order: null,
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

  async ensureMessagingReady(actionLabel) {
    const serviceStatus = await syncServiceStatus(this, true);

    if (!serviceStatus.serviceOnline) {
      wx.showModal({
        // 云开发版改为提示云端服务状态，避免继续指向本地 Python 后端。
        title: '云端服务未连接',
        content: serviceStatus.serviceMessage,
        showCancel: false
      });
      return false;
    }

    if (!serviceStatus.serviceMqttReady) {
      wx.showModal({
        // 这里沿用原有页面行为，但明确说明当前检查的是云端调度可用性。
        title: '云端调度未就绪',
        content: `云端调度通道当前未就绪，暂时无法${actionLabel}。请先确认云函数健康检查返回已就绪状态。`,
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
