/**
 * @file production_data_report.cc
 * @brief ProductionDataReport 实现: HTTP 拉配置 + Modbus 分块读取 + JSON 组包上报.
 * @note 字段目录逐条对应《设备端接入指南》附录表格, 数量与顺序见
 *       production_data_report_fields.inc; 地址一律为「保持寄存器地址」(即表格「地址」列),
 *       不再加 1。
 */
#include "production_data_report.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_timer.h>

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <ctime>
#include <memory>

#include "board.h"
#include "production_modbus.h"
#include "system_info.h"

namespace {
constexpr char kTag[] = "DataReport";
constexpr char kBaseUrl[] = "http://120.53.1.196:8002";
constexpr char kConfigPath[] = "/xiaozhi/data-report-config/current";
constexpr char kUploadPath[] = "/xiaozhi/data-analysis/upload";

// 局域网内可能无法访问公网 NTP, 因此提供两个服务器并允许多次重试.
constexpr char kPrimaryNtpServer[] = "ntp.aliyun.com";
constexpr char kSecondaryNtpServer[] = "pool.ntp.org";
constexpr uint32_t kSntpSyncIntervalSeconds = 60 * 60 * 24;

// 设备无 GPS/定位能力, reportLocation=1 时按协议约定的经纬度原点上送.
constexpr double kReportLocationLongitude = 0.0;
constexpr double kReportLocationLatitude = 0.0;

// 字段取值类型: 32 位字段一律「低字在前、高字在后」, 与 MCP 工具读料塔重量的字序一致.
enum ValueKind : uint8_t {
    kU16 = 0,  // 16 位无符号数
    kS16,      // 16 位有符号数
    kU32,      // 32 位无符号数
    kS32,      // 32 位有符号数
    kUFixed,   // 16 位无符号数 × scale, 以浮点上送
    kBit,      // 取 1 个位(0/1), 用于 0xA6 模块状态拆位
};

constexpr uint8_t kNoBit = 0xFF;

#define PRODUCTION_REPORT_FIELD(key_name, seq, addr, words_val, kind_val) \
    {key_name, (addr), (words_val), (kind_val), kNoBit, 1.0f},
#define PRODUCTION_REPORT_BIT_FIELD(key_name, seq, addr, bit_val) \
    {key_name, (addr), 1, kBit, (bit_val), 1.0f},
#define PRODUCTION_REPORT_FIXED_FIELD(key_name, seq, addr, scale_val) \
    {key_name, (addr), 1, kUFixed, kNoBit, (scale_val)},

const ProductionDataReport::Field kFields[] = {
#include "production_data_report_fields.inc"
};

#undef PRODUCTION_REPORT_FIELD
#undef PRODUCTION_REPORT_BIT_FIELD
#undef PRODUCTION_REPORT_FIXED_FIELD

const ProductionDataReport::Field* FindField(const std::string& key) {
    for (const auto& field : kFields) {
        if (key == field.key) {
            return &field;
        }
    }
    return nullptr;
}

int GetIntValue(const cJSON* object, const char* key, int default_value) {
    if (object == nullptr) {
        return default_value;
    }
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item != nullptr && cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return default_value;
}

// /current 返回 {code,msg,data}; 同时兼容直接返回配置对象.
const cJSON* GetPayload(const cJSON* root) {
    if (root == nullptr) {
        return nullptr;
    }
    const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (data != nullptr && cJSON_IsObject(data)) {
        return data;
    }
    return root;
}

// 只负责读取响应体; HTTP 对象的生命周期交给调用方的作用域, 不等读完后立刻析构.
// 这样避免在接收任务可能仍存活时销毁底层连接(现场曾出现该时序导致的 assert).
template <typename HttpClientT>
std::string ReadResponse(HttpClientT& http, size_t max_bytes) {
    std::string body = http.ReadAll();
    if (body.size() > max_bytes) {
        ESP_LOGW(kTag, "Response truncated from %u to %u bytes for logging/parsing",
                 static_cast<unsigned>(body.size()), static_cast<unsigned>(max_bytes));
        body.resize(max_bytes);
    }
    return body;
}

// 截断并去掉控制字符, 便于把服务端原文安全写进日志.
std::string Printable(const std::string& text, size_t max_length) {
    std::string out;
    for (char ch : text) {
        if (out.size() >= max_length) {
            break;
        }
        out.push_back((ch >= 0x20 && ch < 0x7F) ? ch : '.');
    }
    return out;
}

// 打印一次 HTTP 交互的完整收发内容(请求/响应全文, 不做截断), 便于串口对账与现场排障.
void LogHttpExchange(const char* phase, const char* method, const std::string& url,
                     const std::string* request_body, int status, int last_error,
                     bool opened, const std::string& response_body, int64_t elapsed_ms) {
    ESP_LOGI(kTag, ">>> [%s] %s %s", phase, method, url.c_str());
    if (request_body != nullptr) {
        ESP_LOGI(kTag, ">>> [%s] request body (%u bytes): %s", phase,
                 static_cast<unsigned>(request_body->size()), request_body->c_str());
    }
    if (!opened) {
        ESP_LOGW(kTag, "<<< [%s] open failed: err=%d, elapsed=%" PRId64 " ms", phase, last_error,
                 elapsed_ms);
        return;
    }
    ESP_LOGI(kTag, "<<< [%s] status=%d, %u bytes, elapsed=%" PRId64 " ms", phase, status,
             static_cast<unsigned>(response_body.size()), elapsed_ms);
    if (!response_body.empty()) {
        ESP_LOGI(kTag, "<<< [%s] response body: %s", phase, response_body.c_str());
    } else {
        ESP_LOGW(kTag, "<<< [%s] response body is empty", phase);
    }
}

// 配置拉取结果摘要: 开关/间隔/开关项/键数量, 用于确认「服务端下发 -> 设备采用」.
void LogConfigSummary(const char* prefix, bool enabled, uint32_t interval_seconds,
                      bool report_device_info, bool report_location, size_t key_count,
                      uint32_t skipped) {
    ESP_LOGI(kTag,
             "%s: enabled=%d interval=%" PRIu32 " min (%" PRIu32 " s) deviceInfo=%d location=%d "
             "keys=%u skipped=%" PRIu32,
             prefix, enabled, interval_seconds / 60, interval_seconds, report_device_info,
             report_location, static_cast<unsigned>(key_count), skipped);
}
}  // namespace

