/*
    Copyright 2008-2015, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#include "fost-inet.hpp"
#include <fost/datetime>
#include <fost/insert>

#include <fost/http.useragent.hpp>
#include <fost/parse/parse.hpp>

#include <fost/exception/not_null.hpp>
#include <fost/exception/unexpected_eof.hpp>


using namespace fostlib;


/*
    fostlib::http::user_agent
*/


namespace {
    boost::asio::io_service g_io_service;

    const fostlib::setting< fostlib::string > c_user_agent(
        L"fost-internet/Cpp/fost-inet/http.useragent.cpp",
        L"HTTP", L"UserAgent", L"Felspar user agent", true);
}


fostlib::http::user_agent::user_agent() {
}
fostlib::http::user_agent::user_agent(const url &u)
: base(u) {
}


std::unique_ptr< http::user_agent::response >
        fostlib::http::user_agent::operator () (request &req) const {
    try {
        if ( !req.headers().exists("Date") ) {
            req.headers().set("Date", coerce< string >(
                coerce< rfc1123_timestamp >(timestamp::now())));
        }
        if ( !req.headers().exists("Host") ) {
            req.headers().set("Host", req.address().server().name());
        }
        if ( !req.headers().exists("User-Agent") ) {
            req.headers().set("User-Agent", c_user_agent.value() + L"/Fost 4");
        }
        req.headers().set("TE", "trailers");

        if ( !authentication().isnull() )
            authentication().value()( req );

        std::unique_ptr<network_connection> cnx(
            new network_connection(req.address().server(), req.address().port()));
        if ( req.address().protocol() == ascii_printable_string("https") )
            cnx->start_ssl();

        std::stringstream buffer;
        buffer << coerce< utf8_string >( req.method() ).underlying() << " " <<
            req.address().pathspec().underlying().underlying();
        {
            nullable< ascii_printable_string > q = req.address().query().as_string();
            if ( !q.isnull() ) {
                buffer << "?" << q.value().underlying();
            }
        }
        req.headers().fold_limit(null); // Turn off line folding
        buffer << " HTTP/1.0\r\n" << req.headers() << "\r\n";
        *cnx << buffer;

        for ( mime::const_iterator i( req.data().begin() ); i != req.data().end(); ++i ) {
            *cnx << *i;
        }

        utf8_string first_line;
        *cnx >> first_line;
        string protocol, message; int status;
        {
            fostlib::parser_lock lock;
            if ( !fostlib::parse(lock, first_line.underlying().c_str(),
                (
                    boost::spirit::strlit< wliteral >(L"HTTP/0.9") |
                    boost::spirit::strlit< wliteral >(L"HTTP/1.0") |
                    boost::spirit::strlit< wliteral >(L"HTTP/1.1")
                )[ phoenix::var(protocol) =
                    phoenix::construct_< string >( phoenix::arg1, phoenix::arg2 ) ]
                >> boost::spirit::chlit< wchar_t >( ' ' )
                >> boost::spirit::uint_parser< int, 10, 3, 3 >()
                    [ phoenix::var(status) = phoenix::arg1 ]
                >> boost::spirit::chlit< wchar_t >( ' ' )
                >> (
                    +boost::spirit::chset<>( L"a-zA-Z -" )
                )[ phoenix::var(message) =
                    phoenix::construct_< string >( phoenix::arg1, phoenix::arg2 ) ]
            ).full )
                throw exceptions::not_implemented(
                    "Expected a HTTP response", coerce< string >(first_line));
        }

        return std::unique_ptr<http::user_agent::response>(
                new http::user_agent::response(
                    std::move(cnx), req.method(), req.address(),
                    protocol, status, message));
    } catch ( fostlib::exceptions::exception &e ) {
        insert(e.data(), "http-ua", "method", req.method());
        throw;
    }
}


/*
    fostlib::http::user_agent::request
*/


