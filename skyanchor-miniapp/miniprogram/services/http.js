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

function request(options) {
  if (!wx.cloud || typeof wx.cloud.callFunction !== 'function') {
    return Promise.reject(new Error('当前基础库不支持云开发，请升级微信开发者工具后重试。'));
  }

  // 统一通过聚合云函数承接原本的本地后端接口，保持页面层调用方式稳定。
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
        throw new Error(result.message || '云函数返回失败');
      }
      return result.data;
    })
    .catch((err) => {
      throw normalizeRequestError(err);
    });
}

module.exports = {
  normalizeRequestError,
  request
};
