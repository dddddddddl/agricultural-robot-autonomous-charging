#include "WiFiDockModule.h"

#include <HTTPClient.h>
#include <string.h>

WiFiDockModule::WiFiDockModule(
  const char* ssid,
  const char* password,
  const char* serverBaseUrl
)
  : _ssid(ssid),
    _password(password),
    _serverBaseUrl(serverBaseUrl),
    _configured(false),
    _lastReconnectAttemptMs(0),
    _wasConnected(false),
    _networkMutex(nullptr),
    _networkTaskHandle(nullptr),
    _uploadPending(false),
    _pollPending(false),
    _receivedCommand(CMD_NONE),
    _stopPending(false),
    _lastPollQueuedMs(0),
    _lastHttpCode(0),
    _lastError("NOT_STARTED") {
}

bool WiFiDockModule::hasValidConfig() const {
  if (_ssid == nullptr || _serverBaseUrl == nullptr) return false;

  String ssidText(_ssid);
  String serverText(_serverBaseUrl);
  ssidText.trim();
  serverText.trim();
  return ssidText.length() > 0 && serverText.length() > 0 &&
         ssidText != "YOUR_WIFI_SSID" &&
         serverText != "http://YOUR_PC_IP:5000";
}

void WiFiDockModule::begin() {
  _configured = hasValidConfig();
  if (!_configured) {
    setLastError("WIFI_NOT_CONFIGURED");
    Serial.println("[WiFi] not configured. Configure include/local_config.h using local_config.example.h.");
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  ensureNetworkTask();

  Serial.print("[WiFi] MAC: ");
  Serial.println(WiFi.macAddress());
  startConnection();
}

void WiFiDockModule::startConnection() {
  _lastReconnectAttemptMs = millis();
  _wasConnected = false;
  setLastError("CONNECTING");
  Serial.print("[WiFi] connection started asynchronously, SSID: ");
  Serial.println(_ssid);
  WiFi.begin(_ssid, _password == nullptr ? "" : _password);
}

void WiFiDockModule::update() {
  if (!_configured) return;

  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    if (!_wasConnected) {
      _wasConnected = true;
      setLastError("NONE");
      Serial.print("[WiFi] connected, IP: ");
      Serial.println(WiFi.localIP());
    }
    return;
  }

  if (_wasConnected) {
    _wasConnected = false;
    setLastError("WIFI_DISCONNECTED");
    Serial.println("[WiFi] disconnected; reconnect will be attempted asynchronously.");
  }

  const unsigned long now = millis();
  if (now - _lastReconnectAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
    startConnection();
  }
}

void WiFiDockModule::reconnectNow() {
  if (!_configured) {
    setLastError("WIFI_NOT_CONFIGURED");
    Serial.println("[WiFi] reconnect blocked: WiFi not configured.");
    return;
  }

  WiFi.disconnect(false, false);
  startConnection();
}

void WiFiDockModule::ensureNetworkTask() {
  if (_networkMutex == nullptr) {
    _networkMutex = xSemaphoreCreateMutex();
  }
  if (_networkTaskHandle == nullptr && _networkMutex != nullptr) {
    xTaskCreatePinnedToCore(
      networkTaskEntry,
      "dock_http",
      8192,
      this,
      1,
      &_networkTaskHandle,
      0
    );
  }
}

void WiFiDockModule::networkTaskEntry(void* context) {
  static_cast<WiFiDockModule*>(context)->networkTask();
}

