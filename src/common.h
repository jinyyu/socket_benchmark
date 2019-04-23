#pragma once
#include <stdint.h>
#include <hiredis/hiredis.h>
#include <functional>
#include <unordered_map>
#include <stdio.h>
#include <string.h>

#ifdef DEBUG
#define LOG_DEBUG(format, ...) { fprintf(stderr, "DEBUG [%s:%d] " format "\n", strrchr(__FILE__, '/') + 1, __LINE__, ##__VA_ARGS__); }
#else
#define LOG_DEBUG(format, ...)
#endif

#define LOG_WARN(format, ...) { fprintf(stderr, "WARN [%s:%d] " format "\n", strrchr(__FILE__, '/') + 1, __LINE__, ##__VA_ARGS__); }


uint64_t timestamp_now();

namespace shared
{

static const char* ok = "+OK\r\n";
static const char* err = "-ERR %s\r\n";
static const char* wrong_type = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
static const char* unknown_command = "-ERR unknown command `%s`\r\n";
static const char* wrong_number_arguments = "-ERR wrong number of arguments for '%s' command\r\n";
static const char* pong = "+PONG\r\n";
static const char* null = "$-1\r\n";

typedef std::function<std::string(struct redisReply* reply)> CommandCallback;
extern std::unordered_map<std::string, CommandCallback> g_command_table;

}