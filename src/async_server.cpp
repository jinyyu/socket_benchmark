#include <stdio.h>
#include <boost/asio.hpp>
#include <stdio.h>
#include <glib.h>
#include <stdint.h>
#include <memory>
#include <thread>
#include "ByteBuffer.h"
#include <atomic>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <unordered_map>
#include "common.h"


#define RECEIVE_BUFFER_SIZE (1024 * 4)

static std::atomic<uint32_t> g_connections(0);
static std::atomic<uint64_t> g_read_bytes(0);
static std::atomic<uint64_t> g_write_bytes(0);

struct Session: public std::enable_shared_from_this<Session>
{
    explicit Session(boost::asio::io_service& io_service)
        : socket_(io_service),
          reader_(redisReaderCreate())
    {
        LOG_DEBUG("nenw session");
        g_connections++;
    }

    ~Session()
    {
        LOG_DEBUG("session closed");
        g_connections--;
        redisReaderFree(reader_);
    }

    void start()
    {
        auto self = shared_from_this();
        auto buffer = boost::asio::buffer(read_buffer_, RECEIVE_BUFFER_SIZE);
        auto handler = [self](const boost::system::error_code& error, size_t bytes) {
            if (bytes == 0) {
                return;
            }
            if (error) {
                LOG_WARN("read error %s", error.message().c_str());
                return;
            }

            g_read_bytes += bytes;
            self->handle_read(bytes);

        };
        socket_.async_read_some(buffer, std::move(handler));
    }

    void handle_read(size_t bytes)
    {
        uint8_t* start = read_buffer_;
        uint8_t* end = read_buffer_ + bytes;
        int err = REDIS_OK;
        std::vector<struct redisReply*> replies;

        while (start < end) {
            uint8_t* p = (uint8_t*) memchr(start, '\n', bytes);
            if (!p) {
                this->start();
                break;
            }

            size_t n = p + 1 - start;
            err = redisReaderFeed(reader_, (const char*) start, n);
            if (err != REDIS_OK) {
                LOG_WARN("redis protocol error %d, %s", err, reader_->errstr);
                break;
            }

            struct redisReply* reply = NULL;
            err = redisReaderGetReply(reader_, (void**) &reply);
            if (err != REDIS_OK) {
                LOG_WARN("redis protocol error %d, %s", err, reader_->errstr);
                break;
            }
            if (reply) {
                replies.push_back(reply);
            }

            start += n;
            bytes -= n;
        }
        if (err == REDIS_OK) {
            for (struct redisReply* reply : replies) {
                on_redis_reply(reply);
            }
            this->start();
        }

        for (struct redisReply* reply : replies) {
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
        uint32_t bytes = send_buffer_.readable_bytes();
        send_buffer_.put((uint8_t*) data, len);
        if (bytes == 0) {
            start_send();
        }
    }

    void start_send()
    {
        if (!send_buffer_.readable()) {
            return;
        }
        auto self = shared_from_this();
        uint32_t remaining = send_buffer_.readable_bytes();
        auto buffer = boost::asio::buffer(send_buffer_.reader(), remaining);
        auto handler = [self](const boost::system::error_code& error, std::size_t bytes) {
            if (bytes == 0) {
                return;;
            }
            if (error) {
                LOG_WARN("send error %s", error.message().c_str());
                return;
            }

            g_write_bytes += bytes;

            std::string str((const char*) self->send_buffer_.reader(), bytes);
            self->send_buffer_.read_bytes(bytes);
            self->start_send();
        };
        boost::asio::async_write(socket_, buffer, std::move(handler));
    }

    boost::asio::ip::tcp::socket socket_;
    redisReader* reader_;
    uint8_t read_buffer_[RECEIVE_BUFFER_SIZE];
    ByteBuffer send_buffer_;
};
typedef std::shared_ptr<Session> SessionPtr;

class Server
{
public:
    explicit Server(int port)
        : port_(port),
          acceptor_(service_)
    {

    }

    ~Server()
    {

    }

    void run()
    {

        auto address = boost::asio::ip::address::from_string("0.0.0.0");
        auto endpoint = boost::asio::ip::tcp::endpoint(address, port_);

        acceptor_.open(endpoint.protocol());

        int optval = 1;
        ::setsockopt(acceptor_.native_handle(), SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        ::setsockopt(acceptor_.native_handle(), SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

        acceptor_.bind(endpoint);
        acceptor_.listen();

        start_accept();

        service_.run();
    }


private:
    void start_accept()
    {
        SessionPtr session(new Session(service_));

        acceptor_.async_accept(session->socket_, [this, session](const boost::system::error_code& error) {
            if (error) {
                LOG_WARN("accept error %s", error.message().c_str());
                return;
            }
            this->start_accept();
            session->start();
        });
    }

    int port_;
    boost::asio::io_service service_;
    boost::asio::ip::tcp::acceptor acceptor_;
};

typedef std::shared_ptr<Server> ServerPtr;

int main(int argc, char* argv[])
{

    int threads = 1;
    int port = 4096;

    GOptionEntry entries[] = {
        {"threads", 't', 0, G_OPTION_ARG_INT, &threads, "threads ", NULL},
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

    LOG_INFO("threads %d, port %d", threads, port);


    for (int i = 0; i < threads; ++i) {
        ServerPtr server(new Server(port));
        std::thread t([server]() {
            server->run();
        });
        t.detach();
    }


    while (true) {
        g_read_bytes = 0;
        g_write_bytes = 0;

        ::sleep(1);

        uint64_t read_bytes = g_read_bytes;
        uint64_t write_bytes = g_write_bytes;

        int n = g_connections - threads;

        LOG_INFO("connections %u, read %lu MB, write %lu MB", n, read_bytes, write_bytes);
    }
}

