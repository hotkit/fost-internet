/*
    Copyright 2009-2013,Felspar Co Ltd. http://support.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/

#include "fost-inet.hpp"
#include <fost/pop3.hpp>
#include <fost/exception/out_of_range.hpp>
#include <fost/exception/unicode_encoding.hpp>

#include <fost/log>


using namespace fostlib;
using namespace fostlib::pop3;


namespace {

    mime::mime_headers read_headers(
        network_connection &the_network_connection
    ) {
        try {
            mime::mime_headers headers;

            std::string line;
            the_network_connection >> line;

            while ( !line.empty() ) {
                std::string header(line);

                line = "";
                the_network_connection >> line;

                while (line.substr(0,1) == " " || line.substr(0,1) == "\t") {
                    header += line;
                    line = "";
                    the_network_connection >> line;
                }

                string safe;
                for ( std::string::const_iterator c = header.begin(); c != header.end(); ++c )
                    if ( *c > 127 || * c < 0 )
                        safe += '?';
                    else
                        safe += *c;

                headers.parse(safe);
            }
            return headers;
        } catch ( fostlib::exceptions::exception &e ) {
            e.info() << "Whilst reading MIME headers" << std::endl;
            throw;
        }
    }

    std::string read_body(
        network_connection &the_network_connection
    ) {
        try {
            std::string content;
            std::string line;

            the_network_connection >> line;
            while (
                true
            ) {
                if (
                    (line.length() > 0) &&
                    (line[0] == '.')
                ) {
                    if (
                        (line.length() > 1) &&
                        (line[1] == '.')
                    ) {
                        line = line.substr(1);
                    } else
                        break;
                } else
                    content += line+"\n";

                line = "";
                the_network_connection >> line;
            }

            return content;
        } catch ( fostlib::exceptions::exception &e ) {
            e.info() << "Whilst reading message body" << std::endl;
            throw;
        }
    }


    void check_OK(
        network_connection &the_network_connection,
        string command
    ) {
        try {
            utf8_string server_response;
            the_network_connection >> server_response;

            if (server_response.underlying().substr(0,3) != "+OK") {
                throw exceptions::not_implemented(
                    command,
                    coerce< string >(server_response)
                );
            }
        } catch ( fostlib::exceptions::exception &e ) {
            e.info() << "Whilst waiting for +OK after " << command << std::endl;
            throw;
        }
    }

    void send(
        network_connection &the_network_connection,
        const string command,
        const nullable< string > parameter = null
    ) {
        try {
            the_network_connection << coerce< utf8_string >(command);
            if ( !parameter.isnull() ) {
                the_network_connection << " ";
                the_network_connection << coerce< utf8_string >(parameter);
            }
            the_network_connection << "\r\n";
        } catch ( fostlib::exceptions::exception &e ) {
            e.info() << "Whilst sending command: " << command << std::endl;
            throw;
        }
    }

    void send(
        network_connection &the_network_connection,
        const string command,
        const size_t parameter
    ) {
        std::stringstream i_stream;
        i_stream << parameter;
        string value(coerce<string>(utf8_string(i_stream.str())));

        send(the_network_connection, command, value);
    }


    void send_and_check_OK(
        network_connection &the_network_connection,
        const string command,
        const string parameter
    ) {
        send(the_network_connection, command, parameter);
        check_OK(the_network_connection, command);
    }

    void send_and_check_OK(
        network_connection &the_network_connection,
        const string command,
        const size_t parameter
    ) {
        send(the_network_connection, command, parameter);
        check_OK(the_network_connection, command);
    }


}


namespace {
    class pop3cnx {
        network_connection m_cnx;
        public:
            size_t message_count;
            pop3cnx( const host &h, const string &username, const string &password )
            : m_cnx( h, 110 ) {
                try {
                    utf8_string server_status;
                    m_cnx >> server_status;

                    send_and_check_OK(m_cnx, "user", username);
                    send_and_check_OK(m_cnx, "pass", password);

                    send(m_cnx, "stat");

                    utf8_string server_response;
                    m_cnx >> server_response;

                    std::stringstream server_response_stringstream(
                        server_response.underlying().substr(3)
                    );
                    server_response_stringstream >> message_count;
                    size_t octets;
                    server_response_stringstream >> octets;
                } catch ( fostlib::exceptions::exception &e ) {
                    e.info() << "Whilst establishing POP3 connection" << std::endl;
                    throw;
                }
            }
            ~pop3cnx()
            try {
                send(m_cnx, "quit");
            } catch ( ... ) {
                absorb_exception();
            }
            std::unique_ptr< text_body > message( size_t i ) {
                try {
                    send_and_check_OK(m_cnx, "retr", i);
                    mime::mime_headers h = read_headers(m_cnx);
                    utf8_string content;
                    try {
                        content = read_body(m_cnx);
                    } catch ( fostlib::exceptions::unicode_encoding & ) {
                        return std::unique_ptr< text_body >();
                    }
                    return std::unique_ptr<text_body>(
                        new text_body( coerce< string >( content ), h, "text/plain" )
                    );
                } catch ( fostlib::exceptions::exception &e ) {
                    e.info() << "Whilst retrieving a message" << std::endl;
                    throw;
                }
            }
            void remove( size_t i ) {
                send_and_check_OK(m_cnx, "dele", i);
            }
    };
}

void fostlib::pop3::iterate_mailbox(
    const host &host,
    boost::function<bool (const text_body &)> destroy_message,
    const string &username,
    const string &password
) {
    boost::scoped_ptr< pop3cnx > mailbox(
        new pop3cnx(host, username, password));
    const size_t messages = mailbox->message_count;
    log::info("Number of messages found", messages);

    // Loop from the end so we always process the latest bounce messages first
    for ( std::size_t i = messages; i; --i ) {
        std::unique_ptr< text_body > message = mailbox->message(i);
        if (message.get() && destroy_message(*message))
            mailbox->remove(i);
        if ( i % 20 == 0 ) {
			log::info("Resetting mailbox connection", i);
            mailbox.reset( new pop3cnx(host, username, password) );
            if ( mailbox->message_count < i )
                throw fostlib::exceptions::out_of_range< size_t >(
                    "The number of messages on the server can't go down "
                        "below the ones we've processed!",
                    i, messages, mailbox->message_count);
        }
    }
}
