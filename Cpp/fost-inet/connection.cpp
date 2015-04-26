/*
    Copyright 2008-2015, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#ifdef FOST_OS_LINUX
    // Boost.ASIO checks a pointer for NULL which can never be NULL
    #pragma GCC diagnostic ignored "-Waddress"
#endif


#include "fost-inet.hpp"
#include <fost/connection.hpp>

#include <fost/insert>
#include <fost/datetime>
#include <fost/log>
#include <fost/timer>

#include <boost/asio/ssl.hpp>
#include <boost/lexical_cast.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>


using namespace fostlib;
namespace asio = boost::asio;


namespace {


    const setting< int64_t > c_connect_timeout(
        "fost-internet/Cpp/fost-inet/connection.cpp",
        "Network settings", "Connect time out", 10, true);
    const setting< int64_t > c_read_timeout(
        "fost-internet/Cpp/fost-inet/connection.cpp",
        "Network settings", "Read time out", 30, true);
    const setting< int64_t > c_large_read_chunk_size(
        "fost-internet/Cpp/fost-inet/connection.cpp",
        "Network settings", "Large read chunk size", 1024, true);
    const setting< json > c_socks_version(
        "fost-internet/Cpp/fost-inet/connection.cpp",
        "Network settings", "Socks version", json(), true);
    const setting< string > c_socks_host(
        "fost-internet/Cpp/fost-inet/connection.cpp",
        "Network settings", "Socks host", L"localhost:8888", true);


    /// Counter for each network connection to help with tracking
    std::atomic<int64_t> g_network_counter{};


    /// ASIO IO service for client connections
    asio::io_service g_client_service;
    /// Start a thread to run the service in
    std::thread client_thread([]() {
        asio::io_service::work work(g_client_service);
        atexit([]() {
            g_client_service.reset();
            client_thread.join();
        });
        g_client_service.run();
    });


}


struct ssl_data {
    ssl_data(
        asio::io_service &io_service, asio::ip::tcp::socket &sock
    ) : ctx(io_service, asio::ssl::context::sslv23_client), socket(sock, ctx) {
        socket.handshake(asio::ssl::stream_base::client);
    }

    asio::ssl::context ctx;
    asio::ssl::stream< asio::ip::tcp::socket& > socket;
};


struct network_connection::state {
    int64_t number;
    timer time;
    asio::io_service &io_service;
    std::unique_ptr<asio::ip::tcp::socket> socket;
    std::unique_ptr<ssl_data> ssl;

    asio::streambuf input_buffer;

    std::mutex mutex;
    std::condition_variable signal;
    int connect_timeout, read_timeout;

    state(
        asio::io_service &io_service,
        std::unique_ptr<asio::ip::tcp::socket > s
    ) : number(++g_network_counter), io_service(io_service),
            socket(std::move(s)),
            connect_timeout(coerce<int>(c_connect_timeout.value())),
            read_timeout(coerce<int>(c_read_timeout.value())) {
    }

    void start_ssl() {
        ssl.reset(new ssl_data(io_service, *socket));
    }


    void connect(const host &host, port_number port) {
        using namespace asio::ip;
        tcp::resolver resolver(io_service);
        tcp::resolver::query q(
            coerce<ascii_string>(host.name()).underlying(),
            coerce<ascii_string>(coerce<string>(port)).underlying());
        boost::system::error_code host_error;
        tcp::resolver::iterator endpoint = resolver.resolve(q, host_error), end;
        if ( host_error == asio::error::host_not_found ) {
            throw exceptions::host_not_found(host.name());
        }
        boost::system::error_code connect_error =
            asio::error::host_not_found;
        json errors;
        while ( connect_error && endpoint != end ) {
            std::unique_lock<std::mutex> lock(mutex);
            string ip(endpoint->endpoint().address().to_string());
            insert(errors, ip, "started", timestamp::now());
            socket->async_connect(*endpoint++,
                [this, &connect_error, &errors, &ip](
                    const boost::system::error_code &e
                ) {
                    std::unique_lock<std::mutex> lock(mutex);
                    insert(errors, ip, "elapsed", time.elapsed());
                    insert(errors, ip, "connected", timestamp::now());
                    connect_error = e;
                    lock.unlock();
                    signal.notify_one();
                });
            if ( signal.wait_for(lock, std::chrono::seconds(connect_timeout)) ==
                    std::cv_status::no_timeout ) {
                if ( not connect_error ) {
                    log::debug()
                        ("connection", number)
                        ("connected", errors);
                    return;
                }
            } else {
                insert(errors, ip, "error", "signal for connect timed out");
                insert(errors, ip, "failed", timestamp::now());
                insert(errors, ip, "elapsed", time.elapsed());
                insert(errors, ip, "timeout", connect_timeout);
                connect_error = asio::error::timed_out;
                socket->close();
            }
        }
        if ( connect_error ) {
            exceptions::connect_failure error(connect_error, host, port);
            insert(error.data(), "connection", number);
            insert(error.data(), "errors", errors);
            throw error;
        }
    }

    void check_error(const boost::system::error_code &error, nliteral message) {
        if ( error == asio::error::eof ) {
            socket->close();
            throw exceptions::unexpected_eof(message);
        } else if ( error ) {
            socket->close();
            throw exceptions::socket_error(error, message);
        }
    }

    template<typename B>
    std::size_t send(const B &b, nliteral message) {
        boost::system::error_code error{};
        std::unique_lock<std::mutex> lock(mutex);
        std::size_t sent{};
        auto handler = [this, &error, &sent](
            const boost::system::error_code &e, std::size_t bytes
        ) {
            std::unique_lock<std::mutex> lock(mutex);
            error = e;
            sent += bytes;
            lock.unlock();
            signal.notify_one();
        };
        if ( ssl ) {
            asio::async_write(ssl->socket, b, handler);
        } else {
            asio::async_write(*socket, b, handler);
        }
        signal.wait(lock); // Shouldn't need a time out on writes
        check_error(error, message);
        return sent;
    }

    template<typename F>
    std::vector<utf8> read(F condition, nliteral message) {
        // TODO: Try to replace explicit std::function with auto in C++14
        return do_read([this, condition](std::function<void(const boost::system::error_code&, std::size_t)> handler) {
            if ( ssl ) {
                asio::async_read(ssl->socket, input_buffer, condition, handler);
            } else {
                asio::async_read(*socket, input_buffer, condition, handler);
            }
        }, message);
    }
    template<typename F>
    std::vector<utf8> read_until(F condition, nliteral message) {
        // TODO: Try to replace explicit std::function with auto in C++14
        return do_read([this, condition](std::function<void(const boost::system::error_code&, std::size_t)> handler) {
            if ( ssl ) {
                asio::async_read_until(ssl->socket, input_buffer, condition, handler);
            } else {
                asio::async_read_until(*socket, input_buffer, condition, handler);
            }
        }, message);
    }

private:
    template<typename R>
    std::vector<utf8> do_read(R reader, nliteral message) {
        boost::system::error_code error{};
        std::size_t bytes_read{};
        std::unique_lock<std::mutex> lock(mutex);
        auto handler = [this, &error, &bytes_read](
            const boost::system::error_code &e, std::size_t bytes
        ) {
            std::unique_lock<std::mutex> lock(mutex);
            error = e;
            bytes_read += bytes;
            lock.unlock();
            signal.notify_one();
        };
        reader(handler);
        if ( signal.wait_for(lock, std::chrono::seconds(read_timeout)) ==
                std::cv_status::no_timeout ) {
            std::vector<utf8> data(bytes_read);
            input_buffer.sgetn(reinterpret_cast<char*>(data.data()), bytes_read);
            return data;
        } else {
            socket->close();
            throw exceptions::socket_error(asio::error::timed_out, message);
        }
    }
};


fostlib::network_connection::network_connection(
    asio::io_service &io_service, std::unique_ptr< asio::ip::tcp::socket > socket
) : pimpl(new state(io_service, std::move(socket))) {
}


fostlib::network_connection::network_connection(const host &h, nullable< port_number > p)
: pimpl(new state(g_client_service,
        std::unique_ptr<asio::ip::tcp::socket>(
            new asio::ip::tcp::socket(g_client_service)))) {
    const port_number port = p.value(coerce<port_number>(h.service().value("0")));
    json socks(c_socks_version.value());

    if ( !socks.isnull() ) {
        const host socks_host( coerce< host >( c_socks_host.value() ) );
        pimpl->connect(socks_host, coerce<port_number>(socks_host.service().value("0")));
        if ( c_socks_version.value() == json(4) ) {
            std::vector<unsigned char> buffer(9);
            // Build and send the command to establish the connection
            buffer[0] = 0x04; // SOCKS v4
            buffer[1] = 0x01; // stream
            buffer[2] = (port & 0xff00) >> 8; // MS byte for port
            buffer[3] = port & 0xff; // LS byte for port
            asio::ip::address_v4::bytes_type bytes(h.address().to_v4().to_bytes());
            for ( std::size_t p = 0; p < 4; ++p )
                buffer[4+p] = bytes[p];
            buffer[8] = 0x00; // User ID
            pimpl->send(asio::buffer(buffer), "Trying to establish SOCKS connection");
            // Receive the response
            std::vector<utf8> data{pimpl->read(asio::transfer_exactly(8), "Trying to read SOCKS response")};
            if ( data[0] != 0x00 || data[1] != 0x5a ) {
                throw exceptions::socket_error("SOCKS 4 error handling where the response values are not 0x00 0x5a");
            }
        } else {
            throw exceptions::socket_error("SOCKS version not implemented", coerce< string >(c_socks_version.value()));
        }
    } else {
        pimpl->connect(h, port);
    }
}


fostlib::network_connection::~network_connection() {
}


void fostlib::network_connection::start_ssl() {
    pimpl->start_ssl();
}


network_connection &fostlib::network_connection::operator << (const const_memory_block &p) {
    const unsigned char
        *begin = reinterpret_cast< const unsigned char * >( p.first ),
        *end =  reinterpret_cast< const unsigned char * >( p.second )
    ;
    std::size_t length = end - begin;
    if ( length ) {
        pimpl->send(asio::buffer(begin, length), "Sending memory block");
    }
    return *this;
}
network_connection &fostlib::network_connection::operator << (const utf8_string &s) {
    if ( not s.empty() ) {
        pimpl->send(asio::buffer(s.underlying().c_str(), s.underlying().length()),
            "Sending UTF8 string");
    }
    return *this;
}
network_connection &fostlib::network_connection::operator << (const std::stringstream &ss) {
    std::string s(ss.str());
    if ( not s.empty() ) {
        pimpl->send(asio::buffer(s.c_str(), s.length()), "Sending stringstream");
    }
    return *this;
}


network_connection &fostlib::network_connection::operator >> ( utf8_string &s ) {
    std::string next;
    (*this) >> next;
    s += utf8_string(next);
    return *this;
}
network_connection &fostlib::network_connection::operator >> (std::string &s) {
    static const std::string crlf("\r\n");
    std::vector<utf8> data{pimpl->read_until(crlf, "Reading string")};
    if ( data.size() >= 2 ) {
        s.assign(data.data(), data.data() + data.size() - 2);
    } else {
        throw exceptions::unexpected_eof(
            "Could not find a \\r\\n sequence before network connection ended");
    }
    return *this;
}
network_connection &fostlib::network_connection::operator >> (std::vector< utf8 > &v) {
    const std::size_t chunk = coerce<std::size_t>(c_large_read_chunk_size.value());
    std::size_t read = 0;
    while ( read < v.size() ) {
        std::vector<utf8> block{pimpl->read(
            asio::transfer_exactly(std::min(v.size() - read, chunk)),
            "Reading a block of data")};
        std::copy(block.begin(), block.end(), v.begin() + read);
        read += block.size();
    }
    return *this;
}


/*
    fostlib::exceptions::socket_error
*/


