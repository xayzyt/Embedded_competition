// 云开发版统一展示云端服务状态，页面层继续复用原来的状态结构。
const DEFAULT_MESSAGE = '检查中...';

function pad(value) {
  return String(value).padStart(2, '0');
}

function formatClockTime(timestamp) {
  if (timestamp === undefined || timestamp === null || timestamp === '') {
    return '未检查';
  }

  const numeric = Number(timestamp);
  const value = Number.isFinite(numeric) && numeric > 0 ? numeric : Date.parse(timestamp);
  if (!Number.isFinite(value) || value <= 0) {
    return '未检查';
  }

  const date = new Date(value);
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

function boolText(value) {
  return value ? '正常' : '异常';
}

function defaultServiceStatus() {
  return {
    serviceChecked: false,
    serviceOnline: false,
    serviceChecking: false,
    serviceMqttReady: false,
    serviceWeatherBlocked: false,
    serviceAcceptOrders: false,
    serviceDeviceNotReady: false,
    serviceActionBlocked: true,
    serviceWeatherMode: 'normal',
    serviceState: 'checking',
    serviceMessage: DEFAULT_MESSAGE,
    serviceBlockTitle: '服务暂不可用',
    serviceBlockAdvice: '请稍后重试。',
    serviceDeviceStateReady: false,
    serviceDeviceStateText: '-',
    serviceDefaultDevice: '-',
    serviceDeliveryNoticeTemplateId: '',
    serviceMode: '-',
    serviceLastCheckedAt: 0,
    serviceLastCheckedText: '未检查'
  };
}

function buildCheckingStatus(previousStatus) {
  return {
    ...defaultServiceStatus(),
    ...(previousStatus || {}),
    serviceChecking: true,
    serviceState: 'checking',
    serviceMessage: DEFAULT_MESSAGE
  };
}

function buildReadyMessage(payload) {
  if (payload && payload.device_state_ready === false) {
    return '板端未上报';
  }
  if (payload && payload.weather_blocked) {
    return '天气管制';
  }
  if (payload && payload.accept_orders !== undefined && Number(payload.accept_orders) === 0) {
    return '设备未就绪';
  }

  return '云端正常';
}

function buildBackendOnlyMessage() {
  return '调度未就绪';
}

function buildBlockInfo(payload, mqttReady, deviceStateReady, weatherBlocked, acceptOrders) {
  if (!mqttReady) {
    return {
      title: '云端调度未就绪',
      advice: '云函数可访问，但 MQTT 未连通。请检查 EMQX、网络和云函数依赖部署。'
    };
  }

  if (!deviceStateReady) {
    return {
      title: '未收到板端状态',
      advice: '暂时不能下单和配送。请确认 ESP32 已联网、已连接 MQTT，并持续发布 retained state。'
    };
  }

  if (weatherBlocked) {
    return {
      title: '恶劣天气管制',
      advice: '板端处于天气保护状态，暂时不能下单或开始配送。'
    };
  }

  if (!acceptOrders) {
    return {
      title: '设备未就绪',
      advice: '板端已上报状态，但当前不接受订单。请检查任务状态、天气保护和板端运行状态。'
    };
  }

  return {
    title: '服务正常',
    advice: '云端、MQTT 和板端状态检查通过。'
  };
}

function buildServiceStatusFromHealth(payload) {
  const mqttReady = !!(payload && payload.mqtt_started);
  const deviceStateReady = !!(payload && payload.device_state_ready);
  const weatherBlocked = !!(payload && payload.weather_blocked);
  const acceptOrders = payload && payload.accept_orders !== undefined
    ? Number(payload.accept_orders) !== 0
    : !weatherBlocked;
  const deviceNotReady = deviceStateReady && !weatherBlocked && !acceptOrders;
  const actionBlocked = !mqttReady || !deviceStateReady || weatherBlocked || !acceptOrders;
  const blockInfo = buildBlockInfo(payload, mqttReady, deviceStateReady, weatherBlocked, acceptOrders);
  const checkedAt = Number(payload && payload.checked_at) || Date.now();
  let serviceState = 'ready';

  if (!mqttReady) {
    serviceState = 'backend-only';
  } else if (!deviceStateReady) {
    serviceState = 'device-state-missing';
  } else if (weatherBlocked) {
    serviceState = 'weather-blocked';
  } else if (!acceptOrders) {
    serviceState = 'device-not-ready';
  }

  return {
    serviceChecked: true,
    serviceOnline: true,
    serviceChecking: false,
    serviceMqttReady: mqttReady,
    serviceWeatherBlocked: weatherBlocked,
    serviceAcceptOrders: acceptOrders,
    serviceDeviceNotReady: deviceNotReady,
    serviceActionBlocked: actionBlocked,
    serviceWeatherMode: String(payload && payload.weather_mode || 'normal'),
    serviceState,
    serviceMessage: !mqttReady ? buildBackendOnlyMessage() : buildReadyMessage(payload),
    serviceBlockTitle: blockInfo.title,
    serviceBlockAdvice: blockInfo.advice,
    serviceDeviceStateReady: deviceStateReady,
    serviceDeviceStateText: String(payload && payload.device_state || '').trim() || (deviceStateReady ? 'ready' : '-'),
    serviceDefaultDevice: String(payload && payload.default_device || '-'),
    serviceDeliveryNoticeTemplateId: String(payload && payload.delivery_notice_template_id || '').trim(),
    serviceMode: String(payload && payload.mode || '-'),
    serviceLastCheckedAt: checkedAt,
    serviceLastCheckedText: formatClockTime(checkedAt)
  };
}

function buildServiceStatusFromError(err) {
  const checkedAt = Date.now();
  const errorMessage = String(err && err.message || '').trim();
  const functionMissing = errorMessage.includes('云函数尚未部署');
  return {
    serviceChecked: true,
    serviceOnline: false,
    serviceChecking: false,
    serviceMqttReady: false,
    serviceAcceptOrders: false,
    serviceActionBlocked: true,
    serviceState: 'offline',
    serviceMessage: functionMissing ? '云函数未部署' : '服务离线',
    serviceBlockTitle: functionMissing ? '云函数尚未部署' : '云端服务未连接',
    serviceBlockAdvice: errorMessage || '请确认 skyanchorService 云函数已上传部署，体验版网络正常。',
    serviceLastCheckedAt: checkedAt,
    serviceLastCheckedText: formatClockTime(checkedAt)
  };
}

function readServiceStatus(app) {
  return {
    ...defaultServiceStatus(),
    ...((app && app.globalData && app.globalData.serviceStatus) || {})
  };
}

async function syncServiceStatus(page, force) {
  const app = getApp();
  const currentStatus = readServiceStatus(app);

  if (page && typeof page.setData === 'function') {
    page.setData(currentStatus);
  }

  await app.refreshServiceHealth(!!force);

  const nextStatus = readServiceStatus(app);
  if (page && typeof page.setData === 'function') {
    page.setData(nextStatus);
  }

  return nextStatus;
}

function formatServiceDiagnostics(status) {
  const current = {
    ...defaultServiceStatus(),
    ...(status || {})
  };

  const lines = [
    `云函数：${boolText(current.serviceOnline)}`,
    `MQTT：${boolText(current.serviceMqttReady)}`,
    `板端状态：${current.serviceDeviceStateReady ? `可读（${current.serviceDeviceStateText}）` : '未读取到'}`,
    `天气模式：${current.serviceWeatherMode || 'normal'}`,
    `接单状态：${current.serviceAcceptOrders ? '接受订单' : '暂停接单'}`,
    `默认设备：${current.serviceDefaultDevice || '-'}`,
    `最近刷新：${current.serviceLastCheckedText || '未检查'}`,
    '',
    `建议：${current.serviceBlockAdvice || '请稍后重试。'}`
  ];

  return lines.join('\n');
}

function showServiceDiagnostics(status, title = '服务诊断') {
  wx.showModal({
    title,
    content: formatServiceDiagnostics(status),
    showCancel: false
  });
}

module.exports = {
  defaultServiceStatus,
  buildCheckingStatus,
  buildServiceStatusFromHealth,
  buildServiceStatusFromError,
  formatClockTime,
  formatServiceDiagnostics,
  readServiceStatus,
  showServiceDiagnostics,
  syncServiceStatus
};
