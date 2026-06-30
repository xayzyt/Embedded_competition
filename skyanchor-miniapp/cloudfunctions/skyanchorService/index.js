const cloud = require('wx-server-sdk');
const mqtt = require('mqtt');

cloud.init({
  env: cloud.DYNAMIC_CURRENT_ENV
});

const db = cloud.database();

const ORDERS_COLLECTION = 'orders';
const ORDER_EVENTS_COLLECTION = 'order_events';
const VALID_TARGET_IDS = new Set([0, 1]);
const TERMINAL_ORDER_STATUSES = ['delivered', 'failed', 'cancelled'];
const CLEARABLE_ORDER_STATUSES = ['created', 'delivered', 'failed', 'cancelled'];
// 这里改成真实板子的逻辑设备名，避免继续沿用演示占位值。
const DEFAULT_DEVICE_NAME = 'skyanchor-p4';
const DEVICE_CANDIDATES = [DEFAULT_DEVICE_NAME];
const MANUAL_RETRACT_ORDER_STATUSES = ['acting'];
// 这里复用当前仓库里已经在板子侧生效的 MQTT 参数，先完成最小真实闭环。
const MQTT_CONFIG = {
  brokerUrl: 'mqtts://cd29033a.ala.cn-hangzhou.emqxsl.cn:8883',
  username: 'skyanchor_user',
  password: 'qq134679',
  topicPrefix: 'skyanchor',
  connectTimeoutMs: 2500,
  stateWaitTimeoutMs: 3500,
  ackWaitTimeoutMs: 3500,
  qos: 1
};
// 申请微信订阅消息模板后填写。字段示例见 buildDeliveryNoticeData()。
const DELIVERY_NOTICE_TEMPLATE_ID = '';
const DELIVERY_NOTICE_MINIPROGRAM_STATE = 'trial';
const DELIVERY_NOTICE_LANG = 'zh_CN';
const STATUS_INDEX = {
  created: 0,
  pending_start: 1,
  delivering: 2,
  tag_matched: 3,
  acting: 4,
  delivered: 5,
  failed: 6,
  cancelled: 7
};
const DEVICE_STATE_TO_ORDER_STATUS = {
  configured: 'pending_start',
  wait_approach: 'delivering',
  auth_passed: 'tag_matched',
  docking: 'acting',
  cancelled: 'cancelled'
};

class AppError extends Error {
  constructor(message, code = 'BAD_REQUEST') {
    super(message);
    this.name = 'AppError';
    this.code = code;
  }
}

function now() {
  return Date.now();
}

function clonePlainData(data) {
  return JSON.parse(JSON.stringify(data));
}

function buildOrderId() {
  const date = new Date();
  const pad = (value) => String(value).padStart(2, '0');
  const stamp = [
    date.getFullYear(),
    pad(date.getMonth() + 1),
    pad(date.getDate())
  ].join('') + '-' + [pad(date.getHours()), pad(date.getMinutes()), pad(date.getSeconds())].join('');
  const suffix = Math.random().toString(16).slice(2, 8).toUpperCase();
  return `ORD-${stamp}-${suffix}`;
}

function buildOrderName(orderId) {
  const text = String(orderId || '').trim();
  const parts = text.split('-').filter(Boolean);
  const lastPart = parts.length ? parts[parts.length - 1] : text;
  const suffix = lastPart.replace(/[^0-9A-Za-z]/g, '').toUpperCase().slice(-6);
  return suffix ? `SKY-${suffix}` : 'SKY-ORDER';
}

function getOrderName(order) {
  const explicitName = String(order && order.order_name || '').trim();
  if (explicitName) {
    return explicitName;
  }
  return buildOrderName(order && order.order_id);
}

function getWxContextOpenId() {
  try {
    const wxContext = cloud.getWXContext ? cloud.getWXContext() : {};
    return String(wxContext && wxContext.OPENID || '').trim();
  } catch (error) {
    console.warn('[skyanchorService] 读取 openid 失败', {
      message: error && error.message
    });
    return '';
  }
}

function clipTemplateValue(value, maxLength = 20) {
  const text = String(value || '').trim();
  return text.length > maxLength ? text.slice(0, maxLength) : text;
}