ProductionDataReport& ProductionDataReport::GetInstance() {
    static ProductionDataReport instance;
    return instance;
}

void ProductionDataReport::AttachBoardLinkage(ProductionBoardLinkage* linkage) {
    board_linkage_ = linkage;
}

void ProductionDataReport::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    if (xTaskCreate(WorkerTaskEntry, "data_report", kWorkerStackSize, this, kWorkerPriority,
                    &worker_handle_) != pdPASS) {
        running_.store(false);
        worker_handle_ = nullptr;
        ESP_LOGE(kTag, "Failed to create data report task");
        return;
    }
    ESP_LOGI(kTag, "Data report task started, base url %s", kBaseUrl);
}

void ProductionDataReport::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    TaskHandle_t handle = worker_handle_;
    if (handle != nullptr) {
        for (int i = 0; i < 100 && worker_handle_ != nullptr; ++i) {
            xTaskNotifyGive(handle);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        worker_handle_ = nullptr;
    }
}

void ProductionDataReport::NotifyNetworkConnected() {
    network_up_.store(true);
    // 重新联网后时钟可能已漂移, 需要重新对时后再拉配置.
    time_synced_.store(false);
    if (worker_handle_ != nullptr) {
        xTaskNotifyGive(worker_handle_);
    }
    ESP_LOGI(kTag, "Network connected, refresh report config");
}

void ProductionDataReport::NotifyNetworkDisconnected() {
    network_up_.store(false);
    if (worker_handle_ != nullptr) {
        xTaskNotifyGive(worker_handle_);
    }
    ESP_LOGI(kTag, "Network disconnected, data report paused");
}

