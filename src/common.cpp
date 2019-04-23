#include "common.h"
#include <sys/time.h>
#include <assert.h>
#include <mutex>
#include <glib.h>

#define MICROSECONDS_PER_SECOND (1000 * 1000)


static std::unordered_map<std::string, std::string> g_keys;
static std::mutex g_key_mut;

uint64_t timestamp_now()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec * MICROSECONDS_PER_SECOND + tv.tv_usec);
}

static std::string get_command(struct redisReply* reply)
{
    assert(reply->type = REDIS_REPLY_ARRAY);
    assert(reply->elements > 0);
    char buffer[256];

    if (reply->elements != 2) {
        LOG_WARN("wrong elements %lu", reply->elements);
        int n = snprintf(buffer, sizeof(buffer), shared::wrong_number_arguments, "get");
        return std::string(buffer, n);
    }

    if (reply->element[1]->type != REDIS_REPLY_STRING) {
        LOG_WARN("wrong type %d", reply->element[1]->type);
        return shared::wrong_type;
    }

    std::string key(reply->element[1]->str, reply->element[1]->len);

    std::lock_guard<std::mutex> lock(g_key_mut);

    auto it = g_keys.find(key);
    if (it == g_keys.end()) {
        return shared::null;
    }
    else {
        std::string ret;
        char* str = g_strdup_printf("$%lu\r\n%s\r\n", it->second.size(), it->second.c_str());
        ret = str;
        g_free(str);
        return ret;
    }
}

static std::string set_command(struct redisReply* reply)
{
    assert(reply->type = REDIS_REPLY_ARRAY);
    assert(reply->elements > 0);
    char buffer[256];

    if (reply->elements != 3) {
        LOG_WARN("wrong elements %lu", reply->elements);
        int n = snprintf(buffer, sizeof(buffer), shared::wrong_number_arguments, "set");
        return std::string(buffer, n);
    }

    if (reply->element[1]->type != REDIS_REPLY_STRING || reply->element[2]->type != REDIS_REPLY_STRING) {
        LOG_WARN("wrong type %d", reply->element[1]->type);
        return shared::wrong_type;
    }
    std::string key(reply->element[1]->str, reply->element[1]->len);
    std::string value(reply->element[2]->str, reply->element[2]->len);

    std::lock_guard<std::mutex> lock(g_key_mut);

    g_keys[key] = value;
    return shared::ok;
}

namespace shared
{

std::unordered_map<std::string, CommandCallback> g_command_table = {
    {"get", get_command},
    {"GET", get_command},
    {"set", set_command},
    {"SET", set_command},
};

}

