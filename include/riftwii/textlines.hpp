// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

// Line and word splitting for RiftWii's text files, in place of
// std::istringstream with std::getline and operator>>. The streams bring
// libstdc++'s locale and iostream code into the Wii DOL (close to 500 KiB of
// it), and the DOL has to fit under the start-up stash. These readers keep
// the streams' rules exactly, so the parsers behave as they did.
namespace riftwii {

// std::getline over a string: each call gives the text up to the next
// delimiter (the delimiter itself is dropped). The last piece is given
// even without a delimiter after it, and false comes only when nothing is
// left: "a\nb" is two lines, "a\n" is one, "" is none, "\n" is one empty
// line. A '\r' before the '\n' is kept, as getline keeps it.
class LineReader {
public:
    explicit LineReader(std::string_view text, char delimiter = '\n')
        : text_(text), delimiter_(delimiter) {}
    // The reader only looks at the text, so the text must outlive it.
    explicit LineReader(std::string&&, char = '\n') = delete;

    bool next(std::string& line) { return next(line, delimiter_); }
    // One piece up to `delimiter` instead of the reader's own (a field
    // and then the rest of a line, say).
    bool next(std::string& line, char delimiter);

private:
    std::string_view text_;
    std::size_t at_ = 0;
    char delimiter_;
};

// operator>> over a string: words separated by spaces, tabs and line
// breaks (the "C" locale's white space), leading white space skipped.
// As with a stream, the first failed read fails every later one, and a
// failed read leaves its target as it was, except that a word that is
// not a number sets a number to 0.
class WordReader {
public:
    explicit WordReader(std::string text) : text_(std::move(text)) {}

    // The next word. False at the end of the text.
    bool next(std::string& word);
    // A decimal integer, as `>> int` reads it: an optional sign and the
    // digits, ending at the first other character, which the next read
    // starts at ("10px" gives 10, then "px"). No digits: 0 and a failure;
    // too large: the nearest limit and a failure.
    bool next(int& value);
    // A decimal number, as `>> float` reads it: sign, digits, a point,
    // digits, an exponent. Anything strtof cannot take whole: 0 and a
    // failure; too large: the largest float and a failure.
    bool next(float& value);

    // What is left of the line after the words read so far, its white
    // space kept, as std::getline on the stream gives it. False (and
    // `rest` as it was) after a failed read or with nothing left; getline
    // would clear `rest` when nothing is left on a stream not yet at its
    // end, which no caller relies on (autorun's `set` reads one line after
    // its command, where the end has already been reached).
    bool rest(std::string& rest);

    // False once a read has failed.
    explicit operator bool() const { return !failed_; }

private:
    // Skips white space; false (and failed) when only white space is left.
    bool skip_space();

    std::string text_;
    std::size_t at_ = 0;
    bool failed_ = false;
};

// Whether `c` is white space to operator>> in the "C" locale.
bool is_stream_space(char c);

}  // namespace riftwii