void ProductionDataReport::Tick() {
    // 可选的外部节拍输入: 只唤醒工作者任务, 对时节奏由任务内的 last_sync_request_ 控制,
    // 因此这里可以任意频率、任意线程调用且不阻塞.
    TaskHandle_t handle = worker_handle_;
    if (handle != nullptr && !IsTimeSynced() && network_up_.load()) {
        xTaskNotifyGive(handle);
    }
}

void ProductionDataReport::WorkerTaskEntry(void* context) {
    static_cast<ProductionDataReport*>(context)->WorkerTask();
}

void ProductionDataReport::WorkerTask() {
    UploadConfig config;
    bool have_config = false;
    bool reported_once = false;
    TickType_t next_config_attempt = 0;
    TickType_t next_upload = 0;
    uint32_t fast_retries = 0;

    while (running_.load()) {
        if (!network_up_.load()) {
            if (have_config) {
                ESP_LOGI(kTag, "Report paused until the network is available");
            }
            have_config = false;
            reported_once = false;
            fast_retries = 0;
            next_config_attempt = 0;
            ulTaskNotifyTake(pdTRUE, kWorkerWaitTicks);
            continue;
        }

        const TickType_t now = xTaskGetTickCount();

        // 1) 对时: 未对齐时按 10s 间隔重试; 同步中重复请求会被 SNTP 自身忽略.
        if (!IsTimeSynced() &&
            (last_sync_request_ == 0 || (now - last_sync_request_) >= kSyncRequestIntervalTicks)) {
            last_sync_request_ = now;
            RequestTimeSyncIfNeeded();
        }

        // 2) 拉取配置: 首次、周期复查、或距上次配置后已到间隔.
        if (!have_config || (next_config_attempt != 0 && now >= next_config_attempt)) {
            UploadConfig next;
            if (FetchConfig(next)) {
                next.generation = ++config_loaded_count_;
                config = std::move(next);
                have_config = true;
                fast_retries = 0;
                if (!reported_once) {
                    next_upload = now;
                }
                if (config.enabled && !config.keys.empty()) {
                    next_config_attempt = now + TicksFromSeconds(config.interval_seconds);
                } else {
                    next_config_attempt = now + kNoKeysRecheckTicks;
                }
            } else {
                next_config_attempt = now + kConfigRetryTicks;
            }
        }

        // 3) 上报: enabled=0 或没有可用字段时跳过.
        const TickType_t after_config = xTaskGetTickCount();
        if (have_config && config.enabled && !config.keys.empty() &&
            after_config >= next_upload) {
            std::vector<Chunk> chunks;
            std::string payload;
            uint32_t field_count = 0;
            bool posted = false;
            const uint32_t attempt = ++report_attempts_;
            ESP_LOGI(kTag,
                     "Report #%" PRIu32 ": reading %u configured fields from Modbus address %u, "
                     "interval=%" PRIu32 " s",
                     attempt, static_cast<unsigned>(config.keys.size()),
                     static_cast<unsigned>(ProductionModbus::GetInstance().GetDeviceAddress()),
                     config.interval_seconds);
            if (ReadRegisterChunks(config, chunks) == ESP_OK &&
                BuildPayload(config, chunks, payload, field_count)) {
                ESP_LOGI(kTag,
                         "Report #%" PRIu32 ": built payload with %" PRIu32 " fields (%u bytes)",
                         attempt, field_count, static_cast<unsigned>(payload.size()));
                posted = PostPayload(payload);
            } else {
                ESP_LOGW(kTag,
                         "Report #%" PRIu32 ": skip upload, Modbus read or payload build failed",
                         attempt);
            }
            const TickType_t after = xTaskGetTickCount();
            if (posted) {
                reported_once = true;
                fast_retries = 0;
                next_upload = after + TicksFromSeconds(config.interval_seconds);
                ESP_LOGI(kTag,
                         "Report #%" PRIu32 " done: %" PRIu32
                         " fields accepted, next report in %" PRIu32 " s",
                         attempt, field_count, config.interval_seconds);
            } else {
                ++fast_retries;
                if (fast_retries <= 3) {
                    next_upload = after + kUploadFastRetryTicks;
                    ESP_LOGW(kTag, "Report #%" PRIu32 " failed, retry %" PRIu32 " in %u s",
                             attempt, fast_retries,
                             static_cast<unsigned>(kUploadFastRetryTicks / 100));
                } else {
                    fast_retries = 0;
                    next_upload = after + kUploadRetryTicks;
                    ESP_LOGW(kTag, "Report #%" PRIu32 " keeps failing, retry in %u s", attempt,
                             static_cast<unsigned>(kUploadRetryTicks / 100));
                }
            }
        }

        ulTaskNotifyTake(pdTRUE, kWorkerWaitTicks);
    }

    ESP_LOGI(kTag, "Data report task exiting");
    worker_handle_ = nullptr;
    vTaskDelete(nullptr);
}

