const config = require('../config/index.js');

function normalizeRequestError(err) {
  const rawMessage = (err && err.errMsg) || (err && err.message) || '';

  if (rawMessage.includes('当前基础库不支持云开发')) {
    return new Error(rawMessage);
  }

  if (
    rawMessage.includes('FunctionName parameter could not be found') ||
    rawMessage.includes('function not exists') ||
    rawMessage.includes('找不到对应的FunctionName')
  ) {
    return new Error('云函数尚未部署，请先在微信开发者工具上传并部署 skyanchorService。');
  }

  if (rawMessage.includes('timeout') || rawMessage.includes('超时')) {
    return new Error('云函数请求超时，请检查云函数是否部署完成。');
  }

  if (rawMessage.includes('fail')) {
    return new Error(`云端请求失败：${rawMessage}`);
  }

  return err instanceof Error ? err : new Error(rawMessage || '云端请求失败');
}

function withTimeout(promise, timeout) {
  return new Promise((resolve, reject) => {
    // 云函数本身没有显式超时参数，这里用前端兜底避免页面长时间无反馈。
    const timer = setTimeout(() => {
      reject(new Error('云函数请求超时，请检查云函数是否部署完成。'));
    }, timeout);

    promise.then(
      (result) => {
        clearTimeout(timer);
        resolve(result);
      },
      (err) => {
        clearTimeout(timer);
        reject(err);
      }
    );
  });
}

function normalizeFunctionError(result) {
  const code = String(result && result.code || '').trim();
  const message = String(result && result.message || '').trim();

  if (code === 'MQTT_ACK_TIMEOUT') {
    return new Error('未收到板端确认。请检查 ESP32 是否在线、是否订阅 cmd topic，并确认 ack topic 正常上报。');
  }

  if (code === 'MQTT_UNAVAILABLE') {
    return new Error('MQTT 调度通道不可用。请检查 EMQX 连接、云函数依赖和现场网络。');
  }

  if (code === 'DEVICE_STATE_UNAVAILABLE') {
    return new Error('未收到板端状态，暂时不能下单和配送。请确认 ESP32 已联网并连接 MQTT。');
  }

  if (code === 'DEVICE_NOT_READY') {
    return new Error(message || '设备未就绪，暂时不能下单和配送。请检查任务状态、天气保护和板端运行状态。');
  }

  if (code === 'WEATHER_BLOCKED') {
    return new Error(message || '恶劣天气管制中，暂时不能下单或开始配送。');
  }

  if (code === 'DEVICE_REJECTED') {
    return new Error(message || '板端拒绝执行命令，请查看板端状态和串口日志。');
  }

  if (code === 'NOT_FOUND') {
    return new Error(message || '订单不存在，请返回列表刷新后重试。');
  }

  return new Error(message || '云函数返回失败');
}

function request(options) {
  if (!wx.cloud || typeof wx.cloud.callFunction !== 'function') {
    return Promise.reject(new Error('当前基础库不支持云开发，请升级微信开发者工具后重试。'));
  }

  // 页面层统一通过聚合云函数访问云数据库、MQTT 调度和板端诊断。
  return withTimeout(
    wx.cloud.callFunction({
      name: config.serviceFunctionName,
      data: {
        action: options.action,
        payload: options.data || {}
      }
    }),
    config.requestTimeout
  )
    .then((res) => {
      const result = (res && res.result) || {};
      if (!result.ok) {
        throw normalizeFunctionError(result);
      }
      return result.data;
    })
    .catch((err) => {
      throw normalizeRequestError(err);
    });
}

module.exports = {
  normalizeRequestError,
  normalizeFunctionError,
  request
};
