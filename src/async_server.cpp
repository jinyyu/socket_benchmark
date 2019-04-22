#include <stdio.h>
#include <boost/asio.hpp>
#include <stdio.h>
#include <glib.h>
#include <stdint.h>
#include <memory>
#include <thread>
#include "ByteBuffer.h"


#ifdef DEBUG
#define LOG_DEBUG(format, ...) { fprintf(stderr, "DEBUG [%s:%d] " format "\n", strrchr(__FILE__, '/') + 1, __LINE__, ##__VA_ARGS__); }
#else
#define LOG_DEBUG(format, ...)
#endif


struct Session: public std::enable_shared_from_this<Session>
{
    explicit Session(boost::asio::io_service& io_service)
        : socket_(io_service)
    {
        read_buffer_.resize(4);
    }

    ~Session()
    {
        LOG_DEBUG("session closed");
    }

    void read_size()
    {
        auto self = shared_from_this();
        auto buffer = boost::asio::buffer(read_buffer_.data(), read_buffer_.size());
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
            memcpy(&len, self->read_buffer_.data(), 4);

            len = ntohl(len);
            LOG_DEBUG("len = %u", len)
            self->read_data(len);

        };
        boost::asio::async_read(socket_, buffer, boost::asio::transfer_exactly(4), std::move(handler));
    }

    void read_data(uint32_t bytes)
    {
        if (read_buffer_.size() < bytes) {
            read_buffer_.resize(bytes);
        }

        auto self = shared_from_this();
        auto buffer = boost::asio::buffer(read_buffer_.data(), read_buffer_.size());
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
            self->read_size();
        };
        boost::asio::async_read(socket_, buffer, boost::asio::transfer_exactly(bytes), std::move(handler));
    }

    boost::asio::ip::tcp::socket socket_;
    std::vector<uint8_t> read_buffer_;
    ByteBuffer send_buffer_;
};
typedef std::shared_ptr<Session> SessionPtr;

class Server
{
public:
    explicit Server(int port, int threads)
        : port_(port),
          next_index_(0),
          services_(threads),
          acceptor_(service_)
    {
        LOG_DEBUG("threads %d, port %d", threads, port);
    }

    ~Server()
    {

    }

    void run()
    {
        for (size_t i = 0; i < services_.size(); ++i) {

            std::thread t([i, this] {
                services_[i].run();
            });

            t.detach();

        }

        auto address = boost::asio::ip::address::from_string("0.0.0.0");
        auto endpoint = boost::asio::ip::tcp::endpoint(address, port_);

        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(1));
        acceptor_.bind(endpoint);
        acceptor_.listen();

        start_accept();

        service_.run();
    }


private:
    void start_accept()
    {
        int index = next_index_++ % services_.size();

        SessionPtr session(new Session(services_[index]));

        acceptor_.async_accept(session->socket_, [this, session](const boost::system::error_code& error) {
            if (error) {
                LOG_DEBUG("accept error %s", error.message().c_str());
                return;
            }
            this->start_accept();
            session->read_size();
        });
    }

    int port_;
    int next_index_;
    std::vector<boost::asio::io_service> services_;
    boost::asio::io_service service_;
    boost::asio::ip::tcp::acceptor acceptor_;
};

typedef std::shared_ptr<Server> ServerPtr;

int main(int argc, char* argv[])
{

    static int threads = 8;

    GOptionEntry entries[] = {
        {"threads", 't', 0, G_OPTION_ARG_INT, &threads, "threads ", NULL},
        {NULL}
    };

    GError* error = NULL;
    GOptionContext* context = g_option_context_new("usage");
    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        fprintf(stderr, "option parsing failed: %s\n", error->message);
        exit(EXIT_FAILURE);
    }

    ServerPtr server(new Server(4096, threads));
    std::thread t([server]() {
        server->run();
    });
    t.detach();


    while (true) {
        ::sleep(1);
    }
}

