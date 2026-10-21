// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/textlines.hpp"

#include <cerrno>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>

namespace riftwii {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Past the digits from `at`; how many there were in `count`.
std::size_t skip_digits(const std::string& text, std::size_t at, std::size_t& count) {
    count = 0;
    while (at < text.size() && is_digit(text[at])) {
        ++at;
        ++count;
    }
    return at;
}

}  // namespace

bool is_stream_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

bool LineReader::next(std::string& line, char delimiter) {
    // getline fails only when it takes nothing at all, not even a
    // delimiter: so an empty line between two delimiters still counts.
    if (at_ >= text_.size()) return false;
    const std::size_t end = text_.find(delimiter, at_);
    if (end == std::string_view::npos) {
        line.assign(text_.substr(at_));
        at_ = text_.size();
        return true;
    }
    line.assign(text_.substr(at_, end - at_));
    at_ = end + 1;
    return true;
}

bool WordReader::skip_space() {
    if (failed_) return false;
    while (at_ < text_.size() && is_stream_space(text_[at_])) ++at_;
    if (at_ >= text_.size()) failed_ = true;
    return !failed_;
}

bool WordReader::next(std::string& word) {
    if (!skip_space()) return false;
    const std::size_t start = at_;
    while (at_ < text_.size() && !is_stream_space(text_[at_])) ++at_;
    word = text_.substr(start, at_ - start);
    return true;
}

bool WordReader::rest(std::string& rest) {
    if (failed_ || at_ >= text_.size()) {
        failed_ = true;
        return false;
    }
    // getline stops at a line break, and drops it.
    const std::size_t end = text_.find('\n', at_);
    if (end == std::string::npos) {
        rest = text_.substr(at_);
        at_ = text_.size();
    } else {
        rest = text_.substr(at_, end - at_);
        at_ = end + 1;
    }
    return true;
}

bool WordReader::next(int& value) {
    if (!skip_space()) return false;
    // The characters a stream takes for a decimal integer: they are used
    // up even when they make no number ("-" alone, say).
    const std::size_t start = at_;
    if (text_[at_] == '+' || text_[at_] == '-') ++at_;
    std::size_t digits = 0;
    at_ = skip_digits(text_, at_, digits);
    if (digits == 0) {
        value = 0;
        failed_ = true;
        return false;
    }
    const std::string number = text_.substr(start, at_ - start);
    errno = 0;
    const long long parsed = std::strtoll(number.c_str(), nullptr, 10);
    const bool out_of_range = errno == ERANGE || parsed > INT_MAX || parsed < INT_MIN;
    if (out_of_range) {
        value = number[0] == '-' ? INT_MIN : INT_MAX;
        failed_ = true;
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool WordReader::next(float& value) {
    if (!skip_space()) return false;
    // A stream takes a sign, digits, one point and digits, and an exponent
    // only after some digits; strtof then has to take all of that.
    const std::size_t start = at_;
    if (text_[at_] == '+' || text_[at_] == '-') ++at_;
    std::size_t whole = 0;
    at_ = skip_digits(text_, at_, whole);
    std::size_t fraction = 0;
    if (at_ < text_.size() && text_[at_] == '.') at_ = skip_digits(text_, at_ + 1, fraction);
    const bool mantissa = whole + fraction > 0;
    if (mantissa && at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
        ++at_;
        if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) ++at_;
        std::size_t exponent = 0;
        at_ = skip_digits(text_, at_, exponent);
    }
    const std::string number = text_.substr(start, at_ - start);
    char* end = nullptr;
    const float parsed = number.empty() ? 0.0f : std::strtof(number.c_str(), &end);
    if (number.empty() || end != number.c_str() + number.size()) {
        value = 0;
        failed_ = true;
        return false;
    }
    // Too large for a float: libstdc++ gives the largest one and fails.
    if (parsed == HUGE_VALF || parsed == -HUGE_VALF) {
        value = parsed > 0 ? FLT_MAX : -FLT_MAX;
        failed_ = true;
        return false;
    }
    value = parsed;
    return true;
}

}  // namespace riftwii
