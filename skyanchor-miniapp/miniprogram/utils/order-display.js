function orderSuffix(orderId) {
  const text = String(orderId || '').trim();
  if (!text) {
    return '';
  }

  const parts = text.split('-').filter(Boolean);
  const lastPart = parts.length ? parts[parts.length - 1] : text;
  return lastPart.replace(/[^0-9A-Za-z]/g, '').toUpperCase().slice(-6);
}

function formatOrderName(order) {
  const explicitName = String(order && order.order_name || '').trim();
  if (explicitName) {
    return explicitName;
  }

  const suffix = orderSuffix(order && order.order_id);
  return suffix ? `SKY-${suffix}` : 'SKY-ORDER';
}

const DEVICE_STATE_LABELS = {
  configured: '已配置',
  waiting_tag_match: '等待识别',
  wait_approach: '等待靠近',
  auth_passed: 'Tag 已确认',
  docking: '接驳中',
  completed: '已完成',
  fault: '故障',
  cancelled: '已取消',
  ready: '已上报'
};

const STATUS_VOICE_LABELS = {
  delivering: '播报：无人机识别中',
  tag_matched: '播报：AprilTag 已确认',
  acting: '播报：接驳执行中',
  delivered: '播报：配送完成',
  failed: '播报：异常停止',
  cancelled: '播报：任务取消'
};

const DEVICE_VOICE_LABELS = {
  wait_approach: '播报：无人机识别通过',
  auth_passed: '播报：AprilTag 已确认',
  docking: '播报：接驳执行中',
  completed: '播报：配送完成',
  fault: '播报：异常停止',
  cancelled: '播报：任务取消'
};

const FAULT_CODE_LABELS = {
  demo_reset: '演示复位已触发，任务已停止并回到安全状态。',
  cloud_cancelled: '云端取消了订单，板端任务已停止。',
  task_cancelled: '任务已取消，板端不再推进。',
  weather_blocked: '天气保护触发，配送已安全停止。',
  manual_retract_requested: '已请求托盘回收，任务按安全流程停止。',
  safe_close_failed: '安全关闭命令未完成，机构需要现场确认。',
  ch32_timeout: 'CH32 长时间未响应，机械执行链路超时。',
  ch32_ack_timeout: 'CH32 未确认执行命令，机械控制链路超时。',
  ch32_rejected: 'CH32 拒绝了执行命令，机构未进入预期动作。',
  camera_failed: '摄像头链路异常，识别流程未能继续。',
  vision_failed: '识别链路异常，AprilTag 未能稳定确认。',
  task_fault: '板端进入故障状态，任务已停止。'
};

const MECHANICAL_FAULT_CODES = [
  'manual_retract_requested',
  'safe_close_failed',
  'ch32_timeout',
  'ch32_ack_timeout',
  'ch32_rejected'
];

const VISION_FAULT_CODES = ['camera_failed', 'vision_failed'];

function normalizeText(value) {
  return String(value || '').trim();
}

function containsAny(text, keywords) {
  const source = normalizeText(text).toLowerCase();
  return keywords.some((keyword) => source.includes(String(keyword).toLowerCase()));
}

function formatDeviceState(state) {
  const text = normalizeText(state);
  return DEVICE_STATE_LABELS[text] || text || '-';
}

function extractFaultCode(order) {
  const explicitCode = normalizeText(order && order.fault_code);
  if (FAULT_CODE_LABELS[explicitCode]) {
    return explicitCode;
  }

  const note = normalizeText(order && order.note);
  return Object.keys(FAULT_CODE_LABELS).find((code) => containsAny(note, [code])) || '';
}

function buildOrderInsight(order) {
  const status = normalizeText(order && order.status);
  const note = normalizeText(order && order.note);
  const deviceState = normalizeText(order && order.last_device_state);
  const deviceText = formatDeviceState(deviceState);
  const faultCode = extractFaultCode(order);

  if (status === 'failed') {
    if (
      faultCode === 'weather_blocked' ||
      containsAny(note, ['天气', 'weather', 'severe', 'blocked']) ||
      containsAny(deviceState, ['weather'])
    ) {
      return {
        show: true,
        reason_text: '天气保护触发，配送已安全停止。',
        advice_text: '解除天气管制后，刷新自检并重新发起演示。',
        level: 'warn'
      };
    }

    if (MECHANICAL_FAULT_CODES.includes(faultCode) ||
      containsAny(note, ['ch32', 'ack', 'ctrl', 'door', 'tray', 'mechanical', '机械', '回收'])) {
      return {
        show: true,
        reason_text: FAULT_CODE_LABELS[faultCode] || note || `机械执行异常：${deviceText}`,
        advice_text: '检查 CH32 供电、串口连接和机构状态，确认安全后重试。',
        level: 'error'
      };
    }

    if (VISION_FAULT_CODES.includes(faultCode) ||
      containsAny(note, ['camera', 'vision', 'ai', 'tag', '识别', 'apriltag'])) {
      return {
        show: true,
        reason_text: FAULT_CODE_LABELS[faultCode] || note || `识别链路异常：${deviceText}`,
        advice_text: '确认摄像头画面、AprilTag 位置和无人机识别区域后重试。',
        level: 'error'
      };
    }

    return {
      show: true,
      reason_text: FAULT_CODE_LABELS[faultCode] || note || `板端进入 ${deviceText}`,
      advice_text: '查看板端屏幕和串口日志，确认状态恢复后重新演示。',
      level: 'error'
    };
  }

  if (status === 'cancelled') {
    if (faultCode === 'demo_reset') {
      return {
        show: true,
        reason_text: FAULT_CODE_LABELS[faultCode],
        advice_text: '确认托盘已收回后，可重新创建订单继续演示。',
        level: 'muted'
      };
    }

    return {
      show: true,
      reason_text: FAULT_CODE_LABELS[faultCode] || note || '订单已取消，板端任务已停止推进。',
      advice_text: '如需继续演示，请重新创建订单并分配 AprilTag。',
      level: 'muted'
    };
  }

  return {
    show: false,
    reason_text: '',
    advice_text: '',
    level: 'normal'
  };
}

function formatVoiceNodeFromStatus(status) {
  const text = normalizeText(status);
  return STATUS_VOICE_LABELS[text] || '';
}

function formatVoiceNodeFromDeviceState(payload, fallbackState) {
  const data = payload || {};
  if (Number(data.weather_blocked || 0) === 1) {
    return '播报：天气保护，暂停配送';
  }

  const state = normalizeText(data.state || fallbackState);
  return DEVICE_VOICE_LABELS[state] || '';
}

module.exports = {
  buildOrderInsight,
  formatDeviceState,
  formatOrderName,
  formatVoiceNodeFromDeviceState,
  formatVoiceNodeFromStatus
};
