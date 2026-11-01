// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Parental controls (riftwii/parental.hpp): ages from ratings, the PIN's
// record and the settings lines that hold it.
#include <cstdint>
#include <iostream>
#include <string>

#include "riftwii/parental.hpp"
#include "riftwii/settingsfile.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using namespace riftwii;

static void SaltZeroToFifteen(std::uint8_t (&salt)[kParentalSaltSize]) {
    for (std::size_t i = 0; i < kParentalSaltSize; ++i) salt[i] = static_cast<std::uint8_t>(i);
}

static void test_rating_ages() {
    EXPECT_EQ(rating_minimum_age("ESRB", "E"), 6);
    EXPECT_EQ(rating_minimum_age("ESRB", "E10+"), 10);
    EXPECT_EQ(rating_minimum_age("esrb", " t "), 13);
    EXPECT_EQ(rating_minimum_age("ESRB", "M"), 17);
    EXPECT_EQ(rating_minimum_age("ESRB", "RP"), -1);  // pending: not known
    EXPECT_EQ(rating_minimum_age("PEGI", "3"), 3);
    EXPECT_EQ(rating_minimum_age("PEGI", "18"), 18);
    EXPECT_EQ(rating_minimum_age("USK", "16"), 16);
    EXPECT_EQ(rating_minimum_age("CERO", "A"), 0);
    EXPECT_EQ(rating_minimum_age("CERO", "Z"), 18);
    EXPECT_EQ(rating_minimum_age("GRB", "ALL"), 0);
    EXPECT_EQ(rating_minimum_age("GRB", "19"), 18);
    EXPECT_EQ(rating_minimum_age("CSRR", "PG12"), 12);
    EXPECT_EQ(rating_minimum_age("", ""), -1);
    EXPECT_EQ(rating_minimum_age("ESRB", "K-A"), -1);
    EXPECT_EQ(rating_minimum_age("PEGI", "123"), -1);
}

static void test_pin_record() {
    std::uint8_t salt[kParentalSaltSize];
    SaltZeroToFifteen(salt);
    // Python: hashlib.pbkdf2_hmac('sha256', b'1234', bytes(range(16)), 10000)
    const std::string record = parental_pin_record("1234", salt);
    EXPECT_EQ(record, "pbkdf2-sha256$10000$000102030405060708090a0b0c0d0e0f$"
                      "91ea059bae0333a2969fc8ccd7c77851dda43f5adb96baeea246a624cdf489ec");
    // One round is HMAC alone: hashlib.pbkdf2_hmac('sha256', b'0000', bytes(range(16)), 1)
    EXPECT_EQ(parental_pin_record("0000", salt, 1),
              "pbkdf2-sha256$1$000102030405060708090a0b0c0d0e0f$"
              "85c7d304622fc0d65d64a6acd65799ecd04672f6c56d56d5119ea1de263c2d5d");
    EXPECT_TRUE(parental_record_valid(record));
    EXPECT_TRUE(parental_pin_matches(record, "1234"));
    EXPECT_TRUE(!parental_pin_matches(record, "1235"));
    EXPECT_TRUE(!parental_pin_matches(record, "123"));
    // The PIN is nowhere in the record.
    EXPECT_TRUE(record.find("1234") == std::string::npos);

    // Another salt, another record for the same PIN.
    std::uint8_t other[kParentalSaltSize] = {};
    EXPECT_TRUE(parental_pin_record("1234", other) != record);

    EXPECT_EQ(parental_pin_record("12a4", salt), "");
    EXPECT_EQ(parental_pin_record("12345", salt), "");
    EXPECT_TRUE(!parental_pin_valid(""));
    EXPECT_TRUE(parental_pin_valid("0000"));
}

