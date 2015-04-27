/*
    Copyright 2008-2015, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#ifndef FOST_INTERNET_HTTP_SERVER_HPP
#define FOST_INTERNET_HTTP_SERVER_HPP
#pragma once


#include <fost/server.hpp>
#include <fost/http.hpp>
#include <fost/url.hpp>


namespace fostlib {


    namespace http {


        /// A minimal HTTP server
        class FOST_INET_DECLSPEC server : boost::noncopyable {
        public:
            /// The request from a user agent
            class FOST_INET_DECLSPEC request : boost::noncopyable {
                friend class fostlib::http::server;
                boost::scoped_ptr< network_connection > m_cnx;
                boost::function<void (mime&, const ascii_string&)> m_handler;
                string m_method;
                url::filepath_string m_pathspec;
                url::query_string m_query_string;
                boost::shared_ptr< binary_body > m_mime;

                public:
                    /// Create an empty request
                    request();
                    /// Create a request from data on the provided socket
                    request(
                        boost::asio::io_service &,
                        std::unique_ptr< boost::asio::ip::tcp::socket > connection);
                    /// This constructor is useful for mocking the request that doesn't get responded to
                    request(
                        const string &method, const url::filepath_string &filespec,
                        std::unique_ptr<binary_body> headers_and_body
                            = std::unique_ptr<binary_body>(),
                        const url::query_string &qs = url::query_string());
                    /// This constructor is useful for mocking the request that doesn't get responded to
                    request(
                        const string &method, const url::filepath_string &filespec,
                        const url::query_string &qs,
                        std::unique_ptr<binary_body> headers_and_body
                            = std::unique_ptr<binary_body>());
                    /// This constructor is useful for mocking the request that gets responded to
                    request(
                        const string &method, const url::filepath_string &filespec,
                        std::unique_ptr<binary_body> headers_and_body,
                        boost::function<void (const mime&, const ascii_string &)>);

                    /// The request method
                    const string &method() const { return m_method; }
                    /// The requested resource
                    const url::filepath_string &file_spec() const { return m_pathspec; }
                    /// The query string
                    const url::query_string query_string() const {
                        return m_query_string;
                    }
                    /// The request body and headers
                    boost::shared_ptr< binary_body > data() const;

                    /// Used to pass the response back to the user agent.
                    void operator () (
                        mime &response,
                        const int status = 200);
                    /// Used to pass the response back to the user agent.
                    void operator () (
                        mime &response,
                        const ascii_string &status_text);
            };

            /// Create a server bound to a host and port
            explicit server( const host &h, uint16_t port = 80 );

            /// The host the server is bound to
            accessors< const host > binding;
            /// The port the server is bound to
            accessors< const uint16_t > port;

            /// Return the next request on the underlying socket
            std::unique_ptr<request> operator() ();
            /// Run the provided lambda to service requests forever
            void operator () (
                boost::function< bool (request &) > service_lambda);
            /// Run the provided lambda to service requests until the terminator return true
            void operator () (
                boost::function< bool (request &) > service_lambda,
                boost::function< bool (void) > terminator);

            /// Return the status text associated with a status code
            static nliteral status_text( int code );

        private:
            fostlib::server listener;
        };


    }


}


#endif // FOST_INTERNET_HTTP_SERVER_HPP
