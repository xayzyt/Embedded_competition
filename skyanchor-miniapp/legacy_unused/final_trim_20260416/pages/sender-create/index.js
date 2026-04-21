/*
const api = require('../../services/api.js');
const { defaultServiceStatus, syncServiceStatus } = require('../../utils/service-status.js');

Page({
  data: Object.assign({
    senderId: '',
    receiverId: '',
    targetId: '',
    deviceName: 'skyanchor-p4',
    note: '',
    creating: false
  }, defaultServiceStatus()),

  onLoad(query) {
    const app = getApp();
    this.setData({
      senderId: query.sender_id || app.globalData.senderId,
      receiverId: query.receiver_id || ''
    });
    this.refreshServiceStatus(true);
  },

  onShow() {
    this.refreshServiceStatus();
  },

  async refreshServiceStatus(force) {
    await syncServiceStatus(this, !!force);
  },

  handleInput(e) {
    const key = e.currentTarget.dataset.key;
    this.setData({
      [key]: e.detail.value
    });
  },

  async createOnly() {
    await this.submit(false);
  },

  async createAndStart() {
    const confirmed = await this.confirmCreateAndStart();
    if (!confirmed) {
      return;
    }

    await this.submit(true);
  },

  confirmCreateAndStart() {
    return new Promise((resolve) => {
      wx.showModal({
        title: '\u786e\u8ba4\u7acb\u5373\u542f\u52a8',
        content: '\u8fd9\u4e2a\u64cd\u4f5c\u4f1a\u5728\u521b\u5efa\u8ba2\u5355\u540e\u7acb\u5373\u5411\u8bbe\u5907\u53d1\u9001\u201c\u5f00\u59cb\u914d\u9001\u201d\u6307\u4ee4\u3002\u5982\u679c\u4f60\u53ea\u60f3\u5148\u5efa\u5355\uff0c\u8bf7\u70b9\u201c\u4ec5\u521b\u5efa\u8ba2\u5355\u201d\u3002',
        confirmText: '\u7acb\u5373\u542f\u52a8',
        cancelText: '\u4ec5\u5efa\u5355',
        success: (res) => resolve(!!res.confirm),
        fail: () => resolve(false)
      });
    });
  },

  async submit(autoStart) {
    if (this.data.creating) {
      return;
    }

    const senderId = this.data.senderId.trim();
    const receiverId = this.data.receiverId.trim();
    const deviceName = this.data.deviceName.trim();
    const targetId = Number(this.data.targetId);

    if (!senderId || !receiverId || !deviceName) {
      wx.showModal({
        title: '信息不完整',
        content: '配送员 ID、收货人 ID 和设备名称不能为空。',
        showCancel: false
      });
      return;
    }

    if (!Number.isInteger(targetId) || targetId < 1) {
      wx.showModal({
        title: '目标 ID 无效',
        content: '请输入大于 0 的整数目标 ID。',
        showCancel: false
      });
      return;
    }

    const serviceStatus = await syncServiceStatus(this, true);
    if (!serviceStatus.serviceOnline) {
      wx.showModal({
        title: '服务未连接',
        content: serviceStatus.serviceMessage,
        showCancel: false
      });
      return;
    }

    const app = getApp();
    app.globalData.senderId = senderId;
    wx.setStorageSync('skyanchor_sender_id', senderId);

    this.setData({ creating: true });
    wx.showLoading({ title: autoStart ? '创建并启动中' : '创建订单中' });

    try {
      const order = await api.createOrder({
        sender_id: senderId,
        receiver_id: receiverId,
        target_id: targetId,
        device_name: deviceName,
        note: this.data.note
      });

      if (autoStart) {
        await api.startOrder(order.order_id);
      }

      wx.hideLoading();
      wx.showToast({
        title: autoStart ? '订单已启动' : '订单已创建',
        icon: 'success'
      });

      wx.navigateTo({
        url: `/pages/order-center/index?order_id=${encodeURIComponent(order.order_id)}`
      });
    } catch (err) {
      await this.refreshServiceStatus(true);
      wx.hideLoading();
      wx.showModal({
        title: '操作失败',
        content: err.message || '请求失败',
        showCancel: false
      });
    } finally {
      this.setData({ creating: false });
    }
  }
});
*/

const api = require('../../services/api.js');
const { getDemoProfile } = require('../../utils/demo-profile.js');
const { defaultServiceStatus, syncServiceStatus } = require('../../utils/service-status.js');

