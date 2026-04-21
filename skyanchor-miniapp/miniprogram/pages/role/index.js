Page({
  openSenderCenter() {
    wx.navigateTo({
      url: '/pages/sender-dispatch/index'
    });
  },

  openReceiverCenter() {
    wx.navigateTo({
      url: '/pages/user-orders/index'
    });
  }
});
