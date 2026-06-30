import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from fastapi.testclient import TestClient

from app import config
from app import main as app_main
from app.api import orders as orders_api
from app.db import database
from app.mqtt import client as mqtt_client_module


class FakeMQTTBridge:
    def __init__(self, ready: bool) -> None:
        self.ready = ready
        self.started = False
        self.commands: list[tuple[str, dict]] = []
        self.device_states: dict[str, dict] = {
            '0': {
                'accept_orders': 1,
                'weather_blocked': 0,
                'weather_mode': 'normal',
                'state': 'configured',
            }
        }

    @property
    def is_started(self) -> bool:
        return self.started

    @property
    def is_ready(self) -> bool:
        return self.started and self.ready

    def start(self) -> None:
        self.started = True

    def stop(self) -> None:
        self.started = False

    def publish_command(self, device_name: str, payload: dict) -> None:
        if not self.is_ready:
            raise RuntimeError('MQTT broker is not connected')
        self.commands.append((device_name, payload))

    def latest_device_state(self, device_name: str) -> dict | None:
        state = self.device_states.get(str(device_name or '').strip())
        return dict(state) if state is not None else None

    def is_weather_blocked(self, device_name: str) -> bool:
        state = self.latest_device_state(device_name)
        if not state:
            return False

        weather_mode = str(state.get('weather_mode', '')).strip()
        return int(state.get('weather_blocked', 0) or 0) == 1 or weather_mode in {
            'cloud_guard',
            'emergency',
        }

    def is_accepting_orders(self, device_name: str) -> bool:
        state = self.latest_device_state(device_name)
        if not state:
            return True

        accept_orders = state.get('accept_orders', 1)
        if isinstance(accept_orders, bool):
            return accept_orders
        return int(accept_orders or 0) != 0


class ClosedLoopTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.db_path = Path(self.temp_dir.name) / 'test-skyanchor.db'
        self.original_database_path = os.environ.get('DATABASE_PATH')
        self.original_mqtt_host = os.environ.get('MQTT_HOST')
        self.original_default_device = os.environ.get('MQTT_DEFAULT_DEVICE')
        self.original_dispatch_devices = os.environ.get('MQTT_DISPATCH_DEVICES')

        os.environ['DATABASE_PATH'] = str(self.db_path)
        os.environ['MQTT_HOST'] = ''
        os.environ['MQTT_DEFAULT_DEVICE'] = '0'
        os.environ['MQTT_DISPATCH_DEVICES'] = '0,1'
        config.get_settings.cache_clear()
        app_main.settings = config.get_settings()
        database.init_db()

    def tearDown(self) -> None:
        if self.original_database_path is None:
            os.environ.pop('DATABASE_PATH', None)
        else:
            os.environ['DATABASE_PATH'] = self.original_database_path

        if self.original_mqtt_host is None:
            os.environ.pop('MQTT_HOST', None)
        else:
            os.environ['MQTT_HOST'] = self.original_mqtt_host

        if self.original_default_device is None:
            os.environ.pop('MQTT_DEFAULT_DEVICE', None)
        else:
            os.environ['MQTT_DEFAULT_DEVICE'] = self.original_default_device

        if self.original_dispatch_devices is None:
            os.environ.pop('MQTT_DISPATCH_DEVICES', None)
        else:
            os.environ['MQTT_DISPATCH_DEVICES'] = self.original_dispatch_devices

        config.get_settings.cache_clear()
        app_main.settings = config.get_settings()
        try:
            self.temp_dir.cleanup()
        except Exception:
            pass

    def _client_with_bridge(self, bridge: FakeMQTTBridge) -> TestClient:
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(app_main, 'mqtt_bridge', bridge).start()
        mock.patch.object(orders_api, 'mqtt_bridge', bridge).start()
        return TestClient(app_main.app)

    def test_health_reports_mqtt_readiness(self) -> None:
        bridge = FakeMQTTBridge(ready=False)

        with self._client_with_bridge(bridge) as client:
            first = client.get('/health')
            self.assertEqual(first.status_code, 200)
            self.assertFalse(first.json()['mqtt_started'])
            self.assertEqual(first.json()['device_candidates'], ['0', '1'])

            bridge.ready = True
            second = client.get('/health')
            self.assertEqual(second.status_code, 200)
            self.assertTrue(second.json()['mqtt_started'])
            self.assertEqual(second.json()['device_candidates'], ['0', '1'])

    def test_start_order_rejected_when_mqtt_not_ready(self) -> None:
        bridge = FakeMQTTBridge(ready=False)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            assigned = client.post(f'/api/orders/{order_id}/assign', json={'target_id': 0})
            self.assertEqual(assigned.status_code, 200)

            started = client.post(f'/api/orders/{order_id}/start')
            self.assertEqual(started.status_code, 503)
            self.assertEqual(started.json()['detail'], 'MQTT broker is not connected')

    def test_create_order_defaults_to_receiver_and_unassigned_dispatch(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            self.assertEqual(created.json()['sender_id'], 'receiver_003')
            self.assertEqual(created.json()['receiver_id'], 'receiver_003')
            self.assertEqual(created.json()['device_name'], '')
            self.assertEqual(created.json()['target_id'], -1)

    def test_create_order_ignores_legacy_fields(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post(
                '/api/orders',
                json={
                    'receiver_id': 'receiver_003',
                    'device_name': '1',
                    'note': 'legacy payload should be ignored'
                },
            )
            self.assertEqual(created.status_code, 200)
            self.assertEqual(created.json()['device_name'], '')
            self.assertEqual(created.json()['target_id'], -1)

    def test_create_order_rejected_when_weather_blocked(self) -> None:
        bridge = FakeMQTTBridge(ready=True)
        bridge.device_states['0'] = {
            'accept_orders': 0,
            'weather_blocked': 1,
            'weather_mode': 'emergency',
        }

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 409)
            self.assertEqual(created.json()['detail'], orders_api.WEATHER_BLOCKED_DETAIL)

    def test_create_order_rejected_when_device_busy(self) -> None:
        bridge = FakeMQTTBridge(ready=True)
        bridge.device_states['0'] = {
            'accept_orders': 0,
            'weather_blocked': 0,
            'weather_mode': 'normal',
            'state': 'docking',
        }

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 409)
            self.assertEqual(created.json()['detail'], orders_api.DEVICE_NOT_READY_DETAIL)

    def test_start_order_rejected_when_order_not_dispatched(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            started = client.post(f'/api/orders/{order_id}/start')
            self.assertEqual(started.status_code, 400)
            self.assertEqual(started.json()['detail'], 'Order has no assigned device')

    def test_assign_order_then_start_success(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            assigned = client.post(f'/api/orders/{order_id}/assign', json={'target_id': 1})
            self.assertEqual(assigned.status_code, 200)
            self.assertEqual(assigned.json()['device_name'], '0')
            self.assertEqual(assigned.json()['target_id'], 1)

            started = client.post(f'/api/orders/{order_id}/start')
            self.assertEqual(started.status_code, 200)
            self.assertEqual(started.json()['status'], 'pending_start')
            self.assertEqual(len(bridge.commands), 1)
            self.assertEqual(bridge.commands[0][0], '0')
            self.assertEqual(bridge.commands[0][1]['target_id'], 1)

    def test_assign_order_requires_explicit_target_id(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            assigned = client.post(f'/api/orders/{order_id}/assign', json={})
            self.assertEqual(assigned.status_code, 400)
            self.assertEqual(assigned.json()['detail'], 'target_id is required for manual assignment')

    def test_assign_order_rejects_invalid_target_id(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            assigned = client.post(f'/api/orders/{order_id}/assign', json={'target_id': 2})
            self.assertEqual(assigned.status_code, 422)

    def test_cancel_created_order_without_dispatch(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            cancelled = client.post(f'/api/orders/{order_id}/cancel')
            self.assertEqual(cancelled.status_code, 200)
            self.assertEqual(cancelled.json()['status'], 'cancelled')
            self.assertEqual(len(bridge.commands), 0)

    def test_clear_created_order_deletes_record_without_mqtt(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            cleared = client.delete(f'/api/orders/{order_id}')
            self.assertEqual(cleared.status_code, 200)
            self.assertEqual(cleared.json(), {'order_id': order_id, 'deleted': True})
            self.assertIsNone(database.get_order(order_id))
            self.assertEqual(database.get_order_events(order_id), [])
            self.assertEqual(len(bridge.commands), 0)

            detail = client.get(f'/api/orders/{order_id}')
            self.assertEqual(detail.status_code, 404)

            orders = client.get('/api/orders', params={'role': 'receiver', 'user_id': 'receiver_003'})
            self.assertEqual(orders.status_code, 200)
            self.assertEqual(orders.json(), [])

    def test_clear_delivered_order_deletes_events_and_notifications(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            database.update_order_status(order_id, 'delivered', note='done')
            database.create_notification('receiver_003', order_id, 'order_delivered', 'done', 'done')
            self.assertIsNotNone(database.get_order(order_id))
            self.assertGreater(len(database.get_order_events(order_id)), 0)
            notifications_before = client.get('/api/notifications', params={'user_id': 'receiver_003'})
            self.assertEqual(notifications_before.status_code, 200)
            self.assertEqual(len(notifications_before.json()), 1)

            cleared = client.delete(f'/api/orders/{order_id}')
            self.assertEqual(cleared.status_code, 200)
            self.assertEqual(cleared.json()['deleted'], True)
            self.assertIsNone(database.get_order(order_id))
            self.assertEqual(database.get_order_events(order_id), [])

            notifications = client.get('/api/notifications', params={'user_id': 'receiver_003'})
            self.assertEqual(notifications.status_code, 200)
            self.assertEqual(notifications.json(), [])

    def test_clear_active_order_is_rejected(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            assigned = client.post(f'/api/orders/{order_id}/assign', json={'target_id': 0})
            self.assertEqual(assigned.status_code, 200)

            started = client.post(f'/api/orders/{order_id}/start')
            self.assertEqual(started.status_code, 200)

            cleared = client.delete(f'/api/orders/{order_id}')
            self.assertEqual(cleared.status_code, 400)
            self.assertEqual(database.get_order(order_id)['status'], 'pending_start')
            self.assertEqual(len(bridge.commands), 1)

    def test_success_chain_updates_status_and_creates_single_notification(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            assigned = client.post(f'/api/orders/{order_id}/assign', json={'target_id': 0})
            self.assertEqual(assigned.status_code, 200)

            started = client.post(f'/api/orders/{order_id}/start')
            self.assertEqual(started.status_code, 200)
            self.assertEqual(started.json()['status'], 'pending_start')
            self.assertEqual(len(bridge.commands), 1)
            self.assertEqual(bridge.commands[0][0], '0')
            self.assertEqual(bridge.commands[0][1]['cmd'], 'start_task')
            self.assertEqual(bridge.commands[0][1]['request_id'], order_id)
            self.assertEqual(bridge.commands[0][1]['target_id'], 0)

            mqtt_client_module.mqtt_bridge._handle_ack(
                '0',
                {
                    'request_id': order_id,
                    'cmd': 'start_task',
                    'code': 0,
                    'msg': 'accepted'
                },
            )
            self.assertEqual(database.get_order(order_id)['status'], 'pending_start')

            mqtt_client_module.mqtt_bridge._handle_state(
                '0',
                {'target_id': 0, 'state': 'configured', 'note': 'configured'},
            )
            self.assertEqual(database.get_order(order_id)['status'], 'pending_start')

            mqtt_client_module.mqtt_bridge._handle_state(
                '0',
                {'target_id': 0, 'state': 'wait_approach', 'note': 'moving'},
            )
            self.assertEqual(database.get_order(order_id)['status'], 'delivering')

            mqtt_client_module.mqtt_bridge._handle_state(
                '0',
                {'target_id': 0, 'state': 'auth_passed', 'matched_tag_id': 0, 'note': 'matched'},
            )
            self.assertEqual(database.get_order(order_id)['status'], 'tag_matched')

            mqtt_client_module.mqtt_bridge._handle_state(
                '0',
                {'target_id': 0, 'state': 'docking', 'matched_tag_id': 0, 'note': 'docking'},
            )
            self.assertEqual(database.get_order(order_id)['status'], 'acting')

            mqtt_client_module.mqtt_bridge._handle_state(
                '0',
                {'target_id': 0, 'state': 'completed', 'matched_tag_id': 0, 'note': 'completed'},
            )
            self.assertEqual(database.get_order(order_id)['status'], 'delivered')

            detail = client.get(f'/api/orders/{order_id}')
            self.assertEqual(detail.status_code, 200)
            self.assertEqual(detail.json()['order']['status'], 'delivered')

            notifications = client.get('/api/notifications', params={'user_id': 'receiver_003'})
            self.assertEqual(notifications.status_code, 200)
            self.assertEqual(len(notifications.json()), 2)
            self.assertEqual(notifications.json()[0]['type'], 'order_delivered')
            self.assertEqual(notifications.json()[1]['type'], 'order_delivering')

            mqtt_client_module.mqtt_bridge._handle_state(
                '0',
                {'target_id': 0, 'state': 'completed', 'matched_tag_id': 0, 'note': 'completed again'},
            )
            notifications_again = client.get('/api/notifications', params={'user_id': 'receiver_003'})
            self.assertEqual(notifications_again.status_code, 200)
            self.assertEqual(len(notifications_again.json()), 2)

    def test_start_ack_does_not_roll_back_state_progress(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            assigned = client.post(f'/api/orders/{order_id}/assign', json={'target_id': 1})
            self.assertEqual(assigned.status_code, 200)

            started = client.post(f'/api/orders/{order_id}/start')
            self.assertEqual(started.status_code, 200)
            self.assertEqual(database.get_order(order_id)['status'], 'pending_start')

            mqtt_client_module.mqtt_bridge._handle_state(
                '0',
                {'target_id': 1, 'state': 'wait_approach', 'note': 'moving'},
            )
            self.assertEqual(database.get_order(order_id)['status'], 'delivering')

            mqtt_client_module.mqtt_bridge._handle_ack(
                '0',
                {
                    'request_id': order_id,
                    'cmd': 'start_task',
                    'code': 0,
                    'msg': 'accepted'
                },
            )

            order = database.get_order(order_id)
            self.assertEqual(order['status'], 'delivering')
            self.assertEqual(order['last_device_state'], 'wait_approach')

    def test_manual_retract_publishes_command_without_cancelling_order(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            assigned = client.post(f'/api/orders/{order_id}/assign', json={'target_id': 0})
            self.assertEqual(assigned.status_code, 200)

            started = client.post(f'/api/orders/{order_id}/start')
            self.assertEqual(started.status_code, 200)

            mqtt_client_module.mqtt_bridge._handle_state(
                '0',
                {'order_id': order_id, 'target_id': 0, 'state': 'docking', 'note': 'docking'},
            )
            self.assertEqual(database.get_order(order_id)['status'], 'acting')

            retracted = client.post(f'/api/orders/{order_id}/manual-retract')
            self.assertEqual(retracted.status_code, 200)
            self.assertEqual(retracted.json()['status'], 'acting')
            self.assertEqual(len(bridge.commands), 2)
            self.assertEqual(bridge.commands[1][1]['cmd'], 'manual_retract')
            self.assertEqual(bridge.commands[1][1]['request_id'], order_id)
            self.assertEqual(database.get_order(order_id)['status'], 'acting')

            mqtt_client_module.mqtt_bridge._handle_state(
                '0',
                {
                    'order_id': order_id,
                    'target_id': 0,
                    'state': 'fault',
                    'fault': 1,
                    'note': 'manual retract requested',
                },
            )
            self.assertEqual(database.get_order(order_id)['status'], 'failed')

    def test_delivered_order_cannot_be_cancelled_by_endpoint_or_late_ack(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            assigned = client.post(f'/api/orders/{order_id}/assign', json={'target_id': 0})
            self.assertEqual(assigned.status_code, 200)

            started = client.post(f'/api/orders/{order_id}/start')
            self.assertEqual(started.status_code, 200)

            mqtt_client_module.mqtt_bridge._handle_state(
                '0',
                {'order_id': order_id, 'target_id': 0, 'state': 'completed', 'note': 'completed'},
            )
            self.assertEqual(database.get_order(order_id)['status'], 'delivered')

            cancelled = client.post(f'/api/orders/{order_id}/cancel')
            self.assertEqual(cancelled.status_code, 400)
            self.assertEqual(database.get_order(order_id)['status'], 'delivered')

            mqtt_client_module.mqtt_bridge._handle_ack(
                '0',
                {'request_id': order_id, 'cmd': 'cancel', 'code': 0, 'msg': 'accepted'},
            )
            self.assertEqual(database.get_order(order_id)['status'], 'delivered')

    def test_delivering_notification_created_once(self) -> None:
        bridge = FakeMQTTBridge(ready=True)

        with self._client_with_bridge(bridge) as client:
            created = client.post('/api/orders', json={'receiver_id': 'receiver_003'})
            self.assertEqual(created.status_code, 200)
            order_id = created.json()['order_id']

            assigned = client.post(f'/api/orders/{order_id}/assign', json={'target_id': 1})
            self.assertEqual(assigned.status_code, 200)

            started = client.post(f'/api/orders/{order_id}/start')
            self.assertEqual(started.status_code, 200)

            mqtt_client_module.mqtt_bridge._handle_state(
                '0',
                {'target_id': 1, 'state': 'wait_approach', 'note': 'moving'},
            )
            mqtt_client_module.mqtt_bridge._handle_state(
                '0',
                {'target_id': 1, 'state': 'wait_approach', 'note': 'moving again'},
            )

            notifications = client.get('/api/notifications', params={'user_id': 'receiver_003'})
            self.assertEqual(notifications.status_code, 200)
            self.assertEqual(len(notifications.json()), 1)
            self.assertEqual(notifications.json()[0]['type'], 'order_delivering')


if __name__ == '__main__':
    unittest.main()
