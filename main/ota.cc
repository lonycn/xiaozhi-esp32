#include "ota.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_heap_caps.h>
#include <esp_flash.h>
#include <psa/crypto.h>
#include <cmath>
#include <array>
#include <atomic>
#include <new>
#include <deque>
#include <mutex>
#ifdef SOC_HMAC_SUPPORTED
#include <esp_hmac.h>
#endif

#include <algorithm>
#include <charconv>
#include <cstring>
#include <expected>
#include <system_error>
#include <vector>

#define TAG "Ota"
namespace {
std::atomic<bool> boot_resources_verified{false};
std::atomic<bool> boot_receipt_confirmed{false};
std::atomic<bool> healthy_receipt_confirmed{false};
#if CONFIG_XIAOZHI_RELEASE_ALLOW_LAN_HTTP
constexpr bool kAllowLanHttp = true;
#else
constexpr bool kAllowLanHttp = false;
#endif
}

Ota::Ota() {
#ifdef ESP_EFUSE_BLOCK_USR_DATA
    // USER_DATA may contain board metadata instead of a textual serial number.
    uint8_t serial_number[32] = {0};
    if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serial_number, 32 * 8) == ESP_OK) {
        const auto data_end = serial_number + sizeof(serial_number);
        const auto text_end = std::find(serial_number, data_end, uint8_t{0});
        const bool printable = std::all_of(
            serial_number, text_end, [](uint8_t byte) { return byte >= 0x20 && byte <= 0x7E; });
        const bool non_blank =
            std::any_of(serial_number, text_end, [](uint8_t byte) { return byte != ' '; });
        const bool zero_padding =
            std::all_of(text_end, data_end, [](uint8_t byte) { return byte == 0; });
        if (printable && non_blank && zero_padding) {
            serial_number_.assign(reinterpret_cast<const char*>(serial_number),
                                  text_end - serial_number);
            has_serial_number_ = true;
        } else if (std::any_of(serial_number, data_end, [](uint8_t byte) { return byte != 0; })) {
            ESP_LOGW(TAG, "Ignoring non-textual or blank eFuse serial number; using activation v1");
        }
    }
#endif
}

Ota::~Ota() {
}

std::string Ota::GetCheckVersionUrl() {
#if CONFIG_XIAOZHI_CONTROLLED_RELEASE
    // The release's explicitly injected root wins without overwriting the old
    // wifi.ota_url. A rolled-back legacy image can still read its original URL.
    return release_manifest::TrustedOtaRoot(CONFIG_OTA_URL, "", true, kAllowLanHttp);
#else
    Settings settings("wifi", false);
    return release_manifest::TrustedOtaRoot(CONFIG_OTA_URL, settings.GetString("ota_url"), false, false);
#endif
}

std::unique_ptr<Http> Ota::SetupHttp(int connection_id) {
    auto& board = Board::GetInstance();
    auto network = board.GetNetwork();
    auto http = network->CreateHttp(connection_id);
    auto user_agent = SystemInfo::GetUserAgent();
    http->SetTimeout(5000);
    http->SetHeader("Activation-Version", has_serial_number_ ? "2" : "1");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", board.GetUuid());
    if (has_serial_number_) {
        http->SetHeader("Serial-Number", serial_number_.c_str());
        ESP_LOGI(TAG, "Setup HTTP, User-Agent: %s, Serial-Number: %s", user_agent.c_str(), serial_number_.c_str());
    }
    http->SetHeader("User-Agent", user_agent);
    http->SetHeader("Accept-Language", Lang::CODE);
    http->SetHeader("Content-Type", "application/json");

    return http;
}

/* 
 * Specification: https://ccnphfhqs21z.feishu.cn/wiki/FjW6wZmisimNBBkov6OcmfvknVd
 */
