#include "chat_history.h"

#include <esp_log.h>

#include <cinttypes>
#include <cstdio>

#define TAG "ChatHistory"

using Entry = ChatHistory::Entry;

namespace {
// 单条记录在 NVS 中的 key，例如 m00、m01 ...
std::string EntryKey(uint32_t index) {
    char buf[16];
    snprintf(buf, sizeof(buf), "m%02" PRIu32, index);
    return buf;
}
}  // namespace

// ---------- 内部工具：LoadLocked / RewriteAllLocked 需在持有 mutex_ 时调用 ----------

void ChatHistory::LoadLocked() {
    entries_.clear();
    next_seq_ = 0;

    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        loaded_ = true;  // 即使打开失败也标记已加载，避免重复尝试
        return;
    }

    uint32_t count = 0;
    uint32_t head = 0;
    nvs_get_u32(handle, kCountKey, &count);
    nvs_get_u32(handle, kHeadKey, &head);
    nvs_get_u32(handle, kSeqKey, &next_seq_);
    if (count > kMaxEntries) {
        count = kMaxEntries;
    }

    // 读取环形队列：index = (head + i) % kMaxEntries
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idx = (head + i) % kMaxEntries;
        Entry e;
        if (ReadEntryLocked(handle, e, idx)) {
            entries_.push_back(std::move(e));
        }
    }

    nvs_close(handle);
    loaded_ = true;
    ESP_LOGI(TAG, "Loaded %u history entries", (unsigned)entries_.size());
}

bool ChatHistory::ReadEntryLocked(nvs_handle_t handle, Entry& e, uint32_t index) {
    std::string key = EntryKey(index);
    std::string role_key = key + "_r";
    std::string content_key = key + "_c";

    size_t role_len = 0;
    if (nvs_get_str(handle, role_key.c_str(), nullptr, &role_len) != ESP_OK || role_len == 0) {
        return false;
    }
    std::string role;
    role.resize(role_len);
    if (nvs_get_str(handle, role_key.c_str(), role.data(), &role_len) != ESP_OK) {
        return false;
    }
    while (!role.empty() && role.back() == '\0') {
        role.pop_back();
    }

    size_t content_len = 0;
    if (nvs_get_str(handle, content_key.c_str(), nullptr, &content_len) != ESP_OK) {
        return false;
    }
    std::string content;
    content.resize(content_len);
    if (nvs_get_str(handle, content_key.c_str(), content.data(), &content_len) != ESP_OK) {
        return false;
    }
    while (!content.empty() && content.back() == '\0') {
        content.pop_back();
    }

    e.role = std::move(role);
    e.content = std::move(content);
    return true;
}

void ChatHistory::RewriteAllLocked() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for rewriting");
        return;
    }

    // 清空旧键后按紧凑 index 重写，保证写入与读取的 index 约定一致
    nvs_erase_all(handle);
    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        std::string key = EntryKey((uint32_t)i);
        nvs_set_str(handle, (key + "_r").c_str(), e.role.c_str());
        std::string content = e.content;
        if (content.size() > kMaxContentBytes) {
            content = content.substr(0, kMaxContentBytes);
        }
        nvs_set_str(handle, (key + "_c").c_str(), content.c_str());
    }
    nvs_set_u32(handle, kCountKey, (uint32_t)entries_.size());
    nvs_set_u32(handle, kHeadKey, 0);  // 紧凑写入后 head 恒为 0
    nvs_set_u32(handle, kSeqKey, next_seq_);
    nvs_commit(handle);
    nvs_close(handle);
}

// ---------- 公开接口 ----------

void ChatHistory::Load() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (loaded_) {
        return;
    }
    LoadLocked();
}

void ChatHistory::Add(const std::string& role, const std::string& content) {
    if (content.empty()) {
        return;
    }

    Entry e;
    e.role = role.empty() ? "system" : role;
    e.content = content;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_) {
        LoadLocked();
    }

    if (entries_.size() >= kMaxEntries) {
        entries_.pop_front();  // 淘汰最旧
    }
    entries_.push_back(std::move(e));
    next_seq_++;

    // 每条新消息整体重写一次（上限 40 条，代价可控，NVS 写入量小）
    RewriteAllLocked();
}

std::deque<Entry> ChatHistory::GetAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
}

void ChatHistory::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    next_seq_ = 0;

    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "Chat history cleared");
}

size_t ChatHistory::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}