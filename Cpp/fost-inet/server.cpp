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
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>


using namespace fostlib;
namespace asio = boost::asio;


namespace {
    auto started(std::chrono::steady_clock::now());
    std::ostream &log_thread() {
        return std::cout
            << std::setprecision(6)
                <<(10. + (std::chrono::steady_clock::now() - started).count()  / 1e9) << " "
            << std::this_thread::get_id() << " ";
    }
}


struct network_connection::server::state {
    boost::asio::io_service io_service;
    /// Set to true when we want the message pump to stop
    std::atomic<bool> stop;
    /// Threads to run all of the IO tasks in
    std::vector<std::thread> io_workers;
    /// The accept socket itself
    boost::asio::ip::tcp::acceptor ipv4_listener;
    boost::asio::ip::tcp::acceptor ipv6_listener;

    /// The server callback
    std::function<void(network_connection)> callback;

    state(const host &h, uint16_t p,
            std::function<void(network_connection)> fn, const std::size_t threads)
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

        io_workers.reserve(threads);
        for ( std::size_t thread{}; thread != threads; ++thread ) {
            log_thread() << "Kicking off server thread " << thread << std::endl;
            io_workers.emplace_back([this]() {
                log_thread() << "Server io_service is about to run" << std::endl;
                bool again = false;
                do {
                    again = false;
                    try {
                        io_service.run();
                        if ( !stop ) {
                            log_thread() << "Run out of work, going again" << std::endl;
                            again = true;
                            post_handler(ipv4_listener);
                            post_handler(ipv6_listener);
                        }
                    } catch ( std::exception &e ) {
                        again = true;
                        log_thread() << "**** Caught " << e.what() << std::endl;
                    } catch ( ... ) {
                        again = true;
                        log_thread() << "Unknown exception caught" << std::endl;
                    }
                } while ( again );
                log_thread() << "Service thread stopping" << std::endl;
            });
        }
        log_thread() << "Start up of server complete" << std::endl;
    }

    ~state() {
        log_thread() << "Server tear down requested" << std::endl;
        stop = true;
        io_service.stop();
        std::for_each(io_workers.begin(), io_workers.end(), [](std::thread& t) { t.join(); });
    }

    void post_handler(boost::asio::ip::tcp::acceptor&listener) {
        log_thread() << "Going to listen for another connect" << std::endl;
        /*
            Socket handling is awkward. It's lifetime must at least match the accept handler
            This code assumes there is only a single accept handler that is waiting at any time
            and therefore the socket at this can easily leak.
            With C++14 we'll be able to capture the socket using std::move in the closure, but
            C++11 makes that awkward.
        */
        // TODO: Change to std::move captured in the closure in C++14
        asio::ip::tcp::socket *socket(new asio::ip::tcp::socket(io_service));
        auto handler = [this, socket, &listener](const boost::system::error_code& error) {
            log_thread() << "Got a connect " << error << std::endl;
            post_handler(listener); // Post the replacement handler ASAP
            if ( !error ) {
                try {
                    callback(network_connection(io_service,
                        std::unique_ptr<asio::ip::tcp::socket>(socket)));
                } catch ( exceptions::exception &e ) {
                    log_thread() << "Callback handler caught " << e << std::endl;
                } catch ( std::exception &e ) {
                    log_thread() << "Callback handler caught " << e.what() << std::endl;
                } catch ( ... ) {
                    log_thread() << "Callback handler caught an unkown exception" << std::endl;
                }
            }
        };
        listener.async_accept(*socket, handler);
    }
};


network_connection::server::server(const host &h, uint16_t p,
        std::function<void(network_connection)> fn, std::size_t threads)
: pimpl(new state(h, p, fn, threads)) {
}


network_connection::server::~server() {
}