NetworkResult<> Ota::CheckVersion() {
    auto& board = Board::GetInstance();
    auto app_desc = esp_app_get_description();

    // Check if there is a new firmware version available
    current_version_ = app_desc->version;
    ESP_LOGI(TAG, "Current version: %s", current_version_.c_str());

    std::string url = GetCheckVersionUrl();
    if (url.length() < 10) {
        ESP_LOGE(TAG, "Check version URL is not properly set");
        return std::unexpected(NetworkError::InvalidArgument());
    }

#if CONFIG_XIAOZHI_CONTROLLED_RELEASE
    if (!release_manifest::AllowedUrl(url, kAllowLanHttp)) return std::unexpected(NetworkError::InvalidArgument());
#endif
    auto http = SetupHttp();

    std::string data = board.GetSystemInfoJson();
    std::string method = data.length() > 0 ? "POST" : "GET";
    http->SetContent(std::move(data));

    if (auto opened = http->Open(method, url); !opened) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", opened.error().ToString().c_str());
        return opened;
    }

    auto status_code = http->GetStatusCode();
    if (!status_code) {
        ESP_LOGE(TAG, "Failed to read HTTP status: %s", status_code.error().ToString().c_str());
        return std::unexpected(status_code.error());
    }
    if (*status_code != 200) {
        ESP_LOGE(TAG, "Failed to check version, status code: %d", *status_code);
        return std::unexpected(NetworkError::HttpFailed(*status_code));
    }

    data.clear();
    std::array<char, 1024> response_chunk;
    while (true) {
        auto count = http->Read(response_chunk.data(), response_chunk.size());
        if (!count || *count < 0 || data.size() + size_t(*count) > 65536) {
            http->Close(); return std::unexpected(NetworkError::ProtocolError());
        }
        if (*count == 0) break;
        data.append(response_chunk.data(), *count);
    }
    http->Close();

    // Response: { "firmware": { "version": "1.0.0", "url": "http://" } }
    // Parse the JSON response and check if the version is newer
    // If it is, set has_new_version_ to true and store the new version and URL
    
    cJSON *root = cJSON_Parse(data.c_str());
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return std::unexpected(NetworkError::ProtocolError());
    }

    has_activation_code_ = false;
    has_activation_challenge_ = false;
    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation)) {
        cJSON* message = cJSON_GetObjectItem(activation, "message");
        if (cJSON_IsString(message)) {
            activation_message_ = message->valuestring;
        }
        cJSON* code = cJSON_GetObjectItem(activation, "code");
        if (cJSON_IsString(code)) {
            activation_code_ = code->valuestring;
            has_activation_code_ = true;
        }
        cJSON* challenge = cJSON_GetObjectItem(activation, "challenge");
        if (cJSON_IsString(challenge)) {
            activation_challenge_ = challenge->valuestring;
            has_activation_challenge_ = true;
        }
        cJSON* timeout_ms = cJSON_GetObjectItem(activation, "timeout_ms");
        if (cJSON_IsNumber(timeout_ms)) {
            activation_timeout_ms_ = timeout_ms->valueint;
        }
    }

    has_mqtt_config_ = false;
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        Settings settings("mqtt", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, mqtt) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_mqtt_config_ = true;
    } else {
        ESP_LOGI(TAG, "No mqtt section found !");
    }

    has_websocket_config_ = false;
    cJSON *websocket = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        Settings settings("websocket", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, websocket) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_websocket_config_ = true;
    } else {
        ESP_LOGI(TAG, "No websocket section found!");
    }

    has_server_time_ = false;
    cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
    if (cJSON_IsObject(server_time)) {
        cJSON *timestamp = cJSON_GetObjectItem(server_time, "timestamp");
        cJSON *timezone_offset = cJSON_GetObjectItem(server_time, "timezone_offset");
        
        if (cJSON_IsNumber(timestamp)) {
            // 设置系统时间
            struct timeval tv;
            double ts = timestamp->valuedouble;
            
            // 如果有时区偏移，计算本地时间
            if (cJSON_IsNumber(timezone_offset)) {
                ts += (timezone_offset->valueint * 60 * 1000); // 转换分钟为毫秒
            }
            
            tv.tv_sec = (time_t)(ts / 1000);  // 转换毫秒为秒
            tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;  // 剩余的毫秒转换为微秒
            settimeofday(&tv, NULL);
            has_server_time_ = true;
        }
    } else {
        ESP_LOGW(TAG, "No server_time section found!");
    }

    has_new_version_ = false;
    cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (cJSON_IsObject(firmware)) {
        const bool controlled = ValidateManifest(firmware);
#if CONFIG_XIAOZHI_REQUIRE_RELEASE_MANIFEST
        if (!controlled) {
            ESP_LOGW(TAG, "Release offer rejected: incompatible/unverified manifest or resources");
            cJSON_Delete(root); return {};
        }
#endif
        cJSON *version = cJSON_GetObjectItem(firmware, "version");
        if (cJSON_IsString(version)) {
            firmware_version_ = version->valuestring;
        }
        cJSON *url = cJSON_GetObjectItem(firmware, "url");
        if (cJSON_IsString(url)) {
            firmware_url_ = url->valuestring;
        }

        if (cJSON_IsString(version) && cJSON_IsString(url)) {
            // Check if the version is newer, for example, 0.1.0 is newer than 0.0.1
            has_new_version_ = IsNewVersionAvailable(current_version_, firmware_version_);
            Settings prior("release_ota", false);
            if (controlled && prior.GetInt("selected", 0) == 1 &&
                prior.GetString("release_id") == manifest_.release_id &&
                prior.GetString("version") != current_version_) {
                // The selected image did not survive boot confirmation. Do not
                // enter an automatic rollback/reinstall loop for the same release.
                has_new_version_ = false;
                QueueReceipt("rolled_back");
            }
            if (has_new_version_) {
                ESP_LOGI(TAG, "New version available: %s", firmware_version_.c_str());
            } else {
                ESP_LOGI(TAG, "Current is the latest version");
            }
            // If the force flag is set to 1, the given version is forced to be installed
            cJSON *force = cJSON_GetObjectItem(firmware, "force");
            if (!controlled && cJSON_IsNumber(force) && force->valueint == 1) {
                has_new_version_ = true;
            }
        }
    } else {
        ESP_LOGW(TAG, "No firmware section found!");
    }

    cJSON_Delete(root);
    return {};
}

