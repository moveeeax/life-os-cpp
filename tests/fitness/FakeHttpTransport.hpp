/**
 * @file FakeHttpTransport.hpp
 * @brief Fake HTTP transport: the whole cloud client is tested without a network.
 *
 * Responses are served in the order they were queued. An extra request with no
 * prepared response is a loud failure, not an empty response: a silent fake hides
 * exactly the bugs the test was written to catch.
 */

#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "fitness/xiaomi/Crypto.hpp"
#include "fitness/xiaomi/Errors.hpp"
#include "fitness/xiaomi/HttpTransport.hpp"

class FakeHttpTransport : public Xiaomi::HttpTransport {
public:
    /// The ssecurity returned by reply_login: "secret-material!" in base64.
    static constexpr const char* kSsecurityB64 = "c2VjcmV0LW1hdGVyaWFsIQ==";

    void reply(Xiaomi::HttpResponse response) { queued_.push_back({std::move(response), false, {}, {}}); }

    /// Successful two-step login: a response with fields and a redirect with a cookie.
    void reply_login(const std::string& rotated_token = "NEWTOKEN") {
        reply({200,
               std::string("&&&START&&&") + R"({"passToken":")" + rotated_token +
                   R"(","userId":1234567890,"ssecurity":")" + kSsecurityB64 +
                   R"(","location":"https://account.xiaomi.com/pass/end"})",
               {}});
        reply({200, "", {{"set-cookie", "serviceToken=abc; Path=/"}}});
    }

    /// Transport failure: send throws MiFitnessProtocolError with this text, the
    /// way CurlTransport does on a timeout or a curl network error.
    void reply_transport_error(std::string message) {
        Queued item;
        item.throw_message = std::move(message);
        queued_.push_back(std::move(item));
    }

    /// The request runs into its time limit, as CurlTransport reports it.
    void reply_timeout() {
        Queued item;
        item.timeout = true;
        queued_.push_back(std::move(item));
    }

    /// Encrypted data response. Encrypted at request time: signed_nonce depends
    /// on the _nonce the client puts in the body, which the fake does not know
    /// until the request arrives. The same crypto functions as in the client are
    /// used, so a successful decrypt also cross-checks them.
    void reply_encrypted(std::string envelope_json) {
        queued_.push_back({{200, "", {}}, true, std::move(envelope_json), {}});
    }

    Xiaomi::HttpResponse send(const Xiaomi::HttpRequest& request) override {
        requests_.push_back(request);
        if (queued_.empty()) {
            throw std::runtime_error("FakeHttpTransport: unexpected request #" + std::to_string(requests_.size()) +
                                     " to " + request.url);
        }
        Queued item = queued_.front();
        queued_.erase(queued_.begin());
        if (item.timeout) {
            throw Xiaomi::MiFitnessTimeoutError("Xiaomi request timed out");
        }
        if (!item.throw_message.empty()) {
            throw Xiaomi::MiFitnessProtocolError("Xiaomi request failed: " + item.throw_message);
        }
        if (item.encrypt) {
            const std::string nonce = Xiaomi::Crypto::b64_decode(form_value(request.body, "_nonce"));
            const std::string signed_nonce = Xiaomi::Crypto::signed_nonce(kSsecurityB64, nonce);
            item.response.body = Xiaomi::Crypto::b64_encode(Xiaomi::Crypto::rc4(signed_nonce, item.plaintext));
        }
        return item.response;
    }

    const std::vector<Xiaomi::HttpRequest>& requests() const { return requests_; }

    /// Value of a form-urlencoded body field, percent-decoded.
    static std::string form_value(const std::string& body, const std::string& name) {
        const std::string needle = name + "=";
        std::size_t at = 0;
        while (at < body.size()) {
            const std::size_t end = body.find('&', at);
            const std::string pair = body.substr(at, end == std::string::npos ? std::string::npos : end - at);
            if (pair.rfind(needle, 0) == 0) {
                return percent_decode(pair.substr(needle.size()));
            }
            if (end == std::string::npos) {
                break;
            }
            at = end + 1;
        }
        throw std::runtime_error("FakeHttpTransport: form field not found: " + name);
    }

private:
    static std::string percent_decode(const std::string& value) {
        std::string out;
        out.reserve(value.size());
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '%' && i + 2 < value.size()) {
                out.push_back(static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16)));
                i += 2;
            } else if (value[i] == '+') {
                out.push_back(' ');
            } else {
                out.push_back(value[i]);
            }
        }
        return out;
    }

    struct Queued {
        Xiaomi::HttpResponse response;
        bool encrypt = false;
        std::string plaintext;
        std::string throw_message;
        bool timeout = false;
    };

    std::vector<Xiaomi::HttpRequest> requests_;
    std::vector<Queued> queued_;
};
