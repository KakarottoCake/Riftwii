// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tls.hpp"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>

#include <cstring>
#include <ctime>

#include "bearssl.h"
#include "entropy.hpp"
#include "log.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/http.hpp"

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

// A Wii clock earlier than this release is certainly wrong: its clock
// battery went flat (it then says 2000) or it was never set. Certificates
// last about three months and GitHub's are renewed every few weeks, so no
// fixed date can stand in for it: the date is asked of a server instead
// (example.com, which the network test asks too, else GameTDB; github.com
// sends no date over plain HTTP), once a session, and kept as the
// difference from the Wii's clock. The answer comes over plain HTTP, so
// someone on the network could give a wrong one; accepted only when it is
// after this release came out, it can at most let a certificate count that
// expired after that, while the chain itself is still checked in full.
// With no answer, certificates are checked against the release's first
// day: a forged answer could set that date too, so it adds no new risk,
// and it still passes the certificates current when this release came out.
// Each host gets kDateSeconds in all, so a slow one can't hold the lock
// the other downloads wait on.
constexpr const char* kDateHosts[] = {"example.com", "www.gametdb.com"};
constexpr unsigned kDateSeconds = 8;
mutex_t g_clock_lock = LWP_MUTEX_NULL;
bool g_server_date_asked = false;
bool g_server_date_known = false;
std::time_t g_server_offset = 0;  // the server's date minus the Wii's clock

std::string DateText(std::time_t when) {
    std::tm parts = {};
    char text[32];
    if (::gmtime_r(&when, &parts) == nullptr || std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &parts) == 0)
        return "an unknown date";
    return text;
}

// The Date header of a HEAD request to `host` over plain HTTP.
bool AskServerDate(const char* host, std::time_t& date, std::string& error) {
    HttpUrl url;
    if (!parse_http_url(std::string("http://") + host + "/", url, error)) return false;
    NetServer server;
    if (!ResolveServer(url.host, url.port, server, error)) return false;
    const u64 give_up = gettime() + secs_to_ticks(kDateSeconds);
    SocketTransport socket;
    if (!socket.connect(server, kDateSeconds * 1000, error)) return false;
    // Each wait is cut to what is left of the host's time, so a read that
    // starts just before it ends cannot run on for the socket's usual 10 s.
    const auto wait_only_until_give_up = [&]() {
        const u64 now = gettime();
        const u64 left_ms = now < give_up ? ticks_to_millisecs(give_up - now) : 0;
        socket.set_receive_timeout(left_ms > 0 ? static_cast<int>(left_ms) : 1);
    };
    wait_only_until_give_up();
    const std::string request = http_head_request(url);
    if (!socket.send(request.data(), request.size())) {
        error = "the request could not be sent";
        return false;
    }
    std::vector<std::uint8_t> raw;
    HttpReadState reading;
    reading.head = true;
    unsigned char chunk[1024];
    while (!http_response_complete(raw, reading) && raw.size() < 16384) {
        if (gettime() >= give_up) {
            error = "it answered too slowly";
            return false;
        }
        std::size_t got = 0;
        wait_only_until_give_up();
        if (!socket.receive_some(chunk, sizeof(chunk), got) || got == 0) break;
        if (!try_append(raw, chunk, chunk + got)) {
            error = "no memory for its answer";
            return false;
        }
    }
    HttpResponse response;
    if (!parse_http_response(raw, response, error, true)) return false;
    const auto found = response.headers.find("date");
    if (found == response.headers.end() || !http_date_parse(found->second, date)) {
        error = "its answer gave no date";
        return false;
    }
    return true;
}

