const config = require('../config/index.js');

const DELIVERY_MODAL_STORAGE_PREFIX = 'skyanchor_delivery_modal_seen_';

function getDispatcherPhoneNumber() {
  return String(config.dispatcherPhoneNumber || '').trim();
}

function getDispatcherContactName() {
  return String(config.dispatcherContactName || '').trim() || '配送员';
}

function hasDispatcherPhoneNumber() {
  return !!getDispatcherPhoneNumber();
}

function buildDeliveryModalKey(orderId) {
  return `${DELIVERY_MODAL_STORAGE_PREFIX}${String(orderId || '').trim()}`;
}

function shouldShowDeliveryCompleteModal(orderId) {
  const key = buildDeliveryModalKey(orderId);
  return !!orderId && !wx.getStorageSync(key);
}

function markDeliveryCompleteModalShown(orderId) {
  const key = buildDeliveryModalKey(orderId);
  if (orderId) {
    wx.setStorageSync(key, Date.now());
  }
}

function callDispatcher() {
  const phoneNumber = getDispatcherPhoneNumber();
  if (!phoneNumber) {
    wx.showModal({
      title: '未配置电话',
      content: '请先在小程序配置里填写配送员联系电话。',
      showCancel: false
    });
    return;
  }

  wx.makePhoneCall({
    phoneNumber,
    fail(err) {
      const message = String(err && err.errMsg || '');
      if (message.includes('cancel')) {
        return;
      }
      wx.showToast({
        title: '拨号失败',
        icon: 'none'
      });
    }
  });
}

module.exports = {
  callDispatcher,
  getDispatcherContactName,
  getDispatcherPhoneNumber,
  hasDispatcherPhoneNumber,
  markDeliveryCompleteModalShown,
  shouldShowDeliveryCompleteModal
};