fostlib::http::user_agent::request::request(const string &method, const url &url)
: m_data(new empty_mime), method(method), address(url) {
}
fostlib::http::user_agent::request::request(
    const string &method, const url &url, const string &data
) : m_data(new text_body(data)), method(method), address(url) {
}
fostlib::http::user_agent::request::request(
    const string &method, const url &url, const boost::filesystem::wpath &data
) : m_data(new file_body(data)), method(method), address(url) {
}
fostlib::http::user_agent::request::request(
    const string &method, const url &url,
    boost::shared_ptr< mime > mime_data
) : m_data(mime_data), method(method), address(url) {
}


/*
    fostlib::http::user_agent::response
*/


fostlib::http::user_agent::response::response(
    const string &method, const url &address,
    int status, boost::shared_ptr< binary_body > body,
    const mime::mime_headers &headers,
    const string &message
) : m_headers(headers), method(method), address(address),
        protocol(coerce<string>(address.protocol())),
        status(status), message(message), m_body(body) {
}


namespace {
    void read_headers(
        network_connection &cnx, mime::mime_headers &headers,
        nliteral error_message
    ) {
        try {
            while ( true ) {
                utf8_string line;
                cnx >> line;
                if (line.empty())
                    break;
                headers.parse(coerce< string >(line));
            }
        } catch ( fostlib::exceptions::exception &e ) {
            e.info() << error_message << std::endl;
            throw;
        }
    }
}


fostlib::http::user_agent::response::response(
    std::unique_ptr<network_connection> connection,
    const string &method, const url &url,
    const string &protocol, int status, const string &message
) : method(method), address(url), protocol(protocol),
        status(status), message(message), m_cnx(std::move(connection)) {
    read_headers(*m_cnx, m_headers, "Whilst fetching headers");
}


boost::shared_ptr< const binary_body > fostlib::http::user_agent::response::body() {
    if ( !m_body ) {
        try {
            nullable< int64_t > length;
            if ( method() == L"HEAD" ) {
                length = 0;
            } else if (m_headers.exists("Content-Length")) {
                length = coerce< int64_t >(m_headers["Content-Length"].value());
            }

            if ( status() == 304 || (!length.isnull() && length.value() == 0) ) {
                m_body = boost::shared_ptr< binary_body >(
                    new binary_body(m_headers));
            } else if ( length.isnull() ) {
                if ( m_headers["Transfer-Encoding"].value() == "chunked" ) {
                    std::vector< unsigned char > data;
                    while ( true ) {
                        std::string length, ignore_crlf;
                        *m_cnx >> length;
                        std::size_t chunk_size = fostlib::coerce< std::size_t >(
                            hex_string(length));
                        if ( chunk_size == 0 )
                            break;
                        std::vector< unsigned char > chunk( chunk_size );
                        *m_cnx >> chunk >> ignore_crlf;
                        if ( !ignore_crlf.empty() )
                            throw fostlib::exceptions::not_null(
                                "Expected CRLF after chunk data, but found something else",
                                coerce<string>(utf8_string(ignore_crlf)));
                        data.insert(data.end(), chunk.begin(), chunk.end());
                    }
                    // Read trailing headers
                    read_headers(*m_cnx, m_headers, "Whilst reading trailing headers");
                    m_body = boost::shared_ptr< binary_body >(
                        new binary_body(data, m_headers));
                } else {
                    // Unbounded read to memory (eeek)
                    // TODO: Stop doing this!
                    boost::asio::streambuf body_buffer;
                    *m_cnx >> body_buffer;
                    std::vector< unsigned char > body_data;
                    body_data.reserve(body_buffer.size());
                    while ( body_buffer.size() ) {
                        body_data.push_back( body_buffer.sbumpc() );
                    }
                    m_body = boost::shared_ptr< binary_body >(
                        new binary_body(body_data, m_headers));
                }
            } else {
                std::vector< unsigned char > body(length.value());
                *m_cnx >> body;
                m_body = boost::shared_ptr< binary_body >(
                    new binary_body(body, m_headers));
            }
        } catch ( fostlib::exceptions::exception & ) {
            throw;
        }
    }
    return m_body;
}
