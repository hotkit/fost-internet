#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#include <boost/asio.hpp>


int main() {
    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);
    std::condition_variable signal;

    std::thread server([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        boost::asio::io_service service;
        boost::asio::ip::tcp::acceptor listener(service);
        listener.open(boost::asio::ip::tcp::v4());
        listener.set_option(boost::asio::socket_base::enable_connection_aborted(true));
        listener.bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 45671));
        listener.listen();
        signal.notify_one();
        std::cout << "Server set up" << std::endl;
    });
    signal.wait(lock);

    std::thread client([]() {
    });

    server.join();
    client.join();
    return 0;
}