void Ota::MarkCurrentVersionValid() {
    auto partition = esp_ota_get_running_partition();
    if (strcmp(partition->label, "factory") == 0) {
        ESP_LOGI(TAG, "Running from factory partition, skipping");
        return;
    }

    ESP_LOGI(TAG, "Running partition: %s", partition->label);
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get state of partition");
        return;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking firmware as valid");
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) QueueReceipt("healthy");
    } else if (state == ESP_OTA_IMG_VALID && !healthy_receipt_confirmed.load()) {
        Settings pending("release_ota", false);
        static unsigned retries = 0;
        if (retries < 12 && pending.GetInt("selected", 0) == 1 &&
            pending.GetString("version") == esp_app_get_description()->version) {
            ++retries; QueueReceipt("healthy");
        }
    }
}

namespace {
struct Sha256Stream {
    psa_hash_operation_t context = PSA_HASH_OPERATION_INIT;
    bool ready;
    Sha256Stream() : ready(psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&context, PSA_ALG_SHA_256) == PSA_SUCCESS) {}
    ~Sha256Stream() { psa_hash_abort(&context); }
    bool Add(const void* data, size_t size) { return ready && psa_hash_update(&context, static_cast<const uint8_t*>(data), size) == PSA_SUCCESS; }
    std::string Finish() {
        unsigned char digest[32]; size_t length = 0;
        if (!ready || psa_hash_finish(&context, digest, sizeof(digest), &length) != PSA_SUCCESS || length != 32) return {};
        constexpr char hex[] = "0123456789abcdef"; std::string result; result.reserve(64);
        for (auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; } return result;
    }
};
bool AssetsMatch(const ReleaseManifest& manifest) {
    const auto partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "assets");
    if (!partition || manifest.assets_size > partition->size) return false;
    Sha256Stream hash; std::array<uint8_t, 4096> buffer;
    for (size_t offset = 0; offset < manifest.assets_size; ) {
        auto count = std::min(buffer.size(), manifest.assets_size - offset);
        if (esp_partition_read(partition, offset, buffer.data(), count) != ESP_OK || !hash.Add(buffer.data(), count)) return false;
        offset += count;
    }
    return hash.Finish() == manifest.assets_sha256;
}
}

