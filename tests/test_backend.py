"""Host checks for telemetry, command compatibility and offline English reports.

These checks use an isolated database and never contact a vehicle or cloud API.
"""
import os
import sys
import tempfile
import unittest
from pathlib import Path

os.environ['DEEPSEEK_API_KEY'] = ''
os.environ['TARIFF_API_URL'] = ''
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'web' / 'backend'))

from fastapi.testclient import TestClient
import database
from battery import estimate_soc_from_vehicle_voltage
from main import app


class BackendTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        database.DATABASE_PATH = Path(self.temp.name) / 'test.db'
        self.context = TestClient(app)
        self.client = self.context.__enter__()

    def tearDown(self):
        self.context.__exit__(None, None, None)
        self.temp.cleanup()

    def test_empty_database_does_not_invent_vehicle_measurements(self):
        self.assertTrue(self.client.get('/api/health').json()['ok'])
        status = self.client.get('/api/status').json()
        self.assertEqual(status['telemetry']['dock_state'], 'NO_DATA')
        self.assertIsNone(status['vehicle'])
        self.assertIsNone(status['remaining']['minutes'])
        self.assertEqual(self.client.get('/').status_code, 200)
        self.assertEqual(self.client.get('/static/app.js').status_code, 200)

    def test_firmware_payload_and_vehicle_voltage_remain_separate(self):
        payload = {
            'state': 'CHARGING', 'relayOn': True, 'fault': 'NONE',
            'voltage_v': 25.5, 'current_a': 2.0, 'power_w': 51.0,
            'micro_switch_stable': True, 'temp_ok': True,
            'voltage_ok': True, 'current_ok': True,
        }
        self.assertEqual(self.client.post('/api/status', json=payload).status_code, 200)
        self.assertEqual(self.client.get('/api/status').json()['telemetry']['power_w'], 51.0)
        result = self.client.post('/api/vehicle/status', json={
            'soc_percent': 1, 'voltage_v': 24.0, 'charging': True,
        }).json()
        self.assertEqual(result['soc_percent'], 65)
        self.assertEqual(self.client.get('/api/status').json()['vehicle']['soc_percent'], 65)
        self.assertEqual(estimate_soc_from_vehicle_voltage(21.0), 0)
        self.assertEqual(estimate_soc_from_vehicle_voltage(25.2), 100)
        self.assertEqual(estimate_soc_from_vehicle_voltage(24.3), 73)

    def test_commands_are_compatible_and_consumed_once(self):
        for endpoint, command in (
            ('start', 'REQUEST_START_CHARGE'), ('stop', 'STOP_CHARGING'),
            ('clear-fault', 'CLEAR_FAULT'),
        ):
            result = self.client.post('/api/command/' + endpoint).json()
            self.assertEqual(result['queued_command'], command)
            self.assertEqual(self.client.get('/api/command/poll').json()['command'], command)
            self.assertEqual(self.client.get('/api/command').json()['command'], 'NONE')
        with self.assertRaises(ValueError):
            database.queue_command('RELAY_ON')

    def test_offline_report_and_schedule_are_english(self):
        self.client.post('/api/dev/mock-status', json={'scenario': 'charging', 'points': 4})
        result = self.client.post('/api/report/generate').json()
        self.assertTrue(result['ok'])
        self.assertIn('Weekly', result['report']['headline'])
        self.assertNotRegex(str(result), r'[\u3400-\u9fff]')
        plan = self.client.post('/api/config/smart-charge', json={
            'enabled': True, 'departure_time': '09:00',
        }).json()
        self.assertTrue(plan['ok'])
        self.assertNotRegex(str(plan), r'[\u3400-\u9fff]')

    def test_empty_report_keeps_unmeasured_conditions_unknown(self):
        report = self.client.post('/api/report/generate').json()['report']
        self.assertIn('No dock telemetry', report['battery_assessment']['analysis'])
        self.assertEqual(report['risks'][0]['level'], 'Unknown')
        self.assertIn('configured fallback', report['charging_habits']['analysis'])
        self.assertNotIn('Typical charging start', report['charging_habits']['analysis'])
        self.assertIn('No valid vehicle voltage', report['battery_assessment']['evidence'][1])

    def test_idle_contact_is_not_reported_as_charging_failure(self):
        self.client.post('/api/status', json={
            'state': 'IDLE', 'fault': 'NONE', 'relayOn': False,
            'micro_switch_stable': False,
        })
        report = self.client.post('/api/report/generate').json()['report']
        contact = next(risk for risk in report['risks'] if risk['title'] == 'Unconfirmed contact intervals')
        self.assertIn('includes an idle dock', contact['detail'])
        self.assertNotIn('contact interruptions;', contact['detail'])

    def test_unselected_location_is_labeled_as_default_reference(self):
        tariff = self.client.get('/api/tariff').json()
        self.assertIsNone(tariff['location'])
        self.assertIn('location not provided', tariff['pricing_status_cn'])

    def test_request_validation_and_reference_tariff_region(self):
        self.assertEqual(self.client.post('/api/config/charge-limit', json={
            'charge_limit_percent': 120,
        }).status_code, 422)
        self.assertEqual(self.client.post('/api/config/smart-charge', json={
            'enabled': True, 'departure_time': '25:99',
        }).status_code, 422)
        from tariff import get_tariff
        self.assertTrue(get_tariff({'province': 'Jiangsu Province'})['region_match'])
        self.assertFalse(get_tariff({'province': 'California'})['region_match'])
        self.assertEqual(get_tariff({'province': 'California'})['periods'], [])


if __name__ == '__main__':
    unittest.main()