// The time certificates are checked against (see kDateHosts above).
std::time_t CertificateTime() {
    const std::time_t wii_now = std::time(nullptr);
    const std::time_t released = release_month_start(RIFTWII_VERSION);
    if (released == 0 || wii_now >= released) return wii_now;
    {
        // Made with interrupts off: two download threads may come here first.
        u32 level;
        _CPU_ISR_Disable(level);
        if (g_clock_lock == LWP_MUTEX_NULL && LWP_MutexInit(&g_clock_lock, false) != 0)
            g_clock_lock = LWP_MUTEX_NULL;
        _CPU_ISR_Restore(level);
    }
    // Without the lock, another download may be asking right now: the
    // release's first day is the safe answer, with no shared state read.
    if (g_clock_lock == LWP_MUTEX_NULL || LWP_MutexLock(g_clock_lock) != 0) return released;
    if (!g_server_date_asked) {
        g_server_date_asked = true;
        for (const char* host : kDateHosts) {
            std::time_t date = 0;
            std::string error;
            if (!AskServerDate(host, date, error)) {
                logf("TLS: the Wii's clock says %s, before this RiftWii came out, and %s gave no date (%s)\n",
                     DateText(wii_now).c_str(), host, error.c_str());
                continue;
            }
            if (date < released) {
                logf("TLS: the Wii's clock says %s and %s says %s, both before this RiftWii came out; "
                     "that date is not used\n", DateText(wii_now).c_str(), host, DateText(date).c_str());
                continue;
            }
            g_server_offset = date - wii_now;
            g_server_date_known = true;
            logf("TLS: the Wii's clock says %s, before this RiftWii came out, so certificates are checked against "
                 "%s's date, %s (set the clock in Wii Settings to stop this)\n",
                 DateText(wii_now).c_str(), host, DateText(date).c_str());
            break;
        }
        if (!g_server_date_known)
            logf("TLS: no server gave the date, so certificates are checked against the first day of the month "
                 "this RiftWii came out; a newer certificate will fail until the clock is set in Wii Settings\n");
    }
    const std::time_t checked = g_server_date_known ? wii_now + g_server_offset : released;
    LWP_MutexUnlock(g_clock_lock);
    return checked;
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

void TlsStream::StateRelease::operator()(State* state) const {
    if (!state) return;
    state->~State();
    if (!placed) ::operator delete(state);
}

std::size_t TlsStream::memory_needed() { return sizeof(State) + 32; }

void TlsStream::use_memory(void* block, std::size_t bytes) {
    memory_ = block;
    memory_bytes_ = bytes;
}

void TlsStream::set_deadline(std::uint64_t deadline) {
    deadline_ = deadline;
    if (state_) state_->deadline = deadline;
}

bool TlsStream::timed_out() const { return state_ && state_->timed_out; }

bool TlsStream::open(SocketTransport& socket, const std::string& host, std::string& error) {
    if (memory_ && memory_bytes_ >= memory_needed()) {
        const std::uintptr_t aligned = (reinterpret_cast<std::uintptr_t>(memory_) + 31) & ~std::uintptr_t(31);
        state_ = StatePtr(new (reinterpret_cast<void*>(aligned)) State(), StateRelease{true});
    } else {
        state_ = StatePtr(new (std::nothrow) State(), StateRelease{false});
    }
    if (!state_) {
        error = "no memory for TLS";
        return false;
    }
    State& s = *state_;
    s.socket = &socket;
    s.deadline = deadline_;
    Configure(s.client, s.x509);
    // Certificates are checked against the Wii's clock, or a server's date
    // when the clock is before this release (CertificateTime). The failure
    // text still shows the Wii's own clock, the one the player can fix.
    const std::time_t now = std::time(nullptr);
    const u64 asking_from = gettime();
    const std::time_t checked = CertificateTime();
    // Asking a server for the date isn't part of the handshake's time.
    if (s.deadline != 0) s.deadline += gettime() - asking_from;
    br_x509_minimal_set_time(&s.x509, static_cast<std::uint32_t>(checked / 86400 + 719528),
                             static_cast<std::uint32_t>(checked % 86400));
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
        const std::string reason = ::riftwii::tls_failure_reason(code, now);
        error = "TLS handshake with " + host + " failed (BearSSL error " + std::to_string(code) +
                (reason.empty() ? ")" : ": " + reason + ")");
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
