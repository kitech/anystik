#pragma once
#include "storage.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct MessageRow {
    int64_t rowid = 0;
    std::string event_id;
    std::string chanid;
    std::string data;
    int etype = 0;
    std::string sender_name;
    std::string sender_nick;
    int peer_number = -1;
    std::string sender_pubkey;
    std::string signature;
    std::string avatar_url;
    std::string time_text;
    std::string ip_address;
    std::string category;
    std::string caption;
    std::string media_url;
    std::string media_mime;
    int media_width = 0;
    int media_height = 0;
    std::string file_name;
    int file_size = 0;
    int duration_sec = 0;
    std::string local_path;
    std::string gif_path;
    std::string thumbnail_key;
    int cache_tag = 0;
    int send_state = 0;
    int64_t relates_to_rowid = 0;
    int edited = 0;
    int redacted = 0;
    std::string forwarded_from;
    int mention = 0;
    std::string relates_to_ids;    // "eventId1,eventId2"
    std::string mentions_text;   // "@user1,@user2"
};

struct MessageUpdate {
    bool hasEtype = false;          int etype;
    bool hasSenderName = false;     std::string sender_name;
    bool hasSenderNick = false;     std::string sender_nick;
    bool hasPeerNumber = false;     int peer_number;
    bool hasCategory = false;       std::string category;
    bool hasCaption = false;        std::string caption;
    bool hasMediaUrl = false;       std::string media_url;
    bool hasMediaMime = false;      std::string media_mime;
    bool hasMediaWidth = false;     int media_width;
    bool hasMediaHeight = false;    int media_height;
    bool hasFileName = false;       std::string file_name;
    bool hasFileSize = false;       int file_size;
    bool hasDurationSec = false;    int duration_sec;
    bool hasLocalPath = false;      std::string local_path;
    bool hasThumbnailKey = false;   std::string thumbnail_key;
    bool hasSendState = false;      int send_state;
    bool hasEdited = false;         int edited;
    bool hasRedacted = false;       int redacted;
    bool hasForwardedFrom = false;  std::string forwarded_from;
    bool hasMention = false;        int mention;
    bool hasRelatesToIds = false;     std::string relates_to_ids;
    bool hasMentionsText = false;   std::string mentions_text;
};

struct ReactionRow {
    int64_t message_rowid;
    std::string emoji;
    std::string sender_name;
};

struct TranslationRow {
    int64_t message_rowid;
    std::string target_lang;
    std::string translated_text;
    std::string translated_entities;
    std::string source_lang;
    std::string provider;
};

struct BookmarkRow {
    int64_t id = 0;
    int64_t message_rowid;
    std::string chanid;
    std::string note;
    std::string tag;
};

class MessageDbSyncInterface {
public:
    virtual ~MessageDbSyncInterface() = default;

    virtual int64_t insert_message(const MessageRow& row) = 0;
    virtual bool update_message(int64_t rowid, const MessageUpdate& upd) = 0;
    virtual bool delete_message(int64_t rowid) = 0;
    virtual std::unique_ptr<MessageRow> get_message(int64_t rowid) = 0;
    virtual std::vector<MessageRow> load_messages(const char* chanid, int limit = 50) = 0;
    virtual std::vector<MessageRow> load_messages_before(
        const char* chanid, int64_t before_rowid, int limit = 50) = 0;
    virtual std::vector<int64_t> search_messages(const char* query, int limit = 50) = 0;

    virtual bool add_reaction(int64_t msg_rowid, const char* emoji,
                              const char* sender) = 0;
    virtual bool remove_reaction(int64_t msg_rowid, const char* emoji,
                                 const char* sender) = 0;
    virtual std::vector<ReactionRow> get_reactions(int64_t msg_rowid) = 0;

    virtual bool set_message_tags(int64_t rowid, const char* tags) = 0;
    virtual std::vector<std::string> get_message_tags(int64_t rowid) = 0;

    virtual bool set_translation(const TranslationRow& row) = 0;
    virtual std::unique_ptr<TranslationRow> get_translation(
        int64_t msg_rowid, const char* lang) = 0;
    virtual bool clear_translations_by_lang(const char* lang) = 0;

