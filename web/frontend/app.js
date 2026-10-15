const $ = id => document.getElementById(id);
const fmt = (value, digits = 1) => value === null || value === undefined ? "--" : Number(value).toFixed(digits);
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

const DOCK_STATE_LABELS = {
  IDLE: "Standby",
  READY_TO_CHARGE: "Vehicle positioned",
  CHARGING: "Charging",
  SMART_PAUSED: "Hold charge",
  FAULT: "Check required",
  NO_DATA: "Waiting for data"
};
const FAULT_LABELS = {
  NONE: "None",
  EMERGENCY_STOP: "Emergency stop active",
  TOF_SENSOR_OFFLINE: "Distance sensor fault",
  TEMP_SENSOR_OFFLINE: "Temperature sensor fault",
  OVER_TEMPERATURE: "Over-temperature"
};

let lastStatus = null;
let lastTariff = null;
let locatedCoordinates = null;
let historyResizeTimer = null;

async function api(path, options = {}) {
  const response = await fetch(path, {headers: {"Content-Type": "application/json"}, ...options});
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(data.detail || `Request failed (${response.status})`);
  return data;
}

async function post(path, body) {
  return api(path, {method: "POST", body: body === undefined ? undefined : JSON.stringify(body)});
}

function showToast(message, isError = false) {
  $("toast").textContent = message;
  $("toast").style.color = isError ? "var(--red)" : "var(--subtle)";
}

function timeText(value, fallback = "--:--") {
  if (!value) return fallback;
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return fallback;
  return date.toLocaleTimeString("en-US", {hour: "2-digit", minute: "2-digit", hour12: false});
}

function dateTimeText(value) {
  if (!value) return "Waiting for data";
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return "Waiting for data";
  const today = new Date();
  const sameDay = date.toDateString() === today.toDateString();
  return `${sameDay ? "Today" : date.toLocaleDateString("en-US", {month: "numeric", day: "numeric"})} ${timeText(value)}`;
}

function safetyMessage(telemetry) {
  if (telemetry.emergency_stop) return "Emergency stop active. Check the physical system before continuing.";
  if (telemetry.fault && telemetry.fault !== "NONE") return `Detected ${FAULT_LABELS[telemetry.fault] || "a device fault"}. Inspect before charging.`;
  if (telemetry.timestamp && !telemetry.micro_switch_stable) return "Stable contact has not been confirmed; charging must wait.";
  if (telemetry.temp_warning) return "Temperature warning; charging must wait.";
  return "";
}

function setConnection(online) {
  $("connection").className = `connection ${online ? "online" : "offline"}`;
  $("connection").querySelector("span").textContent = online ? "Backend online" : "Connection lost";
}

function renderPrimaryBattery(percent, timestamp, voltage = null) {
  const valid = percent !== null && percent !== undefined && Number.isFinite(Number(percent));
  const value = valid ? Math.round(clamp(Number(percent), 0, 100)) : null;
  const voltageText = voltage !== null && voltage !== undefined && Number.isFinite(Number(voltage)) ? `${Number(voltage).toFixed(2)} V` : null;
  $("vehicle-soc").textContent = value === null ? "--" : String(value);
  if (value === null) {
    $("battery-caption").textContent = "Waiting for vehicle voltage";
  } else if (value >= 90) {
    $("battery-caption").textContent = `${voltageText ? `Vehicle voltage ${voltageText}; ` : ""}estimated charge is high`;
  } else if (value >= 80) {
    $("battery-caption").textContent = `${voltageText ? `Vehicle voltage ${voltageText}; ` : ""}base charge complete`;
  } else if (value >= 30) {
    $("battery-caption").textContent = `${voltageText ? `Vehicle voltage ${voltageText}; ` : ""}estimated charge is sufficient`;
  } else {
    $("battery-caption").textContent = `${voltageText ? `Vehicle voltage ${voltageText}; ` : ""}estimated charge is low; recharge soon`;
  }
  $("vehicle-freshness").textContent = timestamp ? `Vehicle voltage updated at ${timeText(timestamp)}` : "No vehicle voltage data";
}