bool ProductionDataReport::FetchConfig(UploadConfig& config) {
    auto* network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        return false;
    }
    auto http = network->CreateHttp(0);
    if (http == nullptr) {
        ESP_LOGE(kTag, "Create HTTP client failed");
        return false;
    }
    http->SetTimeout(kHttpTimeoutMs);
    http->SetKeepAlive(false);
    http->SetHeader("Accept", "application/json");
    http->SetHeader("User-Agent", SystemInfo::GetUserAgent());
    const std::string url = std::string(kBaseUrl) + kConfigPath;
    const int64_t started_us = esp_timer_get_time();
    const bool opened = http->Open("GET", url);
    const int64_t elapsed_ms = (esp_timer_get_time() - started_us) / 1000;
    if (!opened) {
        LogHttpExchange("config", "GET", url, nullptr, 0, http->GetLastError(), false, {},
                        elapsed_ms);
        http->Close();
        return false;
    }
    const int status = http->GetStatusCode();
    std::string body = ReadResponse(*http, kMaxResponseBytes);
    // 响应读完后显式关闭: 之后析构时 connected_ 已为 false, 不会再触发二次断开回调.
    http->Close();
    LogHttpExchange("config", "GET", url, nullptr, status, 0, true, body, elapsed_ms);
    if (status != 200) {
        ESP_LOGW(kTag, "Config fetch rejected: status=%d", status);
        return false;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        ESP_LOGW(kTag, "Config response is not valid JSON (%u bytes)",
                 static_cast<unsigned>(body.size()));
        return false;
    }
    const cJSON* payload = GetPayload(root);
    if (payload == nullptr || !cJSON_IsObject(payload)) {
        ESP_LOGW(kTag, "Config response has no object payload");
        cJSON_Delete(root);
        return false;
    }

    UploadConfig next;
    next.enabled = GetIntValue(payload, "enabled", 0) != 0;
    const int minutes = std::clamp(
        GetIntValue(payload, "reportInterval", static_cast<int>(kDefaultIntervalMinutes)),
        static_cast<int>(kMinIntervalMinutes), static_cast<int>(kMaxIntervalMinutes));
    next.interval_seconds = static_cast<uint32_t>(minutes) * 60;
    next.report_device_info = GetIntValue(payload, "reportDeviceInfo", 0) != 0;
    next.report_location = GetIntValue(payload, "reportLocation", 0) != 0;

    uint32_t skipped = 0;
    const cJSON* content = cJSON_GetObjectItemCaseSensitive(payload, "reportContent");
    if (content != nullptr && !cJSON_IsArray(content)) {
        ESP_LOGW(kTag, "reportContent is not an array, ignored");
        content = nullptr;
    } else if (content == nullptr) {
        ESP_LOGW(kTag, "reportContent is missing, nothing to report");
    }
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, content) {
        if (!cJSON_IsString(item) || item->valuestring == nullptr) {
            continue;
        }
        std::string key = item->valuestring;
        // 同一字段重复出现时只上报一次.
        if (std::find(next.keys.begin(), next.keys.end(), key) != next.keys.end()) {
            continue;
        }
        if (FindField(key) == nullptr) {
            if (skipped == 0) {
                ESP_LOGW(kTag, "Key '%s' is not in the local field catalog, skipped",
                         Printable(key, 64).c_str());
            }
            ++skipped;
            continue;
        }
        if (next.keys.size() >= kMaxReportKeys) {
            ESP_LOGW(kTag, "reportContent exceeds %u keys, truncated",
                     static_cast<unsigned>(kMaxReportKeys));
            break;
        }
        next.keys.push_back(std::move(key));
    }
    cJSON_Delete(root);

    LogConfigSummary("Config applied", next.enabled, next.interval_seconds,
                     next.report_device_info, next.report_location, next.keys.size(), skipped);
    if (!next.keys.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < next.keys.size() && 64 > joined.size(); ++i) {
            if (i != 0) {
                joined += ",";
            }
            joined += next.keys[i];
        }
        ESP_LOGI(kTag, "Config keys(%u): %s%s", static_cast<unsigned>(next.keys.size()),
                 joined.c_str(), next.keys.size() > 8 ? ",..." : "");
    }
    if (next.enabled && next.keys.empty()) {
        ESP_LOGW(kTag, "Reporting enabled but no usable keys, nothing will be uploaded");
    }
    config = std::move(next);
    return true;
}

