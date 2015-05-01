#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

#include <boost/asio.hpp>


namespace {
    auto started(std::chrono::steady_clock::now());
    std::ostream &log_thread() {
        return std::cout
            << std::setprecision(6)
                <<(10. + (std::chrono::steady_clock::now() - started).count()  / 1e9) << " "
            << std::this_thread::get_id() << " ";
    }
}


int main() {
    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);
    std::condition_variable signal;

    boost::asio::io_service server_service;
    boost::asio::ip::tcp::acceptor listener(server_service);
    boost::asio::ip::tcp::socket server_socket(server_service);
    boost::asio::streambuf server_buffer;
    std::thread server([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        listener.open(boost::asio::ip::tcp::v4());
        listener.set_option(boost::asio::socket_base::enable_connection_aborted(true));
        listener.bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 4567));
        listener.listen();

        std::mutex read_mutex;
        std::unique_lock<std::mutex> read_lock(read_mutex);
        std::condition_variable read_done;
        listener.async_accept(server_socket, [&](const boost::system::error_code& error) {
            log_thread() << "Server got a connection " << error << std::endl;
            boost::asio::async_read_until(server_socket, server_buffer, '\n',
                [&](const boost::system::error_code& error, std::size_t bytes) {
                    std::unique_lock<std::mutex> lock(read_mutex);
                    log_thread() << "Got " << bytes << ", " << error << std::endl;
                    lock.unlock();
                    read_done.notify_one();
                });
        });
        lock.unlock();
        signal.notify_one();
        if ( read_done.wait_for(read_lock, std::chrono::seconds(2)) == std::cv_status::timeout ) {
            log_thread() << "Server read timed out" << std::endl;
            server_socket.close();
        } else {
            log_thread() << "Server data read" << std::endl;
        }
    });
    signal.wait(lock);
    log_thread() << "Server set up" << std::endl;

    boost::asio::io_service client_service;
    boost::asio::ip::tcp::socket client_socket(client_service);
    std::thread client([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        boost::asio::ip::tcp::endpoint address(boost::asio::ip::address_v4(0ul), 4567);
        client_socket.async_connect(address, [](const boost::system::error_code& error) {
            log_thread() << "Connected " << error << std::endl;
        });
        lock.unlock();
        signal.notify_one();
    });
    signal.wait(lock);
    log_thread() << "Client set up" << std::endl;

    std::thread server_io([&]() {
        log_thread() << "About to service IO requests" << std::endl;
        server_service.run();
        log_thread() << "Service jobs all run" << std::endl;
        signal.notify_one();
    });
    if ( signal.wait_for(lock, std::chrono::seconds(10)) == std::cv_status::timeout ) {
        log_thread() << "IO thread timed out servicing requests -- stopping it"
            "\n^^^ This should not have happen because the server should have timed out" << std::endl;
        server_service.stop();
        exit(1);
    }

    server_io.join();
    client.join();
    server.join();
    return 0;
}

