const APRILTAG_OPTIONS = [
  { id: 0, label: 'AprilTag 0' },
  { id: 1, label: 'AprilTag 1' }
];

function normalizeApriltagId(value) {
  if (value === undefined || value === null || value === '') {
    return null;
  }

  const parsed = Number(value);
  return Number.isInteger(parsed) ? parsed : null;
}

function isAssignedApriltag(value) {
  const tagId = normalizeApriltagId(value);
  return tagId === 0 || tagId === 1;
}

function formatApriltagValue(value) {
  const tagId = normalizeApriltagId(value);
  return tagId === 0 || tagId === 1 ? String(tagId) : '待调度';
}

module.exports = {
  APRILTAG_OPTIONS,
  normalizeApriltagId,
  isAssignedApriltag,
  formatApriltagValue
};
