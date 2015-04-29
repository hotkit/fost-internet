/*
    Copyright 2015, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#include "fost-inet.hpp"
#include <fost/server.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>


using namespace fostlib;
namespace asio = boost::asio;


struct network_connection::server::state {
    boost::asio::io_service io_service;
    /// Set to true when we want the message pump to stop
    std::atomic<bool> stop;
    /// Thread to run all of the IO tasks in
    std::thread io_worker;
    /// The accept socket itself
    boost::asio::ip::tcp::acceptor ipv4_listener;
    boost::asio::ip::tcp::acceptor ipv6_listener;

    /// The server callback
    std::function<void(network_connection)> callback;

    state(const host &h, uint16_t p, std::function<void(network_connection)> fn)
    : stop(false), ipv4_listener(io_service), ipv6_listener(io_service), callback(fn) {
        // Report aborts
        asio::ip::tcp::endpoint endpoint(h.address(), p);
        if ( endpoint.protocol() == asio::ip::tcp::v4() ) {
            ipv4_listener.open(endpoint.protocol());
            ipv4_listener.set_option(asio::socket_base::enable_connection_aborted(true));
            ipv4_listener.bind(endpoint);
            ipv4_listener.listen();
            asio::ip::tcp::endpoint ipv6_end(host("::1").address(), p);
            ipv6_listener.open(ipv6_end.protocol());
            ipv6_listener.set_option(asio::socket_base::enable_connection_aborted(true));
            ipv6_listener.bind(ipv6_end);
            ipv6_listener.listen();
        } else {
            throw exceptions::not_implemented("IPv6 binding");
        }
        post_handler(ipv4_listener);
        post_handler(ipv6_listener);

        // Spin up the threads that are going to handle processing
        std::mutex mutex;
        std::unique_lock<std::mutex> lock(mutex);
        std::condition_variable signal;
        io_worker = std::move(std::thread([this, &mutex, &signal]() {
            std::unique_lock<std::mutex> lock(mutex);
            lock.unlock();
            std::cout << "Signalling that io_service is running" << std::endl;
            signal.notify_one();
            bool again = false;
            do {
                again = false;
                try {
                    io_service.run();
                    if ( !stop ) {
                        std::cout << "Run out of work, going again" << std::endl;
                        again = true;
                        post_handler(ipv4_listener);
                        post_handler(ipv6_listener);
                    }
                } catch ( std::exception &e ) {
                    again = true;
                    std::cout << "Caught " << e.what() << std::endl;
                } catch ( ... ) {
                    again = true;
                    std::cout << "Unknown exception caught" << std::endl;
                }
            } while ( again );
            std::cout << "Service thread stopping" << std::endl;
        }));
        signal.wait(lock);
        std::cout << "Start up of server complete" << std::endl;
    }

    ~state() {
        std::cout << "Server tear down requested" << std::endl;
        stop = true;
        io_service.stop();
        io_worker.join();
    }

    void post_handler(boost::asio::ip::tcp::acceptor&listener) {
        std::cout << "Going to listen for another connect" << std::endl;
        /*
        * Socket handling is awkward. It's lifetime must at least match the accept handler
        * This code assumes there is only a single accept handler that is waiting at any time
        * and therefore the socket at this can easily leak.
        * With C++14 we'll be able to capture the socket using std::move in the closure, but
        * C++11 makes that awkward.
        */
        // TODO: Change to std::move captured in the closure in C++14
        asio::ip::tcp::socket *socket(new asio::ip::tcp::socket(io_service));
        auto handler = [this, socket, &listener](const boost::system::error_code& error) {
            std::cout << "Got a connect " << error << std::endl;
            if ( !error ) {
                try {
                    callback(network_connection(io_service,
                        std::unique_ptr<asio::ip::tcp::socket>(socket)));
                } catch ( exceptions::exception &e ) {
                    std::cout << "Callback handler caught " << e << std::endl;
                } catch ( std::exception &e ) {
                    std::cout << "Callback handler caught " << e.what() << std::endl;
                } catch ( ... ) {
                    std::cout << "Caught an unkown exception" << std::endl;
                }
            }
            post_handler(listener);
        };
        listener.async_accept(*socket, handler);
    }
};


network_connection::server::server(const host &h, uint16_t p,
        std::function<void(network_connection)> fn)
: pimpl(new state(h, p, fn)) {
}


network_connection::server::~server() {
}

