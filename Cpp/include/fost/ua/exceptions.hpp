/**
    Copyright 2020 Red Anchor Trading Co. Ltd.

    Distributed under the Boost Software License, Version 1.0.
    See <http://www.boost.org/LICENSE_1_0.txt>
 */


#pragma once


#include <fost/core>
#include <fost/ua/cache.hpp>


namespace fostlib::ua {


    // The exception type that is thrown when there are no expectations
    /// left for the request and no cache exists and no HTTP requests can
    /// be made.
    class no_expectation : public fostlib::exceptions::exception {
      public:
        no_expectation(
                f5::u8view message,
                f5::u8view method,
                url const &url,
                std::optional<json> body,
                headers const &headers);

        const wchar_t *const message() const;
    };


    /// Generic status error exception
    class http_error : public fostlib::exceptions::exception {
      public:
        http_error(url const &u);
        http_error(url const &u, int status_code);

        const wchar_t *const message() const;
    };


    /// The resource could not be found.
    class resource_not_found : public http_error {
      public:
        resource_not_found(fostlib::url const &);

        const wchar_t *const message() const;
    };


    /// The user is not authorized yet (401)
    class unauthorized : public http_error {
      public:
        unauthorized(fostlib::url const &);

        const wchar_t *const message() const;
    };


    /// The resource is forbidden (403)
    class forbidden : public http_error {
      public:
        forbidden(fostlib::url const &);

        const wchar_t *const message() const;
    };


}
