// 云开发版统一展示云端服务状态，页面层继续复用原来的状态结构。
const DEFAULT_MESSAGE = '正在检查云端服务状态...';

function defaultServiceStatus() {
  return {
    serviceChecked: false,
    serviceOnline: false,
    serviceChecking: false,
    serviceMqttReady: false,
    serviceWeatherBlocked: false,
    serviceAcceptOrders: true,
    serviceWeatherMode: 'normal',
    serviceState: 'checking',
    serviceMessage: DEFAULT_MESSAGE
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
  if (payload && payload.weather_blocked) {
    return '恶劣天气管制中，暂时不能下单或开始配送。恢复天气后可重新操作。';
  }
  if (payload && payload.accept_orders === false) {
    return '板端状态未同步，暂时不能下单或开始配送。请确认设备在线后刷新。';
  }

  const mode = String(payload && payload.mode || '').trim();

  // 这里按健康检查结果区分“真实 MQTT 闭环”与“演示模式”，避免页面继续误报为演示态。
  if (mode === 'cloud-mqtt') {
    return '云端调度与真实 MQTT 状态同步已就绪，可以直接联调建单、开始配送与识别回传。';
  }

  return '云端服务已就绪，当前为比赛演示模式，可以直接演示下单、分配与配送流程。';
}

function buildBackendOnlyMessage() {
  return '云端服务已启动，但调度通道未就绪。可以先建单和分配 AprilTag，暂时无法开始配送。';
}

function buildServiceStatusFromHealth(payload) {
  const mqttReady = !!(payload && payload.mqtt_started);
  const weatherBlocked = !!(payload && payload.weather_blocked);
  const acceptOrders = payload && payload.accept_orders !== undefined
    ? Number(payload.accept_orders) !== 0
    : !weatherBlocked;
  const ordersBlocked = weatherBlocked || !acceptOrders;

  return {
    serviceChecked: true,
    serviceOnline: true,
    serviceChecking: false,
    serviceMqttReady: mqttReady,
    serviceWeatherBlocked: ordersBlocked,
    serviceAcceptOrders: acceptOrders,
    serviceWeatherMode: String(payload && payload.weather_mode || 'normal'),
    serviceState: ordersBlocked ? 'weather-blocked' : (mqttReady ? 'ready' : 'backend-only'),
    serviceMessage: ordersBlocked ? buildReadyMessage(payload) :
      (mqttReady ? buildReadyMessage(payload) : buildBackendOnlyMessage())
  };
}

function buildServiceStatusFromError(err) {
  return {
    serviceChecked: true,
    serviceOnline: false,
    serviceChecking: false,
    serviceMqttReady: false,
    serviceState: 'offline',
    serviceMessage: (err && err.message) || '云端服务不可用，请检查云函数是否已经部署。'
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

module.exports = {
  defaultServiceStatus,
  buildCheckingStatus,
  buildServiceStatusFromHealth,
  buildServiceStatusFromError,
  readServiceStatus,
  syncServiceStatus
};