esp_err_t ProductionDataReport::ReadRegisterChunks(const UploadConfig& config,
                                                   std::vector<Chunk>& chunks) {
    if (!network_up_.load() || (board_linkage_ != nullptr && !board_linkage_->IsNetworkLinked())) {
        ESP_LOGW(kTag, "Network lost before reading registers, skip this report");
        return ESP_ERR_INVALID_STATE;
    }
    auto& modbus = ProductionModbus::GetInstance();
    if (modbus.GetDeviceAddress() == ProductionModbus::kUnsetDeviceAddress) {
        ESP_LOGW(kTag, "Modbus device address is not configured, skip this report");
        return ESP_ERR_INVALID_STATE;
    }

    // 按 64 寄存器对齐分块, 每块只读一次; 空缺块不读.
    chunks.clear();
    for (const auto& key : config.keys) {
        const Field* field = FindField(key);
        if (field == nullptr || field->address >= ProductionModbus::kRegisterCount) {
            continue;
        }
        const uint16_t start = static_cast<uint16_t>((field->address / kChunkSize) * kChunkSize);
        const bool exists =
            std::any_of(chunks.begin(), chunks.end(),
                        [start](const Chunk& chunk) { return chunk.start == start; });
        if (exists) {
            continue;
        }
        Chunk chunk;
        chunk.start = start;
        chunk.count = static_cast<uint8_t>(
            std::min<size_t>(kChunkSize, ProductionModbus::kRegisterCount - start));
        chunks.push_back(chunk);
    }
    std::sort(chunks.begin(), chunks.end(),
              [](const Chunk& a, const Chunk& b) { return a.start < b.start; });

    uint32_t registers_read = 0;
    for (auto& chunk : chunks) {
        esp_err_t err = modbus.ReadHoldingRegisters(chunk.start, chunk.count, chunk.values);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "Read registers 0x%02X..0x%02X failed: %s", chunk.start,
                     chunk.start + chunk.count - 1, esp_err_to_name(err));
            return err;
        }
        chunk.loaded = true;
        registers_read += chunk.count;
        ESP_LOGD(kTag, "Modbus 0x03 read 0x%02X..0x%02X (%u registers) ok", chunk.start,
                 chunk.start + chunk.count - 1, chunk.count);
    }
    ESP_LOGI(kTag, "Modbus snapshot ready: %u chunks, %u registers for this report",
             static_cast<unsigned>(chunks.size()), static_cast<unsigned>(registers_read));
    return ESP_OK;
}

