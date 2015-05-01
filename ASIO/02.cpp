#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#include <boost/asio.hpp>


int main() {
    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);
    std::condition_variable signal;

    boost::asio::io_service service;

    std::thread server([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        boost::asio::ip::tcp::acceptor listener(service);
        listener.open(boost::asio::ip::tcp::v4());
        listener.set_option(boost::asio::socket_base::enable_connection_aborted(true));
        listener.bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 4567));
        listener.listen();
        boost::asio::ip::tcp::socket socket(service);
        listener.async_accept(socket, [](const boost::system::error_code& error) {
            std::cout << "Got a connection " << error << std::endl;
        });
        signal.notify_one();
        std::cout << "Server set up" << std::endl;
    });
    signal.wait(lock);

    std::thread client([&]() {
        boost::asio::ip::tcp::endpoint address(boost::asio::ip::address_v4(0ul), 4567);
        boost::asio::ip::tcp::socket socket(service);
        socket.async_connect(address, [](const boost::system::error_code& error) {
            std::cout << "Connected " << error << std::endl;
        });
        signal.notify_one();
        std::cout << "Client set up" << std::endl;
    });
    signal.wait(lock);

    server.join();
    client.join();
    return 0;
}