static void test_bad_records() {
    EXPECT_TRUE(!parental_record_valid(""));
    EXPECT_TRUE(!parental_record_valid("1234"));
    EXPECT_TRUE(!parental_record_valid("pbkdf2-sha256$10000$0001$91ea"));
    // Rounds that would freeze the menu.
    EXPECT_TRUE(!parental_record_valid("pbkdf2-sha256$999999$000102030405060708090a0b0c0d0e0f$"
                                       "91ea059bae0333a2969fc8ccd7c77851dda43f5adb96baeea246a624cdf489ec"));
    EXPECT_TRUE(!parental_record_valid("pbkdf2-sha256$0$000102030405060708090a0b0c0d0e0f$"
                                       "91ea059bae0333a2969fc8ccd7c77851dda43f5adb96baeea246a624cdf489ec"));
    // Upper-case hex and an extra field are not what RiftWii writes.
    EXPECT_TRUE(!parental_record_valid("pbkdf2-sha256$10000$000102030405060708090A0B0C0D0E0F$"
                                       "91ea059bae0333a2969fc8ccd7c77851dda43f5adb96baeea246a624cdf489ec"));
    EXPECT_TRUE(!parental_record_valid("pbkdf2-sha256$10000$000102030405060708090a0b0c0d0e0f$"
                                       "91ea059bae0333a2969fc8ccd7c77851dda43f5adb96baeea246a624cdf489ec$x"));
}

static void test_verdicts() {
    ParentalRules rules;
    EXPECT_TRUE(parental_verdict(rules, 18) == ParentalVerdict::Allowed);  // no PIN: nothing locked
    rules.active = true;
    rules.highest_age = 12;
    EXPECT_TRUE(parental_verdict(rules, 12) == ParentalVerdict::Allowed);
    EXPECT_TRUE(parental_verdict(rules, 13) == ParentalVerdict::Locked);
    EXPECT_TRUE(parental_verdict(rules, -1) == ParentalVerdict::Allowed);
    rules.restrict_unrated = true;
    EXPECT_TRUE(parental_verdict(rules, -1) == ParentalVerdict::Locked);
    rules.hide = true;
    EXPECT_TRUE(parental_verdict(rules, 17) == ParentalVerdict::Hidden);
    EXPECT_TRUE(parental_verdict(rules, 0) == ParentalVerdict::Allowed);
}

static void test_settings_lines() {
    std::uint8_t salt[kParentalSaltSize];
    SaltZeroToFifteen(salt);
    const std::string record = parental_pin_record("4321", salt);

    LoaderSettings settings;
    settings.parse("parental_pin = " + record + "\nparental_mode = hide\nparental_age = 7\nparental_unrated = restrict\n");
    EXPECT_EQ(settings.parental_pin, record);
    EXPECT_EQ(settings.parental_mode, "hide");
    EXPECT_EQ(settings.parental_age, 7);
    EXPECT_EQ(settings.parental_unrated, "restrict");
    ParentalRules rules = parental_rules(settings);
    EXPECT_TRUE(rules.active && rules.hide && rules.restrict_unrated && rules.highest_age == 7);

    // Written and read back the same.
    LoaderSettings again;
    again.parse(settings.serialize());
    EXPECT_EQ(again.parental_pin, record);
    EXPECT_EQ(again.parental_mode, "hide");
    EXPECT_EQ(again.parental_age, 7);

    // A reset from the menu keeps the controls.
    const LoaderSettings reset = settings_after_reset(settings);
    EXPECT_EQ(reset.parental_pin, record);
    EXPECT_EQ(reset.parental_mode, "hide");

    // The PIN's line deleted: no controls, whatever the others say.
    LoaderSettings deleted;
    deleted.parse("parental_mode = hide\nparental_age = 7\nparental_unrated = restrict\n");
    EXPECT_TRUE(!parental_rules(deleted).active);
    EXPECT_TRUE(deleted.serialize().find("parental_pin") == std::string::npos);

    // A line that is not a record (a PIN typed in by hand) is no PIN either.
    LoaderSettings typed;
    typed.parse("parental_pin = 1234\nparental_age = 11\n");
    EXPECT_TRUE(typed.parental_pin.empty());
    EXPECT_EQ(typed.parental_age, 12);  // 11 is not one of the choices
}

int main() {
    test_rating_ages();
    test_pin_record();
    test_bad_records();
    test_verdicts();
    test_settings_lines();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "parental: all passed" << std::endl;
    return 0;
}