function renderPlan(smart, config) {
  const enabled = Boolean(smart && smart.enabled);
  const plan = smart && smart.plan;
  const runtime = smart && smart.runtime;
  $("smart-toggle").checked = enabled;
  $("hero-smart-toggle").textContent = enabled ? "Disable smart charging" : "Enable smart charging";
  $("plan-phase").textContent = runtime && runtime.phase_cn ? runtime.phase_cn : (enabled ? "Preparing schedule" : "Standby");
  $("smart-summary").textContent = runtime && runtime.detail_cn
    ? runtime.detail_cn
    : "Enable to schedule charging around observed usage and expected departure.";
  if (!plan) {
    $("plan-charge-start").textContent = "--:--";
    $("topoff-start").textContent = "--:--";
    $("predicted-departure").textContent = "--:--";
    return;
  }
  $("plan-charge-start").textContent = timeText(plan.predicted_charge_start);
  $("topoff-start").textContent = timeText(plan.topoff_start);
  $("predicted-departure").textContent = timeText(plan.predicted_departure);
  $("departure-time").value = timeText(plan.predicted_departure, config.default_leave_time || "09:00");
  if (plan.reasoning_cn) {
    $("smart-summary").textContent = `${runtime && runtime.detail_cn ? `${runtime.detail_cn}. ` : ""}${plan.reasoning_cn}`;
  }
}

function renderStatus(data) {
  lastStatus = data;
  setConnection(true);
  const telemetry = data.telemetry || {};
  const vehicle = data.vehicle || {};
  const config = data.config || {};
  const remaining = data.remaining || {};
  const delivery = data.power_delivery || {};

  if (!vehicleState.lastBatteryAt && vehicle.soc_percent !== null && vehicle.soc_percent !== undefined && Number.isFinite(Number(vehicle.soc_percent))) {
    vehicleState.latestPercent = Number(vehicle.soc_percent);
    vehicleState.latestVoltage = vehicle.voltage_v !== null && vehicle.voltage_v !== undefined && Number.isFinite(Number(vehicle.voltage_v)) ? Number(vehicle.voltage_v) : null;
    vehicleState.lastBatteryAt = new Date(vehicle.timestamp).getTime();
  }
  const primarySoc = vehicleState.latestPercent !== null ? vehicleState.latestPercent : vehicle.soc_percent;
  const primaryTime = vehicleState.lastBatteryAt ? new Date(vehicleState.lastBatteryAt).toISOString() : vehicle.timestamp;
  const primaryVoltage = vehicleState.latestVoltage !== null ? vehicleState.latestVoltage : vehicle.voltage_v;
  renderPrimaryBattery(primarySoc, primaryTime, primaryVoltage);

  $("target").textContent = `${config.charge_limit_percent || 90}%`;
  $("limit").value = config.charge_limit_percent || 90;
  $("limit-value").textContent = `${config.charge_limit_percent || 90}%`;
  $("departure-time").value = $("departure-time").value || config.default_leave_time || "09:00";

  if (!delivery.charging) {
    $("power-delivery").textContent = "Not charging";
    $("max-power-note").textContent = `Configured power limit: ${fmt(delivery.max_power_w, 0)} W`;
  } else if (delivery.at_max_power) {
    $("power-delivery").textContent = "Near configured limit";
    $("max-power-note").textContent = "Near the configured dock power limit";
  } else {
    $("power-delivery").textContent = `${delivery.ratio_percent || 0}% of configured power`;
    $("max-power-note").textContent = `${fmt(delivery.actual_power_w, 1)} W / ${fmt(delivery.max_power_w, 1)} W configured`;
  }
  $("completion").textContent = remaining.completion_time ? dateTimeText(remaining.completion_time) : "Waiting for valid power";
  $("remaining").textContent = remaining.minutes === null || remaining.minutes === undefined
    ? "Waiting for estimate"
    : remaining.minutes === 0 ? "Target reached" : `About ${remaining.minutes} min`;
  $("power").textContent = `${fmt(telemetry.power_w, 1)} W`;
  $("temperature").textContent = `${fmt(telemetry.temperature_c, 1)} ℃`;
  $("micro").textContent = telemetry.micro_switch_stable ? "Stable" : "Unconfirmed";
  $("estop").textContent = telemetry.emergency_stop ? "Active" : "Normal";
  $("fault").textContent = FAULT_LABELS[telemetry.fault] || (telemetry.fault ? "Check required" : "None");
  $("charge-state").textContent = DOCK_STATE_LABELS[telemetry.dock_state] || "Updating state";

  const backendCharging = vehicle.charging === null || vehicle.charging === undefined ? null : Boolean(vehicle.charging);
  const vehicleCharging = vehicleState.charging === null ? backendCharging : vehicleState.charging;
  $("vehicle-charging").textContent = vehicleCharging === null ? "Unknown" : vehicleCharging ? "Charging" : "Not charging";
  renderPlan(data.smart_charge, config);

  const warning = safetyMessage(telemetry);
  $("alert").textContent = warning;
  $("alert").classList.toggle("hidden", !warning);
}

