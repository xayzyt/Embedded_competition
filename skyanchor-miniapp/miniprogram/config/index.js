module.exports = {
  // 统一维护云开发环境和聚合云函数名称，避免页面层分散写死。
  envId: 'cloud1-d5g90ikff6eed3f26',
  serviceFunctionName: 'skyanchorService',
  requestTimeout: 10000,
  // 在微信公众平台申请“配送完成通知”订阅消息模板后，把模板 ID 填到这里和云函数中。
  deliveryCompleteTemplateId: '',
  // 用户主动点击“联系配送员”时才会拉起拨号界面；留空时页面会提示先配置号码。
  dispatcherPhoneNumber: '',
  dispatcherContactName: '配送员'
};