bool ProductionDataReport::BuildPayload(const UploadConfig& config,
                                        const std::vector<Chunk>& chunks, std::string& payload,
                                        uint32_t& field_count) {
    cJSON* data = cJSON_CreateObject();
    if (data == nullptr) {
        return false;
    }
    field_count = 0;
    uint32_t unusable = 0;

    for (const auto& key : config.keys) {
        const Field* field = FindField(key);
        if (field == nullptr) {
            ++unusable;
            continue;
        }
        if (static_cast<size_t>(field->address) + field->words >
            ProductionModbus::kRegisterCount) {
            ESP_LOGW(kTag, "'%s' at 0x%02X is out of range, skipped", field->key, field->address);
            ++unusable;
            continue;
        }
        const uint16_t start = static_cast<uint16_t>((field->address / kChunkSize) * kChunkSize);
        const Chunk* chunk = nullptr;
        for (const auto& candidate : chunks) {
            if (candidate.start == start && candidate.loaded) {
                chunk = &candidate;
                break;
            }
        }
        if (chunk == nullptr) {
            ++unusable;
            continue;
        }

        const uint16_t offset = field->address % kChunkSize;
        const uint16_t low = chunk->values[offset];
        const uint16_t high = field->words == 2 ? chunk->values[offset + 1] : 0;
        const uint32_t raw = (static_cast<uint32_t>(high) << 16) | low;
        switch (field->kind) {
            case kU16:
                cJSON_AddNumberToObject(data, field->key, low);
                break;
            case kS16:
                cJSON_AddNumberToObject(data, field->key, static_cast<int16_t>(low));
                break;
            case kU32:
                cJSON_AddNumberToObject(data, field->key, static_cast<double>(raw));
                break;
            case kS32:
                cJSON_AddNumberToObject(data, field->key,
                                        static_cast<double>(static_cast<int32_t>(raw)));
                break;
            case kUFixed: {
                const double scaled = static_cast<double>(low) * field->scale;
                cJSON_AddNumberToObject(data, field->key, std::round(scaled * 1000.0) / 1000.0);
                break;
            }
            case kBit:
                cJSON_AddNumberToObject(data, field->key, (low >> field->bit) & 0x01u);
                break;
            default:
                ESP_LOGW(kTag, "'%s' has unsupported kind %u, skipped", field->key,
                         field->kind);
                ++unusable;
                continue;
        }
        ++field_count;
    }

    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        cJSON_Delete(data);
        return false;
    }
    cJSON_AddStringToObject(root, "deviceId", Board::GetInstance().GetUuid().c_str());
    cJSON_AddNumberToObject(root, "timestamp", static_cast<double>(GetEpochMillis()));
    cJSON_AddItemToObject(root, "data", data);

    if (config.report_device_info) {
        const esp_app_desc_t* app = esp_app_get_description();
        cJSON* device_info = cJSON_CreateObject();
        if (device_info != nullptr) {
            cJSON_AddStringToObject(device_info, "firmware",
                                    app != nullptr ? app->version : "unknown");
            cJSON_AddStringToObject(device_info, "model",
                                    Board::GetInstance().GetBoardType().c_str());
            cJSON_AddStringToObject(device_info, "chip", SystemInfo::GetChipModelName().c_str());
            cJSON_AddStringToObject(device_info, "mac", SystemInfo::GetMacAddress().c_str());
            cJSON_AddItemToObject(root, "deviceInfo", device_info);
        }
    }
    if (config.report_location) {
        cJSON* location = cJSON_CreateObject();
        if (location != nullptr) {
            // 设备无定位能力, 按协议固定上送原点, 由平台侧补录真实位置.
            cJSON_AddNumberToObject(location, "lng", kReportLocationLongitude);
            cJSON_AddNumberToObject(location, "lat", kReportLocationLatitude);
            cJSON_AddItemToObject(root, "location", location);
        }
    }

    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) {
        return false;
    }
    payload.assign(text);
    cJSON_free(text);
    if (unusable > 0) {
        ESP_LOGW(kTag, "%" PRIu32 " requested fields could not be filled", unusable);
    }
    return field_count > 0;
}

