const STATUS_LABELS = {
  created: '\u5f85\u8c03\u5ea6',
  pending_start: '\u7b49\u5f85\u677f\u7aef\u54cd\u5e94',
  // 这里明确“开始配送”和“识别成功”是两个不同阶段。
  delivering: '\u914d\u9001\u4e2d/\u7b49\u5f85\u8bc6\u522b',
  tag_matched: '\u5df2\u8bc6\u522b\u76ee\u6807',
  acting: '\u6267\u884c\u52a8\u4f5c\u4e2d',
  delivered: '\u5df2\u9001\u8fbe',
  failed: '\u5931\u8d25',
  cancelled: '\u5df2\u53d6\u6d88'
};

function statusLabel(status) {
  return STATUS_LABELS[status] || status || '-';
}

module.exports = {
  statusLabel
};