async function refresh() {
  try {
    renderStatus(await api("/api/status"));
  } catch (error) {
    setConnection(false);
    showToast(error.message, true);
  }
}

function renderTariff(tariff) {
  lastTariff = tariff;
  const current = tariff.current_period;
  const location = tariff.location;
  if (location) {
    $("location-label").textContent = location.city || location.province || "Location updated";
  } else {
    $("location-label").textContent = tariff.region || "Default region";
  }
  $("tariff-band").textContent = tariff.pricing_status_cn || "Tariff unverified";
  $("current-tariff").textContent = current && Number.isFinite(Number(current.rate)) ? Number(current.rate).toFixed(4) : "Unverified";
  $("tariff-source").textContent = `${tariff.source_name || "Reference tariff data"} · Reference date ${tariff.verified_on || "Unverified"} · Check your electricity bill`;

  const periods = $("tariff-periods");
  periods.replaceChildren();
  (tariff.periods || []).forEach(period => {
    const item = document.createElement("div");
    item.className = `tariff-period ${current && current.kind === period.kind ? "active" : ""}`;
    const label = document.createElement("span");
    label.textContent = `${period.label} ${period.start}—${period.end}`;
    const value = document.createElement("strong");
    value.textContent = Number.isFinite(Number(period.rate)) ? `${Number(period.rate).toFixed(4)} CNY` : "Rate unverified";
    item.append(label, value);
    periods.append(item);
  });

  const next = tariff.next_cheapest_window;
  if (!tariff.region_match) {
    $("tariff-next").textContent = "Location does not match the reference region. Check the applicable tariff and time bands.";
  } else if (next && next.period) {
    const start = new Date(next.start);
    const startsNow = Date.now() - start.getTime() > -60000;
    $("tariff-next").textContent = startsNow
      ? `Reference off-peak period ends at ${next.period.end}.`
      : `Next reference off-peak window: ${next.period.start}-${next.period.end}. The schedule uses observed usage times.`;
  } else {
    $("tariff-next").textContent = "Tariff periods are unavailable. Try again later.";
  }
}

async function loadTariff() {
  try { renderTariff(await api("/api/tariff")); }
  catch (error) { showToast(error.message, true); }
}

function requestLocation({silent = false} = {}) {
  if (!navigator.geolocation) {
    if (!silent) showToast("Geolocation is unavailable; showing the default reference tariff.", true);
    return Promise.resolve(null);
  }
  $("location-label").textContent = "Locating";
  return new Promise(resolve => {
    navigator.geolocation.getCurrentPosition(async position => {
      locatedCoordinates = {
        latitude: position.coords.latitude,
        longitude: position.coords.longitude
      };
      try {
        const result = await post("/api/location", locatedCoordinates);
        renderTariff(result.tariff);
        if (!silent) showToast("Location and reference tariff updated");
        resolve(result);
      } catch (error) {
        if (!silent) showToast(error.message, true);
        resolve(null);
      }
    }, () => {
      $("location-label").textContent = lastTariff && lastTariff.region ? lastTariff.region : "Using default region";
      if (!silent) showToast("Location was not provided; showing the default reference tariff.", true);
      resolve(null);
    }, {enableHighAccuracy: false, timeout: 8000, maximumAge: 15 * 60 * 1000});
  });
}

