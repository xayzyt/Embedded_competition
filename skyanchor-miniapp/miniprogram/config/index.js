module.exports = {
  // 统一维护云开发环境和聚合云函数名称，避免页面层分散写死。
  envId: 'cloud1-d5g90ikff6eed3f26',
  serviceFunctionName: 'skyanchorService',
  requestTimeout: 20000,
  // 用户主动点击“联系配送员”时才会拉起拨号界面；留空时不显示电话按钮。
  dispatcherPhoneNumber: '15234711471',
  dispatcherContactName: '配送员'
};
