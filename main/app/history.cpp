// =============================================================================
// HISTORY
// =============================================================================
// Append-only ring of saved notes in the `notes` NVS partition, read by the
// navigation in app/pipeline.cpp and written once per successful save.
#include "app/history.h"

#include <cstring>
#include <mutex>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui/text.h"

namespace history {
namespace {

constexpr const char *kTag = "history";
constexpr const char *kPartition = "notes";
constexpr const char *kNamespace = "notes";
constexpr const char *kSeqKey = "seq";
constexpr const char *kOldestKey = "oldest";

// The namespace the pre-history firmware wrote its single note into. Shared
// with settings.cpp, which is why only the one key is touched here.
constexpr const char *kLegacyNamespace = "sticky";
constexpr const char *kLegacyNoteKey = "last_note";

std::mutex s_mutex;
nvs_handle_t s_handle = 0;
bool s_available = false;
// Next sequence to write, and the lowest still stored. Empty when equal.
uint32_t s_seq = 0;
uint32_t s_oldest = 0;
int s_cap = 1;

// Builds the key for one entry. NVS allows 15 characters and the widest this
// produces is "n4294967295", 11.
std::string key_for(uint32_t seq)
{
    return "n" + std::to_string(seq);
}

// Reads a u32 counter, leaving dest alone when the key has never been written.
void load_u32(const char *key, uint32_t &dest)
{
    uint32_t value = 0;
    if (nvs_get_u32(s_handle, key, &value) == ESP_OK) {
        dest = value;
    }
}

// Drops entries from the old end until no more than s_cap remain. Erasing a
// missing key is harmless, so a counter that got ahead of the data self-heals.
void prune_locked()
{
    bool erased = false;
    while (s_seq - s_oldest > static_cast<uint32_t>(s_cap)) {
        nvs_erase_key(s_handle, key_for(s_oldest).c_str());
        ++s_oldest;
        erased = true;
    }
    if (erased) {
        nvs_set_u32(s_handle, kOldestKey, s_oldest);
        nvs_commit(s_handle);
    }
}

// Copies the single note the pre-history firmware stored into the ring, then
// erases it. Timestamp 0, because that firmware never recorded one.
void migrate_legacy_locked()
{
    if (s_seq != s_oldest) {
        return;
    }
    nvs_handle_t legacy = 0;
    if (nvs_open(kLegacyNamespace, NVS_READWRITE, &legacy) != ESP_OK) {
        return;
    }
    size_t length = 0;
    std::string note;
    if (nvs_get_str(legacy, kLegacyNoteKey, nullptr, &length) == ESP_OK && length > 1) {
        note.resize(length);
        if (nvs_get_str(legacy, kLegacyNoteKey, note.data(), &length) == ESP_OK) {
            note.resize(length - 1);
        } else {
            note.clear();
        }
    }
    if (!note.empty()) {
        std::string blob(sizeof(int64_t), '\0');
        blob += note;
        if (nvs_set_blob(s_handle, key_for(s_seq).c_str(), blob.data(), blob.size()) == ESP_OK) {
            ++s_seq;
            nvs_set_u32(s_handle, kSeqKey, s_seq);
            nvs_commit(s_handle);
            ESP_LOGI(kTag, "Migrated last_note, %u bytes", static_cast<unsigned>(note.size()));
        }
    }
    nvs_erase_key(legacy, kLegacyNoteKey);
    nvs_commit(legacy);
    nvs_close(legacy);
}

// Reads one entry with the lock already held.
bool read_locked(uint32_t seq, Entry &out)
{
    if (!s_available || seq == kNoSeq || seq < s_oldest || seq >= s_seq) {
        return false;
    }
    const std::string key = key_for(seq);
    size_t length = 0;
    if (nvs_get_blob(s_handle, key.c_str(), nullptr, &length) != ESP_OK ||
        length < sizeof(int64_t)) {
        return false;
    }
    std::string blob;
    blob.resize(length);
    if (nvs_get_blob(s_handle, key.c_str(), blob.data(), &length) != ESP_OK) {
        return false;
    }
    int64_t when = 0;
    std::memcpy(&when, blob.data(), sizeof(when));
    out.when = when;
    out.text = blob.substr(sizeof(int64_t));
    out.seq = seq;
    return true;
}

}  // namespace


esp_err_t init(int cap)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_cap = cap < 1 ? 1 : cap;

    esp_err_t err = nvs_flash_init_partition(kPartition);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Safe in a way the erase in main.cpp is not: this partition holds
        // nothing but notes, where erasing the shared one takes the Wi-Fi
        // credentials with it.
        ESP_LOGW(kTag, "Notes partition unusable, erasing");
        err = nvs_flash_erase_partition(kPartition);
        if (err == ESP_OK) {
            err = nvs_flash_init_partition(kPartition);
        }
    }
    if (err != ESP_OK) {
        // ESP_ERR_NOT_FOUND is a device flashed with the four-row table, which
        // is what writing only app.bin over an older release leaves behind.
        ESP_LOGW(kTag, "No notes partition (%s), history disabled", esp_err_to_name(err));
        return ESP_OK;
    }
    if (nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &s_handle) != ESP_OK) {
        ESP_LOGW(kTag, "Cannot open the notes namespace, history disabled");
        return ESP_OK;
    }

    s_available = true;
    load_u32(kSeqKey, s_seq);
    load_u32(kOldestKey, s_oldest);
    migrate_legacy_locked();
    prune_locked();
    // The newest stamp tells a blank date on the screen apart from a note that
    // was never dated, which look identical once drawn.
    Entry newest;
    read_locked(s_seq - 1, newest);
    ESP_LOGI(kTag, "%u notes, cap %d, newest stamped %lld",
             static_cast<unsigned>(s_seq - s_oldest), s_cap, newest.when);
    return ESP_OK;
}


bool available()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_available;
}


int count()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_available ? static_cast<int>(s_seq - s_oldest) : 0;
}


uint32_t newest_seq()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return (s_available && s_seq != s_oldest) ? s_seq - 1 : kNoSeq;
}


uint32_t oldest_seq()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return (s_available && s_seq != s_oldest) ? s_oldest : kNoSeq;
}


bool get(uint32_t seq, Entry &out)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return read_locked(seq, out);
}


uint32_t append(const std::string &text, int64_t when)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_available) {
        return kNoSeq;
    }
    const std::string kept = text::truncate_utf8(text, kNoteBytes);
    // time_t is 64-bit here, so the width is spelled out rather than left to
    // whatever the toolchain picks.
    const int64_t stamp = when;
    std::string blob(sizeof(int64_t), '\0');
    std::memcpy(blob.data(), &stamp, sizeof(stamp));
    blob += kept;

    const uint32_t seq = s_seq;
    if (nvs_set_blob(s_handle, key_for(seq).c_str(), blob.data(), blob.size()) != ESP_OK) {
        ESP_LOGW(kTag, "Store failed, %u bytes", static_cast<unsigned>(blob.size()));
        return kNoSeq;
    }
    s_seq = seq + 1;
    nvs_set_u32(s_handle, kSeqKey, s_seq);
    nvs_commit(s_handle);
    prune_locked();
    return seq;
}


void set_capacity(int cap)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_cap = cap < 1 ? 1 : cap;
    if (s_available) {
        prune_locked();
    }
}

}  // namespace history
