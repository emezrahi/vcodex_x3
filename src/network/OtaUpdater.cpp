#include "OtaUpdater.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <cstring>

#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_wifi.h"

namespace {
#ifndef OTA_MANIFEST_URL
#define OTA_MANIFEST_URL "https://raw.githubusercontent.com/franssjz/cpr-vcodex/main/ota.json"
#endif

constexpr char otaManifestUrl[] = OTA_MANIFEST_URL;

/* This is buffer and size holder to keep upcoming data from latestReleaseUrl */
char* local_buf;
int output_len;

struct ParsedVersion {
  int segments[4] = {0, 0, 0, 0};
  uint8_t count = 0;
  bool valid = false;
  bool isRc = false;
};

ParsedVersion parseVersion(const char* version) {
  ParsedVersion parsed;
  if (version == nullptr || *version == '\0') {
    return parsed;
  }

  int currentValue = -1;
  for (const char* cursor = version; *cursor != '\0'; ++cursor) {
    const char ch = *cursor;
    if (ch >= '0' && ch <= '9') {
      currentValue = (currentValue < 0) ? (ch - '0') : (currentValue * 10 + (ch - '0'));
      continue;
    }

    if (ch == '-' && cursor[1] == 'r' && cursor[2] == 'c') {
      parsed.isRc = true;
    }

    if (currentValue >= 0) {
      if (parsed.count < 4) {
        parsed.segments[parsed.count++] = currentValue;
      }
      currentValue = -1;
    }
  }

  if (currentValue >= 0 && parsed.count < 4) {
    parsed.segments[parsed.count++] = currentValue;
  }

  parsed.valid = parsed.count > 0;
  return parsed;
}

int compareVersions(const ParsedVersion& left, const ParsedVersion& right) {
  const uint8_t segmentCount = (left.count > right.count) ? left.count : right.count;
  for (uint8_t index = 0; index < segmentCount; ++index) {
    const int leftSegment = (index < left.count) ? left.segments[index] : 0;
    const int rightSegment = (index < right.count) ? right.segments[index] : 0;
    if (leftSegment != rightSegment) {
      return (leftSegment > rightSegment) ? 1 : -1;
    }
  }

  if (left.isRc != right.isRc) {
    return left.isRc ? -1 : 1;
  }

  return 0;
}

/*
 * When esp_crt_bundle.h included, it is pointing wrong header file
 * which is something under WifiClientSecure because of our framework based on arduno platform.
 * To manage this obstacle, don't include anything, just extern and it will point correct one.
 */
extern "C" {
extern esp_err_t esp_crt_bundle_attach(void* conf);
}

esp_err_t http_client_set_header_cb(esp_http_client_handle_t http_client) {
  return esp_http_client_set_header(http_client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
}

esp_err_t event_handler(esp_http_client_event_t* event) {
  /* We do interested in only HTTP_EVENT_ON_DATA event only */
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;

  if (!esp_http_client_is_chunked_response(event->client)) {
    int content_len = esp_http_client_get_content_length(event->client);
    int copy_len = 0;

    if (local_buf == NULL) {
      /* local_buf life span is tracked by caller checkForUpdate */
      local_buf = static_cast<char*>(calloc(content_len + 1, sizeof(char)));
      output_len = 0;
      if (local_buf == NULL) {
        LOG_ERR("OTA", "HTTP Client Out of Memory Failed, Allocation %d", content_len);
        return ESP_ERR_NO_MEM;
      }
    }
    copy_len = min(event->data_len, (content_len - output_len));
    if (copy_len) {
      memcpy(local_buf + output_len, event->data, copy_len);
    }
    output_len += copy_len;
  } else {
    /* Code might be hits here, It happened once (for version checking) but I need more logs to handle that */
    int chunked_len;
    esp_http_client_get_chunk_length(event->client, &chunked_len);
    LOG_DBG("OTA", "esp_http_client_is_chunked_response failed, chunked_len: %d", chunked_len);
  }

  return ESP_OK;
} /* event_handler */

OtaUpdater::OtaUpdaterError performJsonRequest(const char* url, std::string& lastErrorMessage, const char* label) {
  local_buf = NULL;
  output_len = 0;

  esp_http_client_config_t client_config = {};
  client_config.url = url;
  client_config.event_handler = event_handler;
  client_config.timeout_ms = 15000;
  /* Default HTTP client buffer size 512 byte only */
  client_config.buffer_size = 8192;
  client_config.buffer_size_tx = 8192;
  client_config.skip_cert_common_name_check = true;
  client_config.crt_bundle_attach = esp_crt_bundle_attach;
  client_config.keep_alive_enable = false;
  client_config.addr_type = HTTP_ADDR_TYPE_INET;

  struct localBufCleaner {
    char** bufPtr;
    ~localBufCleaner() {
      if (*bufPtr) {
        free(*bufPtr);
        *bufPtr = NULL;
      }
    }
  } localBufCleaner = {&local_buf};

  esp_http_client_handle_t client_handle = esp_http_client_init(&client_config);
  if (!client_handle) {
    LOG_ERR("OTA", "HTTP Client Handle Failed");
    lastErrorMessage = std::string("Could not create ") + label + " client";
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }

  esp_err_t esp_err = esp_http_client_set_header(client_handle, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_set_header Failed : %s", esp_err_to_name(esp_err));
    lastErrorMessage = std::string(label) + " header error: " + esp_err_to_name(esp_err);
    esp_http_client_cleanup(client_handle);
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_http_client_perform(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_perform Failed : %s", esp_err_to_name(esp_err));
    if (esp_err == ESP_ERR_HTTP_CONNECT) {
      lastErrorMessage = std::string("Could not reach ") + label;
    } else if (esp_err == ESP_ERR_HTTP_FETCH_HEADER) {
      lastErrorMessage = std::string(label) + " headers failed";
    } else if (esp_err == ESP_ERR_HTTP_EAGAIN) {
      lastErrorMessage = std::string(label) + " timed out";
    } else {
      lastErrorMessage = std::string(label) + " request failed: " + esp_err_to_name(esp_err);
    }
    esp_http_client_cleanup(client_handle);
    return OtaUpdater::HTTP_ERROR;
  }

  const int statusCode = esp_http_client_get_status_code(client_handle);
  esp_err = esp_http_client_cleanup(client_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_http_client_cleanup Failed : %s", esp_err_to_name(esp_err));
    lastErrorMessage = std::string(label) + " cleanup error: " + esp_err_to_name(esp_err);
    return OtaUpdater::INTERNAL_UPDATE_ERROR;
  }

  if (statusCode < 200 || statusCode >= 300) {
    LOG_ERR("OTA", "%s returned HTTP %d", label, statusCode);
    lastErrorMessage = std::string(label) + " returned HTTP " + std::to_string(statusCode);
    return OtaUpdater::HTTP_ERROR;
  }

  return OtaUpdater::OK;
}
} /* namespace */

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  JsonDocument filter;
  JsonDocument doc;
  lastErrorMessage.clear();
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = 0;
  processedSize = 0;
  totalSize = 0;

  const auto requestResult = performJsonRequest(otaManifestUrl, lastErrorMessage, "OTA manifest");
  if (requestResult != OK) {
    return requestResult;
  }

  filter["version"] = true;
  filter["firmware_url"] = true;
  filter["size"] = true;
  const DeserializationError error = deserializeJson(doc, local_buf, DeserializationOption::Filter(filter));
  if (error) {
    LOG_ERR("OTA", "JSON parse failed: %s", error.c_str());
    lastErrorMessage = std::string("OTA manifest parse failed: ") + error.c_str();
    return JSON_PARSE_ERROR;
  }

  if (!doc["version"].is<std::string>()) {
    LOG_ERR("OTA", "No version found in manifest");
    lastErrorMessage = "OTA manifest version missing";
    return JSON_PARSE_ERROR;
  }

  if (!doc["firmware_url"].is<std::string>()) {
    LOG_ERR("OTA", "No firmware_url found in manifest");
    lastErrorMessage = "OTA firmware URL missing";
    return JSON_PARSE_ERROR;
  }

  if (!doc["size"].is<size_t>() && !doc["size"].is<uint32_t>() && !doc["size"].is<int>()) {
    LOG_ERR("OTA", "No size found in manifest");
    lastErrorMessage = "OTA firmware size missing";
    return JSON_PARSE_ERROR;
  }

  latestVersion = doc["version"].as<std::string>();
  otaUrl = doc["firmware_url"].as<std::string>();
  otaSize = doc["size"].as<size_t>();
  totalSize = otaSize;
  updateAvailable = !latestVersion.empty() && !otaUrl.empty() && otaSize > 0;

  if (!updateAvailable) {
    lastErrorMessage = "OTA manifest incomplete";
    return JSON_PARSE_ERROR;
  }

  LOG_DBG("OTA", "Found update: %s", latestVersion.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  const ParsedVersion latest = parseVersion(latestVersion.c_str());
  const ParsedVersion current = parseVersion(CROSSPOINT_VERSION);
  if (!latest.valid || !current.valid) {
    return latestVersion != CROSSPOINT_VERSION;
  }
  return compareVersions(latest, current) > 0;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate() {
  if (!isUpdateNewer()) {
    lastErrorMessage = "Release is not newer";
    return UPDATE_OLDER_ERROR;
  }

  esp_https_ota_handle_t ota_handle = NULL;
  esp_err_t esp_err;
  /* Signal for OtaUpdateActivity */
  render = false;

  esp_http_client_config_t client_config = {};
  client_config.url = otaUrl.c_str();
  client_config.timeout_ms = 15000;
  /* Default HTTP client buffer size 512 byte only
   * not sufficent to handle URL redirection cases or
   * parsing of large HTTP headers.
   */
  client_config.buffer_size = 8192;
  client_config.buffer_size_tx = 8192;
  client_config.skip_cert_common_name_check = true;
  client_config.crt_bundle_attach = esp_crt_bundle_attach;
  client_config.keep_alive_enable = false;
  client_config.addr_type = HTTP_ADDR_TYPE_INET;

  esp_https_ota_config_t ota_config = {
      .http_config = &client_config,
      .http_client_init_cb = http_client_set_header_cb,
  };

  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);

  esp_err = esp_https_ota_begin(&ota_config, &ota_handle);
  if (esp_err != ESP_OK) {
    LOG_DBG("OTA", "HTTP OTA Begin Failed: %s", esp_err_to_name(esp_err));
    if (esp_err == ESP_ERR_HTTP_CONNECT) {
      lastErrorMessage = "Could not reach OTA download URL";
    } else {
      lastErrorMessage = std::string("OTA begin failed: ") + esp_err_to_name(esp_err);
    }
    return INTERNAL_UPDATE_ERROR;
  }

  do {
    esp_err = esp_https_ota_perform(ota_handle);
    processedSize = esp_https_ota_get_image_len_read(ota_handle);
    /* Sent signal to  OtaUpdateActivity */
    render = true;
    delay(100);  // TODO: should we replace this with something better?
  } while (esp_err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

  /* Return back to default power saving for WiFi in case of failing */
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_https_ota_perform Failed: %s", esp_err_to_name(esp_err));
    if (esp_err == ESP_ERR_HTTP_CONNECT) {
      lastErrorMessage = "OTA download connection failed";
    } else if (esp_err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      lastErrorMessage = "OTA still in progress";
    } else {
      lastErrorMessage = std::string("OTA download failed: ") + esp_err_to_name(esp_err);
    }
    esp_https_ota_finish(ota_handle);
    return HTTP_ERROR;
  }

  if (!esp_https_ota_is_complete_data_received(ota_handle)) {
    LOG_ERR("OTA", "esp_https_ota_is_complete_data_received Failed: %s", esp_err_to_name(esp_err));
    lastErrorMessage = "OTA data incomplete";
    esp_https_ota_finish(ota_handle);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_https_ota_finish(ota_handle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_https_ota_finish Failed: %s", esp_err_to_name(esp_err));
    lastErrorMessage = std::string("OTA finish failed: ") + esp_err_to_name(esp_err);
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  lastErrorMessage.clear();
  return OK;
}
