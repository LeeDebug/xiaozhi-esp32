#ifndef CHAT_HISTORY_H
#define CHAT_HISTORY_H

#include <nvs.h>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

// 历史对话管理：在 NVS 中持久化最近一轮对话文本，
// 以容量受限的环形队列形式保存，避免过度写入与内存膨胀。
class ChatHistory {
public:
    struct Entry {
        std::string role;    // "user" / "assistant" / "system"
        std::string content; // 对话文本
    };

    static ChatHistory& GetInstance() {
        static ChatHistory instance;
        return instance;
    }

    ChatHistory(const ChatHistory&) = delete;
    ChatHistory& operator=(const ChatHistory&) = delete;

    // 加载 NVS 中已保存的历史。应在应用初始化后、首次展示页面之前调用。
    void Load();

    // 追加一条记录；超容时淘汰最旧记录。
    void Add(const std::string& role, const std::string& content);

    // 返回按时间正序排列的历史记录副本（旧 -> 新）。
    std::deque<Entry> GetAll() const;

    // 清空全部历史（同时删除 NVS 数据）。
    void Clear();

    // 当前有效记录条数。
    size_t Size() const;

private:
    ChatHistory() = default;
    ~ChatHistory() = default;

    // 以下方法须在持有 mutex_ 时调用。
    void LoadLocked();
    bool ReadEntryLocked(nvs_handle_t handle, Entry& e, uint32_t index);
    void RewriteAllLocked();

    // NVS 元数据 key
    static constexpr char kCountKey[] = "count";
    static constexpr char kHeadKey[] = "head";
    static constexpr char kSeqKey[] = "seq";

    // 容量限制
    static constexpr uint32_t kMaxEntries = 40;        // 最多保留 40 条
    static constexpr size_t kMaxContentBytes = 512;    // 单条文本上限

    static constexpr char kNvsNamespace[] = "chat_history";

    mutable std::mutex mutex_;
    std::deque<Entry> entries_;   // 已加载到 RAM 的缓存（正序）
    bool loaded_ = false;
    uint32_t next_seq_ = 0;
};

#endif  // CHAT_HISTORY_H