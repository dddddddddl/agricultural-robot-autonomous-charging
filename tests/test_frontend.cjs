// Test the real battery renderer with a minimal DOM, without a vehicle or browser.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../web/frontend/app.js'), 'utf8');
const elements = new Map();
const scope = {
  document: {getElementById(id) {
    if (!elements.has(id)) elements.set(id, {textContent: ''});
    return elements.get(id);
  }},
};
const setup = source.slice(0, source.indexOf('const DOCK_STATE_LABELS'));
const renderer = source.slice(source.indexOf('function renderPrimaryBattery('), source.indexOf('function renderPlan('));
vm.runInNewContext(setup + '\nfunction timeText() { return "12:00"; }\n' + renderer, scope);
for (const value of [null, undefined]) {
  scope.renderPrimaryBattery(value, null, null);
  assert.equal(elements.get('vehicle-soc').textContent, '--');
  assert.equal(elements.get('battery-caption').textContent, 'Waiting for vehicle voltage');
}
scope.renderPrimaryBattery(0, 'test', 21.0);
assert.equal(elements.get('vehicle-soc').textContent, '0');
scope.renderPrimaryBattery(65, 'test', 24.0);
assert.equal(elements.get('vehicle-soc').textContent, '65');
assert.match(elements.get('battery-caption').textContent, /24.00 V/);
console.log('PASS: missing battery data stays unknown; zero and measured data render correctly.');
