#ifndef WIFI_DOCK_MODULE_H
#define WIFI_DOCK_MODULE_H

#include <Arduino.h>
#include <WiFi.h>

class WiFiDockModule {
public:
  enum RemoteCommand {
    CMD_NONE = 0,
    CMD_QUERY_STATUS,
    CMD_REQUEST_START_CHARGE,
    CMD_STOP_CHARGING,
    CMD_CLEAR_FAULT
  };

  WiFiDockModule(const char* ssid, const char* password, const char* serverBaseUrl);

  // These methods only start/check the asynchronous Wi-Fi connection. They
  // never wait for association or DHCP in the safety loop.
  void begin();
  void update();
  void reconnectNow();

  // HTTP is executed by a dedicated FreeRTOS task. uploadStatus() queues the
  // newest snapshot; pollCommand() returns a completed command, if any, and
  // schedules future polls without doing network I/O in the caller.
  bool uploadStatus(const String& statusJson);
  RemoteCommand pollCommand();

  bool isConfigured() const;
  bool isConnected() const;
  String ipAddress() const;
  String macAddress() const;
  int lastHttpCode() const;
  String lastError() const;

  static const char* commandToString(RemoteCommand cmd);

private:
  const char* _ssid;
  const char* _password;
  const char* _serverBaseUrl;

  bool _configured;
  unsigned long _lastReconnectAttemptMs;
  bool _wasConnected;

  mutable SemaphoreHandle_t _networkMutex;
  TaskHandle_t _networkTaskHandle;
  bool _uploadPending;
  bool _pollPending;
  String _pendingStatusJson;
  RemoteCommand _receivedCommand;
  bool _stopPending;
  unsigned long _lastPollQueuedMs;
  int _lastHttpCode;
  String _lastError;

  static const unsigned long WIFI_RECONNECT_INTERVAL_MS = 10000;
  static const unsigned long COMMAND_POLL_INTERVAL_MS = 1500;
  static const unsigned long HTTP_TIMEOUT_MS = 500;

  bool hasValidConfig() const;
  void startConnection();
  void ensureNetworkTask();
  static void networkTaskEntry(void* context);
  void networkTask();
  void performStatusUpload(const String& statusJson);
  void performCommandPoll();
  void setNetworkResult(int httpCode, const char* error);
  void setLastError(const char* error);
  String makeUrl(const char* path) const;
  RemoteCommand parseCommandPayload(const String& payload) const;
  static String extractCommandValue(const String& payload);
};

#endif