Page({
  data: Object.assign({
    senderId: '',
    receiverId: '',
    targetId: '',
    deviceName: '',
    note: '',
    creating: false
  }, defaultServiceStatus()),

  onLoad(query) {
    const app = getApp();
    const demoProfile = app.globalData.demoProfile || getDemoProfile();

    this.setData({
      senderId: query.sender_id || app.globalData.senderId || demoProfile.senderId,
      receiverId: query.receiver_id || app.globalData.receiverId || demoProfile.receiverId,
      targetId: String(demoProfile.targetId),
      deviceName: demoProfile.deviceName,
      note: ''
    });

    this.refreshServiceStatus(true);
  },

  onShow() {
    this.refreshServiceStatus();
  },

  async refreshServiceStatus(force) {
    const serviceStatus = await syncServiceStatus(this, !!force);
    const app = getApp();
    const currentDemoDevice = (
      (app.globalData.demoProfile && app.globalData.demoProfile.deviceName) ||
      getDemoProfile().deviceName
    );

    if (
      serviceStatus.serviceDefaultDevice &&
      (!this.data.deviceName.trim() || this.data.deviceName === currentDemoDevice)
    ) {
      this.setData({
        deviceName: serviceStatus.serviceDefaultDevice
      });
    }

    return serviceStatus;
  },

  handleInput(e) {
    const key = e.currentTarget.dataset.key;
    this.setData({
      [key]: e.detail.value
    });
  },

  async createOnly() {
    await this.submit(false);
  },

  async createAndStart() {
    const confirmed = await this.confirmCreateAndStart();
    if (!confirmed) {
      return;
    }

    await this.submit(true);
  },

  confirmCreateAndStart() {
    return new Promise((resolve) => {
      wx.showModal({
        title: '\u786e\u8ba4\u7acb\u5373\u542f\u52a8',
        content: '\u8fd9\u4e2a\u64cd\u4f5c\u4f1a\u5728\u521b\u5efa\u8ba2\u5355\u540e\u7acb\u5373\u5411\u8bbe\u5907\u53d1\u9001\u201c\u5f00\u59cb\u914d\u9001\u201d\u6307\u4ee4\u3002\u5982\u679c\u4f60\u53ea\u60f3\u5148\u5efa\u5355\uff0c\u8bf7\u70b9\u201c\u4ec5\u521b\u5efa\u8ba2\u5355\u201d\u3002',
        confirmText: '\u7acb\u5373\u542f\u52a8',
        cancelText: '\u4ec5\u5efa\u5355',
        success: (res) => resolve(!!res.confirm),
        fail: () => resolve(false)
      });
    });
  },

  async submit(autoStart) {
    if (this.data.creating) {
      return;
    }

    const senderId = this.data.senderId.trim();
    const receiverId = this.data.receiverId.trim();
    const deviceName = this.data.deviceName.trim();
    const targetId = Number(this.data.targetId);

    if (!senderId || !receiverId || !deviceName) {
      wx.showModal({
        title: '信息不完整',
        content: '配送员 ID、收货人 ID 和设备名不能为空。',
        showCancel: false
      });
      return;
    }

    if (!Number.isInteger(targetId) || targetId < 1) {
      wx.showModal({
        title: '目标 ID 无效',
        content: '请输入大于 0 的整数 AprilTag ID。',
        showCancel: false
      });
      return;
    }

    const serviceStatus = await this.refreshServiceStatus(true);
    if (!serviceStatus.serviceOnline) {
      wx.showModal({
        title: '服务未连接',
        content: serviceStatus.serviceMessage,
        showCancel: false
      });
      return;
    }

    const app = getApp();
    app.globalData.senderId = senderId;
    wx.setStorageSync('skyanchor_sender_id', senderId);

    this.setData({ creating: true });
    wx.showLoading({ title: autoStart ? '创建并启动中' : '创建订单中' });

    try {
      const order = await api.createOrder({
        sender_id: senderId,
        receiver_id: receiverId,
        target_id: targetId,
        device_name: deviceName,
        note: this.data.note
      });

      if (autoStart && serviceStatus.serviceMqttReady) {
        await api.startOrder(order.order_id);
        wx.hideLoading();
        wx.showToast({
          title: '订单已启动',
          icon: 'success'
        });
        wx.navigateTo({
          url: `/pages/order-center/index?order_id=${encodeURIComponent(order.order_id)}&role=sender`
        });
        return;
      }

      wx.hideLoading();

      if (autoStart) {
        wx.showModal({
          title: '消息通道未连接',
          content: '订单已创建，但当前 MQTT 未就绪。请进入订单详情页，待消息通道恢复后再点击“开始配送”。',
          showCancel: false,
          success: () => {
            wx.navigateTo({
              url: `/pages/order-center/index?order_id=${encodeURIComponent(order.order_id)}&role=sender`
            });
          }
        });
        return;
      }

      wx.showToast({
        title: '订单已创建',
        icon: 'success'
      });

      wx.navigateTo({
        url: `/pages/order-center/index?order_id=${encodeURIComponent(order.order_id)}&role=sender`
      });
    } catch (err) {
      await this.refreshServiceStatus(true);
      wx.hideLoading();
      wx.showModal({
        title: '操作失败',
        content: err.message || '请求失败',
        showCancel: false
      });
    } finally {
      this.setData({ creating: false });
    }
  }
});