bool Ota::ValidateManifest(const cJSON* firmware) {
    manifest_ = {};
    auto text = [](const cJSON* object, const char* key) -> std::string {
        auto item = cJSON_GetObjectItemCaseSensitive(object, key);
        return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
    };
    auto number = [](const cJSON* object, const char* key, uint64_t maximum) -> uint64_t {
        auto item = cJSON_GetObjectItemCaseSensitive(object, key);
        if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble <= 0 ||
            item->valuedouble > double(maximum) || std::floor(item->valuedouble) != item->valuedouble) return 0;
        return uint64_t(item->valuedouble);
    };
    if (number(firmware, "schema_version", 1) != 1) return false;
    auto development = cJSON_GetObjectItemCaseSensitive(firmware, "development_only");
    if (!cJSON_IsBool(development)) return false;
    manifest_.development_only = cJSON_IsTrue(development);
    auto assets = cJSON_GetObjectItemCaseSensitive(firmware, "assets");
    if (!cJSON_IsObject(assets)) return false;
    manifest_.release_id = text(firmware, "release_id"); manifest_.version = text(firmware, "version");
    manifest_.url = text(firmware, "url"); manifest_.sha256 = text(firmware, "sha256");
    manifest_.board_type = text(firmware, "board_type"); manifest_.board_name = text(firmware, "board_name");
    manifest_.chip = text(firmware, "chip"); manifest_.update_policy = text(firmware, "update_policy");
    manifest_.size = number(firmware, "size", 32 * 1024 * 1024);
    manifest_.min_flash_size = number(firmware, "min_flash_size", 128 * 1024 * 1024);
    manifest_.assets_version = text(assets, "version"); manifest_.assets_sha256 = text(assets, "sha256");
    manifest_.assets_size = number(assets, "size", 32 * 1024 * 1024);
    auto required = cJSON_GetObjectItemCaseSensitive(assets, "required");
    if (!cJSON_IsBool(required)) return false;
    manifest_.assets_required = cJSON_IsTrue(required);
    manifest_.valid = !manifest_.release_id.empty() && manifest_.release_id.size() <= 64 &&
        manifest_.release_id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") == std::string::npos &&
        !manifest_.assets_version.empty() && !manifest_.version.empty();
    uint32_t flash = 0;
    if (esp_flash_get_size(nullptr, &flash) != ESP_OK ||
        !release_manifest::Compatible(manifest_, BOARD_TYPE, BOARD_NAME, CONFIG_IDF_TARGET, flash, kAllowLanHttp) ||
        !release_manifest::SameOrigin(manifest_.url, GetCheckVersionUrl()) || !AssetsMatch(manifest_)) {
        manifest_.valid = false; return false;
    }
    boot_resources_verified.store(true);
    return true;
}

bool Ota::BootReceiptConfirmed() {
#if CONFIG_XIAOZHI_REQUIRE_RELEASE_MANIFEST
    return boot_receipt_confirmed.load();
#else
    return true;
#endif
}

bool Ota::BootResourcesVerified() {
#if CONFIG_XIAOZHI_REQUIRE_RELEASE_MANIFEST
    return boot_resources_verified.load();
#else
    return true;
#endif
}

bool Ota::Upgrade(const std::string& url, std::function<void(int,size_t)> callback) {
#if CONFIG_XIAOZHI_REQUIRE_RELEASE_MANIFEST
    Ota offer;
    if (!offer.CheckVersion() || !offer.manifest_.valid ||
        !release_manifest::SameArtifactUrl(url, offer.firmware_url_, kAllowLanHttp)) return false;
    // Tickets expire and are refreshed on every offer. Pin the immutable release
    // path, then download with the newly authorized manifest and ticket.
    return UpgradeVerified(offer.firmware_url_, &offer.manifest_, callback);
#else
    return UpgradeVerified(url, nullptr, callback);
#endif
}

bool Ota::UpgradeVerified(const std::string& url, const ReleaseManifest* manifest, std::function<void(int,size_t)> callback) {
    const auto partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition || (manifest && (manifest->size > partition->size || !AssetsMatch(*manifest)))) return false;
#if CONFIG_XIAOZHI_CONTROLLED_RELEASE
    if (!release_manifest::AllowedUrl(url, kAllowLanHttp)) return false;
