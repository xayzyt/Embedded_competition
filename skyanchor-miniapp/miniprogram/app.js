const api = require('./services/api.js');
const { getDemoProfile } = require('./utils/demo-profile.js');
const {
  defaultServiceStatus,
  buildCheckingStatus,
  buildServiceStatusFromHealth,
  buildServiceStatusFromError
} = require('./utils/service-status.js');

const RECEIVER_ID_STORAGE_KEY = 'skyanchor_receiver_id';
const LEGACY_DEMO_RECEIVER_IDS = ['receiver_003', 'receiver__003'];

function resolveInitialReceiverId(defaultReceiverId) {
  const fallbackReceiverId = String(defaultReceiverId || '').trim() || 'receiver';
  const storedReceiverId = String(wx.getStorageSync(RECEIVER_ID_STORAGE_KEY) || '').trim();

  if (!storedReceiverId) {
    return fallbackReceiverId;
  }

  if (LEGACY_DEMO_RECEIVER_IDS.indexOf(storedReceiverId) >= 0) {
    wx.setStorageSync(RECEIVER_ID_STORAGE_KEY, fallbackReceiverId);
    return fallbackReceiverId;
  }

  return storedReceiverId;
}

App({
  onLaunch() {
    // 统一初始化云开发环境，后续接口全部改走云函数。
    if (wx.cloud) {
      wx.cloud.init({
        env: 'cloud1-d5g90ikff6eed3f26',
        traceUser: true
      });
    } else {
      console.error('当前基础库不支持云开发，请升级微信开发者工具后重试。');
    }

    const demoProfile = getDemoProfile();

    this.globalData.demoProfile = demoProfile;
    this.globalData.receiverId = resolveInitialReceiverId(demoProfile.receiverId);
    this.globalData.serviceStatus = defaultServiceStatus();

    this.refreshServiceHealth();
  },

  async refreshServiceHealth(force) {
    if (this._serviceHealthPromise && !force) {
      return this._serviceHealthPromise;
    }

    this.globalData.serviceStatus = buildCheckingStatus(this.globalData.serviceStatus);

    this._serviceHealthPromise = api
      .getHealth()
      .then((payload) => {
        const nextStatus = buildServiceStatusFromHealth(payload);
        this.globalData.serviceStatus = nextStatus;
        return nextStatus;
      })
      .catch((err) => {
        const nextStatus = buildServiceStatusFromError(err);
        this.globalData.serviceStatus = nextStatus;
        return nextStatus;
      })
      .finally(() => {
        this._serviceHealthPromise = null;
      });

    return this._serviceHealthPromise;
  },

  globalData: {
    demoProfile: getDemoProfile(),
    receiverId: '',
    serviceStatus: defaultServiceStatus()
  }
});
