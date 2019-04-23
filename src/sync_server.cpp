#include <stdio.h>
#include <glib.h>
#include <memory>
#include <thread>
#include <unistd.h>
#include "common.h"
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <atomic>
#include <vector>
#include <hiredis/hiredis.h>
#include "ByteBuffer.h"


#define RECEIVE_BUFFER_SIZE (1024 * 4)

static std::atomic<uint32_t> g_connections(0);

class Session
{
public:
    explicit Session(int fd)
        : fd_(fd),
          read_pos_(0),
          reader_(redisReaderCreate())
    {
        g_connections++;
    }

    ~Session()
    {
        --g_connections;
        ::close(fd_);
        redisReaderFree(reader_);

    }

    void run()
    {
        while (true) {
            read_pos_ = 0;
            uint32_t left = RECEIVE_BUFFER_SIZE - read_pos_;

            int n = ::recv(fd_, recv_buff_ + read_pos_, left, 0);
            if (n == 0) {
                break;
            }

            read_pos_ += n;

            handle_read();

            if (!send_buff_.empty()) {
                int ret = flush_send();
                if (ret != send_buff_.size()) {
                    LOG_DEBUG("flush send error %s", strerror(errno));
                }
                send_buff_.clear();
            }
        }
    }

    void handle_read()
    {
        uint8_t* start = recv_buff_;
        uint8_t* end = recv_buff_ + read_pos_;
        uint32_t bytes = read_pos_;

        int err = REDIS_OK;
        std::vector<struct redisReply*> replays;

        while (start < end) {
            uint8_t* p = (uint8_t*) memchr(start, '\n', bytes);
            if (!p) {
                break;
            }

            size_t n = p + 1 - start;
            err = redisReaderFeed(reader_, (const char*) start, n);
            if (err != REDIS_OK) {
                LOG_DEBUG("redis protocol error %d, %s", err, reader_->errstr);
                break;
            }

            struct redisReply* reply = NULL;
            err = redisReaderGetReply(reader_, (void**) &reply);
            if (err != REDIS_OK) {
                LOG_DEBUG("redis protocol error %d, %s", err, reader_->errstr);
                break;
            }
            if (reply) {
                replays.push_back(reply);
            }

            start += n;
            bytes -= n;
        }
        if (err == REDIS_OK) {
            for (struct redisReply* reply : replays) {
                on_redis_reply(reply);
            }
        }

        for (struct redisReply* reply : replays) {
            freeReplyObject(reply);
        }
    }

    void on_redis_reply(struct redisReply* reply)
    {
        char buffer[256];
        if (reply->type != REDIS_REPLY_ARRAY) {
            LOG_WARN("wrong type %d", reply->type);
            send_reply(shared::wrong_type, strlen(shared::wrong_type));
            return;
        }

        if (reply->elements < 1) {
            LOG_WARN("wrong elements %lu", reply->elements);
            int n = snprintf(buffer, sizeof(buffer), shared::wrong_number_arguments, "");
            send_reply(buffer, n);
            return;
        }

        if (reply->element[0]->type != REDIS_REPLY_STRING) {
            LOG_WARN("wrong type %d", reply->element[0]->type);
            send_reply(shared::wrong_type, strlen(shared::wrong_type));
            return;
        }

        std::string command(reply->element[0]->str, reply->element[0]->len);
        auto it = shared::g_command_table.find(command);
        if (it == shared::g_command_table.end()) {
            int n = snprintf(buffer, sizeof(buffer), shared::unknown_command, command.c_str());
            send_reply(buffer, n);
            return;
        }
        shared::CommandCallback& cb = it->second;
        std::string str = cb(reply);
        send_reply(str.data(), str.size());
    }

    void send_reply(const char* data, uint32_t len)
    {
        send_buff_.insert(send_buff_.end(), data, data + len);
    }

    int flush_send()
    {
        int pos = 0;
        while (pos < send_buff_.size()) {
            uint8_t* p = send_buff_.data() + pos;
            uint32_t left = send_buff_.size() - pos;

            int n = ::send(fd_, p, left, 0);
            if (n == 0) {
                LOG_DEBUG("connection close");
                return 0;
            }

            if (n < 0) {
                LOG_ERROR("send error %s", strerror(errno));
            }

            pos += n;
        }

        return pos;
    }


private:

    int fd_;
    uint8_t recv_buff_[RECEIVE_BUFFER_SIZE];
    uint32_t read_pos_;
    redisReader* reader_;
    std::vector<uint8_t> send_buff_;
};
typedef std::shared_ptr<Session> SessionPtr;

class Server
{
public:
    explicit Server(int port)
        : port_(port),
          server_(0)
    {
    }

    ~Server()
    {
        if (server_) {
            close(server_);
        }
    }

    void run()
    {
        int server_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_ == -1) {
            LOG_ERROR("socket error %s", strerror(errno));
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        int ret = bind(server_, (struct sockaddr*) &addr, sizeof(addr));

        if (ret != 0) {
            LOG_ERROR("bind error %s", strerror(errno));
        }

        if (::listen(server_, SOMAXCONN) < 0) {
            LOG_DEBUG("lisetn error %s", strerror(errno));
        }

        while (true) {
            struct sockaddr_in in_addr;
            socklen_t len = sizeof(in_addr);
            memset(&in_addr, 0, len);

            int cli = ::accept4(server_, (struct sockaddr*) &in_addr, &len, SOCK_CLOEXEC);
            if (cli < 0) {
                LOG_ERROR("accept4 error %s", strerror(errno));
            }

            SessionPtr session(new Session(cli));
            std::thread t([session]() {
                session->run();
            });

            t.detach();

        }
    }

private:

    int server_;
    int port_;
};
typedef std::shared_ptr<Server> ServerPtr;

int main(int argc, char* argv[])
{
    int port = 4097;

    GOptionEntry entries[] = {
        {"port", '0', 0, G_OPTION_ARG_INT, &port, "port ", NULL},
        {NULL}
    };

    GError* error = NULL;
    GOptionContext* context = g_option_context_new("usage");
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        fprintf(stderr, "option parsing failed: %s\n", error->message);
        exit(EXIT_FAILURE);
    }

    ServerPtr server(new Server(port));
    std::thread t([server]() {
        server->run();
    });
    t.detach();

    LOG_DEBUG("port %d", port);

    while (true) {
        sleep(1);
        uint32_t n = g_connections;
        LOG_DEBUG("connections %u", n);
    }
}

