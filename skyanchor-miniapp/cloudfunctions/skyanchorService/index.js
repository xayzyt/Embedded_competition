const cloud = require('wx-server-sdk');
const mqtt = require('mqtt');
const crypto = require('crypto');

cloud.init({
  env: cloud.DYNAMIC_CURRENT_ENV
});

const db = cloud.database();

const ORDERS_COLLECTION = 'orders';
const ORDER_EVENTS_COLLECTION = 'order_events';
const DELIVERY_PHOTO_READY_STATUSES = ['ready', 'uploaded'];
const VALID_TARGET_IDS = new Set([0, 1]);
const TERMINAL_ORDER_STATUSES = ['delivered', 'failed', 'cancelled'];
const CLEARABLE_ORDER_STATUSES = ['created', 'delivered', 'failed', 'cancelled'];
const BACKGROUND_SYNC_ORDER_STATUSES = ['pending_start', 'delivering', 'tag_matched', 'acting', 'delivered'];
const BACKGROUND_SYNC_ORDER_LIMIT = 20;
// 这里改成真实板子的逻辑设备名，避免继续沿用演示占位值。
const DEFAULT_DEVICE_NAME = 'skyanchor-p4';
const DEVICE_CANDIDATES = [DEFAULT_DEVICE_NAME];
const MANUAL_RETRACT_ORDER_STATUSES = ['acting'];
const DEMO_RESET_ORDER_STATUSES = ['pending_start', 'delivering', 'tag_matched', 'acting'];
const PHOTO_META_QUIET_MS = 400;
// 这里复用当前仓库里已经在板子侧生效的 MQTT 参数，先完成最小真实闭环。
const MQTT_CONFIG = {
  brokerUrl: 'mqtts://cd29033a.ala.cn-hangzhou.emqxsl.cn:8883',
  username: 'skyanchor_user',
  password: 'qq134679',
  topicPrefix: 'skyanchor',
  connectTimeoutMs: 2500,
  stateWaitTimeoutMs: 3500,
  ackWaitTimeoutMs: 3500,
  photoWaitTimeoutMs: 3000,
  qos: 1
};
// 申请微信订阅消息模板后填写。字段示例见 buildDeliveryNoticeData()。
const DELIVERY_NOTICE_TEMPLATE_ID = 'VDW0I3ahja-JvOUwJNv45DvNgZx3XO79HIFnS_Ix5EE';
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

