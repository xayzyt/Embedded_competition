const TARGET_OPTIONS = [
  { id: 1, name: '\u0031\u53f7\u76ee\u6807' },
  { id: 2, name: '\u0032\u53f7\u76ee\u6807' },
  { id: 3, name: '\u0033\u53f7\u76ee\u6807' }
];

function getTargetOptions() {
  return TARGET_OPTIONS.map((item) => ({ ...item }));
}

function getTargetLabel(targetId) {
  const normalized = Number(targetId);
  const matched = TARGET_OPTIONS.find((item) => item.id === normalized);
  return matched ? matched.name : `目标号 ${targetId || '-'}`;
}

module.exports = {
  TARGET_OPTIONS,
  getTargetOptions,
  getTargetLabel
};
