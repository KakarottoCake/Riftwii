// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tls.hpp"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include <cstring>
#include <ctime>

#include "bearssl.h"
#include "entropy.hpp"

extern "C" {
extern const br_x509_trust_anchor kTlsRoots[];
extern const unsigned kTlsRootCount;
}

namespace riftwii::wii {

struct TlsStream::State {
    br_ssl_client_context client;
    br_x509_minimal_context x509;
    br_sslio_context io;
    SocketTransport* socket = nullptr;
    // The time base at which reads give up (0: none), and whether one did.
    u64 deadline = 0;
    bool timed_out = false;
    unsigned char buffer[BR_SSL_BUFSIZE_MONO];
};

namespace {

// BearSSL reads until it has a whole record (up to 16 KB), however long
// that takes: past the deadline, a read fails instead, so a server sending
// a byte at a time cannot hold a handshake or a record for hours.
int LowRead(void* ctx, unsigned char* data, std::size_t length) {
    auto* s = static_cast<TlsStream::State*>(ctx);
    if (s->deadline != 0 && gettime() >= s->deadline) {
        s->timed_out = true;
        return -1;
    }
    std::size_t got = 0;
    if (!s->socket->receive_some(data, length, got) || got == 0) return -1;
    return static_cast<int>(got);
}

int LowWrite(void* ctx, const unsigned char* data, std::size_t length) {
    auto* s = static_cast<TlsStream::State*>(ctx);
    return s->socket->send(data, length) ? static_cast<int>(length) : -1;
}

// Seeds BearSSL's generator from the session's pool (entropy.hpp): the
// card's secret seed file, the console's ID and the jitter gathered since
// the start. The clock alone was guessable from the handshake's first
// message, and with it the session's keys (SEC-01).
void Seed(br_ssl_engine_context& engine) {
    unsigned char seed[64];
    EntropyTake(seed, sizeof(seed));
    br_ssl_engine_inject_entropy(&engine, seed, sizeof(seed));
    std::memset(seed, 0, sizeof(seed));
}

// Only what GitHub needs, to keep the DOL small: TLS 1.2, ECDHE with
// AES-GCM, P-256/P-384 (BearSSL's compact i15 code), SHA-256/384, and RSA
// signatures in case its certificate changes kind.
void Configure(br_ssl_client_context& cc, br_x509_minimal_context& xc) {
    static const std::uint16_t kSuites[] = {
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
    };
    br_ssl_client_zero(&cc);
    br_ssl_engine_set_versions(&cc.eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_suites(&cc.eng, kSuites, sizeof(kSuites) / sizeof(kSuites[0]));
    br_x509_minimal_init(&xc, &br_sha256_vtable, kTlsRoots, kTlsRootCount);
    br_ssl_engine_set_hash(&cc.eng, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_hash(&cc.eng, br_sha384_ID, &br_sha384_vtable);
    br_x509_minimal_set_hash(&xc, br_sha256_ID, &br_sha256_vtable);
    br_x509_minimal_set_hash(&xc, br_sha384_ID, &br_sha384_vtable);
    br_x509_minimal_set_rsa(&xc, br_rsa_i15_pkcs1_vrfy);
    br_x509_minimal_set_ecdsa(&xc, &br_ec_prime_i15, br_ecdsa_i15_vrfy_asn1);
    br_ssl_engine_set_ec(&cc.eng, &br_ec_prime_i15);
    br_ssl_engine_set_rsavrfy(&cc.eng, br_rsa_i15_pkcs1_vrfy);
    br_ssl_engine_set_ecdsa(&cc.eng, br_ecdsa_i15_vrfy_asn1);
    br_ssl_engine_set_prf_sha256(&cc.eng, br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(&cc.eng, br_tls12_sha384_prf);
    br_ssl_engine_set_aes_ctr(&cc.eng, &br_aes_ct_ctr_vtable);
    br_ssl_engine_set_ghash(&cc.eng, br_ghash_ctmul32);
    br_ssl_engine_set_gcm(&cc.eng, &br_sslrec_in_gcm_vtable, &br_sslrec_out_gcm_vtable);
    br_ssl_engine_set_x509(&cc.eng, &xc.vtable);
}

}  // namespace

TlsStream::TlsStream() = default;
TlsStream::~TlsStream() = default;

void TlsStream::set_deadline(std::uint64_t deadline) {
    deadline_ = deadline;
    if (state_) state_->deadline = deadline;
}

bool TlsStream::timed_out() const { return state_ && state_->timed_out; }

bool TlsStream::open(SocketTransport& socket, const std::string& host, std::string& error) {
    state_.reset(new (std::nothrow) State());
    if (!state_) {
        error = "no memory for TLS";
        return false;
    }
    State& s = *state_;
    s.socket = &socket;
    s.deadline = deadline_;
    Configure(s.client, s.x509);
    // Certificates are checked against the Wii's clock.
    const std::time_t now = std::time(nullptr);
    br_x509_minimal_set_time(&s.x509, static_cast<std::uint32_t>(now / 86400 + 719528),
                             static_cast<std::uint32_t>(now % 86400));
    br_ssl_engine_set_buffer(&s.client.eng, s.buffer, sizeof(s.buffer), 0);
    Seed(s.client.eng);
    if (!br_ssl_client_reset(&s.client, host.c_str(), 0)) {
        error = "TLS setup failed (" + std::to_string(br_ssl_engine_last_error(&s.client.eng)) + ")";
        return false;
    }
    br_sslio_init(&s.io, &s.client.eng, LowRead, &s, LowWrite, &s);
    // The handshake runs with the first write; flushing an empty one
    // forces it now, so a failure is reported as a TLS one.
    if (br_sslio_flush(&s.io) != 0) {
        if (s.timed_out) {
            error = host + " is answering too slowly (TLS handshake)";
            return false;
        }
        const int code = br_ssl_engine_last_error(&s.client.eng);
        error = "TLS handshake with " + host + " failed (BearSSL error " + std::to_string(code) +
                (code >= BR_ERR_X509_OK ? ": the certificate was not accepted; check the Wii's clock)" : ")");
        return false;
    }
    return true;
}

bool TlsStream::send(const void* data, std::size_t length) {
    if (!state_) return false;
    return br_sslio_write_all(&state_->io, data, length) == 0 && br_sslio_flush(&state_->io) == 0;
}

bool TlsStream::receive_some(void* data, std::size_t max, std::size_t& got) {
    got = 0;
    if (!state_) return false;
    const int n = br_sslio_read(&state_->io, data, max);
    if (n > 0) {
        got = static_cast<std::size_t>(n);
        return true;
    }
    // A clean close (close_notify, or the socket closing after it).
    return br_ssl_engine_last_error(&state_->client.eng) == BR_ERR_OK;
}

int TlsStream::last_error() const { return state_ ? br_ssl_engine_last_error(&state_->client.eng) : 0; }

}  // namespace riftwii::wii