function formatNoticeTime(timestamp) {
  const date = new Date(Number(timestamp || 0) || now());
  const pad = (value) => String(value).padStart(2, '0');
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}`;
}

function buildDeliveryNoticePage(order) {
  const orderId = encodeURIComponent(String(order && order.order_id || ''));
  return `pages/order-panel/index?order_id=${orderId}&role=receiver`;
}

function buildDeliveryNoticeData(order) {
  // 订阅消息模板字段需要和微信公众平台里选择的关键词一致。
  return {
    thing1: {
      value: clipTemplateValue(getOrderName(order))
    },
    phrase2: {
      value: '已送达'
    },
    thing3: {
      value: clipTemplateValue(`Tag ${normalizeTargetId(order && order.target_id)}`)
    },
    time4: {
      value: formatNoticeTime(order && (order.delivered_at || order.updated_at))
    },
    thing5: {
      value: '请前往取件'
    }
  };
}

function normalizeTargetId(value) {
  if (value === undefined || value === null || value === '') {
    return null;
  }

  const parsed = Number(value);
  return Number.isInteger(parsed) ? parsed : null;
}

function normalizeAssignedTargetId(value) {
  const parsed = normalizeTargetId(value);
  return parsed !== null && VALID_TARGET_IDS.has(parsed) ? parsed : null;
}

function ensureString(value, fieldName) {
  const nextValue = String(value || '').trim();
  if (!nextValue) {
    throw new AppError(`${fieldName} 不能为空`);
  }
  return nextValue;
}

function isTerminalStatus(status) {
  return TERMINAL_ORDER_STATUSES.includes(status);
}

function isClearableOrderStatus(status) {
  return CLEARABLE_ORDER_STATUSES.includes(status);
}

function normalizeDeviceName(deviceName) {
  const nextValue = String(deviceName || '').trim();
  if (!nextValue) {
    return DEFAULT_DEVICE_NAME;
  }
  return nextValue;
}

function buildCommandTopic(deviceName) {
  return `${MQTT_CONFIG.topicPrefix}/${normalizeDeviceName(deviceName)}/cmd`;
}

function buildStateTopic(deviceName) {
  return `${MQTT_CONFIG.topicPrefix}/${normalizeDeviceName(deviceName)}/state`;
}

function buildAckTopic(deviceName) {
  return `${MQTT_CONFIG.topicPrefix}/${normalizeDeviceName(deviceName)}/ack`;
}

function buildMqttClientId(prefix) {
  return `${prefix}-${Date.now()}-${Math.random().toString(16).slice(2, 8)}`;
}

function buildMqttOptions(prefix) {
  return {
    username: MQTT_CONFIG.username,
    password: MQTT_CONFIG.password,
    clientId: buildMqttClientId(prefix),
    clean: true,
    reconnectPeriod: 0,
    connectTimeout: MQTT_CONFIG.connectTimeoutMs
  };
}

function closeMqttClient(client) {
  if (!client) {
    return;
  }

  try {
    client.end(true);
  } catch (error) {
    console.warn('[skyanchorService] MQTT 客户端关闭失败', {
      message: error && error.message
    });
  }
}

function connectMqttClient(prefix) {
  return new Promise((resolve, reject) => {
    const client = mqtt.connect(MQTT_CONFIG.brokerUrl, buildMqttOptions(prefix));
    let settled = false;

    const timer = setTimeout(() => {
      if (settled) {
        return;
      }
      settled = true;
      closeMqttClient(client);
      reject(new Error('MQTT 连接超时'));
    }, MQTT_CONFIG.connectTimeoutMs);

    const fail = (error) => {
      if (settled) {
        return;
      }
      settled = true;
      clearTimeout(timer);
      closeMqttClient(client);
      reject(error instanceof Error ? error : new Error(String(error || 'MQTT 连接失败')));
    };

    client.once('connect', () => {
      if (settled) {
        return;
      }
      settled = true;
      clearTimeout(timer);
      resolve(client);
    });

    client.once('error', fail);
    client.once('close', () => {
      if (!settled) {
        fail(new Error('MQTT 连接已关闭'));
      }
    });
  });
}

function isAckForCommand(ack, payload) {
  if (!ack || !payload) {
    return false;
  }

  const ackCmd = String(ack.cmd || '').trim();
  const payloadCmd = String(payload.cmd || '').trim();
  if (ackCmd && payloadCmd && ackCmd !== payloadCmd) {
    return false;
  }

  const ackRequestId = String(ack.request_id || '').trim();
  const payloadRequestId = String(payload.request_id || payload.order_id || '').trim();
  if (payloadRequestId && ackRequestId !== payloadRequestId) {
    return false;
  }

  return true;
}

async function publishMqttCommand(deviceName, payload, options = {}) {
  const client = await connectMqttClient('skyanchor-cloud-pub');
  const cmdTopic = buildCommandTopic(deviceName);
  const ackTopic = buildAckTopic(deviceName);
  const waitAck = !!options.waitAck;
  const ackTimeoutMs = Number(options.ackTimeoutMs || MQTT_CONFIG.ackWaitTimeoutMs);

  try {
    return await new Promise((resolve, reject) => {
      let settled = false;
      let timer = null;

      const finish = (error, value) => {
        if (settled) {
          return;
        }
        settled = true;
        if (timer) {
          clearTimeout(timer);
        }
        client.removeListener('message', onMessage);
        client.removeListener('error', onError);
        if (error) {
          reject(error);
          return;
        }
        resolve(value);
      };

      const publishCommand = () => {
        client.publish(
          cmdTopic,
          JSON.stringify(payload),
          { qos: MQTT_CONFIG.qos },
          (error) => {
            if (error) {
              finish(error);
              return;
            }
            if (!waitAck) {
              finish(null, true);
            }
          }
        );
      };

      const onMessage = (receivedTopic, payloadBuffer) => {
        if (!waitAck || receivedTopic !== ackTopic) {
          return;
        }

        try {
          const ack = JSON.parse(payloadBuffer.toString('utf8'));
          if (isAckForCommand(ack, payload)) {
            finish(null, ack);
          }
        } catch (error) {
          console.warn('[skyanchorService] MQTT ACK 解析失败', {
            topic: receivedTopic,
            message: error && error.message
          });
        }
      };

      const onError = (error) => finish(error);

      if (!waitAck) {
        publishCommand();
        return;
      }

      timer = setTimeout(() => {
        finish(new AppError('未收到板端确认，配送命令未完成', 'MQTT_ACK_TIMEOUT'));
      }, ackTimeoutMs);

      client.on('message', onMessage);
      client.once('error', onError);
      client.subscribe(ackTopic, { qos: MQTT_CONFIG.qos }, (error) => {
        if (error) {
          finish(error);
          return;
        }
        publishCommand();
      });
    });
  } finally {
    closeMqttClient(client);
  }
}

function assertDeviceAckAccepted(ack) {
  const code = Number(ack && ack.code);
  const msg = String(ack && ack.msg || '').trim();

  if (code === 0) {
    return;
  }

  if (msg === 'weather_blocked') {
    throw new AppError('恶劣天气管制中，暂时不能开始配送', 'WEATHER_BLOCKED');
  }

  throw new AppError(msg || '板端拒绝执行配送命令', 'DEVICE_REJECTED');
}

async function probeMqttReady() {
  try {
    const client = await connectMqttClient('skyanchor-cloud-health');
    closeMqttClient(client);
    return true;
  } catch (error) {
    console.warn('[skyanchorService] MQTT 健康检查失败', {
      message: error && error.message
    });
    return false;
  }
}

async function fetchLatestDeviceState(deviceName) {
  const client = await connectMqttClient('skyanchor-cloud-state');
  const topic = buildStateTopic(deviceName);

  try {
    return await new Promise((resolve) => {
      let settled = false;

      const finish = (payload) => {
        if (settled) {
          return;
        }
        settled = true;
        clearTimeout(timer);
        client.removeListener('message', onMessage);
        client.removeListener('error', onError);
        resolve(payload);
      };

      const onMessage = (receivedTopic, payloadBuffer) => {
        if (receivedTopic !== topic) {
          return;
        }

        try {
          finish(JSON.parse(payloadBuffer.toString('utf8')));
        } catch (error) {
          console.warn('[skyanchorService] MQTT 状态报文解析失败', {
            topic,
            message: error && error.message
          });
          finish(null);
        }
      };

      const onError = (error) => {
        console.warn('[skyanchorService] 读取 MQTT 状态失败', {
          topic,
          message: error && error.message
        });
        finish(null);
      };

      const timer = setTimeout(() => {
        // 这里允许取不到 retained state，避免订单详情因为设备暂时离线而直接报错。
        finish(null);
      }, MQTT_CONFIG.stateWaitTimeoutMs);

      client.on('message', onMessage);
      client.once('error', onError);
      client.subscribe(topic, { qos: MQTT_CONFIG.qos }, (error) => {
        if (error) {
          onError(error);
        }
      });
    });
  } finally {
    closeMqttClient(client);
  }
}

function isDeviceWeatherBlocked(payload) {
  if (!payload) {
    return false;
  }

  const weatherBlocked = Number(payload.weather_blocked || 0) === 1;
  const weatherMode = String(payload.weather_mode || '').trim();
  return weatherBlocked || weatherMode === 'cloud_guard' || weatherMode === 'emergency';
}

function isDeviceAcceptingOrders(payload) {
  if (!payload) {
    return false;
  }

  const acceptOrdersValue = payload.accept_orders;
  if (acceptOrdersValue === undefined) {
    return true;
  }

  if (acceptOrdersValue === true || acceptOrdersValue === false) {
    return acceptOrdersValue;
  }

  return Number(acceptOrdersValue) !== 0;
}

async function fetchDefaultDeviceState() {
  try {
    return await fetchLatestDeviceState(DEFAULT_DEVICE_NAME);
  } catch (error) {
    console.warn('[skyanchorService] 读取板端状态失败', {
      message: error && error.message
    });
    return null;
  }
}

async function assertOrdersAccepted() {
  const deviceState = await fetchDefaultDeviceState();
  if (!deviceState) {
    throw new AppError('未收到板端状态，暂时不能下单和配送', 'DEVICE_STATE_UNAVAILABLE');
  }
  if (isDeviceWeatherBlocked(deviceState)) {
    throw new AppError('恶劣天气，暂停下单和配送', 'WEATHER_BLOCKED');
  }
  if (!isDeviceAcceptingOrders(deviceState)) {
    throw new AppError('设备未就绪，暂时不能下单和配送', 'DEVICE_NOT_READY');
  }
  return deviceState;
}

function mapDeviceStateToOrderStatus(payload) {
  const state = String(payload && payload.state || '').trim();
  const fault = Number(payload && payload.fault || 0);
  const cargoReceived = Number(payload && payload.cargo_received || 0);

  if (cargoReceived === 1 || state === 'completed') {
    return 'delivered';
  }

  if (fault === 1 || state === 'fault') {
    return 'failed';
  }

  return DEVICE_STATE_TO_ORDER_STATUS[state] || null;
}

function isPayloadForOrder(order, payload) {
  if (!order || !payload) {
    return false;
  }

  const payloadRequestId = String(payload.request_id || payload.order_id || '').trim();
  const orderRequestId = String(order.request_id || order.order_id || '').trim();
  // 这里优先按 request_id 精确匹配，避免旧 retained state 错套到新订单。
  if (!payloadRequestId || payloadRequestId !== orderRequestId) {
    return false;
  }

  const payloadDeviceName = normalizeDeviceName(payload.device || payload.device_name);
  if (payloadDeviceName !== normalizeDeviceName(order.device_name)) {
    return false;
  }

  const payloadTargetId = normalizeTargetId(payload.target_id);
  if (payloadTargetId !== null && Number(order.target_id) !== payloadTargetId) {
    return false;
  }

  return true;
}

async function addOrderEvent(orderId, eventType, eventData = {}) {
  // 事件单独存集合，便于详情页继续展示时间线。
  await db.collection(ORDER_EVENTS_COLLECTION).add({
    data: {
      order_id: orderId,
      event_type: eventType,
      event_data: clonePlainData(eventData),
      created_at: now()
    }
  });
}

async function getOrderById(orderId) {
  const result = await db.collection(ORDERS_COLLECTION).where({ order_id: orderId }).limit(1).get();
  return (result.data && result.data[0]) || null;
}

async function listOrderEvents(orderId) {
  const result = await db.collection(ORDER_EVENTS_COLLECTION).where({ order_id: orderId }).limit(100).get();
  return (result.data || [])
    .slice()
    .sort((left, right) => Number(left.created_at || 0) - Number(right.created_at || 0))
    .map((item) => ({
      id: item._id,
      event_type: item.event_type,
      event_data: clonePlainData(item.event_data || {}),
      created_at: item.created_at
    }));
}

function stripOrderDoc(order) {
  if (!order) {
    return null;
  }

  const {
    _id,
    _openid,
    receiver_openid,
    delivery_notice_enabled,
    delivery_notice_template_id,
    delivery_notice_requested_at,
    delivery_notice_sent_at,
    delivery_notice_error,
    ...rest
  } = order;
  return clonePlainData(rest);
}

async function updateOrderByDocId(docId, patch) {
  await db.collection(ORDERS_COLLECTION).doc(docId).update({
    data: clonePlainData(patch)
  });
}

async function deleteOrderEvents(orderId) {
  const result = await db.collection(ORDER_EVENTS_COLLECTION).where({ order_id: orderId }).remove();
  return Number(result && result.stats && result.stats.removed || 0);
}

async function sendDeliveryCompleteNotice(order) {
  if (!order || order.status !== 'delivered') {
    return;
  }

  if (!DELIVERY_NOTICE_TEMPLATE_ID || !order.delivery_notice_enabled || order.delivery_notice_sent_at) {
    return;
  }

  const receiverOpenId = String(order.receiver_openid || '').trim();
  if (!receiverOpenId) {
    return;
  }

  if (!cloud.openapi || !cloud.openapi.subscribeMessage || !cloud.openapi.subscribeMessage.send) {
    await updateOrderByDocId(order._id, {
      delivery_notice_error: 'subscribeMessage API unavailable',
      updated_at: order.updated_at || now()
    });
    return;
  }

  try {
    await cloud.openapi.subscribeMessage.send({
      touser: receiverOpenId,
      templateId: DELIVERY_NOTICE_TEMPLATE_ID,
      page: buildDeliveryNoticePage(order),
      data: buildDeliveryNoticeData(order),
      miniprogramState: DELIVERY_NOTICE_MINIPROGRAM_STATE,
      lang: DELIVERY_NOTICE_LANG
    });

    await updateOrderByDocId(order._id, {
      delivery_notice_sent_at: now(),
      delivery_notice_error: ''
    });
  } catch (error) {
    console.warn('[skyanchorService] 配送完成订阅消息发送失败', {
      order_id: order.order_id,
      message: error && error.message
    });
    await updateOrderByDocId(order._id, {
      delivery_notice_error: error && error.message ? error.message : 'send failed'
    });
  }
}

async function updateOrderStatus(order, status, note, extraPatch = {}) {
  const patch = {
    status,
    note: note || order.note || '',
    updated_at: now(),
    ...extraPatch
  };

  if (status === 'delivered' && !order.delivered_at && !patch.delivered_at) {
    patch.delivered_at = patch.updated_at;
  }

  if (status === 'failed' && !order.failed_at && !patch.failed_at) {
    patch.failed_at = patch.updated_at;
  }

  await updateOrderByDocId(order._id, patch);
  await addOrderEvent(order.order_id, 'status_changed', {
    status,
    note: patch.note,
    matched_tag_id: patch.matched_tag_id !== undefined ? patch.matched_tag_id : order.matched_tag_id,
    last_device_state: patch.last_device_state !== undefined ? patch.last_device_state : order.last_device_state
  });
  const updatedOrder = await getOrderById(order.order_id);
  await sendDeliveryCompleteNotice(updatedOrder);
  return getOrderById(order.order_id);
}

async function syncOrderWithRealDeviceState(order, deviceStateCache = null) {
  // 这里仅在订单已开始且未结束时尝试拉取真实设备状态，避免无意义访问 MQTT。
  if (!order || !order.started_at || isTerminalStatus(order.status)) {
    return order;
  }

  const normalizedDeviceName = normalizeDeviceName(order.device_name);
  let deviceState = null;

  try {
    if (deviceStateCache) {
      if (!deviceStateCache.has(normalizedDeviceName)) {
        deviceStateCache.set(normalizedDeviceName, fetchLatestDeviceState(normalizedDeviceName));
      }
      deviceState = await deviceStateCache.get(normalizedDeviceName);
    } else {
      deviceState = await fetchLatestDeviceState(normalizedDeviceName);
    }
  } catch (error) {
    console.warn('[skyanchorService] 同步真实设备状态失败', {
      order_id: order.order_id,
      device_name: normalizedDeviceName,
      message: error && error.message
    });
    return order;
  }

  if (!isPayloadForOrder(order, deviceState)) {
    return order;
  }

  const nextStatus = mapDeviceStateToOrderStatus(deviceState);
  if (!nextStatus) {
    return order;
  }

  const currentIndex = STATUS_INDEX[order.status];
  const nextIndex = STATUS_INDEX[nextStatus];
  if (nextIndex === undefined || currentIndex === undefined || nextIndex <= currentIndex) {
    return order;
  }

  const matchedTagId = normalizeAssignedTargetId(deviceState.matched_tag_id);
  const nextNote = String(deviceState.note || deviceState.state || order.note || '').trim();
  const nextDeviceState = String(deviceState.state || order.last_device_state || '').trim();

  await addOrderEvent(order.order_id, 'device_state', {
    device_name: normalizedDeviceName,
    payload: clonePlainData(deviceState)
  });

  return updateOrderStatus(order, nextStatus, nextNote, {
    matched_tag_id: matchedTagId !== null ? matchedTagId : order.matched_tag_id,
    last_device_state: nextDeviceState
  });
}

async function syncOrderProgress(order, deviceStateCache = null) {
  return syncOrderWithRealDeviceState(order, deviceStateCache);
}

async function ensureCollectionsReady() {
  // 云开发数据库会在首次写入时自动建集合，这里保留空初始化钩子，避免后续扩展再改入口。
  return true;
}

async function handleHealth() {
  const mqttReady = await probeMqttReady();
  const deviceState = mqttReady ? await fetchDefaultDeviceState() : null;
  const weatherBlocked = isDeviceWeatherBlocked(deviceState);
  const acceptOrders = !!deviceState && isDeviceAcceptingOrders(deviceState);
  const checkedAt = now();
  const deviceStateReady = !!deviceState;

  return {
    ok: true,
    app: 'SkyAnchor Cloud MQTT Service',
    checked_at: checkedAt,
    mqtt_started: mqttReady,
    weather_blocked: weatherBlocked,
    accept_orders: acceptOrders,
    device_state_ready: deviceStateReady,
    device_ready: deviceStateReady && acceptOrders && !weatherBlocked,
    weather_mode: String(deviceState && deviceState.weather_mode || 'normal'),
    device_state: String(deviceState && deviceState.state || ''),
    default_device: DEFAULT_DEVICE_NAME,
    device_candidates: DEVICE_CANDIDATES,
    state_topic: buildStateTopic(DEFAULT_DEVICE_NAME),
    command_topic: buildCommandTopic(DEFAULT_DEVICE_NAME),
    ack_topic: buildAckTopic(DEFAULT_DEVICE_NAME),
    mode: 'cloud-mqtt'
  };
}

async function handleCreateOrder(payload) {
  const receiverId = ensureString(payload.receiver_id, 'receiver_id');
  await assertOrdersAccepted();

  const senderId = String(payload.sender_id || '').trim() || receiverId;
  const targetId = normalizeTargetId(payload.target_id);
  if (targetId !== null && !VALID_TARGET_IDS.has(targetId)) {
    throw new AppError('target_id 仅支持 0 或 1');
  }

  const orderId = buildOrderId();
  const orderName = buildOrderName(orderId);
  const currentTime = now();
  const receiverOpenId = getWxContextOpenId();
  const requestedNoticeTemplateId = String(payload.delivery_notice_template_id || '').trim();
  const deliveryNoticeEnabled = !!(
    payload.delivery_notice_subscribed &&
    receiverOpenId &&
    DELIVERY_NOTICE_TEMPLATE_ID &&
    requestedNoticeTemplateId === DELIVERY_NOTICE_TEMPLATE_ID
  );

  const orderData = {
    order_id: orderId,
    order_name: orderName,
    sender_id: senderId,
    receiver_id: receiverId,
    receiver_openid: receiverOpenId,
    device_name: '',
    target_id: targetId === null ? -1 : targetId,
    request_id: orderId,
    status: 'created',
    note: '',
    matched_tag_id: -1,
    last_device_state: '',
    created_at: currentTime,
    started_at: null,
    delivered_at: null,
    failed_at: null,
    updated_at: currentTime,
    delivery_notice_enabled: deliveryNoticeEnabled,
    delivery_notice_template_id: deliveryNoticeEnabled ? DELIVERY_NOTICE_TEMPLATE_ID : '',
    delivery_notice_requested_at: deliveryNoticeEnabled ? currentTime : null,
    delivery_notice_sent_at: null,
    delivery_notice_error: ''
  };

  await db.collection(ORDERS_COLLECTION).add({
    data: orderData
  });

  await addOrderEvent(orderId, 'created', {
    order_name: orderName,
    target_id: orderData.target_id,
    device_name: orderData.device_name
  });

  return stripOrderDoc(await getOrderById(orderId));
}

async function handleListOrders(payload) {
  const role = String(payload.role || '').trim();
  const userId = String(payload.userId || payload.user_id || '').trim();
  let query = db.collection(ORDERS_COLLECTION);

  if (role === 'sender' && userId) {
    query = query.where({ sender_id: userId });
  } else if (role === 'receiver' && userId) {
    query = query.where({ receiver_id: userId });
  }

  const result = await query.orderBy('created_at', 'desc').limit(100).get();
  const orders = [];
  const deviceStateCache = new Map();

  for (const item of (result.data || []).slice()) {
    const syncedOrder = await syncOrderProgress(item, deviceStateCache);
    orders.push(stripOrderDoc(syncedOrder));
  }

  return orders;
}

async function handleGetOrder(payload) {
  const orderId = ensureString(payload.order_id, 'order_id');
  let order = await getOrderById(orderId);

  if (!order) {
    throw new AppError('订单不存在', 'NOT_FOUND');
  }

  order = await syncOrderProgress(order);

  return {
    order: stripOrderDoc(order),
    events: await listOrderEvents(orderId)
  };
}

async function handleDeleteOrder(payload) {
  const orderId = ensureString(payload.order_id, 'order_id');
  const order = await getOrderById(orderId);

  if (!order) {
    throw new AppError('订单不存在', 'NOT_FOUND');
  }

  if (!isClearableOrderStatus(order.status)) {
    throw new AppError(`当前状态 ${order.status} 不能清除订单，请先取消或等待结束`);
  }

  await deleteOrderEvents(orderId);
  await db.collection(ORDERS_COLLECTION).doc(order._id).remove();

  return {
    order_id: orderId,
    deleted: true
  };
}

async function handleAssignOrder(payload) {
  const orderId = ensureString(payload.order_id, 'order_id');
  const targetId = normalizeTargetId(payload.target_id);

  if (!VALID_TARGET_IDS.has(targetId)) {
    throw new AppError('target_id 仅支持 0 或 1');
  }

  const order = await getOrderById(orderId);
  if (!order) {
    throw new AppError('订单不存在', 'NOT_FOUND');
  }

  if (order.status !== 'created') {
    throw new AppError(`当前状态 ${order.status} 不允许重新分配 AprilTag`);
  }

  await updateOrderByDocId(order._id, {
    device_name: DEFAULT_DEVICE_NAME,
    target_id: targetId,
    updated_at: now()
  });

  await addOrderEvent(orderId, 'dispatch_assigned', {
    device_name: DEFAULT_DEVICE_NAME,
    target_id: targetId
  });

  return stripOrderDoc(await getOrderById(orderId));
}

async function handleStartOrder(payload) {
  const orderId = ensureString(payload.order_id, 'order_id');
  const order = await getOrderById(orderId);

  if (!order) {
    throw new AppError('订单不存在', 'NOT_FOUND');
  }

  if (!['created', 'pending_start'].includes(order.status)) {
    throw new AppError(`当前状态 ${order.status} 无法开始配送`);
  }

  const assignedDeviceName = String(order.device_name || '').trim();
  if (!assignedDeviceName) {
    throw new AppError('订单还没有分配设备');
  }
  const deviceName = normalizeDeviceName(assignedDeviceName);

  if (!VALID_TARGET_IDS.has(Number(order.target_id))) {
    throw new AppError('订单还没有分配有效的 AprilTag');
  }

  await assertOrdersAccepted();

  try {
    // 这里改成真实发 MQTT 开始命令，不再只在数据库里假装已发送。
    const ack = await publishMqttCommand(deviceName, {
      cmd: 'start_task',
      order_id: order.order_id,
      order_name: getOrderName(order),
      target_id: Number(order.target_id),
      request_id: String(order.request_id || order.order_id)
    }, { waitAck: true });
    assertDeviceAckAccepted(ack);
  } catch (error) {
    if (error instanceof AppError) {
      throw error;
    }
    console.error('[skyanchorService] MQTT start_task 发送失败', {
      order_id: order.order_id,
      device_name: deviceName,
      message: error && error.message
    });
    throw new AppError('MQTT 调度通道不可用，开始配送命令未发送成功', 'MQTT_UNAVAILABLE');
  }

  const startTime = Number(order.started_at || 0) || now();
  await updateOrderByDocId(order._id, {
    // “开始配送”与“识别成功”必须是两个不同状态。
    // 这里仅进入“配送中/等待识别”，后续只允许真实设备状态继续推进。
    device_name: deviceName,
    status: 'delivering',
    note: '已开始配送，等待识别成功信号',
    matched_tag_id: -1,
    last_device_state: 'waiting_tag_match',
    started_at: startTime,
    updated_at: now()
  });

  await addOrderEvent(orderId, 'start_requested', {
    note: 'mqtt start_task sent, waiting real tag match signal',
    device_name: deviceName,
    request_id: String(order.request_id || order.order_id)
  });

  return {
    order_id: orderId,
    status: 'delivering',
    mqtt_sent: true
  };
}

async function handleManualRetractOrder(payload) {
  const orderId = ensureString(payload.order_id, 'order_id');
  const order = await getOrderById(orderId);

  if (!order) {
    throw new AppError('订单不存在', 'NOT_FOUND');
  }

  if (!MANUAL_RETRACT_ORDER_STATUSES.includes(order.status)) {
    throw new AppError(`当前状态 ${order.status} 不能手动回收托盘`);
  }

  const assignedDeviceName = String(order.device_name || '').trim();
  if (!assignedDeviceName) {
    throw new AppError('订单还没有分配设备');
  }
  const deviceName = normalizeDeviceName(assignedDeviceName);

  try {
    const ack = await publishMqttCommand(deviceName, {
      cmd: 'manual_retract',
      order_id: order.order_id,
      order_name: getOrderName(order),
      target_id: Number(order.target_id),
      request_id: String(order.request_id || order.order_id)
    }, {
      waitAck: true,
      ackTimeoutMs: 6000
    });
    assertDeviceAckAccepted(ack);
  } catch (error) {
    if (error instanceof AppError) {
      throw error;
    }
    console.error('[skyanchorService] MQTT manual_retract 发送失败', {
      order_id: order.order_id,
      device_name: deviceName,
      message: error && error.message
    });
    throw new AppError('MQTT 调度通道不可用，手动回收命令未发送成功', 'MQTT_UNAVAILABLE');
  }

  await addOrderEvent(orderId, 'manual_retract_requested', {
    note: 'manual retract tray requested',
    device_name: deviceName,
    request_id: String(order.request_id || order.order_id)
  });

  return {
    order_id: orderId,
    status: order.status,
    mqtt_sent: true
  };
}

async function handleCancelOrder(payload) {
  const orderId = ensureString(payload.order_id, 'order_id');
  const order = await getOrderById(orderId);

  if (!order) {
    throw new AppError('订单不存在', 'NOT_FOUND');
  }

  if (order.status === 'cancelled') {
    return {
      order_id: orderId,
      status: 'cancelled'
    };
  }

  if (order.status !== 'created') {
    const deviceName = normalizeDeviceName(order.device_name);

    try {
      // 这里真实下发取消命令，避免页面显示取消而板子仍继续执行。
      await publishMqttCommand(deviceName, {
        cmd: 'cancel',
        order_id: order.order_id,
        order_name: getOrderName(order),
        target_id: Number(order.target_id),
        request_id: String(order.request_id || order.order_id)
      });
    } catch (error) {
      console.error('[skyanchorService] MQTT cancel 发送失败', {
        order_id: order.order_id,
        device_name: deviceName,
        message: error && error.message
      });
      throw new AppError('MQTT 调度通道不可用，取消命令未发送成功', 'MQTT_UNAVAILABLE');
    }
  }

  const updated = await updateOrderStatus(order, 'cancelled', '云端已取消订单', {
    last_device_state: 'cancelled'
  });

  return {
    order_id: updated.order_id,
    status: updated.status
  };
}

async function routeAction(action, payload) {
  switch (action) {
    case 'health':
      return handleHealth();
    case 'createOrder':
      return handleCreateOrder(payload);
    case 'listOrders':
      return handleListOrders(payload);
    case 'getOrder':
      return handleGetOrder(payload);
    case 'deleteOrder':
      return handleDeleteOrder(payload);
    case 'assignOrder':
      return handleAssignOrder(payload);
    case 'startOrder':
      return handleStartOrder(payload);
    case 'manualRetractOrder':
      return handleManualRetractOrder(payload);
    case 'cancelOrder':
      return handleCancelOrder(payload);
    default:
      throw new AppError(`不支持的云函数动作：${action}`);
  }
}

exports.main = async (event) => {
  const action = String(event.action || '').trim();
  const payload = clonePlainData(event.payload || {});

  try {
    await ensureCollectionsReady();
    const data = await routeAction(action, payload);
    return {
      ok: true,
      data
    };
  } catch (error) {
    console.error('[skyanchorService] 调用失败', {
      action,
      payload,
      message: error && error.message,
      stack: error && error.stack
    });

    return {
      ok: false,
      code: error && error.code ? error.code : 'SERVER_ERROR',
      message: error && error.message ? error.message : '云端服务处理失败'
    };
  }
};
