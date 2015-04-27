/*
    Copyright 2015, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


namespace fostlib {


    /// Implement a server accept socket
    class server {
    private:
        boost::asio::io_service m_service;
        boost::asio::ip::tcp::acceptor m_server;
    };


}

