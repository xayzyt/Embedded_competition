const { request } = require('./http.js');

// 保持页面层 API 形状不变，仅把底层请求切换为云函数动作。
function getHealth() {
  return request({
    action: 'health'
  });
}

function createOrder(data) {
  return request({
    action: 'createOrder',
    data
  });
}

function listOrders(params = {}) {
  return request({
    action: 'listOrders',
    data: params
  });
}

function getOrder(orderId) {
  return request({
    action: 'getOrder',
    data: {
      order_id: orderId
    }
  });
}

function assignOrder(orderId, data) {
  return request({
    action: 'assignOrder',
    data: {
      order_id: orderId,
      ...data
    }
  });
}

function startOrder(orderId) {
  return request({
    action: 'startOrder',
    data: {
      order_id: orderId
    }
  });
}

function manualRetractOrder(orderId) {
  return request({
    action: 'manualRetractOrder',
    data: {
      order_id: orderId
    }
  });
}

function cancelOrder(orderId) {
  return request({
    action: 'cancelOrder',
    data: {
      order_id: orderId
    }
  });
}

module.exports = {
  getHealth,
  createOrder,
  listOrders,
  getOrder,
  assignOrder,
  startOrder,
  manualRetractOrder,
  cancelOrder
};