#endif
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    http->SetTimeout(10000);
    if (!http->Open("GET", url)) return false;
    auto status = http->GetStatusCode();
    if (!status || *status != 200) return false;
    const size_t length = http->GetBodyLength();
    if (length < 256 || length > partition->size || (manifest && length != manifest->size)) return false;
    // esp_ota_end validates the ESP image/chip; only choose a new boot slot after
    // the exact manifest length and streamed SHA256 have also been verified.
    if (manifest) {
        Settings pending("release_ota", true);
        pending.SetString("release_id", manifest->release_id);
        pending.SetString("version", manifest->version);
        pending.SetInt("selected", 0);
        Ota receipt;
        bool acknowledged = false;
        for (int attempt = 0; attempt < 2 && !acknowledged; ++attempt) {
            acknowledged = receipt.SendReceipt("downloading", manifest->release_id, manifest->version);
            if (!acknowledged) vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if (!acknowledged) return false;
    }
    // The activation/upgrade call chain is deep. Keep the bounded download
    // buffer off its task stack; 2.5.3 exhausted that stack before downloading.
    constexpr size_t kDownloadChunk = 4096;
    std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[kDownloadChunk]);
    if (!buffer) return false;
    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(partition, length, &handle) != ESP_OK) return false;
    Sha256Stream hash; size_t total = 0, recent = 0;
    auto last = esp_timer_get_time(); bool good = true;
    std::string image_header;
    bool descriptor_checked = manifest == nullptr;
    while (true) {
        auto read = http->Read(reinterpret_cast<char*>(buffer.get()), kDownloadChunk);
        if (!read || *read < 0) { good = false; break; }
        if (*read == 0) break;
        size_t count = *read;
        if (!descriptor_checked) {
            constexpr size_t descriptor_offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
            constexpr size_t header_size = descriptor_offset + sizeof(esp_app_desc_t);
            image_header.append(reinterpret_cast<const char*>(buffer.get()), std::min(count, header_size - image_header.size()));
            if (image_header.size() >= header_size) {
                esp_app_desc_t descriptor;
                memcpy(&descriptor, image_header.data() + descriptor_offset, sizeof(descriptor));
                if (strnlen(descriptor.version, sizeof(descriptor.version)) == sizeof(descriptor.version) ||
                    manifest->version != descriptor.version ||
                    strncmp(descriptor.project_name, esp_app_get_description()->project_name, sizeof(descriptor.project_name)) != 0) {
                    good = false; break;
                }
                descriptor_checked = true;
            }
        }
        if (count > length - total || !hash.Add(buffer.get(), count) || esp_ota_write(handle, buffer.get(), count) != ESP_OK) { good = false; break; }
        total += count; recent += count;
        if (esp_timer_get_time() - last >= 1000000) {
            if (callback) callback(int(total * 100 / length), recent);
            recent = 0; last = esp_timer_get_time();
        }
    }
    http->Close();
    if (!good || !descriptor_checked || total != length || (manifest && hash.Finish() != manifest->sha256)) { esp_ota_abort(handle); QueueReceipt("failed"); return false; }
    if (esp_ota_end(handle) != ESP_OK) return false;
    if (manifest) {
        Ota receipt; bool acknowledged = false;
        for (int attempt = 0; attempt < 2 && !acknowledged; ++attempt) {
            acknowledged = receipt.SendReceipt("verified", manifest->release_id, manifest->version);
            if (!acknowledged) vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if (!acknowledged) { QueueReceipt("failed"); return false; }
    }
    if (manifest) { Settings pending("release_ota", true); pending.SetInt("selected", 1); }
    if (esp_ota_set_boot_partition(partition) != ESP_OK) {
        if (manifest) { Settings pending("release_ota", true); pending.SetInt("selected", 0); }
        QueueReceipt("failed"); return false;
    }
    if (callback) callback(100, recent);
    return true;
}

bool Ota::StartUpgrade(std::function<void(int,size_t)> callback) {
    if (!manifest_.valid) return false;
    return UpgradeVerified(firmware_url_, &manifest_, callback);
}

