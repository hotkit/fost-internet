#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#include <boost/asio.hpp>


namespace {
    std::ostream &log_thread() {
        return std::cout << std::this_thread::get_id() << " ";
    }
}


int main() {
    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);
    std::condition_variable signal;

    boost::asio::io_service service;

    boost::asio::ip::tcp::acceptor listener(service);
    boost::asio::ip::tcp::socket server_socket(service);
    std::thread server([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        listener.open(boost::asio::ip::tcp::v4());
        listener.set_option(boost::asio::socket_base::enable_connection_aborted(true));
        listener.bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 45673));
        listener.listen();
        listener.async_accept(server_socket, [](const boost::system::error_code& error) {
            log_thread() << "Got a connection " << error << std::endl;
        });
        signal.notify_one();
        log_thread() << "Server set up" << std::endl;
    });
    signal.wait(lock);

    boost::asio::ip::tcp::socket client_socket(service);
    std::thread client([&]() {
        boost::asio::ip::tcp::endpoint address(boost::asio::ip::address_v4(0ul), 45673);
        client_socket.async_connect(address, [](const boost::system::error_code& error) {
            log_thread() << "Connected " << error << std::endl;
        });
        signal.notify_one();
        log_thread() << "Client set up" << std::endl;
    });
    signal.wait(lock);

    std::thread io([&]() {
        log_thread() << "About to service IO requests" << std::endl;
        service.run();
        log_thread() << "Service jobs all run" << std::endl;
        signal.notify_one();
    });
    signal.wait(lock);

    io.join();
    client.join();
    server.join();
    return 0;
}