fostlib::exceptions::socket_error::socket_error() throw () {
}

fostlib::exceptions::socket_error::socket_error(
    const string &message
) throw ()
: exception(message) {
}

fostlib::exceptions::socket_error::socket_error(
    const string &message, const string &extra
) throw ()
: exception(message) {
    insert(data(), "context", extra);
}

fostlib::exceptions::socket_error::socket_error(
    boost::system::error_code error
) throw ()
: error(error) {
    insert(data(), "error", string(boost::lexical_cast<std::string>(error).c_str()));
}

fostlib::exceptions::socket_error::socket_error(
    boost::system::error_code error, const string &message
) throw ()
: exception(message), error(error) {
    insert(data(), "error", string(boost::lexical_cast<std::string>(error).c_str()));
}

fostlib::exceptions::socket_error::~socket_error() throw ()
try {
} catch ( ... ) {
    fostlib::absorb_exception();
}


wliteral const fostlib::exceptions::socket_error::message()
        const throw () {
    return L"Socket error";
}


/*
    fostlib::exceptions::connect_failure
*/


fostlib::exceptions::connect_failure::connect_failure(
    boost::system::error_code error, const host &h, port_number p
) throw ()
: socket_error(error) {
    insert(data(), "host", h);
    insert(data(), "port", p);
}


fostlib::wliteral const fostlib::exceptions::connect_failure::message()
        const throw () {
    return L"Network connection failure";
}


/*
    fostlib::exceptions::read_timeout
*/


fostlib::exceptions::read_timeout::read_timeout() throw () {
}


wliteral const fostlib::exceptions::read_timeout::message() const throw () {
    return L"Read time out";
}


/*
    fostlib::exceptions::read_error
*/


fostlib::exceptions::read_error::read_error() throw () {
}


fostlib::exceptions::read_error::read_error(
    boost::system::error_code error
) throw ()
: socket_error(error) {
}


wliteral const fostlib::exceptions::read_error::message()
        const throw () {
    return L"Read error";
}
