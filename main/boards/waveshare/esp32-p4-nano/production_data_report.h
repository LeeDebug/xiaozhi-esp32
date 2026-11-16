#pragma once
/**
 * @file production_data_report.h
 * @brief 产线数据上报: 拉取服务端上报配置, 按配置读取 Modbus 保持寄存器并定时 POST 上报.
 *
 * 流程(见《设备端接入指南》):
 *  1. 网络连接后 GET {base}/xiaozhi/data-report-config/current (公开接口, 无需登录);
 *  2. enabled==1 时按 reportContent(全量字段目录的 mb-mcp_* key)从 Modbus 读数据;
 *  3. 每 reportInterval 分钟 POST {base}/xiaozhi/data-analysis/upload,
 *     deviceInfo / location 仅在对应开关为 1 时携带.
 *
 * 设计要点:
 * - `NotifyNetworkConnected()` / `NotifyNetworkDisconnected()` / `Tick()` 可在任意线程调用,
 *   只做原子标记与任务通知; 拉配置、Modbus 读取和 HTTP 上报都在本模块专用任务中串行执行,
 *   不占用 App 主事件循环, 也不在调用方阻塞 Modbus 总线.
 * - reportContent 中不在本地字段目录里的 key 直接跳过(每个配置周期只提示一次).
 * - 网络断开时立即停止上报; 重新连接后重新对时并重新拉取配置, 再按新配置上报.
 * - 时间戳优先使用 SNTP 同步的网络时间; 未同步时退回「开机运行时长」毫秒数.
 * - enabled=0 或 reportContent 为空时不上报, 但仍周期复查配置以便服务端动态开启.
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/time.h>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief 板卡侧的接线查询接口, 由板卡实现(生产上为 EthernetWifiBoard).
 * 数据上报模块只依赖这个抽象, 不依赖具体板卡类.
 */
class ProductionBoardLinkage {
public:
    virtual ~ProductionBoardLinkage() = default;
    /** @brief 板卡当前是否已与网络连接(Ethernet 或 WiFi). */
    virtual bool IsNetworkLinked() const = 0;
};

class ProductionDataReport {
public:
    /** @brief 本地字段目录中的一项(对应《设备端接入指南》全量字段目录). */
    struct Field {
        const char* key;
        uint16_t address;  // 保持寄存器起始地址(0..166)
        uint8_t words;     // 占用寄存器数: 1 或 2(低字在前、高字在后)
        uint8_t kind;      // 取值类型, 见 production_data_report.cc 的 ValueKind
        uint8_t bit;       // 位字段的位下标; 非位字段为 kNoBit
        float scale;       // 定点换算系数; 非定点字段为 1.0
    };

    static ProductionDataReport& GetInstance();

    /** @brief 绑定板卡接线(必须先于 Start 调用). */
    void AttachBoardLinkage(ProductionBoardLinkage* linkage);

    /** @brief 启动上报任务; 在收到 `NotifyNetworkConnected()` 之后才开始拉配置. */
    void Start();
    void Stop();

    /** @brief 网络已连接: 重新对时、重新拉取配置并重启上报周期. 任意线程可调用. */
    void NotifyNetworkConnected();

    /** @brief 网络已断开: 立即停止上报与重试. 任意线程可调用. */
    void NotifyNetworkDisconnected();

    /**
     * @brief 可选的外部节拍(任意线程、任意频率, 不阻塞): 唤醒工作者任务补做对时/上报检查.
     * 工作者任务本身每秒自转, 不依赖外部调用也能正常对时与上报.
     */
    void Tick();

private:
    struct UploadConfig {
        bool enabled = false;
        uint32_t interval_seconds = 0;
        bool report_device_info = false;
        bool report_location = false;
        std::vector<std::string> keys;
        uint32_t generation = 0;  // 第几次成功拉取到的配置, 仅用于日志排查
    };

    struct Chunk {
        uint16_t start = 0;
        uint8_t count = 0;
        bool loaded = false;
        uint16_t values[64] = {};
    };

    // 时间参数.
    static constexpr TickType_t kWorkerWaitTicks = pdMS_TO_TICKS(1000);
    static constexpr TickType_t kConfigRetryTicks = pdMS_TO_TICKS(15 * 1000);
    static constexpr TickType_t kSyncRequestIntervalTicks = pdMS_TO_TICKS(10 * 1000);
    static constexpr TickType_t kUploadFastRetryTicks = pdMS_TO_TICKS(5 * 1000);
    static constexpr TickType_t kUploadRetryTicks = pdMS_TO_TICKS(30 * 1000);
    static constexpr TickType_t kNoKeysRecheckTicks = pdMS_TO_TICKS(30 * 60 * 1000);
    static constexpr uint32_t kMaxFastRetries = 3;
    static constexpr uint32_t kSntpSyncIntervalSeconds = 60 * 60 * 24;

    // 配置约束.
    static constexpr uint32_t kDefaultIntervalMinutes = 5;
    static constexpr uint32_t kMinIntervalMinutes = 1;
    static constexpr uint32_t kMaxIntervalMinutes = 7 * 24 * 60;
    static constexpr size_t kMaxReportKeys = 256;
    static constexpr size_t kChunkSize = 64;

    // 任务与 HTTP 参数.
    static constexpr size_t kWorkerStackSize = 8192;
    static constexpr UBaseType_t kWorkerPriority = 3;
    static constexpr int kHttpTimeoutMs = 15000;
    static constexpr size_t kMaxResponseBytes = 32 * 1024;

    static void WorkerTaskEntry(void* context);
    void WorkerTask();

    // 配置.
    bool FetchConfig(UploadConfig& config);

    // 上报.
    esp_err_t ReadRegisterChunks(const UploadConfig& config, std::vector<Chunk>& chunks);
    bool BuildPayload(const UploadConfig& config, const std::vector<Chunk>& chunks,
                      std::string& payload, uint32_t& field_count);
    bool PostPayload(const std::string& payload);

    // 对时.
    void RequestTimeSyncIfNeeded();
    static void OnSntpSync(timeval* offset);
    bool IsTimeSynced() const { return time_synced_.load(); }
    int64_t GetEpochMillis() const;
    static TickType_t TicksFromSeconds(uint32_t seconds);

    ProductionBoardLinkage* board_linkage_ = nullptr;
    TaskHandle_t worker_handle_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> network_up_{false};
    std::atomic<bool> time_synced_{false};
    TickType_t last_sync_request_ = 0;
    uint32_t config_loaded_count_ = 0;
    uint32_t report_attempts_ = 0;
    bool sntp_started_ = false;
};

/**
 * @brief 幂等初始化: 启动上报任务. 仅在板卡接线(`Start()` 里的网络观察者)就绪后调用,
 * 连接事件会触发「先对时、再拉配置」。
 */
void InitializeProductionDataReport();
