#include <stdio.h>
#include <glib.h>
#include <stdint.h>
#include <memory>
#include <boost/asio.hpp>
#include <thread>
#include <boost/algorithm/string.hpp>

#ifdef DEBUG
#define LOG_DEBUG(format, ...) { fprintf(stderr, "DEBUG [%s:%d] " format "\n", strrchr(__FILE__, '/') + 1, __LINE__, ##__VA_ARGS__); }
#else
#define LOG_DEBUG(format, ...)
#endif


class Session: public std::enable_shared_from_this<Session>
{
public:
    explicit Session(boost::asio::io_service& io_service, const boost::asio::ip::tcp::endpoint& ep)
        : socket_(io_service),
          endpoint_(ep)
    {

    }

    void start_connect()
    {
        auto self = shared_from_this();
        socket_.async_connect(endpoint_, [self](const boost::system::error_code& err) {
            if (err) {
                LOG_DEBUG("connect error %s", err.message().c_str());
                return;
            }

            LOG_DEBUG("connected success");
        });
    }

    ~Session()
    {
        LOG_DEBUG("session closed");
    }

private:
    boost::asio::ip::tcp::socket socket_;
    const boost::asio::ip::tcp::endpoint& endpoint_;

};
typedef std::shared_ptr<Session> SessionPtr;

class Client
{
public:
    explicit Client(const std::string& addr, int sessions)
        : sessions_(sessions)
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

        for (int i =0; i < sessions_; ++i) {
            SessionPtr session(new Session(service_, endpoint_));
            session->start_connect();
        }

        service_.run();
    }


private:
    int sessions_;
    boost::asio::ip::tcp::endpoint endpoint_;
    boost::asio::io_service service_;

};
typedef std::shared_ptr<Client> ClientPtr;

int main(int argc, char* argv[])
{
    int sessions = 128;
    int msg_size = 100;
    int threads = 8;
    const char* addr = "127.0.0.1:4096";

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
        ClientPtr client(new Client(addr, sessions));
        std::thread t([client]() {
            client->run();
        });
        t.detach();
    }

    while (true) {
        sleep(1);

    }

}