    virtual bool add_bookmark(int64_t msg_rowid, const char* chanid,
                              const char* note) = 0;
    virtual bool remove_bookmark(int64_t msg_rowid) = 0;
    virtual std::unique_ptr<BookmarkRow> get_bookmark(int64_t msg_rowid) = 0;
    virtual std::vector<int64_t> list_bookmark_rowids(const char* chanid) = 0;

    virtual bool clear_channel(const char* chanid) = 0;

    virtual int64_t countMessages() = 0;
    virtual int64_t countTranslations() = 0;
    virtual int64_t countReactions() = 0;
    virtual int64_t countBookmarks() = 0;

    virtual bool begin_write_transaction() = 0;
    virtual bool commit_transaction() = 0;
};

class MessageDbSyncSafeInterface {
public:
    virtual ~MessageDbSyncSafeInterface() = default;
    virtual MessageDbSyncInterface& get() = 0;
};

class WriteQueue;

class MessageDbAsyncInterface {
public:
    virtual ~MessageDbAsyncInterface() = default;
    virtual void insert_message(MessageRow row,
                                std::function<void(int64_t)> done) = 0;
    virtual void update_message(int64_t rowid, MessageUpdate upd,
                                std::function<void(bool)> done) = 0;
    virtual void delete_message(int64_t rowid,
                                std::function<void(bool)> done) = 0;
    virtual void get_message(int64_t rowid,
                             std::function<void(std::unique_ptr<MessageRow>)> done) = 0;
    virtual void load_messages(std::string chanid, int limit,
                               std::function<void(std::vector<MessageRow>)> done) = 0;
    virtual void load_messages_before(std::string chanid, int64_t before_rowid,
                                      int limit,
                                      std::function<void(std::vector<MessageRow>)> done) = 0;
    virtual void search_messages(std::string query, int limit,
                                 std::function<void(std::vector<int64_t>)> done) = 0;
    virtual void add_reaction(int64_t msg_rowid, std::string emoji,
                              std::string sender,
                              std::function<void(bool)> done) = 0;
    virtual void remove_reaction(int64_t msg_rowid, std::string emoji,
                                 std::string sender,
                                 std::function<void(bool)> done) = 0;
    virtual void get_reactions(int64_t msg_rowid,
                               std::function<void(std::vector<ReactionRow>)> done) = 0;
    virtual void set_message_tags(int64_t rowid, std::string tags,
                                  std::function<void(bool)> done) = 0;
    virtual void get_message_tags(int64_t rowid,
                                  std::function<void(std::vector<std::string>)> done) = 0;
    virtual void set_translation(TranslationRow row,
                                 std::function<void(bool)> done) = 0;
    virtual void get_translation(int64_t msg_rowid, std::string lang,
                                 std::function<void(std::unique_ptr<TranslationRow>)> done) = 0;
    virtual void clear_translations_by_lang(std::string lang,
                                            std::function<void(bool)> done) = 0;
    virtual void add_bookmark(int64_t msg_rowid, std::string chanid,
                              std::string note,
                              std::function<void(bool)> done) = 0;
    virtual void remove_bookmark(int64_t msg_rowid,
                                 std::function<void(bool)> done) = 0;
    virtual void get_bookmark(int64_t msg_rowid,
                              std::function<void(std::unique_ptr<BookmarkRow>)> done) = 0;
    virtual void clear_channel(std::string chanid,
                               std::function<void(bool)> done) = 0;
    virtual void close(std::function<void()> done) = 0;
};

std::shared_ptr<MessageDbSyncSafeInterface> create_message_db(
    std::shared_ptr<SqliteConnectionSafe> conn);

std::shared_ptr<MessageDbAsyncInterface> create_message_db_async(
    std::shared_ptr<MessageDbSyncSafeInterface> sync,
    std::shared_ptr<WriteQueue> queue);

bool init_message_db(SqliteDb& db);
bool drop_message_db(SqliteDb& db);
