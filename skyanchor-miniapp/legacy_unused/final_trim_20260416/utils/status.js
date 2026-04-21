/*
const STATUS_LABELS = {
  created: '已创建',
  pending_start: '待开始',
  delivering: '配送中',
  tag_matched: '已识别目标',
  acting: '执行动作中',
  delivered: '已送达',
  failed: '失败',
  cancelled: '已取消'
};

function statusLabel(status) {
  return STATUS_LABELS[status] || status || '-';
}

module.exports = {
  statusLabel
};
*/

const STATUS_LABELS = {
  created: '已创建',
  pending_start: '待启动',
  delivering: '配送中',
  tag_matched: '已识别目标',
  acting: '执行动作中',
  delivered: '已送达',
  failed: '失败',
  cancelled: '已取消'
};

function statusLabel(status) {
  return STATUS_LABELS[status] || status || '-';
}

module.exports = {
  statusLabel
};
