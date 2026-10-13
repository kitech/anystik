#pragma once
#include "storage.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ChannelRow {
    std::string chanid;
    std::string proto_type = "tox";
    int64_t last_message_rowid = 0;
    int64_t last_read_rowid = 0;
    int unread_count = 0;
    int pinned_order = 0;
    std::string draft_text;
    int muted = 0;
    std::string name;
    std::string status;
    int is_connected = 0;
    std::string last_message_text;
    std::string last_message_time;
    int64_t last_active = 0;
    int auto_translate = 0;
    std::string icon_url;
    std::string pubkey;
};

struct ChannelUpdate {
    bool hasProtoType = false;          std::string proto_type;
    bool hasLastMessageRowid = false;   int64_t last_message_rowid;
    bool hasLastReadRowid = false;      int64_t last_read_rowid;
    bool hasUnreadCount = false;        int unread_count;
    bool hasPinnedOrder = false;        int pinned_order;
    bool hasDraftText = false;          std::string draft_text;
    bool hasMuted = false;              int muted;
    bool hasLastMessageText = false;    std::string last_message_text;
    bool hasLastMessageTime = false;    std::string last_message_time;
    bool hasLastActive = false;         int64_t last_active;
};

struct PeerRow {
    std::string chanid;
    int peer_number;
    std::string public_key;
    std::string name;
    std::string nickname;
    std::string avatar_url;
    std::string status_text;
    std::string status_str;
    std::string user_status;
    std::string peer_ip;
    int role = 0;
    std::string role_str;
    bool is_self = false;
    int64_t last_seen = 0;
    int status = 0;
};

class ChannelDbSyncInterface {
public:
    virtual ~ChannelDbSyncInterface() = default;

    virtual bool add_channel(const ChannelRow& row) = 0;
    virtual std::unique_ptr<ChannelRow> get_channel(const char* chanid) = 0;
    virtual bool update_channel(const char* chanid, const ChannelUpdate& upd) = 0;
    virtual int delete_channel(const char* chanid) = 0;
    virtual std::vector<ChannelRow> load_pinned() = 0;
    virtual std::vector<ChannelRow> load_all_channels() = 0;

    virtual bool add_contact_channel(const ChannelRow& row) = 0;
    virtual bool update_contact_channel(const ChannelRow& row) = 0;
    virtual bool set_channel_tags(const char* chanid, const char* tags) = 0;
    virtual std::vector<std::string> get_channel_tags(const char* chanid) = 0;
    virtual bool increment_unread(const char* chanid, int delta,
                                  int64_t msgRowid = 0) = 0;
    virtual bool mark_read(const char* chanid, int64_t lastReadRowid) = 0;

    virtual bool add_peer(const PeerRow& row) = 0;
    virtual bool update_peer(const PeerRow& row) = 0;
    virtual std::unique_ptr<PeerRow> get_chan_peer(const char* chanid, int peer_number) = 0;
    virtual std::vector<PeerRow> load_chan_peers(const char* chanid) = 0;
    virtual bool remove_chan_peers(const char* chanid) = 0;

    virtual bool begin_write_transaction() = 0;
    virtual bool commit_transaction() = 0;

    virtual int64_t countChannels() = 0;
    virtual int64_t countPeers() = 0;
    virtual int64_t totalUnread() = 0;
};

class ChannelDbSyncSafeInterface {
public:
    virtual ~ChannelDbSyncSafeInterface() = default;
    virtual ChannelDbSyncInterface& get() = 0;
};

class WriteQueue;

class ChannelDbAsyncInterface {
public:
    virtual ~ChannelDbAsyncInterface() = default;
    virtual void add_channel(ChannelRow row, std::function<void(bool)> done) = 0;
    virtual void get_channel(std::string chanid,
                             std::function<void(std::unique_ptr<ChannelRow>)> done) = 0;
    virtual void update_channel(std::string chanid, ChannelUpdate upd,
                                std::function<void(bool)> done) = 0;
    virtual void delete_channel(std::string chanid, std::function<void(int)> done) = 0;
    virtual void load_pinned(std::function<void(std::vector<ChannelRow>)> done) = 0;
    virtual void load_all_channels(
        std::function<void(std::vector<ChannelRow>)> done) = 0;
    virtual void add_contact_channel(ChannelRow row, std::function<void(bool)> done) = 0;
    virtual void update_contact_channel(ChannelRow row, std::function<void(bool)> done) = 0;
    virtual void set_channel_tags(std::string chanid, std::string tags,
                                  std::function<void(bool)> done) = 0;
    virtual void get_channel_tags(std::string chanid,
                                  std::function<void(std::vector<std::string>)> done) = 0;
    virtual void increment_unread(std::string chanid, int delta, int64_t msgRowid,
                                  std::function<void(bool)> done) = 0;
    virtual void mark_read(std::string chanid, int64_t lastReadRowid,
                           std::function<void(bool)> done) = 0;
    virtual void add_peer(PeerRow row, std::function<void(bool)> done) = 0;
    virtual void update_peer(PeerRow row, std::function<void(bool)> done) = 0;
    virtual void get_chan_peer(std::string chanid, int peer_number,
                               std::function<void(std::unique_ptr<PeerRow>)> done) = 0;
    virtual void load_chan_peers(std::string chanid,
                                 std::function<void(std::vector<PeerRow>)> done) = 0;
    virtual void remove_chan_peers(std::string chanid,
                                   std::function<void(bool)> done) = 0;
    virtual void close(std::function<void()> done) = 0;
};

std::shared_ptr<ChannelDbSyncSafeInterface> create_channel_db(
    std::shared_ptr<SqliteConnectionSafe> conn);

std::shared_ptr<ChannelDbAsyncInterface> create_channel_db_async(
    std::shared_ptr<ChannelDbSyncSafeInterface> sync,
    std::shared_ptr<WriteQueue> queue);

bool init_channel_db(SqliteDb& db);
bool drop_channel_db(SqliteDb& db);