void WiFiDockModule::networkTask() {
  for (;;) {
    bool doUpload = false;
    bool doPoll = false;
    String statusJson;

    if (_networkMutex != nullptr && xSemaphoreTake(_networkMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (WiFi.status() == WL_CONNECTED) {
        doUpload = _uploadPending;
        doPoll = _pollPending;
        if (doUpload) {
          statusJson = _pendingStatusJson;
          _uploadPending = false;
        }
        if (doPoll) _pollPending = false;
      }
      xSemaphoreGive(_networkMutex);
    }

    if (doUpload) performStatusUpload(statusJson);
    if (doPoll) performCommandPoll();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

bool WiFiDockModule::uploadStatus(const String& statusJson) {
  if (!_configured || WiFi.status() != WL_CONNECTED || _networkMutex == nullptr) {
    setLastError(_configured ? "WIFI_DISCONNECTED" : "WIFI_NOT_CONFIGURED");
    return false;
  }

  if (xSemaphoreTake(_networkMutex, 0) != pdTRUE) return false;
  _pendingStatusJson = statusJson;
  _uploadPending = true;
  xSemaphoreGive(_networkMutex);
  return true;
}

WiFiDockModule::RemoteCommand WiFiDockModule::pollCommand() {
  if (!_configured || WiFi.status() != WL_CONNECTED || _networkMutex == nullptr) {
    return CMD_NONE;
  }

  RemoteCommand result = CMD_NONE;
  if (xSemaphoreTake(_networkMutex, 0) != pdTRUE) return result;

  // STOP owns a dedicated latch and always wins over every queued command.
  if (_stopPending) {
    result = CMD_STOP_CHARGING;
    _stopPending = false;
    _receivedCommand = CMD_NONE;
  } else if (_receivedCommand != CMD_NONE) {
    result = _receivedCommand;
    _receivedCommand = CMD_NONE;
  }

  const unsigned long now = millis();
  if (!_pollPending && now - _lastPollQueuedMs >= COMMAND_POLL_INTERVAL_MS) {
    _pollPending = true;
    _lastPollQueuedMs = now;
  }

  xSemaphoreGive(_networkMutex);
  return result;
}

void WiFiDockModule::performStatusUpload(const String& statusJson) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(makeUrl("/api/status"))) {
    setNetworkResult(0, "HTTP_BEGIN_FAILED");
    return;
  }

  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(statusJson);
  http.end();
  setNetworkResult(code, code >= 200 && code < 300 ? "NONE" : "STATUS_POST_FAILED");
}

void WiFiDockModule::performCommandPoll() {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(makeUrl("/api/command/poll"))) {
    setNetworkResult(0, "HTTP_BEGIN_FAILED");
    return;
  }

  const int code = http.GET();
  String payload;
  if (code >= 200 && code < 300) payload = http.getString();
  http.end();

  if (code < 200 || code >= 300) {
    setNetworkResult(code, "COMMAND_POLL_FAILED");
    return;
  }

  const RemoteCommand command = parseCommandPayload(payload);
  if (_networkMutex != nullptr && xSemaphoreTake(_networkMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    if (command == CMD_STOP_CHARGING) {
      // A late STOP must replace START/STATUS/CLEAR_FAULT, even if the Arduino
      // loop is temporarily busy rendering or printing.
      _stopPending = true;
      _receivedCommand = CMD_NONE;
    } else if (!_stopPending && _receivedCommand == CMD_NONE) {
      _receivedCommand = command;
    }
    _lastHttpCode = code;
    _lastError = "NONE";
    xSemaphoreGive(_networkMutex);
  }
}

void WiFiDockModule::setNetworkResult(int httpCode, const char* error) {
  if (_networkMutex == nullptr) {
    // No worker task can exist before the mutex is created, so direct access
    // is safe during early initialization.
    _lastHttpCode = httpCode;
    _lastError = error;
    return;
  }

  if (xSemaphoreTake(_networkMutex, portMAX_DELAY) != pdTRUE) return;
  _lastHttpCode = httpCode;
  _lastError = error;
  xSemaphoreGive(_networkMutex);
}

void WiFiDockModule::setLastError(const char* error) {
  if (_networkMutex == nullptr) {
    _lastError = error;
    return;
  }

  if (xSemaphoreTake(_networkMutex, portMAX_DELAY) != pdTRUE) return;
  _lastError = error;
  xSemaphoreGive(_networkMutex);
}

String WiFiDockModule::makeUrl(const char* path) const {
  String base(_serverBaseUrl == nullptr ? "" : _serverBaseUrl);
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  if (path == nullptr || strlen(path) == 0) return base;
  String p(path);
  if (!p.startsWith("/")) p = "/" + p;
  return base + p;
}

String WiFiDockModule::extractCommandValue(const String& payload) {
  String text(payload);
  text.trim();

  // Keep exact plain-text commands for compatibility with the existing test
  // backend, but never use substring matching.
  if (!text.startsWith("{")) {
    text.toUpperCase();
    return text;
  }

  // Lightweight strict extraction for {"command":"..."}. This deliberately
  // ignores state/message fields, so {"state":"STOPPED"} is not a command.
  String upper(text);
  upper.toUpperCase();
  const int keyPos = upper.indexOf("\"COMMAND\"");
  if (keyPos < 0) return "";

  const int colonPos = upper.indexOf(':', keyPos + 9);
  if (colonPos < 0) return "";

  int valueStart = colonPos + 1;
  while (valueStart < text.length() && isspace(static_cast<unsigned char>(text[valueStart]))) {
    valueStart++;
  }
  if (valueStart >= text.length() || text[valueStart] != '"') return "";

  const int valueEnd = text.indexOf('"', valueStart + 1);
  if (valueEnd < 0) return "";

  String value = text.substring(valueStart + 1, valueEnd);
  value.trim();
  value.toUpperCase();
  return value;
}

WiFiDockModule::RemoteCommand WiFiDockModule::parseCommandPayload(const String& payload) const {
  const String command = extractCommandValue(payload);
  if (command == "STOP_CHARGING" || command == "STOP") return CMD_STOP_CHARGING;
  if (command == "CLEAR_FAULT" || command == "RESET") return CMD_CLEAR_FAULT;
  if (command == "QUERY_STATUS" || command == "STATUS") return CMD_QUERY_STATUS;
  if (command == "REQUEST_START_CHARGE" || command == "START_CHARGE" || command == "START") {
    return CMD_REQUEST_START_CHARGE;
  }
  return CMD_NONE;
}

bool WiFiDockModule::isConfigured() const { return _configured; }
bool WiFiDockModule::isConnected() const { return _configured && WiFi.status() == WL_CONNECTED; }

String WiFiDockModule::ipAddress() const {
  return isConnected() ? WiFi.localIP().toString() : "0.0.0.0";
}

String WiFiDockModule::macAddress() const { return WiFi.macAddress(); }

int WiFiDockModule::lastHttpCode() const {
  if (_networkMutex == nullptr) return _lastHttpCode;

  int value = 0;
  if (xSemaphoreTake(_networkMutex, portMAX_DELAY) != pdTRUE) return value;
  value = _lastHttpCode;
  xSemaphoreGive(_networkMutex);
  return value;
}

String WiFiDockModule::lastError() const {
  if (_networkMutex == nullptr) return _lastError;

  String value("MUTEX_READ_FAILED");
  if (xSemaphoreTake(_networkMutex, portMAX_DELAY) != pdTRUE) return value;
  value = _lastError;
  xSemaphoreGive(_networkMutex);
  return value;
}

const char* WiFiDockModule::commandToString(RemoteCommand cmd) {
  switch (cmd) {
    case CMD_QUERY_STATUS: return "QUERY_STATUS";
    case CMD_REQUEST_START_CHARGE: return "REQUEST_START_CHARGE";
    case CMD_STOP_CHARGING: return "STOP_CHARGING";
    case CMD_CLEAR_FAULT: return "CLEAR_FAULT";
    case CMD_NONE:
    default: return "NONE";
  }
}
