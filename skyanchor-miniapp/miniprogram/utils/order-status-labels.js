const STATUS_LABELS = {
  created: '\u5f85\u8c03\u5ea6',
  pending_start: '\u5f85\u54cd\u5e94',
  delivering: '\u8bc6\u522b\u4e2d',
  tag_matched: '\u5df2\u8bc6\u522b',
  acting: '\u6267\u884c\u4e2d',
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