bool ProductionDataReport::PostPayload(const std::string& payload) {
    if (!network_up_.load() || (board_linkage_ != nullptr && !board_linkage_->IsNetworkLinked())) {
        ESP_LOGW(kTag, "Network lost before uploading, skip this report");
        return false;
    }
    auto* network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        return false;
    }
    auto http = network->CreateHttp(0);
    if (http == nullptr) {
        return false;
    }
    http->SetTimeout(kHttpTimeoutMs);
    http->SetKeepAlive(false);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Accept", "application/json");
    http->SetHeader("User-Agent", SystemInfo::GetUserAgent());
    const std::string url = std::string(kBaseUrl) + kUploadPath;
    // 先把请求体交给底层再 Open, 保证 POST body 随请求一起发出.
    http->SetContent(std::string(payload));
    const int64_t started_us = esp_timer_get_time();
    const bool opened = http->Open("POST", url);
    const int64_t elapsed_ms = (esp_timer_get_time() - started_us) / 1000;
    if (!opened) {
        LogHttpExchange("upload", "POST", url, &payload, 0, http->GetLastError(), false, {},
                        elapsed_ms);
        http->Close();
        return false;
    }
    const int status = http->GetStatusCode();
    std::string body = ReadResponse(*http, kMaxResponseBytes);
    http->Close();
    LogHttpExchange("upload", "POST", url, &payload, status, 0, true, body, elapsed_ms);
    if (status < 200 || status >= 300) {
        ESP_LOGW(kTag, "Upload rejected: HTTP status=%d", status);
        return false;
    }
    if (body.find("\"code\":0") == std::string::npos) {
        ESP_LOGW(kTag, "Upload rejected: server code in body is not 0");
        return false;
    }
    return true;
}

void ProductionDataReport::RequestTimeSyncIfNeeded() {
    if (IsTimeSynced()) {
        return;
    }
    if (!sntp_started_) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, kPrimaryNtpServer);
        esp_sntp_setservername(1, kSecondaryNtpServer);
        sntp_set_time_sync_notification_cb(&ProductionDataReport::OnSntpSync);
        sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
        sntp_set_sync_interval(kSntpSyncIntervalSeconds * 1000);
        sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
        esp_sntp_init();
        sntp_started_ = true;
        ESP_LOGI(kTag, "SNTP started with %s / %s", kPrimaryNtpServer, kSecondaryNtpServer);
        return;
    }
    if (esp_sntp_restart() != ESP_OK) {
        // 上一次同步结束后 SNTP 会自行停止, 重启失败时保持未对齐状态, 下次再试.
        ESP_LOGW(kTag, "SNTP restart failed");
        return;
    }
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    ESP_LOGI(kTag, "SNTP restart requested");
}

void ProductionDataReport::OnSntpSync(timeval* offset) {
    // 回调运行在 lwIP/SNTP 任务上下文, 只做原子标记, 不在此处做任何阻塞操作.
    (void)offset;
    GetInstance().time_synced_.store(true);
    ESP_LOGI(kTag, "SNTP time synchronized");
}

int64_t ProductionDataReport::GetEpochMillis() const {
    if (time_synced_.load()) {
        // 秒级时钟 + 微秒余量, 得到毫秒级 UTC 时间.
        const int64_t now_us = esp_timer_get_time();
        return static_cast<int64_t>(time(nullptr)) * 1000 + (now_us % 1000000) / 1000;
    }
    // 未对齐: 退回开机运行时长(ms), 保证字段类型正确且单调递增.
    return esp_timer_get_time() / 1000;
}

TickType_t ProductionDataReport::TicksFromSeconds(uint32_t seconds) {
    return pdMS_TO_TICKS(static_cast<uint64_t>(seconds) * 1000ULL);
}

void InitializeProductionDataReport() {
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;
    ProductionDataReport::GetInstance().Start();
}
