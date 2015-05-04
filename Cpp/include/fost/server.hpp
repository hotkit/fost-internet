/*
    Copyright 2015, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#pragma once


#include <fost/connection.hpp>


namespace fostlib {


    /// Implement a server accept socket
    class network_connection::server final : boost::noncopyable {
        struct state;
        std::unique_ptr<state> pimpl;
    public:
        /// Construct a server socket bound to the requested IP and port
        server(const host &, uint16_t, std::function<void(network_connection)>,
            std::size_t threads = 1);

        /// Destructor so we can use pimpl
        ~server();

        /// Return the next network connection that needs to be dealt with
        network_connection operator () ();
    };


}

