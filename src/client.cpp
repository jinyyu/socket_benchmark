#include <stdio.h>
#include <glib.h>
#include <stdint.h>
#include <memory>
#include <boost/asio.hpp>
#include <thread>
#include <boost/algorithm/string.hpp>
#include <atomic>
#include "common.h"
#include "ByteBuffer.h"

#ifdef DEBUG
#define LOG_DEBUG(format, ...) { fprintf(stderr, "DEBUG [%s:%d] " format "\n", strrchr(__FILE__, '/') + 1, __LINE__, ##__VA_ARGS__); }
#else
#define LOG_DEBUG(format, ...)
#endif

static std::atomic<uint32_t> g_connections(0);

class Session: public std::enable_shared_from_this<Session>
{
public:
    explicit Session(boost::asio::io_service& io_service, const boost::asio::ip::tcp::endpoint& ep, int msg_size)
        : socket_(io_service),
          endpoint_(ep),
          msg_size_(msg_size)
    {
        recv_buff_.resize(4 + 8 + msg_size);
        g_connections++;
    }

    ~Session()
    {
        LOG_DEBUG("session closed");
        g_connections--;
    }

    void start_connect()
    {
        auto self = shared_from_this();
        socket_.async_connect(endpoint_, [self](const boost::system::error_code& err) {
            if (err) {
                LOG_DEBUG("connect error %s", err.message().c_str());
                return;
            }

            self->send_msg();
        });
    }

    void send_msg()
    {
        uint64_t now = timestamp_now();
        uint32_t total_len = 8 + msg_size_;
        total_len = htonl(total_len);

        send_buff_.put((const uint8_t*) &total_len, 4);
        send_buff_.put((const uint8_t*) &now, 8);

        send_buff_.put(recv_buff_.data(), msg_size_);

        start_async_send();
    }

    void start_async_send()
    {
        if (!send_buff_.readable()) {
            LOG_DEBUG("send finished");
            read_size();
            return;
        }


        auto self = shared_from_this();
        uint32_t remaining = send_buff_.readable_bytes();
        auto buffer = boost::asio::buffer(send_buff_.reader(), remaining);
        auto handler = [self](const boost::system::error_code& error, std::size_t bytes) {
            if (error || bytes == 0) {
                LOG_DEBUG("send error %s", error.message().c_str());
                return;
            }

            LOG_DEBUG("------------------- %lu", bytes);
            self->send_buff_.read_bytes(bytes);
            self->start_async_send();
        };
        boost::asio::async_write(socket_, buffer, handler);
    }

    void read_size()
    {
        auto self = shared_from_this();
        auto buffer = boost::asio::buffer(recv_buff_.data(), recv_buff_.size());
        auto handler = [self](const boost::system::error_code& error, size_t bytes) {
            if (bytes == 0) {
                return;
            }
            if (error) {
                LOG_DEBUG("read error %s", error.message().c_str());
                return;
            }

            if (bytes != 4) {
                LOG_DEBUG("invalid data");
                return;
            }

            uint32_t len;
            memcpy(&len, self->recv_buff_.data(), 4);

            len = ntohl(len);
            self->read_data(len);

        };
        boost::asio::async_read(socket_, buffer, boost::asio::transfer_exactly(4), std::move(handler));
    }

    void read_data(uint32_t bytes)
    {
        assert(bytes == 8 + uint32_t(msg_size_));

        auto self = shared_from_this();
        auto buffer = boost::asio::buffer(recv_buff_.data(), recv_buff_.size());
        auto handler = [self, bytes](const boost::system::error_code& error, size_t n_read) {
            if (bytes == 0) {
                return;
            }
            if (error) {
                LOG_DEBUG("read error %s", error.message().c_str());
                return;
            }

            if (bytes != n_read) {
                LOG_DEBUG("invalid data");
                return;
            }
            self->send_msg();
        };
        boost::asio::async_read(socket_, buffer, boost::asio::transfer_exactly(bytes), std::move(handler));
    }

private:
    boost::asio::ip::tcp::socket socket_;
    const boost::asio::ip::tcp::endpoint& endpoint_;
    int msg_size_;
    ByteBuffer send_buff_;
    std::vector<uint8_t> recv_buff_;
};
typedef std::shared_ptr<Session> SessionPtr;

class Client
{
public:
    explicit Client(const std::string& addr, int sessions, int msg_size)
        : msg_size_(msg_size),
          sessions_(sessions)
    {
        std::vector<std::string> strs;
        boost::split(strs, addr, boost::is_any_of(":"));
        if (strs.size() != 2) {
            LOG_DEBUG("invalid host %s", addr.c_str());
            exit(0);
        }
        auto address = boost::asio::ip::address::from_string(strs[0]);
        int port = std::atoi(strs[1].c_str());
        endpoint_ = boost::asio::ip::tcp::endpoint(address, port);

    }

    void run()
    {

        for (int i = 0; i < sessions_; ++i) {
            SessionPtr session(new Session(service_, endpoint_, msg_size_));
            session->start_connect();
        }

        service_.run();
    }


private:
    int msg_size_;
    int sessions_;
    boost::asio::ip::tcp::endpoint endpoint_;
    boost::asio::io_service service_;

};
typedef std::shared_ptr<Client> ClientPtr;

int main(int argc, char* argv[])
{
    int sessions = 16;
    int msg_size = 100;
    int threads = 8;
    const char* addr = "192.168.147.134:4096";

    GOptionEntry entries[] = {
        {"sessions", 'c', 0, G_OPTION_ARG_INT, &sessions, "sessions", NULL},
        {"msg_size", 's', 0, G_OPTION_ARG_INT, &msg_size, "msg size", NULL},
        {"threads", 't', 0, G_OPTION_ARG_INT, &threads, "threads", NULL},
        {"addr", 'a', 0, G_OPTION_ARG_STRING, &addr, "address", NULL},
        {NULL}
    };

    GError* error = NULL;
    GOptionContext* context = g_option_context_new("usage");
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        fprintf(stderr, "option parsing failed: %s\n", error->message);
        exit(EXIT_FAILURE);
    }
    LOG_DEBUG("address %s, threads %d, sessions %d, msg size %d", addr, threads, sessions, msg_size);

    for (int i = 0; i < threads; ++i) {
        ClientPtr client(new Client(addr, sessions, msg_size));
        std::thread t([client]() {
            client->run();
        });
        t.detach();
    }

    while (true) {
        sleep(1);
        uint32_t n = g_connections;
        LOG_DEBUG("connections %u", n);

    }

}