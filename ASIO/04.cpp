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
    boost::asio::streambuf server_buffer;
    std::thread server([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        listener.open(boost::asio::ip::tcp::v4());
        listener.set_option(boost::asio::socket_base::enable_connection_aborted(true));
        listener.bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 45674));
        listener.listen();
        listener.async_accept(server_socket, [&](const boost::system::error_code& error) {
            log_thread() << "Got a connection " << error << std::endl;
            boost::asio::async_read_until(server_socket, server_buffer, '\n',
                [&](const boost::system::error_code& error, std::size_t bytes) {
                    log_thread() << "Got " << bytes << ", " << error << std::endl;
                });
        });
        signal.notify_one();
        log_thread() << "Server set up" << std::endl;
    });
    signal.wait(lock);

    boost::asio::ip::tcp::socket client_socket(service);
    std::thread client([&]() {
        boost::asio::ip::tcp::endpoint address(boost::asio::ip::address_v4(0ul), 45674);
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
    if ( signal.wait_for(lock, std::chrono::seconds(1)) == std::cv_status::timeout ) {
        log_thread() << "IO thread timed out servicing requests -- stopping it" << std::endl;
        service.stop();
    }

    io.join();
    client.join();
    server.join();
    return 0;
}

