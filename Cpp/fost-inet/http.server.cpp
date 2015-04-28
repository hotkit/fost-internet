/*
    Copyright 2008-2015, Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#include "fost-inet.hpp"
#include <fost/log>
#include <fost/threading>
#include <fost/http.server.hpp>
#include <fost/parse/url.hpp>


using namespace fostlib;


/*
    fostlib::http::server
*/


fostlib::http::server::server(const host &h, uint16_t p,
    std::function<void(request&)> service_lambda)
: binding(h), port(p), listener(binding(), port(),
        [service_lambda](network_connection cnx) {
            try {
                http::server::request req(std::move(cnx));
                try {
                    service_lambda(req);
                } catch ( fostlib::exceptions::exception &e ) {
                    text_body error(
                        fostlib::coerce<fostlib::string>(e));
                    req( error, 500 );
                }
            } catch ( fostlib::exceptions::exception & ) {
                // A 400 response has already been sent by the request handler
            }
        }) {
}


namespace {
    void respond_on_socket(
        fostlib::network_connection *cnx,
        mime &response, const ascii_string &status
    ) {
        std::stringstream buffer;
        response.headers().fold_limit(null); // Turn off MIME line folding
        buffer << "HTTP/1.0 " << status.underlying() << "\r\n"
            << response.headers() << "\r\n";
        *cnx << buffer;
        for ( mime::const_iterator i( response.begin() ); i != response.end(); ++i ) {
            *cnx << *i;
        }
    }

    void raise_connection_error (
        const mime &response, const ascii_string &status
    ) {
        throw exceptions::null(
            "This is a mock server request. It cannot send a response to any client");
    }

    bool return_false() {
        return false;
    }

}


/*
    fostlib::http::server::request
*/


fostlib::http::server::request::request() {
}
fostlib::http::server::request::request(
    network_connection connection
) : m_cnx(new network_connection(std::move(connection))), m_handler(raise_connection_error) {
//     static boost::atomic<int> g_request_id{};
//     const int request(++g_request_id);
//     std::cout << request << " " << timestamp::now() << " About to start to parse response" << std::endl;
    m_handler = boost::bind(respond_on_socket, m_cnx.get(), _1, _2);
    query_string_parser qsp;

//     std::cout << request << " " << timestamp::now() << " Starting read of first line" << std::endl;
    utf8_string first_line;
    (*m_cnx) >> first_line;
//     std::cout << request << " " << timestamp::now() << " Got first line " << first_line << std::endl;

    try {
        {
            fostlib::parser_lock lock;
            if ( !fostlib::parse(lock, first_line.underlying().c_str(),
                (
                    +boost::spirit::chset<>( "A-Z" )
                )[
                    phoenix::var(m_method) =
                        phoenix::construct_< string >( phoenix::arg1, phoenix::arg2 )
                ]
                >> boost::spirit::chlit< char >( ' ' )
                >> (+boost::spirit::chset<>( "_@a-zA-Z0-9/.,:'&()%=~!+*-" ))[
                    phoenix::var(m_pathspec) =
                        phoenix::construct_< url::filepath_string >(
                            phoenix::arg1, phoenix::arg2
                        )
                ]
                >> !(
                    boost::spirit::chlit< char >('?')
                    >> !(
                            qsp[phoenix::var(m_query_string) = phoenix::arg1]
                        |
                            (+boost::spirit::chset<>( "&\\/:_@a-zA-Z0-9.,'()%+*=-" ))
                                [phoenix::var(m_query_string) =
                                    phoenix::construct_< ascii_printable_string >
                                        (phoenix::arg1, phoenix::arg2)]
                    )
                )
                >> !(
                    boost::spirit::chlit< char >( ' ' )
                    >> (
                        boost::spirit::strlit< nliteral >("HTTP/1.0") |
                        boost::spirit::strlit< nliteral >("HTTP/1.1")
                    )
                )
            ).full ) {
                log::error()
                    ("message", "First line failed to parse")
                    ("first line", coerce<string>(first_line));
                throw exceptions::not_implemented(
                    "Expected a HTTP request", coerce<string>(first_line));
            }
        }

        mime::mime_headers headers;
        while ( true ) {
            utf8_string line;
            *m_cnx >> line;
            if ( line.empty() )
                break;
            headers.parse(coerce< string >(line));
        }

        std::size_t content_length = 0;
        if ( headers.exists("Content-Length") )
            content_length = coerce< int64_t >(headers["Content-Length"].value());

        if ( content_length ) {
            std::vector< unsigned char > data( content_length );
            *m_cnx >> data;
            m_mime.reset( new binary_body(data, headers) );
        } else {
            m_mime.reset( new binary_body(headers) );
        }
    } catch ( fostlib::exceptions::exception &e ) {
        try {
            text_body error(coerce<string>(e));
            (*this)( error, 400 );
        } catch ( ... ) {
            log::critical("Exception whilst sending bad request response");
            absorb_exception();
        }
        throw;
    }
}
fostlib::http::server::request::request(
    const string &method,
    const url::filepath_string &filespec,
    std::unique_ptr<binary_body> headers_and_body,
    const url::query_string &qs
) : m_handler(raise_connection_error),
        m_method( method ), m_pathspec( filespec ), m_query_string(qs),
        m_mime( headers_and_body.get()
            ? headers_and_body.release()
            : new binary_body() ) {
}
fostlib::http::server::request::request(
    const string &method,
    const url::filepath_string &filespec,
    const url::query_string &qs,
    std::unique_ptr<binary_body> headers_and_body
) : m_handler(raise_connection_error),
        m_method( method ), m_pathspec( filespec ), m_query_string(qs),
        m_mime( headers_and_body.get()
            ? headers_and_body.release()
            : new binary_body() ) {
}

fostlib::http::server::request::request(
    const string &method, const url::filepath_string &filespec,
    std::unique_ptr<binary_body> headers_and_body,
    boost::function<void (const mime&, const ascii_string&)> handler
) : m_handler(handler),
        m_method( method ), m_pathspec( filespec ),
        m_mime( headers_and_body.release() ) {
}


boost::shared_ptr< fostlib::binary_body > fostlib::http::server::request::data(
) const {
    if ( !m_mime.get() )
        throw exceptions::null(
            "This server request has no MIME data, not even headers");
    return m_mime;
}


void fostlib::http::server::request::operator() (
    mime &response, const ascii_string &status
) {
    m_handler(response, status);
}


nliteral fostlib::http::server::status_text( int code ) {
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";

        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 207: return "Multi-Status";

        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 305: return "Use Proxy";
        case 306: return "(Unused)";
        case 307: return "Temporary Redirect";

        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Request Entity Too Large";
        case 414: return "Request-URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Requested Range Not Satisfiable";
        case 417: return "Expectation Failed";
        case 423: return "Locked";
        case 449: return "Retry With";

        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 506: return "HTTP Version Not Supported";

        default: return "(unknown status code)";
    }
}


void fostlib::http::server::request::operator() (
    mime &response, const int status
) {
    std::stringstream ss;
    ss << status << " " << status_text(status);
    (*this)(response, ss.str());
}