const ROS_TOPICS = {voltage: "/PowerVoltage", charging: "/robot_charging_flag"};
const ROS_MESSAGE_TYPES = {voltage: "std_msgs/msg/Float32", charging: "std_msgs/msg/Bool"};
// Original 24 V/6S vehicle voltage-to-charge curve.
// Use vehicle ROS /PowerVoltage, not dock INA226 voltage.
const VEHICLE_BATTERY_POINTS = [
  [21.0, 0], [21.6, 10], [22.2, 20], [22.8, 35],
  [23.4, 50], [24.0, 65], [24.6, 80], [25.2, 100]
];
const ROS_STORAGE_KEY = "smartCharging.vehicleBridgeAddress";
const ROS_FRESH_MS = 12000;
const vehicleState = {
  ros: null,
  subscriptions: [],
  connectionToken: 0,
  connected: false,
  connecting: false,
  lastError: false,
  lastBatteryAt: 0,
  lastChargingAt: 0,
  latestVoltage: null,
  latestPercent: null,
  charging: null,
  syncTimer: null
};

function normalizeBridgeUrl(value) {
  const raw = String(value || "").trim();
  if (!raw) return "";
  if (/^wss?:\/\//i.test(raw)) return raw;
  const withoutTrailingSlash = raw.replace(/\/+$/, "");
  if (withoutTrailingSlash.includes(":")) return `ws://${withoutTrailingSlash}`;
  return `ws://${withoutTrailingSlash}:9090`;
}

function voltageToVehiclePercent(voltage) {
  if (!Number.isFinite(voltage) || voltage <= 0) return null;
  if (voltage <= VEHICLE_BATTERY_POINTS[0][0]) return 0;
  const last = VEHICLE_BATTERY_POINTS[VEHICLE_BATTERY_POINTS.length - 1];
  if (voltage >= last[0]) return 100;
  for (let index = 1; index < VEHICLE_BATTERY_POINTS.length; index += 1) {
    const previous = VEHICLE_BATTERY_POINTS[index - 1];
    const current = VEHICLE_BATTERY_POINTS[index];
    if (voltage <= current[0]) {
      const ratio = (voltage - previous[0]) / (current[0] - previous[0]);
      return Math.round(previous[1] + ratio * (current[1] - previous[1]));
    }
  }
  return 0;
}

function setRosUi(kind, title, detail) {
  $("ros-status").textContent = title;
  $("ros-status").dataset.kind = kind;
  $("ros-log").textContent = detail;
  $("ros-connect").textContent = kind === "connected" ? "Disconnect vehicle" : kind === "connecting" ? "Cancel connection" : kind === "error" ? "Retry connection" : "Connect vehicle";
}

function clearRosSubscriptions() {
  vehicleState.subscriptions.forEach(topic => { try { topic.unsubscribe(); } catch (error) { console.warn(error); } });
  vehicleState.subscriptions = [];
}

function recordVehicleEvent(event) {
  post("/api/vehicle/event", {event, soc_percent: vehicleState.latestPercent}).catch(() => {});
}

function resetVehicleTelemetry() {
  vehicleState.lastBatteryAt = 0;
  vehicleState.lastChargingAt = 0;
  vehicleState.latestVoltage = null;
  vehicleState.latestPercent = null;
  vehicleState.charging = null;
  if (lastStatus && lastStatus.vehicle) {
    renderPrimaryBattery(lastStatus.vehicle.soc_percent, lastStatus.vehicle.timestamp, lastStatus.vehicle.voltage_v);
  } else {
    renderPrimaryBattery(null, null, null);
  }
  $("vehicle-charging").textContent = "Unknown";
}

function disconnectRos(updateUi = true) {
  const wasConnected = vehicleState.connected;
  vehicleState.connectionToken += 1;
  clearRosSubscriptions();
  if (vehicleState.ros) { try { vehicleState.ros.close(); } catch (error) { console.warn(error); } }
  vehicleState.ros = null;
  vehicleState.connected = false;
  vehicleState.connecting = false;
  vehicleState.lastError = false;
  if (wasConnected) recordVehicleEvent("ros_disconnected");
  resetVehicleTelemetry();
  if (updateUi) setRosUi("idle", "Disconnected", "Vehicle disconnected.");
}

function scheduleVehicleSync() {
  window.clearTimeout(vehicleState.syncTimer);
  vehicleState.syncTimer = window.setTimeout(async () => {
    if (vehicleState.latestPercent === null) return;
    try {
      await post("/api/vehicle/status", {
        soc_percent: Number(vehicleState.latestPercent.toFixed(2)),
        voltage_v: Number(vehicleState.latestVoltage.toFixed(3)),
        charging: vehicleState.charging,
        source: "rosbridge_power_voltage_estimated"
      });
    } catch (error) {
      showToast("Vehicle voltage received, but schedule synchronization failed.", true);
    }
  }, 900);
}

function renderLiveVehicle() {
  const timestamp = vehicleState.lastBatteryAt ? new Date(vehicleState.lastBatteryAt).toISOString() : null;
  renderPrimaryBattery(vehicleState.latestPercent, timestamp, vehicleState.latestVoltage);
  $("vehicle-charging").textContent = vehicleState.charging === null ? "Unknown" : vehicleState.charging ? "Charging" : "Not charging";
}

function subscribeVehicleTelemetry() {
  const voltageTopic = new ROSLIB.Topic({
    ros: vehicleState.ros,
    name: ROS_TOPICS.voltage,
    messageType: ROS_MESSAGE_TYPES.voltage,
    throttle_rate: 300,
    queue_length: 1
  });
  voltageTopic.subscribe(message => {
    const voltage = Number(message && message.data);
    const percent = voltageToVehiclePercent(voltage);
    if (percent === null) return;
    vehicleState.latestVoltage = voltage;
    vehicleState.latestPercent = percent;
    vehicleState.lastBatteryAt = Date.now();
    renderLiveVehicle();
    scheduleVehicleSync();
  });
  vehicleState.subscriptions.push(voltageTopic);

  const chargingTopic = new ROSLIB.Topic({
    ros: vehicleState.ros,
    name: ROS_TOPICS.charging,
    messageType: ROS_MESSAGE_TYPES.charging,
    throttle_rate: 300,
    queue_length: 1
  });
  chargingTopic.subscribe(message => {
    vehicleState.charging = Boolean(message && message.data);
    vehicleState.lastChargingAt = Date.now();
    renderLiveVehicle();
    scheduleVehicleSync();
  });
  vehicleState.subscriptions.push(chargingTopic);
}

function connectRos() {
  const wsUrl = normalizeBridgeUrl($("ros-url").value);
  if (!wsUrl) { setRosUi("error", "Connection error", "Enter the vehicle address."); return; }
  if (typeof ROSLIB === "undefined") { setRosUi("error", "Connection error", "ROSLIB failed to load. Check the local static files."); return; }
  disconnectRos(false);
  const token = vehicleState.connectionToken + 1;
  vehicleState.connectionToken = token;
  vehicleState.lastError = false;
  vehicleState.connecting = true;
  localStorage.setItem(ROS_STORAGE_KEY, $("ros-url").value.trim());
  setRosUi("connecting", "Connecting", "Connecting to vehicle...");
  const ros = new ROSLIB.Ros({url: wsUrl});
  vehicleState.ros = ros;
  ros.on("connection", () => {
    if (token !== vehicleState.connectionToken) return;
    vehicleState.connected = true;
    vehicleState.connecting = false;
    setRosUi("connected", "Vehicle online", "Connected; receiving vehicle voltage and charging state.");
    recordVehicleEvent("ros_connected");
    subscribeVehicleTelemetry();
  });
  ros.on("error", error => {
    if (token !== vehicleState.connectionToken) return;
    vehicleState.connected = false;
    vehicleState.connecting = false;
    vehicleState.lastError = true;
    setRosUi("error", "Connection error", error && error.message ? `Connection failed: ${error.message}` : "Check that the vehicle is powered on and on the same network.");
  });
  ros.on("close", () => {
    if (token !== vehicleState.connectionToken) return;
    const wasConnected = vehicleState.connected;
    clearRosSubscriptions();
    vehicleState.connected = false;
    vehicleState.connecting = false;
    if (wasConnected) recordVehicleEvent("ros_disconnected");
    setRosUi(vehicleState.lastError ? "error" : "idle", vehicleState.lastError ? "Connection error" : "Disconnected", vehicleState.lastError ? "Check the vehicle network and retry." : "Vehicle connection closed.");
    vehicleState.lastError = false;
  });
}

function updateVehicleFreshness() {
  if (!vehicleState.lastBatteryAt) return;
  const ageSeconds = Math.floor((Date.now() - vehicleState.lastBatteryAt) / 1000);
  if (ageSeconds > ROS_FRESH_MS / 1000) {
    $("vehicle-freshness").textContent = `Vehicle voltage is ${ageSeconds} seconds old`;
  } else {
    $("vehicle-freshness").textContent = ageSeconds <= 1 ? "Vehicle voltage just updated" : `Vehicle voltage updated ${ageSeconds} seconds ago`;
  }
}

function draw(canvas, values, color, bars = false) {
  const context = canvas.getContext("2d");
  const rect = canvas.getBoundingClientRect();
  if (!rect.width || !rect.height) return;
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.round(rect.width * ratio);
  canvas.height = Math.round(rect.height * ratio);
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  const width = rect.width;
  const height = rect.height;
  const padding = 24;
  context.clearRect(0, 0, width, height);
  context.strokeStyle = "rgba(255,255,255,.07)";
  context.lineWidth = 1;
  for (let index = 0; index < 4; index += 1) {
    const y = padding + (height - 2 * padding) * index / 3;
    context.beginPath();
    context.moveTo(padding, y);
    context.lineTo(width - padding, y);
    context.stroke();
  }
  const numeric = values.filter(value => Number.isFinite(value));
  if (!numeric.length) {
    context.fillStyle = "#737570";
    context.font = "12px sans-serif";
    context.textAlign = "center";
    context.fillText("No valid records", width / 2, height / 2);
    return;
  }
  let min = Math.min(...numeric);
  let max = Math.max(...numeric);
  if (min === max) { min -= 1; max += 1; }
  const x = index => padding + index * (width - 2 * padding) / Math.max(1, values.length - 1);
  const y = value => height - padding - (value - min) * (height - 2 * padding) / (max - min);
  context.strokeStyle = color;
  context.fillStyle = color;
  context.lineWidth = 2;
  if (bars) {
    const barWidth = Math.max(2, (width - 2 * padding) / values.length * .62);
    values.forEach((value, index) => {
      if (Number.isFinite(value)) context.fillRect(x(index) - barWidth / 2, y(value), barWidth, height - padding - y(value));
    });
  } else {
    const gradient = context.createLinearGradient(0, padding, 0, height - padding);
    gradient.addColorStop(0, `${color}45`);
    gradient.addColorStop(1, `${color}00`);
    context.beginPath();
    let started = false;
    values.forEach((value, index) => {
      if (!Number.isFinite(value)) return;
      if (started) context.lineTo(x(index), y(value));
      else { context.moveTo(x(index), y(value)); started = true; }
    });
    context.stroke();
    context.lineTo(x(values.length - 1), height - padding);
    context.lineTo(padding, height - padding);
    context.closePath();
    context.fillStyle = gradient;
    context.fill();
  }
  context.fillStyle = "#737570";
  context.font = "10px sans-serif";
  context.textAlign = "left";
  context.fillText(max.toFixed(1), 2, 12);
  context.fillText(min.toFixed(1), 2, height - 4);
}

async function loadHistory() {
  try {
    const [dock, vehicle] = await Promise.all([api("/api/history?limit=300"), api("/api/vehicle/history?limit=300")]);
    const numeric = value => value === null || value === undefined ? null : Number(value);
    draw($("soc-chart"), vehicle.items.map(item => numeric(item.soc_percent)), "#ded6c5");
    draw($("power-chart"), dock.items.map(item => numeric(item.power_w)), "#b6e56d", true);
    draw($("temp-chart"), dock.items.map(item => numeric(item.temperature_c)), "#e0b56d");
  } catch (error) {
    showToast(error.message, true);
  }
}

function node(tag, className, text) {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (text !== undefined && text !== null) element.textContent = String(text);
  return element;
}

function reportSection(title, className = "") {
  const section = node("article", `report-section ${className}`.trim());
  section.append(node("h3", "", title));
  return section;
}

function appendEvidence(section, items) {
  const list = node("ul", "report-list");
  (Array.isArray(items) ? items : []).forEach(item => list.append(node("li", "", item)));
  if (list.children.length) section.append(list);
}

function renderReport(report) {
  const content = $("report-content");
  content.replaceChildren();

  const cover = node("div", "report-cover");
  cover.append(node("h3", "", report.headline || "Weekly charging summary"));
  cover.append(node("p", "", report.executive_summary || "Preparing this week's data."));
  content.append(cover);

  const grid = node("div", "report-grid");
  const habits = report.charging_habits || {};
  const habitSection = reportSection("Charging habits");
  habitSection.append(node("p", "", habits.analysis || "Collecting charging history."));
  appendEvidence(habitSection, habits.evidence);
  grid.append(habitSection);

  const battery = report.battery_assessment || {};
  const batterySection = reportSection("Battery and charging observations");
  batterySection.append(node("p", "", battery.analysis || "Insufficient data for assessment."));
  const batteryFacts = node("div", "report-facts");
  const statusFact = node("div", "report-fact");
  statusFact.append(node("span", "", "Assessment status"), node("strong", "", battery.status || "Insufficient data"));
  batteryFacts.append(statusFact);
  (Array.isArray(battery.evidence) ? battery.evidence.slice(0, 2) : []).forEach((item, index) => {
    const fact = node("div", "report-fact");
    fact.append(node("span", "", index === 0 ? "Key observation" : "Data source"), node("strong", "", item));
    batteryFacts.append(fact);
  });
  batterySection.append(batteryFacts);
  grid.append(batterySection);

  const tariff = report.tariff_advice || {};
  const tariffSection = reportSection("Tariff guidance", "wide");
  tariffSection.append(node("p", "", tariff.recommendation || "Local tariff requires verification."));
  const tariffFacts = node("div", "report-facts");
  [
    ["Region", tariff.location || "Default region"],
    ["Current period", tariff.current_period || "Unverified"],
    ["Off-peak window", tariff.cheapest_window || "Unverified"]
  ].forEach(([label, value]) => {
    const fact = node("div", "report-fact");
    fact.append(node("span", "", label), node("strong", "", value));
    tariffFacts.append(fact);
  });
  tariffSection.append(tariffFacts);
  if (tariff.source_note) tariffSection.append(node("p", "source-note", tariff.source_note));
  grid.append(tariffSection);

  const riskSection = reportSection("Observations requiring attention");
  const riskList = node("div", "risk-list");
  (Array.isArray(report.risks) ? report.risks : []).forEach(risk => {
    const item = node("div", "risk-item");
    item.append(node("span", "risk-level", risk.level || "Note"));
    const copy = node("div", "");
    copy.append(node("strong", "", risk.title || "Operating note"), node("p", "", risk.detail || ""));
    item.append(copy);
    riskList.append(item);
  });
  riskSection.append(riskList);
  grid.append(riskSection);

  const actionSection = reportSection("Suggested actions");
  const actionList = node("div", "action-list");
  (Array.isArray(report.actions) ? report.actions : []).forEach((action, index) => {
    const item = node("div", "action-item");
    item.append(node("span", "action-index", action.priority || index + 1));
    const copy = node("div", "");
    copy.append(node("strong", "", action.action || "Continue monitoring"), node("p", "", action.reason || ""));
    item.append(copy);
    actionList.append(item);
  });
  actionSection.append(actionList);
  grid.append(actionSection);
  content.append(grid);

  const note = `${report.data_note || "Report based on recorded data."}${report.analysis_source ? ` Analysis source: ${report.analysis_source}. ` : ""}`;
  content.append(node("p", "report-note", note));
}

async function generateReport() {
  $("report-panel").classList.remove("hidden");
  $("report-loading").classList.remove("hidden");
  $("report-content").replaceChildren();
  $("report-panel").scrollIntoView({behavior: "smooth", block: "start"});
  $("report").disabled = true;
  // Use the saved/default tariff; geolocation requires an explicit button click.
  try {
    const result = await post("/api/report/generate", locatedCoordinates || {});
    renderReport(result.report || {});
    showToast(result.message || "Weekly report generated");
  } catch (error) {
    const message = node("p", "report-note", `Report unavailable: ${error.message}`);
    $("report-content").append(message);
    showToast(error.message, true);
  } finally {
    $("report-loading").classList.add("hidden");
    $("report").disabled = false;
  }
}

async function setSmartCharging(enabled) {
  $("smart-toggle").disabled = true;
  $("hero-smart-toggle").disabled = true;
  try {
    const result = await post("/api/config/smart-charge", {
      enabled,
      departure_time: $("departure-time").value || "09:00"
    });
    showToast(result.message || "Schedule updated");
    await refresh();
  } catch (error) {
    $("smart-toggle").checked = !enabled;
    showToast(error.message, true);
  } finally {
    $("smart-toggle").disabled = false;
    $("hero-smart-toggle").disabled = false;
  }
}

document.querySelectorAll("[data-command]").forEach(button => {
  button.addEventListener("click", async () => {
    try {
      const result = await post(`/api/command/${button.dataset.command}`);
      showToast(result.message || "Request queued");
      await refresh();
    } catch (error) { showToast(error.message, true); }
  });
});

$("limit").addEventListener("input", event => { $("limit-value").textContent = `${event.target.value}%`; });
$("save-limit").addEventListener("click", async () => {
  try {
    const result = await post("/api/config/charge-limit", {charge_limit_percent: Number($("limit").value)});
    showToast(result.message || "Charge limit saved");
    await refresh();
  } catch (error) { showToast(error.message, true); }
});
$("smart-toggle").addEventListener("change", event => setSmartCharging(event.target.checked));
$("hero-smart-toggle").addEventListener("click", () => setSmartCharging(!$("smart-toggle").checked));
$("regenerate-plan").addEventListener("click", async () => {
  try {
    const result = await post("/api/smart-charge/regenerate", {enabled: true, departure_time: $("departure-time").value || "09:00"});
    showToast(result.message || "Schedule updated");
    await refresh();
  } catch (error) { showToast(error.message, true); }
});
$("report").addEventListener("click", generateReport);
$("close-report").addEventListener("click", () => $("report-panel").classList.add("hidden"));
$("location-button").addEventListener("click", () => requestLocation());
$("refresh-history").addEventListener("click", loadHistory);
$("ros-connect").addEventListener("click", () => vehicleState.connected || vehicleState.connecting ? disconnectRos() : connectRos());
$("ros-url").addEventListener("keydown", event => { if (event.key === "Enter") connectRos(); });
window.addEventListener("resize", () => {
  window.clearTimeout(historyResizeTimer);
  historyResizeTimer = window.setTimeout(loadHistory, 180);
});

const savedRosUrl = localStorage.getItem(ROS_STORAGE_KEY);
if (savedRosUrl) $("ros-url").value = savedRosUrl;
setRosUi("idle", "Disconnected", "Enter the vehicle address to connect.");
setInterval(updateVehicleFreshness, 1000);
setInterval(refresh, 5000);

refresh();
loadTariff();
loadHistory();
// Geolocation is requested only when the user clicks the tariff-location button.
