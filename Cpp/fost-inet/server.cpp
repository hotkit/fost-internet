/*
    Copyright 2015, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#include "fost-inet.hpp"
#include <fost/server.hpp>

#include <thread>


using namespace fostlib;
namespace asio = boost::asio;


struct network_connection::server::state {
    boost::asio::io_service io_service;
    boost::asio::ip::tcp::acceptor socket;

    state(const host &h, uint16_t p)
    : socket(io_service, asio::ip::tcp::endpoint(h.address(), p)) {
    }

    ~state() {
        io_service.stop();
    }
};


network_connection::server::server(const host &h, uint16_t p)
: pimpl(new state(h, p)) {
}


network_connection::server::~server() {
}