void Ota::RollbackUnconfirmedBoot() {
    const auto partition = esp_ota_get_running_partition(); esp_ota_img_states_t state;
    if (!partition || esp_ota_get_state_partition(partition, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) return;
    static std::atomic<bool> running{false};
    if (running.exchange(true)) return;
    if (xTaskCreate([](void*) {
        Ota ota; Settings pending("release_ota", false);
        ota.SendReceipt("rolled_back", pending.GetString("release_id"), pending.GetString("version"));
        esp_ota_mark_app_invalid_rollback_and_reboot();
        running.store(false); vTaskDelete(nullptr);
    }, "rollback_release", 4096, nullptr, 1, nullptr) != pdPASS) running.store(false);
}

bool Ota::SendReceipt(const std::string& status, const std::string& release, const std::string& version) {
    if (release.empty() || release.size() > 64 || version.empty()) return false;
    auto base = GetCheckVersionUrl();
    if (!release_manifest::AllowedUrl(base, kAllowLanHttp)) return false;
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.size() < 4 || base.substr(base.size() - 4) != "/ota") return false;
    Settings websocket("websocket", false); auto token = websocket.GetString("token");
    if (token.empty()) return false;
    if (token.find(' ') == std::string::npos) token = "Bearer " + token;
    auto http = SetupHttp(2); http->SetHeader("Authorization", token);
    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "release_id", release.c_str());
    const auto key = release + "." + status;
    cJSON_AddStringToObject(body, "event_key", key.c_str());
    cJSON_AddStringToObject(body, "status", status.c_str());
    cJSON_AddStringToObject(body, "version", version.c_str());
    char* encoded = cJSON_PrintUnformatted(body); std::string content(encoded ? encoded : "{}");
    cJSON_free(encoded); cJSON_Delete(body); http->SetContent(std::move(content));
    if (!http->Open("POST", base + "/esp32/receipts")) return false;
    auto code = http->GetStatusCode();
    if (!code || (*code != 200 && *code != 204)) { http->Close(); return false; }
    if (*code == 204) { http->Close(); return true; }
    std::string response; std::array<char, 512> buffer;
    while (true) {
        auto count = http->Read(buffer.data(), buffer.size());
        if (!count || *count < 0 || response.size() + size_t(*count) > 2048) { http->Close(); return false; }
        if (*count == 0) break;
        response.append(buffer.data(), *count);
    }
    http->Close(); auto acknowledgement = cJSON_Parse(response.c_str());
    if (!acknowledgement) return false;
    auto result = cJSON_GetObjectItemCaseSensitive(acknowledgement, "code");
    const bool accepted = cJSON_IsNumber(result) && result->valuedouble == 0;
    cJSON_Delete(acknowledgement); return accepted;
}

void Ota::QueueBootReceipt() {
    Settings pending("release_ota", false);
    if (pending.GetInt("selected", 0) != 1) return;
    static unsigned attempts = 0;
    if (attempts++ < 12) QueueReceipt(pending.GetString("version") == esp_app_get_description()->version ? "booted" : "rolled_back");
}

void Ota::QueueReceipt(const std::string& status) {
    struct Receipt { std::string status, release, version; };
    struct Pending { std::mutex mutex; std::deque<Receipt> items; std::string active; bool running = false; };
    static Pending queue;
    if (status != "downloading" && status != "verified" && status != "booted" && status != "healthy" && status != "failed" && status != "rolled_back") return;
    Settings stored("release_ota", false);
    Receipt item{status, stored.GetString("release_id"), stored.GetString("version")};
    if (item.release.empty() || item.version.empty()) return;
    {
        std::lock_guard<std::mutex> lock(queue.mutex);
        if (queue.active == item.release + "." + item.status) return;
        for (const auto& previous : queue.items) if (previous.release == item.release && previous.status == item.status) return;
        if (queue.items.size() >= 8) return;
        queue.items.push_back(std::move(item));
        if (queue.running) return;
        queue.running = true;
    }
    if (xTaskCreate([](void* raw) {
        auto state = static_cast<Pending*>(raw); auto ota = std::make_unique<Ota>();
        while (true) {
            Receipt current;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->items.empty()) { state->running = false; break; }
                current = std::move(state->items.front()); state->items.pop_front();
                state->active = current.release + "." + current.status;
            }
            // Same immutable identity/key after an ACK loss. Retries are bounded.
            for (int attempt = 0; attempt < 2; ++attempt) {
                if (ota->SendReceipt(current.status, current.release, current.version)) {
                    if (current.status == "booted") boot_receipt_confirmed.store(true);
                    if (current.status == "healthy") healthy_receipt_confirmed.store(true);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            { std::lock_guard<std::mutex> lock(state->mutex); state->active.clear(); }
        }
        ota.reset();
        vTaskDelete(nullptr);
    }, "release_receipt", 4096, &queue, 1, nullptr) != pdPASS) {
        std::lock_guard<std::mutex> lock(queue.mutex); queue.running = false;
    }
}

