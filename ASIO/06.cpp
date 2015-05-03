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


struct connection {
    boost::asio::ip::tcp::socket socket;
    boost::asio::streambuf buffer;
    connection(boost::asio::io_service &service)
    : socket(service) {
    }
    ~connection() {
        log_thread() << "~connection run" << std::endl;
    }
};


int main() {
    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);
    std::condition_variable signal;

    boost::asio::io_service server_service;
    boost::asio::ip::tcp::acceptor listener(server_service);
    std::mutex read_mutex;
    std::condition_variable read_done;
    std::thread server([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        listener.open(boost::asio::ip::tcp::v4());
        listener.set_option(boost::asio::socket_base::enable_connection_aborted(true));
        listener.bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 45676));
        listener.listen();

        std::unique_lock<std::mutex> read_lock(read_mutex);
        std::shared_ptr<connection> server_cnx(new connection(server_service));
        listener.async_accept(server_cnx->socket,
            [&, server_cnx](const boost::system::error_code& error) mutable {
                log_thread() << "Server got a connection " << error << std::endl;
                boost::asio::async_read_until(server_cnx->socket, server_cnx->buffer, '\n',
                    [&, server_cnx](const boost::system::error_code& error, std::size_t bytes) mutable {
                        log_thread() << "Got " << bytes << " bytes, " << error
                            << ". server_cnx use count: " << server_cnx.use_count() << std::endl;
                        std::unique_lock<std::mutex> lock(read_mutex);
                        lock.unlock();
                        read_done.notify_one();
                    });
            });
        lock.unlock();
        signal.notify_one();
        if ( read_done.wait_for(read_lock, std::chrono::seconds(1)) == std::cv_status::timeout ) {
            log_thread() << "Server read timed out -- cancelling socket jobs. "
                "server_cnx use count: " << server_cnx.use_count() << std::endl;
            server_cnx->socket = boost::asio::ip::tcp::socket(server_service);
        } else {
            log_thread() << "Server data read" << std::endl;
        }
        log_thread() << "Exiting server thread" << std::endl;
    });
    signal.wait(lock);
    log_thread() << "Server set up" << std::endl;

    boost::asio::io_service client_service;
    boost::asio::ip::tcp::socket client_socket(client_service);
    std::thread client([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        boost::asio::ip::tcp::endpoint address(boost::asio::ip::address_v4(0ul), 45676);
        client_socket.async_connect(address, [](const boost::system::error_code& error) {
            log_thread() << "Connected " << error << std::endl;
        });
        lock.unlock();
        signal.notify_one();
    });
    signal.wait(lock);
    log_thread() << "Client set up" << std::endl;

    std::thread server_io([&]() {
        log_thread() << "About to service server IO requests" << std::endl;
        try {
            const std::size_t jobs = server_service.run();
            log_thread() << "Server service has run " << jobs << " jobs" << std::endl;
        } catch ( ... ) {
            log_thread() << "Exception caught" << std::endl;
        }
        log_thread() << "**** Service jobs all run" << std::endl;
        signal.notify_one();
    });
    if ( signal.wait_for(lock, std::chrono::seconds(10)) == std::cv_status::timeout ) {
        log_thread() << "IO thread timed out servicing requests -- stopping it"
            "\n^^^ This should not happen because the server service "
                "should have run out of work" << std::endl;
        server_service.stop();
        log_thread() << "Waiting for things to close...." << std::endl;
        sleep(2);
        log_thread() << "Wait over, exiting. server_service is "
            << (server_service.stopped() ? "stopped" : "running") << std::endl;
        exit(1);
    }

    server_io.join();
    client.join();
    server.join();
    return 0;
}

