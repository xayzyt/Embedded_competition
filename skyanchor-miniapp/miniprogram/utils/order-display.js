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

module.exports = {
  formatOrderName
};