// Versions are compared as dot-separated decimal numbers. Parsing must never throw: exceptions are
// disabled in this project, so a malformed string from the server would otherwise terminate the
// firmware instead of being reported as an error.
std::expected<std::vector<int>, std::string> Ota::ParseVersion(const std::string& version) {
    std::vector<int> version_numbers;
    size_t start = 0;

    while (true) {
        size_t end = version.find('.', start);
        std::string segment =
            version.substr(start, end == std::string::npos ? std::string::npos : end - start);

        int value = 0;
        auto [ptr, ec] = std::from_chars(segment.data(), segment.data() + segment.size(), value);
        if (segment.empty() || ec != std::errc() || ptr != segment.data() + segment.size()) {
            return std::unexpected("invalid version segment \"" + segment + "\"");
        }
        version_numbers.push_back(value);

        if (end == std::string::npos) {
            return version_numbers;
        }
        start = end + 1;
    }
}

bool Ota::IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion) {
    auto current = ParseVersion(currentVersion);
    if (!current) {
        ESP_LOGW(TAG, "Cannot parse current version \"%s\": %s", currentVersion.c_str(),
                 current.error().c_str());
        return false;
    }

    auto newer = ParseVersion(newVersion);
    if (!newer) {
        // Never report an update we cannot compare, otherwise a malformed version string would
        // trigger a firmware downgrade or an endless upgrade loop.
        ESP_LOGW(TAG, "Ignoring firmware version \"%s\": %s", newVersion.c_str(),
                 newer.error().c_str());
        return false;
    }

    for (size_t i = 0; i < std::min(current->size(), newer->size()); ++i) {
        if ((*newer)[i] > (*current)[i]) {
            return true;
        } else if ((*newer)[i] < (*current)[i]) {
            return false;
        }
    }

    return newer->size() > current->size();
}

std::string Ota::GetActivationPayload() {
    if (!has_serial_number_) {
        return "{}";
    }

    std::string hmac_hex;
#ifdef SOC_HMAC_SUPPORTED
    uint8_t hmac_result[32]; // SHA-256 输出为32字节
    
    // 使用Key0计算HMAC
    esp_err_t ret = esp_hmac_calculate(HMAC_KEY0, (uint8_t*)activation_challenge_.data(), activation_challenge_.size(), hmac_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HMAC calculation failed: %s", esp_err_to_name(ret));
        return "{}";
    }

    for (size_t i = 0; i < sizeof(hmac_result); i++) {
        char buffer[3];
        sprintf(buffer, "%02x", hmac_result[i]);
        hmac_hex += buffer;
    }
#endif

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "algorithm", "hmac-sha256");
    cJSON_AddStringToObject(payload, "serial_number", serial_number_.c_str());
    cJSON_AddStringToObject(payload, "challenge", activation_challenge_.c_str());
    cJSON_AddStringToObject(payload, "hmac", hmac_hex.c_str());
    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(payload);

    ESP_LOGI(TAG, "Activation payload prepared");
    return json;
}

esp_err_t Ota::Activate() {
    if (!has_activation_challenge_) {
        ESP_LOGW(TAG, "No activation challenge found");
        return ESP_FAIL;
    }

    std::string url = GetCheckVersionUrl();
    if (url.back() != '/') {
        url += "/activate";
    } else {
        url += "activate";
    }

    auto http = SetupHttp();

    std::string data = GetActivationPayload();
    http->SetContent(std::move(data));

    if (auto opened = http->Open("POST", url); !opened) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", opened.error().ToString().c_str());
        return ESP_FAIL;
    }

    auto status_code = http->GetStatusCode();
    if (!status_code) {
        ESP_LOGE(TAG, "Failed to read HTTP status: %s", status_code.error().ToString().c_str());
        return ESP_FAIL;
    }
    if (*status_code == 202) {
        return ESP_ERR_TIMEOUT;
    }
    if (*status_code != 200) {
        ESP_LOGE(TAG, "Failed to activate, code: %d, body: %s", *status_code, http->ReadAll().c_str());
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Activation successful");
    return ESP_OK;
}
