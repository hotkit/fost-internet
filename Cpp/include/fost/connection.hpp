/*
    Copyright 2008-2015, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#ifndef FOST_CONNECTION_HPP
#define FOST_CONNECTION_HPP
#pragma once


#include <fost/core>
#include <fost/pointers>
#include <fost/host.hpp>

#include <fost/exception/unexpected_eof.hpp>


namespace fostlib {


    /// A TCP/IP network connection from either a server or client
    class FOST_INET_DECLSPEC network_connection final : boost::noncopyable {
        struct state;
        std::unique_ptr<state> pimpl;
    public:
        /// Used when we want a server socket to listen for connections
        class server;

        /// Used for server end points where accept returns a socket
        network_connection(
            boost::asio::io_service &io_service,
            std::unique_ptr< boost::asio::ip::tcp::socket > socket);
        /// Used for clients where a host is connected to on a given port number
        network_connection(const host &h, nullable< port_number > p = null);
        /// Allow moves
        network_connection(network_connection &&) noexcept;

        /// Allow us to use incomplete pimpl type
        ~network_connection();

        /// Start SSL on this connection. After a successful handshake all traffic will be over SSL.
        void start_ssl();

        /// Immediately push data over the network
        network_connection &operator << (const const_memory_block &);
        /// Immediately push data over the network
        network_connection &operator << (const utf8_string &s);
        /// Immediately push data over the network
        network_connection &operator << (const std::stringstream &ss);

        /// Read up until the next \r\n which is discarded
        network_connection &operator >> (std::string &s);
        /// Read up until the next \r\n which is discarded and decode the line as UTF-8
        network_connection &operator >> (utf8_string &s);
        /// Read into the vector. Reads exactly the vector size number of bytes.
        network_connection &operator >> (std::vector<unsigned char> &v);
        /// Unbounded read (i.e. until EOF). Don't do this
        void operator >> (boost::asio::streambuf &);
    };


    namespace exceptions {


        /// The base exception type for all kinds of transmission errors
        class FOST_INET_DECLSPEC socket_error : public exception {
            public:
                /// Construct a socket error
                socket_error() throw();
                /// Throw an exception providing a message
                socket_error(const string &message) throw ();
                /// Construct a connect failure exception
                socket_error(boost::system::error_code) throw();
                /// Throw providing a message and extra information
                socket_error(const string &message, const string &extra) throw ();
                /// Allow us to throw from a Boost error code with a message
                socket_error(boost::system::error_code error,
                    const string &message) throw ();

                /// Destruct the exception without throwing
                ~socket_error() throw ();

                /// Allow access to the error code that caused the exception
                accessors< const nullable< boost::system::error_code > > error;

            protected:
                /// The error message title
                const wchar_t * const message() const throw ();
        };


        /// Thrown for errors during connection to a socket
        class FOST_INET_DECLSPEC connect_failure : public socket_error {
            public:
                /// Construct a connect failure exception
                connect_failure(boost::system::error_code,
                    const host &, port_number) throw();

            protected:
                /// The error message title
                const wchar_t * const message() const throw ();
        };

        /// Thrown for errors during connection to a socket or reading from a socket
        class FOST_INET_DECLSPEC read_timeout : public socket_error {
            public:
                /// Construct a connect failure exception
                read_timeout() throw();

            protected:
                /// The error message title
                const wchar_t * const message() const throw ();
        };

        /// Thrown for general errors when reading from a socket
        class FOST_INET_DECLSPEC read_error : public socket_error {
            public:
                /// Construct a connect failure exception
                read_error() throw();
                /// Construct a read error from an error code
                read_error(boost::system::error_code) throw ();

            protected:
                /// The error message title
                const wchar_t * const message() const throw ();
        };


    }


}


#endif // FOST_CONNECTION_HPP
