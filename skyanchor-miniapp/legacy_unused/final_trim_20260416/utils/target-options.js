const TARGET_OPTIONS = [
  { id: 1, name: '1号点位' },
  { id: 2, name: '2号点位' },
  { id: 3, name: '3号点位' }
];

function getTargetOptions() {
  return TARGET_OPTIONS.map((item) => ({ ...item }));
}

function getTargetLabel(targetId) {
  const normalized = Number(targetId);
  const matched = TARGET_OPTIONS.find((item) => item.id === normalized);
  return matched ? matched.name : `点位 ${targetId || '-'}`;
}

module.exports = {
  TARGET_OPTIONS,
  getTargetOptions,
  getTargetLabel
};
