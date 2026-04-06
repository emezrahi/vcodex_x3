#include "OtaUpdater.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Logging.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <esp_wifi.h>

#include <memory>
#include <string>

#include <HalStorage.h>
#include "network/HttpDownloader.h"
#include "util/UrlUtils.h"

namespace {
#ifndef OTA_RELEASE_REPO
#define OTA_RELEASE_REPO "franssjz/cpr-vcodex"
#endif

constexpr char latestReleaseUrl[] = "https://api.github.com/repos/" OTA_RELEASE_REPO "/releases/latest";
constexpr uint16_t kHttpTimeoutMs = 15000;
constexpr char kOtaTempDir[] = "/.crosspoint";
constexpr char kOtaTempFile[] = "/.crosspoint/ota_firmware.bin";

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

std::string formatHttpFailure(const int httpCode, const char* prefix) {
  std::string message = prefix;
  if (httpCode >= 0) {
    message += "HTTP ";
    message += std::to_string(httpCode);
  } else {
    const String errorText = HTTPClient::errorToString(httpCode);
    message += errorText.c_str();
  }
  return message;
}

bool beginHttpRequest(HTTPClient& http, std::unique_ptr<NetworkClientSecure>& secureClient, NetworkClient& plainClient,
                      const std::string& url, const followRedirects_t redirectMode) {
  http.setFollowRedirects(redirectMode);
  http.setConnectTimeout(kHttpTimeoutMs);
  http.setTimeout(kHttpTimeoutMs);
  http.setReuse(false);

  if (UrlUtils::isHttpsUrl(url)) {
    secureClient.reset(new NetworkClientSecure);
    if (!secureClient) {
      return false;
    }
    secureClient->setInsecure();
    secureClient->setTimeout((kHttpTimeoutMs / 1000U) + 1U);
    return http.begin(*secureClient, url.c_str());
  }

  return http.begin(plainClient, url.c_str());
}

std::string resolveDownloadUrl(const std::string& url) {
  HTTPClient http;
  std::unique_ptr<NetworkClientSecure> secureClient;
  NetworkClient plainClient;
  if (!beginHttpRequest(http, secureClient, plainClient, url, HTTPC_DISABLE_FOLLOW_REDIRECTS)) {
    return url;
  }

  const char* headerKeys[] = {"Location"};
  http.collectHeaders(headerKeys, 1);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  const int httpCode = http.sendRequest("HEAD");

  std::string resolved = url;
  if ((httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND || httpCode == HTTP_CODE_SEE_OTHER ||
       httpCode == HTTP_CODE_TEMPORARY_REDIRECT || httpCode == 308) &&
      http.hasHeader("Location")) {
    const String location = http.header("Location");
    if (location.length() > 0) {
      resolved = location.c_str();
    }
  }

  http.end();
  return resolved;
}

}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  JsonDocument filter;
  JsonDocument doc;

  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  lastErrorMessage.clear();
  otaSize = 0;
  processedSize = 0;
  totalSize = 0;

  HTTPClient http;
  std::unique_ptr<NetworkClientSecure> secureClient;
  NetworkClient plainClient;
  if (!beginHttpRequest(http, secureClient, plainClient, latestReleaseUrl, HTTPC_STRICT_FOLLOW_REDIRECTS)) {
    lastErrorMessage = "Release client open failed";
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    return INTERNAL_UPDATE_ERROR;
  }

  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    lastErrorMessage = formatHttpFailure(httpCode, "Release request failed: ");
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    http.end();
    return HTTP_ERROR;
  }

  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  filter["assets"][0]["size"] = true;
  NetworkClient* responseStream = http.getStreamPtr();
  if (responseStream == nullptr) {
    lastErrorMessage = "Release stream unavailable";
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    http.end();
    return INTERNAL_UPDATE_ERROR;
  }

  const int responseLength = http.getSize();
  const DeserializationError error = deserializeJson(doc, *responseStream, DeserializationOption::Filter(filter));
  http.end();

  if (error) {
    if (error == DeserializationError::EmptyInput) {
      if (responseLength == 0) {
        lastErrorMessage = "Release response empty";
      } else {
        lastErrorMessage = "Release stream empty";
      }
    } else {
      lastErrorMessage = std::string("Release parse failed: ") + error.c_str();
    }
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    return JSON_PARSE_ERROR;
  }

  if (!doc["tag_name"].is<std::string>()) {
    lastErrorMessage = "Release tag missing";
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    return JSON_PARSE_ERROR;
  }

  if (!doc["assets"].is<JsonArray>()) {
    lastErrorMessage = "Release assets missing";
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    return JSON_PARSE_ERROR;
  }

  latestVersion = doc["tag_name"].as<std::string>();

  bool fallbackBinFound = false;
  std::string fallbackUrl;
  size_t fallbackSize = 0;
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    if (!asset["name"].is<std::string>() || !asset["browser_download_url"].is<std::string>() ||
        (!asset["size"].is<size_t>() && !asset["size"].is<int>() && !asset["size"].is<uint32_t>())) {
      continue;
    }

    const std::string assetName = asset["name"].as<std::string>();
    const std::string assetUrl = asset["browser_download_url"].as<std::string>();
    const size_t assetSize = asset["size"].as<size_t>();

    if (assetName == "firmware.bin") {
      otaUrl = assetUrl;
      otaSize = assetSize;
      updateAvailable = true;
      break;
    }

    if (!fallbackBinFound && assetName.size() > 4 && assetName.substr(assetName.size() - 4) == ".bin") {
      fallbackUrl = assetUrl;
      fallbackSize = assetSize;
      fallbackBinFound = true;
    }
  }

  if (!updateAvailable && fallbackBinFound) {
    otaUrl = fallbackUrl;
    otaSize = fallbackSize;
    updateAvailable = true;
  }

  if (!updateAvailable) {
    lastErrorMessage = "No .bin asset found";
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    return ASSET_NOT_FOUND_ERROR;
  }

  totalSize = otaSize;
  otaUrl = resolveDownloadUrl(otaUrl);
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

  render = false;
  processedSize = 0;

  HTTPClient http;
  std::unique_ptr<NetworkClientSecure> secureClient;
  NetworkClient plainClient;
  if (!beginHttpRequest(http, secureClient, plainClient, otaUrl, HTTPC_STRICT_FOLLOW_REDIRECTS)) {
    lastErrorMessage = "OTA client open failed";
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    return INTERNAL_UPDATE_ERROR;
  }

  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

  esp_wifi_set_ps(WIFI_PS_NONE);

  http.end();

  Storage.mkdir(kOtaTempDir);
  if (Storage.exists(kOtaTempFile)) {
    Storage.remove(kOtaTempFile);
  }

  totalSize = otaSize;
  processedSize = 0;
  render = true;
  const auto downloadResult =
      HttpDownloader::downloadToFile(otaUrl, kOtaTempFile, [this](const size_t downloaded, const size_t total) {
        processedSize = downloaded;
        if (total > 0) {
          totalSize = total;
        }
        render = true;
      });

  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (downloadResult != HttpDownloader::OK) {
    if (downloadResult == HttpDownloader::HTTP_ERROR) {
      lastErrorMessage = "OTA download failed";
    } else if (downloadResult == HttpDownloader::FILE_ERROR) {
      lastErrorMessage = "OTA temp file failed";
    } else {
      lastErrorMessage = "OTA download aborted";
    }
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    Storage.remove(kOtaTempFile);
    return (downloadResult == HttpDownloader::HTTP_ERROR) ? HTTP_ERROR : INTERNAL_UPDATE_ERROR;
  }

  HalFile firmwareFile;
  if (!Storage.openFileForRead("OTA", kOtaTempFile, firmwareFile)) {
    lastErrorMessage = "OTA temp file unavailable";
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    Storage.remove(kOtaTempFile);
    return INTERNAL_UPDATE_ERROR;
  }

  const size_t firmwareSize = firmwareFile.size();
  if (!Update.begin(firmwareSize > 0 ? firmwareSize : UPDATE_SIZE_UNKNOWN)) {
    lastErrorMessage = std::string("OTA begin failed: ") + Update.errorString();
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    firmwareFile.close();
    Storage.remove(kOtaTempFile);
    return INTERNAL_UPDATE_ERROR;
  }

  uint8_t buffer[1024];
  size_t writtenTotal = 0;
  while (writtenTotal < firmwareSize) {
    const size_t remaining = firmwareSize - writtenTotal;
    const size_t toRead = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    const int bytesRead = firmwareFile.read(buffer, toRead);
    if (bytesRead <= 0) {
      break;
    }

    const size_t bytesWritten = Update.write(buffer, static_cast<size_t>(bytesRead));
    if (bytesWritten != static_cast<size_t>(bytesRead)) {
      Update.abort();
      firmwareFile.close();
      Storage.remove(kOtaTempFile);
      lastErrorMessage = std::string("OTA write failed: ") + Update.errorString();
      LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
      return INTERNAL_UPDATE_ERROR;
    }

    writtenTotal += bytesWritten;
  }

  firmwareFile.close();
  Storage.remove(kOtaTempFile);

  if (writtenTotal == 0) {
    Update.abort();
    lastErrorMessage = "OTA local file empty";
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    return INTERNAL_UPDATE_ERROR;
  }

  if (writtenTotal != firmwareSize) {
    Update.abort();
    lastErrorMessage = "OTA local apply incomplete";
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    return INTERNAL_UPDATE_ERROR;
  }

  if (!Update.end()) {
    lastErrorMessage = std::string("OTA finish failed: ") + Update.errorString();
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    return INTERNAL_UPDATE_ERROR;
  }

  if (!Update.isFinished()) {
    lastErrorMessage = "OTA image not finished";
    LOG_ERR("OTA", "%s", lastErrorMessage.c_str());
    return INTERNAL_UPDATE_ERROR;
  }

  lastErrorMessage.clear();
  LOG_INF("OTA", "Update completed");
  return OK;
}