function sleepMs(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function clonePlainData(data) {
  return JSON.parse(JSON.stringify(data));
}

function cloneDeviceStateForEvent(payload) {
  const cloned = clonePlainData(payload || {});
  if (cloned.photo_inline_b64) {
    delete cloned.photo_inline_b64;
    cloned.photo_inline_omitted = true;
  }
  return cloned;
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
    time3: {
      value: formatNoticeTime(order && (order.delivered_at || order.updated_at))
    },
    thing4: {
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

function normalizeBoolean(value, fieldName) {
  if (value === true || value === false) {
    return value;
  }
  if (value === 1 || value === 0) {
    return Number(value) === 1;
  }
  if (typeof value === 'string') {
    const text = value.trim().toLowerCase();
    if (['1', 'true', 'on', 'yes', 'enabled'].includes(text)) {
      return true;
    }
    if (['0', 'false', 'off', 'no', 'disabled'].includes(text)) {
      return false;
    }
  }
  throw new AppError(`${fieldName || 'value'} must be boolean`);
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

function normalizePhotoId(photoId) {
  const text = String(photoId || '').trim();
  return /^[0-9A-Za-z_-]{1,32}$/.test(text) ? text : '';
}

function buildPhotoMetaTopic(deviceName, photoId) {
  return `${MQTT_CONFIG.topicPrefix}/${normalizeDeviceName(deviceName)}/photo/${normalizePhotoId(photoId)}/meta`;
}

function buildPhotoMetaFilterTopic(deviceName) {
  return `${MQTT_CONFIG.topicPrefix}/${normalizeDeviceName(deviceName)}/photo/+/meta`;
}

function buildPhotoChunkTopic(deviceName, photoId, index) {
  return `${MQTT_CONFIG.topicPrefix}/${normalizeDeviceName(deviceName)}/photo/${normalizePhotoId(photoId)}/chunk/${String(index).padStart(3, '0')}`;
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

  if (msg === 'demo_reset_failed') {
    throw new AppError('板端演示复位未完成，请检查机构状态后重试', 'DEVICE_RESET_FAILED');
  }

  if (msg === 'manual_retract_failed') {
    throw new AppError('板端托盘回收未完成，请检查机构状态后重试', 'DEVICE_RETRACT_FAILED');
  }

  if (msg === 'set_voice_failed') {
    throw new AppError('板端语音开关未更新成功，请稍后重试', 'DEVICE_VOICE_FAILED');
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

async function fetchRetainedMqttPayloads(topics, timeoutMs) {
  const uniqueTopics = Array.from(new Set((topics || []).filter(Boolean)));
  if (!uniqueTopics.length) {
    return new Map();
  }
  const client = await connectMqttClient('skyanchor-cloud-photo');

  try {
    return await new Promise((resolve) => {
      let settled = false;
      const payloads = new Map();
      const pending = new Set(uniqueTopics);

      const finish = () => {
        if (settled) {
          return;
        }
        settled = true;
        clearTimeout(timer);
        client.removeListener('message', onMessage);
        client.removeListener('error', onError);
        resolve(payloads);
      };

      const onMessage = (receivedTopic, payloadBuffer) => {
        if (!pending.has(receivedTopic)) {
          return;
        }
        payloads.set(receivedTopic, Buffer.from(payloadBuffer));
        pending.delete(receivedTopic);
        if (!pending.size) {
          finish();
        }
      };

      const onError = (error) => {
        console.warn('[skyanchorService] MQTT 照片分片读取失败', {
          message: error && error.message
        });
        finish();
      };

      const timer = setTimeout(finish, Number(timeoutMs || MQTT_CONFIG.photoWaitTimeoutMs));

      client.on('message', onMessage);
      client.once('error', onError);
      client.subscribe(uniqueTopics, { qos: MQTT_CONFIG.qos }, (error) => {
        if (error) {
          onError(error);
        }
      });
    });
  } finally {
    closeMqttClient(client);
  }
}

async function fetchRetainedMqttPayloadsByFilter(filterTopic, timeoutMs) {
  const topic = String(filterTopic || '').trim();
  if (!topic) {
    return new Map();
  }
  const client = await connectMqttClient('skyanchor-cloud-photo-scan');

  try {
    return await new Promise((resolve) => {
      let settled = false;
      let quietTimer = null;
      const payloads = new Map();

      const finish = () => {
        if (settled) {
          return;
        }
        settled = true;
        clearTimeout(timer);
        clearTimeout(quietTimer);
        client.removeListener('message', onMessage);
        client.removeListener('error', onError);
        resolve(payloads);
      };

      const scheduleQuietFinish = () => {
        clearTimeout(quietTimer);
        quietTimer = setTimeout(finish, PHOTO_META_QUIET_MS);
      };

      const onMessage = (receivedTopic, payloadBuffer) => {
        payloads.set(receivedTopic, Buffer.from(payloadBuffer));
        scheduleQuietFinish();
      };

      const onError = (error) => {
        console.warn('[skyanchorService] MQTT 照片元数据扫描失败', {
          topic,
          message: error && error.message
        });
        finish();
      };

      const timer = setTimeout(finish, Number(timeoutMs || MQTT_CONFIG.photoWaitTimeoutMs));

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

  const payloadIds = [payload.request_id, payload.order_id]
    .map((value) => String(value || '').trim())
    .filter(Boolean);
  const orderIds = [order.request_id, order.order_id]
    .map((value) => String(value || '').trim())
    .filter(Boolean);
  // request_id 与 order_id 都是订单的强标识；任一精确相等即可兼容设备端字段回退，
  // 同时仍可阻止其他订单的 retained state 被误套用。
  if (!payloadIds.some((payloadId) => orderIds.includes(payloadId))) {
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

async function recordDeliveryNoticeFailure(order, message) {
  if (!order || !order._id) {
    return;
  }

  const nextMessage = String(message || 'send failed').trim() || 'send failed';
  await updateOrderByDocId(order._id, {
    delivery_notice_error: nextMessage,
    updated_at: now()
  });
  if (nextMessage !== String(order.delivery_notice_error || '').trim()) {
    await addOrderEvent(order.order_id, 'delivery_notice_failed', { message: nextMessage });
  }
}

async function sendDeliveryCompleteNotice(order) {
  if (!order || order.status !== 'delivered') {
    return;
  }

  const templateId = String(order.delivery_notice_template_id || DELIVERY_NOTICE_TEMPLATE_ID || '').trim();
  if (!templateId || !order.delivery_notice_enabled || order.delivery_notice_sent_at) {
    return;
  }

  const receiverOpenId = String(order.receiver_openid || '').trim();
  if (!receiverOpenId) {
    await recordDeliveryNoticeFailure(order, 'receiver openid missing');
    return;
  }

  if (!cloud.openapi || !cloud.openapi.subscribeMessage || !cloud.openapi.subscribeMessage.send) {
    await recordDeliveryNoticeFailure(order, 'subscribeMessage API unavailable');
    return;
  }

  try {
    await cloud.openapi.subscribeMessage.send({
      touser: receiverOpenId,
      templateId,
      page: buildDeliveryNoticePage(order),
      data: buildDeliveryNoticeData(order),
      miniprogramState: DELIVERY_NOTICE_MINIPROGRAM_STATE,
      lang: DELIVERY_NOTICE_LANG
    });

    await updateOrderByDocId(order._id, {
      delivery_notice_sent_at: now(),
      delivery_notice_error: ''
    });
    await addOrderEvent(order.order_id, 'delivery_notice_sent', {
      template_id: templateId
    });
  } catch (error) {
    const message = error && error.message ? error.message : 'send failed';
    console.warn('[skyanchorService] 配送完成订阅消息发送失败', {
      order_id: order.order_id,
      message
    });
    await recordDeliveryNoticeFailure(order, message);
  }
}

async function updateOrderStatus(order, status, note, extraPatch = {}, options = {}) {
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
  if (!options.skipDeliveryNotice) {
    await sendDeliveryCompleteNotice(updatedOrder);
    return getOrderById(order.order_id);
  }
  return updatedOrder;
}

function isDevicePhotoReady(payload) {
  const status = String(payload && payload.photo_status || '').trim();
  const photoId = normalizePhotoId(payload && payload.photo_id);
  const chunks = Number(payload && payload.photo_chunks || 0);
  return !!photoId && chunks > 0 && DELIVERY_PHOTO_READY_STATUSES.includes(status);
}

function sanitizeCloudPathPart(value) {
  return String(value || '')
    .trim()
    .replace(/[^0-9A-Za-z_-]/g, '_')
    .slice(0, 64) || 'unknown';
}

function buildDeliveryPhotoCloudPath(order, photoId) {
  return `delivery-photos/${sanitizeCloudPathPart(order && order.order_id)}/${sanitizeCloudPathPart(photoId)}.jpg`;
}

function compactErrorMessage(error) {
  return String(error && error.message || error || 'photo unavailable').slice(0, 120);
}

function patchDiffers(order, patch) {
  return Object.keys(patch || {}).some((key) => {
    const left = order && order[key];
    const right = patch[key];
    return JSON.stringify(left === undefined ? null : left) !== JSON.stringify(right === undefined ? null : right);
  });
}

async function patchOrderDeliveryPhoto(order, patch) {
  if (!order || !order._id || !patchDiffers(order, patch)) {
    return order;
  }
  await updateOrderByDocId(order._id, {
    ...patch,
    updated_at: now()
  });
  return getOrderById(order.order_id);
}

function parsePhotoMeta(buffer) {
  if (!buffer || !buffer.length) {
    return null;
  }
  try {
    return JSON.parse(buffer.toString('utf8'));
  } catch (error) {
    throw new AppError('照片元数据解析失败', 'PHOTO_META_BAD_JSON');
  }
}

function extractPhotoIdFromMetaTopic(deviceName, topic) {
  const prefix = `${MQTT_CONFIG.topicPrefix}/${normalizeDeviceName(deviceName)}/photo/`;
  const suffix = '/meta';
  const text = String(topic || '').trim();
  if (!text.startsWith(prefix) || !text.endsWith(suffix)) {
    return '';
  }
  return normalizePhotoId(text.slice(prefix.length, -suffix.length));
}

function normalizePhotoMeta(meta, topic, deviceName) {
  if (!meta) {
    return null;
  }

  const photoId = normalizePhotoId(meta.photo_id) || extractPhotoIdFromMetaTopic(deviceName, topic);
  const size = Number(meta.size || 0);
  const chunks = Number(meta.chunks || 0);
  if (!photoId || size <= 0 || chunks <= 0) {
    return null;
  }

  return {
    ...meta,
    photo_id: photoId,
    order_id: String(meta.order_id || '').trim(),
    request_id: String(meta.request_id || meta.order_id || '').trim(),
    mime: String(meta.mime || 'image/jpeg'),
    size,
    chunks,
    width: Number(meta.width || 0),
    height: Number(meta.height || 0),
    sha256: String(meta.sha256 || '').trim().toLowerCase()
  };
}

function photoMetaMatchesOrderId(order, meta) {
  const orderId = String(order && order.order_id || '').trim();
  const metaOrderId = String(meta && meta.order_id || '').trim();
  return !!orderId && metaOrderId === orderId;
}

function photoMetaMatchesRequestId(order, meta) {
  const orderRequestId = String(order && (order.request_id || order.order_id) || '').trim();
  const metaRequestId = String(meta && (meta.request_id || meta.order_id) || '').trim();
  return !!orderRequestId && metaRequestId === orderRequestId;
}

function selectPhotoMetaForOrder(order, candidates) {
  const metas = (candidates || []).filter(Boolean);
  return metas.find((meta) => photoMetaMatchesOrderId(order, meta)) ||
    metas.find((meta) => photoMetaMatchesRequestId(order, meta)) ||
    null;
}

async function fetchDeliveryPhotoMetaCandidates(deviceName) {
  const filterTopic = buildPhotoMetaFilterTopic(deviceName);
  const payloads = await fetchRetainedMqttPayloadsByFilter(filterTopic, MQTT_CONFIG.photoWaitTimeoutMs);
  const candidates = [];

  for (const [topic, payload] of payloads.entries()) {
    try {
      const meta = normalizePhotoMeta(parsePhotoMeta(payload), topic, deviceName);
      if (meta) {
        candidates.push(meta);
      }
    } catch (error) {
      console.warn('[skyanchorService] 跳过异常照片元数据', {
        topic,
        message: error && error.message
      });
    }
  }

  return candidates;
}

async function fetchDeliveryPhotoMetaForOrder(deviceName, order, cache = null) {
  const normalizedDeviceName = normalizeDeviceName(deviceName);
  const cacheKey = `__photo_meta__:${normalizedDeviceName}`;
  let candidates = null;

  if (cache) {
    if (!cache.has(cacheKey)) {
      cache.set(cacheKey, fetchDeliveryPhotoMetaCandidates(normalizedDeviceName));
    }
    candidates = await cache.get(cacheKey);
  } else {
    candidates = await fetchDeliveryPhotoMetaCandidates(normalizedDeviceName);
  }

  return selectPhotoMetaForOrder(order, candidates);
}

function parseInlineDeliveryPhoto(deviceState) {
  const inline = Number(deviceState && deviceState.photo_inline || 0) === 1;
  const b64 = String(deviceState && deviceState.photo_inline_b64 || '').trim();
  const photoId = normalizePhotoId(deviceState && deviceState.photo_id);
  if (!inline || !b64 || !photoId) {
    return null;
  }

  const photoBuffer = Buffer.from(b64, 'base64');
  const size = Number(deviceState && deviceState.photo_size || photoBuffer.length);
  if (!photoBuffer.length || photoBuffer.length !== size) {
    throw new AppError('内联照片大小校验失败', 'PHOTO_INLINE_SIZE_MISMATCH');
  }

  const expectedSha = String(deviceState && (deviceState.photo_sha256 || deviceState.sha256) || '').trim().toLowerCase();
  if (expectedSha) {
    const actualSha = crypto.createHash('sha256').update(photoBuffer).digest('hex');
    if (actualSha !== expectedSha) {
      throw new AppError('内联照片 sha256 校验失败', 'PHOTO_INLINE_SHA_MISMATCH');
    }
  }

  return {
    photo_id: photoId,
    buffer: photoBuffer,
    mime: String(deviceState && deviceState.photo_mime || 'image/jpeg'),
    size: photoBuffer.length,
    width: Number(deviceState && deviceState.photo_width || 0),
    height: Number(deviceState && deviceState.photo_height || 0),
    sha256: expectedSha
  };
}

async function fetchDeliveryPhotoFromMqtt(deviceName, deviceState) {
  const photoId = normalizePhotoId(deviceState && deviceState.photo_id);
  const stateChunks = Number(deviceState && deviceState.photo_chunks || 0);
  if (!photoId || stateChunks <= 0) {
    return null;
  }

  const metaTopic = buildPhotoMetaTopic(deviceName, photoId);
  const topics = [metaTopic];
  for (let i = 0; i < stateChunks; i += 1) {
    topics.push(buildPhotoChunkTopic(deviceName, photoId, i));
  }

  for (let attempt = 0; attempt < 2; attempt += 1) {
    const payloads = await fetchRetainedMqttPayloads(topics, MQTT_CONFIG.photoWaitTimeoutMs);
    const meta = parsePhotoMeta(payloads.get(metaTopic));
    if (!meta) {
      if (attempt === 0) {
        await sleepMs(1200);
        continue;
      }
      return null;
    }

    const chunks = Number(meta.chunks || stateChunks);
    const size = Number(meta.size || deviceState.photo_size || 0);
    const expectedSha = String(meta.sha256 || '').trim().toLowerCase();
    if (!chunks || !size) {
      throw new AppError('照片元数据不完整', 'PHOTO_META_INCOMPLETE');
    }

    const pieces = [];
    let missingChunk = false;
    for (let i = 0; i < chunks; i += 1) {
      const chunkTopic = buildPhotoChunkTopic(deviceName, photoId, i);
      const chunkPayload = payloads.get(chunkTopic);
      if (!chunkPayload || !chunkPayload.length) {
        missingChunk = true;
        break;
      }
      pieces.push(Buffer.from(chunkPayload.toString('utf8').trim(), 'base64'));
    }
    if (missingChunk) {
      if (attempt === 0) {
        await sleepMs(1200);
        continue;
      }
      return null;
    }

    const photoBuffer = Buffer.concat(pieces).slice(0, size);
    if (photoBuffer.length !== size) {
      throw new AppError('照片大小校验失败', 'PHOTO_SIZE_MISMATCH');
    }
    if (expectedSha) {
      const actualSha = crypto.createHash('sha256').update(photoBuffer).digest('hex');
      if (actualSha !== expectedSha) {
        throw new AppError('照片 sha256 校验失败', 'PHOTO_SHA_MISMATCH');
      }
    }

    return {
      photo_id: photoId,
      buffer: photoBuffer,
      mime: String(meta.mime || 'image/jpeg'),
      size,
      width: Number(meta.width || 0),
      height: Number(meta.height || 0),
      sha256: expectedSha
    };
  }

  return null;
}

async function fetchDeliveryPhotoFromMeta(deviceName, meta) {
  if (!meta) {
    return null;
  }
  return fetchDeliveryPhotoFromMqtt(deviceName, {
    photo_id: meta.photo_id,
    photo_size: meta.size,
    photo_width: meta.width,
    photo_height: meta.height,
    photo_chunks: meta.chunks
  });
}

async function uploadDeliveryPhoto(order, photo) {
  const cloudPath = buildDeliveryPhotoCloudPath(order, photo.photo_id);
  const result = await cloud.uploadFile({
    cloudPath,
    fileContent: photo.buffer
  });
  return {
    fileID: result.fileID,
    cloudPath
  };
}

async function resolveDeliveryPhotoForOrder(order, deviceName, state, deviceStateCache = null) {
  const stateMatchesOrder = isPayloadForOrder(order, state);
  let photo = null;
  let fallbackMeta = null;

  if (stateMatchesOrder && isDevicePhotoReady(state)) {
    photo = parseInlineDeliveryPhoto(state) || await fetchDeliveryPhotoFromMqtt(deviceName, state);
  }

  if (!photo) {
    fallbackMeta = await fetchDeliveryPhotoMetaForOrder(deviceName, order, deviceStateCache);
    if (fallbackMeta) {
      photo = await fetchDeliveryPhotoFromMeta(deviceName, fallbackMeta);
    }
  }

  return {
    photo,
    stateMatchesOrder,
    fallbackMeta
  };
}

async function syncDeliveryPhotoForOrder(order, deviceState = null, deviceStateCache = null, options = {}) {
  if (!order || order.status !== 'delivered' || order.delivery_photo_file_id) {
    return order;
  }

  const deviceName = normalizeDeviceName(order.device_name);
  let state = deviceState;
  if (!state && !options.skipStateFetch) {
    try {
      if (deviceStateCache) {
        if (!deviceStateCache.has(deviceName)) {
          deviceStateCache.set(deviceName, fetchLatestDeviceState(deviceName));
        }
        state = await deviceStateCache.get(deviceName);
      } else {
        state = await fetchLatestDeviceState(deviceName);
      }
    } catch (error) {
      console.warn('[skyanchorService] 读取送达照片状态失败', {
        order_id: order.order_id,
        message: error && error.message
      });
      state = null;
    }
  }

  try {
    const { photo, stateMatchesOrder, fallbackMeta } =
      await resolveDeliveryPhotoForOrder(order, deviceName, state, deviceStateCache);

    if (!photo) {
      const status = stateMatchesOrder
        ? String(state && state.photo_status || order.delivery_photo_status || 'waiting')
        : 'waiting';
      return patchOrderDeliveryPhoto(order, {
        delivery_photo_status: status === 'failed' ? 'waiting' : status,
        delivery_photo_error: stateMatchesOrder
          ? String(state && state.photo_error || (fallbackMeta ? '照片分片未齐' : '照片待同步'))
          : (fallbackMeta ? '照片分片未齐' : '未找到照片元数据')
      });
    }

    const uploaded = await uploadDeliveryPhoto(order, photo);
    await updateOrderByDocId(order._id, {
      delivery_photo_file_id: uploaded.fileID,
      delivery_photo_cloud_path: uploaded.cloudPath,
      delivery_photo_status: 'ready',
      delivery_photo_uploaded_at: now(),
      delivery_photo_error: '',
      delivery_photo_meta: {
        photo_id: photo.photo_id,
        size: photo.size,
        width: photo.width,
        height: photo.height,
        mime: photo.mime,
        sha256: photo.sha256
      },
      updated_at: now()
    });
    await addOrderEvent(order.order_id, 'delivery_photo_uploaded', {
      photo_id: photo.photo_id,
      size: photo.size,
      cloud_path: uploaded.cloudPath
    });
    return getOrderById(order.order_id);
  } catch (error) {
    console.warn('[skyanchorService] 送达照片同步失败', {
      order_id: order.order_id,
      message: error && error.message
    });
    return patchOrderDeliveryPhoto(order, {
      delivery_photo_status: 'waiting',
      delivery_photo_error: compactErrorMessage(error)
    });
  }
}

async function syncOrderWithRealDeviceState(order, deviceStateCache = null, options = {}) {
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
  const faultCode = String(deviceState.fault_code || '').trim();
  const rawNote = String(deviceState.note || deviceState.state || order.note || '').trim();
  const nextNote = faultCode
    ? (rawNote && rawNote !== faultCode ? `${faultCode}: ${rawNote}` : faultCode)
    : rawNote;
  const nextDeviceState = String(deviceState.state || order.last_device_state || '').trim();

  await addOrderEvent(order.order_id, 'device_state', {
    device_name: normalizedDeviceName,
    payload: cloneDeviceStateForEvent(deviceState)
  });

  const updatedOrder = await updateOrderStatus(order, nextStatus, nextNote, {
    matched_tag_id: matchedTagId !== null ? matchedTagId : order.matched_tag_id,
    last_device_state: nextDeviceState
  }, options);
  if (updatedOrder && updatedOrder.status === 'delivered') {
    if (options.skipPhotoSync) {
      return updatedOrder;
    }
    return syncDeliveryPhotoForOrder(updatedOrder, deviceState, deviceStateCache);
  }
  return updatedOrder;
}

async function syncOrderProgress(order, deviceStateCache = null, options = {}) {
  if (order &&
      order.status === 'delivered' &&
      order.delivery_notice_enabled &&
      !order.delivery_notice_sent_at &&
      !options.skipDeliveryNotice) {
    await sendDeliveryCompleteNotice(order);
    order = await getOrderById(order.order_id);
  }
  if (order && order.status === 'delivered' && !order.delivery_photo_file_id && !options.skipPhotoSync) {
    return syncDeliveryPhotoForOrder(order, null, deviceStateCache);
  }
  return syncOrderWithRealDeviceState(order, deviceStateCache, options);
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
    device_note: String(deviceState && deviceState.note || ''),
    device_fault: Number(deviceState && deviceState.fault || 0),
    device_fault_code: String(deviceState && deviceState.fault_code || ''),
    voice_enabled: deviceState && deviceState.voice_enabled !== undefined
      ? Number(deviceState.voice_enabled) !== 0
      : true,
    default_device: DEFAULT_DEVICE_NAME,
    device_candidates: DEVICE_CANDIDATES,
    state_topic: buildStateTopic(DEFAULT_DEVICE_NAME),
    command_topic: buildCommandTopic(DEFAULT_DEVICE_NAME),
    ack_topic: buildAckTopic(DEFAULT_DEVICE_NAME),
    delivery_notice_template_id: DELIVERY_NOTICE_TEMPLATE_ID,
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
    requestedNoticeTemplateId
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
    delivery_notice_template_id: deliveryNoticeEnabled ? requestedNoticeTemplateId : '',
    delivery_notice_requested_at: deliveryNoticeEnabled ? currentTime : null,
    delivery_notice_sent_at: null,
    delivery_notice_error: '',
    delivery_photo_file_id: '',
    delivery_photo_cloud_path: '',
    delivery_photo_status: 'none',
    delivery_photo_uploaded_at: null,
    delivery_photo_error: '',
    delivery_photo_meta: null
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
    const syncedOrder = await syncOrderProgress(item, deviceStateCache, {
      skipPhotoSync: true,
      skipDeliveryNotice: true
    });
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

  order = await syncOrderProgress(order, null, {
    skipPhotoSync: true
  });

  return {
    order: stripOrderDoc(order),
    events: await listOrderEvents(orderId)
  };
}

async function handleScheduledOrderSync() {
  const result = await db.collection(ORDERS_COLLECTION)
    .where({ status: db.command.in(BACKGROUND_SYNC_ORDER_STATUSES) })
    .limit(BACKGROUND_SYNC_ORDER_LIMIT)
    .get();
  const deviceStateCache = new Map();
  let checked = 0;
  let updated = 0;
  let noticesSent = 0;

  for (const order of (result.data || [])) {
    if (order.status === 'delivered' &&
        (!order.delivery_notice_enabled || order.delivery_notice_sent_at)) {
      continue;
    }

    checked += 1;
    try {
      const previousStatus = order.status;
      const previousNoticeSentAt = order.delivery_notice_sent_at;
      const syncedOrder = await syncOrderProgress(order, deviceStateCache, {
        skipPhotoSync: true
      });
      if (syncedOrder && syncedOrder.status !== previousStatus) {
        updated += 1;
      }
      if (syncedOrder && !previousNoticeSentAt && syncedOrder.delivery_notice_sent_at) {
        noticesSent += 1;
      }
    } catch (error) {
      console.warn('[skyanchorService] 后台同步订单失败', {
        order_id: order.order_id,
        message: error && error.message
      });
    }
  }

  return {
    checked,
    updated,
    notices_sent: noticesSent
  };
}

async function handleSyncDeliveryPhoto(payload) {
  const orderId = ensureString(payload.order_id, 'order_id');
  const order = await getOrderById(orderId);

  if (!order) {
    throw new AppError('订单不存在', 'NOT_FOUND');
  }

  const photoCache = new Map();
  const syncedOrder = await syncDeliveryPhotoForOrder(order, {}, photoCache, { skipStateFetch: true });
  let inlinePhoto = null;
  if (!syncedOrder || !syncedOrder.delivery_photo_file_id) {
    try {
      const deviceName = normalizeDeviceName(order.device_name);
      const resolved = await resolveDeliveryPhotoForOrder(order, deviceName, {}, photoCache);
      if (resolved.photo && resolved.photo.buffer) {
        inlinePhoto = resolved.photo;
      }
    } catch (error) {
      console.warn('[skyanchorService] 送达照片内联兜底失败', {
        order_id: order.order_id,
        message: error && error.message
      });
    }
  }
  return {
    order: stripOrderDoc(syncedOrder),
    events: await listOrderEvents(orderId),
    photo_status: String(syncedOrder && syncedOrder.delivery_photo_status || ''),
    photo_error: String(syncedOrder && syncedOrder.delivery_photo_error || ''),
    photo_file_id: String(syncedOrder && syncedOrder.delivery_photo_file_id || ''),
    photo_inline_b64: inlinePhoto ? inlinePhoto.buffer.toString('base64') : '',
    photo_inline_mime: inlinePhoto ? inlinePhoto.mime : '',
    photo_inline_id: inlinePhoto ? inlinePhoto.photo_id : ''
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

async function handleSetVoiceEnabled(payload) {
  const enabled = normalizeBoolean(
    payload.enabled !== undefined ? payload.enabled : payload.voice_enabled,
    'enabled'
  );
  const deviceName = normalizeDeviceName(payload.device_name || DEFAULT_DEVICE_NAME);
  const requestId = `VOICE-${now()}`;

  try {
    const ack = await publishMqttCommand(deviceName, {
      cmd: 'set_voice',
      voice_enabled: enabled ? 1 : 0,
      request_id: requestId
    }, {
      waitAck: true,
      ackTimeoutMs: 5000
    });
    assertDeviceAckAccepted(ack);
  } catch (error) {
    if (error instanceof AppError) {
      throw error;
    }
    console.error('[skyanchorService] MQTT set_voice 发送失败', {
      device_name: deviceName,
      message: error && error.message
    });
    throw new AppError('MQTT 调度通道不可用，语音开关未发送成功', 'MQTT_UNAVAILABLE');
  }

  return {
    device_name: deviceName,
    voice_enabled: enabled,
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
    note: 'manual tray retract fallback when cargo weight is not detected',
    device_name: deviceName,
    request_id: String(order.request_id || order.order_id)
  });

  return {
    order_id: orderId,
    status: order.status,
    mqtt_sent: true
  };
}

async function handleDemoResetOrder(payload) {
  const orderId = ensureString(payload.order_id, 'order_id');
  const order = await getOrderById(orderId);

  if (!order) {
    throw new AppError('订单不存在', 'NOT_FOUND');
  }

  if (!DEMO_RESET_ORDER_STATUSES.includes(order.status)) {
    throw new AppError(`当前状态 ${order.status} 不能执行演示复位`);
  }

  const assignedDeviceName = String(order.device_name || '').trim();
  if (!assignedDeviceName) {
    throw new AppError('订单还没有分配设备');
  }
  const deviceName = normalizeDeviceName(assignedDeviceName);

  try {
    const ack = await publishMqttCommand(deviceName, {
      cmd: 'demo_reset',
      order_id: order.order_id,
      order_name: getOrderName(order),
      target_id: Number(order.target_id),
      request_id: String(order.request_id || order.order_id)
    }, {
      waitAck: true,
      ackTimeoutMs: 7000
    });
    assertDeviceAckAccepted(ack);
  } catch (error) {
    if (error instanceof AppError) {
      throw error;
    }
    console.error('[skyanchorService] MQTT demo_reset 发送失败', {
      order_id: order.order_id,
      device_name: deviceName,
      message: error && error.message
    });
    throw new AppError('MQTT 调度通道不可用，演示复位命令未发送成功', 'MQTT_UNAVAILABLE');
  }

  await addOrderEvent(orderId, 'demo_reset_requested', {
    note: 'demo_reset',
    device_name: deviceName,
    request_id: String(order.request_id || order.order_id)
  });

  const updated = await updateOrderStatus(order, 'cancelled', 'demo_reset', {
    last_device_state: 'cancelled'
  });

  return {
    order_id: updated.order_id,
    status: updated.status,
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

  if (TERMINAL_ORDER_STATUSES.includes(order.status)) {
    throw new AppError(`当前状态 ${order.status} 不能取消订单`);
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
    case 'syncDeliveryPhoto':
      return handleSyncDeliveryPhoto(payload);
    case 'deleteOrder':
      return handleDeleteOrder(payload);
    case 'assignOrder':
      return handleAssignOrder(payload);
    case 'startOrder':
      return handleStartOrder(payload);
    case 'setVoiceEnabled':
      return handleSetVoiceEnabled(payload);
    case 'manualRetractOrder':
      return handleManualRetractOrder(payload);
    case 'demoResetOrder':
      return handleDemoResetOrder(payload);
    case 'cancelOrder':
      return handleCancelOrder(payload);
    default:
      throw new AppError(`不支持的云函数动作：${action}`);
  }
}

exports.main = async (event) => {
  const input = event || {};
  const timerTriggered = String(input.Type || '').trim() === 'Timer';
  const action = String(input.action || '').trim();
  const payload = clonePlainData(input.payload || {});

  try {
    await ensureCollectionsReady();
    const data = timerTriggered
      ? await handleScheduledOrderSync()
      : await routeAction(action, payload);
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
