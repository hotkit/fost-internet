/*
    Copyright 1999-2010, Felspar Co Ltd. http://fost.3.felspar.com/
    Distributed under the Boost Software License, Version 1.0.
    See accompanying file LICENSE_1_0.txt or copy at
        http://www.boost.org/LICENSE_1_0.txt
*/


#ifndef FOST_HEADERS_HPP
#define FOST_HEADERS_HPP
#pragma once


#include <fost/core>
#include <fost/pointers>
#include <boost/filesystem.hpp>


namespace fostlib {


    /// An abstract base class used to describe headers as they appear in protocols like SMTP and HTTP.
    class FOST_INET_DECLSPEC FSL_ABSTRACT headers_base {
    public:
        class content;

        /// Construct empty headers
        headers_base();
        /// Allow the headers to be sub-classed
        virtual ~headers_base();

        /// Parse the string and add the found headers
        void parse( const string &headers );

        /// Returns true if a specified header exists
        bool exists( const string & ) const;
        /// Allows a header to be set, but without any value
        content &set( const string &name );
        /// Allows a header to be given a specified value
        content &set( const string &name, const content & );
        /// Allow a specified sub-value on the specified header to be set
        content &set_subvalue( const string &name, const string &k, const string &v );
        /// Fetches a header throwing if the header doesn't exist
        const content &operator [] ( const string & ) const;

        /// Allow the fields to be iterated
        typedef std::map< string, content >::const_iterator const_iterator;
        /// The beginning of the header fields
        const_iterator begin() const;
        /// The end of the header fields
        const_iterator end() const;

        /// The content of header fields
        class FOST_INET_DECLSPEC content {
            std::map< string, string > m_subvalues;
            public:
                /// Create empty content for a header value
                content();
                /// Create header value content from a narrow character literal
                content( nliteral );
                /// Create header value content from a wide character literal
                content( wliteral );
                /// Create header value content from a string
                content( const string & );
                /// Create header value content from a string with sub-values
                content( const string &, const std::map< string, string > & );

                /// The main value of the header field
                accessors< string > value;

                /// Set a field's sub-value
                content &subvalue( const string &k, const string &v );
                /// Access a field's sub-value
                nullable< string > subvalue( const string &k ) const;

                /// Allows the sub-values to be iterated
                typedef std::map< string, string >::const_iterator const_iterator;
                /// The start of the sub-values
                const_iterator begin() const;
                /// The end of the sub-values
                const_iterator end() const;
        };

    protected:
        virtual std::pair< string, content > value(
            const string &name, const string &value
        ) = 0;

    private:
        std::map< string, content > m_headers;
    };


    /// Allow headers to be written to a narrow stream
    FOST_INET_DECLSPEC std::ostream &operator <<(
        std::ostream &o, const headers_base &headers
    );
    /// Allow header field values to be written to a narrow stream
    FOST_INET_DECLSPEC std::ostream &operator <<(
        std::ostream &o, const headers_base::content &value
    );


}


#endif // FOST_HEADERS_HPP