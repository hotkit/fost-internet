/**
    Copyright 2019 Red Anchor Trading Co. Ltd.

    Distributed under the Boost Software License, Version 1.0.
    See <http://www.boost.org/LICENSE_1_0.txt>
 */


#include <fost/http>
#include <fost/log>
#include <fost/test>


namespace {
    /// Needs to be set to the path to the cacert.pem file in the
    /// `Configuration` folder.
    fostlib::setting<fostlib::string> const c_ca_cert_file{
            __FILE__, "fost-internet tests", "CA Certificate file",
            __FILE__ "/../../Configuration/cacert.pem", true};
}


FSL_TEST_SUITE(tls);


FSL_TEST_FUNCTION(defaults) {
    fostlib::http::user_agent ua;
    auto const response = ua.get(fostlib::url{"https://sha256.badssl.com/"});
    FSL_CHECK_EQ(response->status(), 200);
    FSL_CHECK_EXCEPTION(
            ua.get(fostlib::url{"https://self-signed.badssl.com/"}),
            fostlib::exceptions::socket_error &);
}


FSL_TEST_FUNCTION(no_default_paths) {
    fostlib::setting<bool> const no_default_paths{
            "fost-inet-test/tls.cpp", fostlib::c_tls_use_standard_verify_paths,
            false};
    fostlib::http::user_agent ua;
    FSL_CHECK_EXCEPTION(
            ua.get(fostlib::url{"https://sha256.badssl.com/"}),
            fostlib::exceptions::socket_error &);
}


FSL_TEST_FUNCTION(specified_digicert_leaf) {
    fostlib::setting<bool> const no_default_paths{
            "fost-inet-test/tls.cpp", fostlib::c_tls_use_standard_verify_paths,
            false};
    fostlib::setting<fostlib::json> const digicert_ca{
            "fost-inet-test/tls.cpp", fostlib::c_extra_leaf_certificates,
            fostlib::json::array_t{{fostlib::digicert_root_ca()}}};
    fostlib::http::user_agent ua;
    auto const response = ua.get(fostlib::url{"https://sha256.badssl.com/"});
    FSL_CHECK_EQ(response->status(), 200);
}


FSL_TEST_FUNCTION(specify_lets_encrypt_leaf) {
    fostlib::setting<bool> const no_default_paths{
            "fost-inet-test/tls.cpp", fostlib::c_tls_use_standard_verify_paths,
            false};
    fostlib::setting<fostlib::json> const digicert_ca{
            "fost-inet-test/tls.cpp", fostlib::c_extra_leaf_certificates,
            fostlib::json::array_t{{fostlib::lets_encrypt_root()}}};
    fostlib::http::user_agent ua;
    auto const response =
            ua.get(fostlib::url{"https://valid-isrgrootx1.letsencrypt.org/"});
    FSL_CHECK_EQ(response->status(), 200);
}


FSL_TEST_FUNCTION(specify_multiple_roots) {
    auto logger = fostlib::log::debug(fostlib::c_fost_inet);
    fostlib::setting<bool> const no_default_paths{
            "fost-inet-test/tls.cpp", fostlib::c_tls_use_standard_verify_paths,
            false};
    fostlib::setting<fostlib::json> const digicert_ca{
            "fost-inet-test/tls.cpp", fostlib::c_extra_leaf_certificates,
            fostlib::json::array_t{
                    {fostlib::digicert_root_ca()},
                    {fostlib::lets_encrypt_root()}}};
    fostlib::http::user_agent ua;
    {
        auto const response =
                ua.get(fostlib::url{"https://sha256.badssl.com/"});
        logger("sha256.badssl.com", response->status());
        FSL_CHECK_EQ(response->status(), 200);
    }
    {
        auto const response = ua.get(
                fostlib::url{"https://valid-isrgrootx1.letsencrypt.org/"});
        logger("valid-isrgrootx1.letsencrypt.org", response->status());
        FSL_CHECK_EQ(response->status(), 200);
    }
    /// This host is used for web proxy tests. It doesn't work because although
    /// it uses Let's Encrypt, they sign with an intermediate and this setting
    /// doesn't do chain verification.
    FSL_CHECK_EXCEPTION(
            ua.get(fostlib::url{"https://kirit.com/"}),
            fostlib::exceptions::socket_error &);
}


FSL_TEST_FUNCTION(specify_certificate_verification_file) {
    auto logger = fostlib::log::debug(fostlib::c_fost_inet);
    fostlib::setting<bool> const no_default_paths{
            "fost-inet-test/tls.cpp", fostlib::c_tls_use_standard_verify_paths,
            false};
    fostlib::http::user_agent ua;

    FSL_CHECK_EXCEPTION(
            ua.get(fostlib::url{"https://kirit.com/"}),
            fostlib::exceptions::socket_error &);

    /// TODO Add verification file setting to known file
    { // Fail the test if the certificate file doesn't actually exist
        auto const certfile =
                fostlib::coerce<fostlib::fs::path>(c_ca_cert_file.value());
        logger("certfile", certfile);
        FSL_CHECK(fostlib::fs::exists(certfile));
        /// If the above check fails then a setting is needed to point to the
        /// certificate file. See top of this file.
    }
    fostlib::setting<std::optional<fostlib::string>> const ca_file{
            __FILE__, fostlib::c_certificate_verification_file,
            c_ca_cert_file.value()};

    auto const response = ua.get(fostlib::url{"https://kirit.com/"});
    logger("kirit.com", response->status());
    FSL_CHECK_EQ(response->status(), 200);
}
