// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/hidconfig.hpp"
#include "rtgcad.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "riftwii/gccrash.hpp"

namespace {

unsigned failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

// These are test fixtures written for RiftWii, not upstream config files.
std::string Config(const std::string& extra = {}) {
    return "[Test pad]\nVID=1234\nPID=5678\nPolltype=1\nDigitalLR=1\n"
           "A=0,01\nB=0,02\nX=0,04\nY=0,08\nZ=0,10\nS=0,20\nPower=0,40\n"
           "L=1,01\nR=1,02\nZL=1,04\nLeft=2,01\nDown=2,02\nRight=2,04\nUp=2,08\n"
           "StickX=3,08,100\nStickY=4,08,100\nCStickX=5\nCStickY=6\n" + extra;
}

rthid_mapping Parse(const std::string& text) {
    rthid_mapping mapping{};
    std::string error;
    const bool valid = riftwii::parse_hid_config(text, mapping, error);
    if (!valid) std::cerr << error << '\n';
    CHECK(valid);
    CHECK(rthid_valid(&mapping));
    return mapping;
}

void Reject(const std::string& text) {
    rthid_mapping mapping{};
    mapping.vid = 0xFFFF;
    std::string error;
    CHECK(!riftwii::parse_hid_config(text, mapping, error));
    CHECK(!error.empty());
    CHECK(mapping.vid == 0);
}

void ParserAndReports() {
    const auto mapping = Parse(Config("# comment\r\nMultiIn=0\n"));
    CHECK(mapping.report_bytes == 7);
    CHECK(mapping.axes[0].deadzone == 8);
    CHECK(mapping.axes[0].scale == 1066); // decimal radius, hex deadzone
    std::array<std::uint8_t, RTHID_REPORT_MAX> report{};
    report[0] = 0x3F;
    report[1] = 3;
    report[2] = 9;
    report[3] = 128;
    report[4] = 127;
    report[5] = 128;
    report[6] = 127;
    gcad_pad pad{};
    CHECK(rthid_decode(&mapping, report.data(), 7, &pad));
    CHECK(pad.buttons == (GCAD_PAD_A | GCAD_PAD_B | GCAD_PAD_X | GCAD_PAD_Y | GCAD_PAD_Z |
                          GCAD_PAD_START | GCAD_PAD_L | GCAD_PAD_R | GCAD_PAD_LEFT | GCAD_PAD_UP));
    CHECK(pad.trigger_l == 255 && pad.trigger_r == 255);
    CHECK(pad.stick_x == 0 && pad.stick_y == 0);
    CHECK(pad.substick_x == 0 && pad.substick_y == 0);
    CHECK(pad.wireless == 1);
    report[1] = 7;
    report[3] = 255;
    report[4] = 0;
    CHECK(rthid_decode(&mapping, report.data(), 7, &pad));
    CHECK(!(pad.buttons & (GCAD_PAD_L | GCAD_PAD_R)));
    CHECK(pad.trigger_l == 127 && pad.trigger_r == 127);
    CHECK(pad.stick_x == 126 && pad.stick_y == 126);
    report[3] = 136;
    report[4] = 119;
    CHECK(rthid_decode(&mapping, report.data(), 7, &pad));
    CHECK(pad.stick_x == 0 && pad.stick_y == 0);
    CHECK(!rthid_decode(&mapping, report.data(), 6, &pad));
    CHECK(!rthid_decode(&mapping, report.data(), RTHID_REPORT_MAX + 1, &pad));

    auto analog_text = Config("LAnalog=7\nRAnalog=8\n");
    analog_text.replace(analog_text.find("DigitalLR=1"), 11, "DigitalLR=2");
    analog_text.replace(analog_text.find("L=1,01"), 6, "L=7,F0");
    analog_text.replace(analog_text.find("R=1,02"), 6, "R=8,F0");
    const auto analog = Parse(analog_text);
    report[7] = 239;
    report[8] = 240;
    CHECK(rthid_decode(&analog, report.data(), 9, &pad));
    CHECK(!(pad.buttons & GCAD_PAD_L) && (pad.buttons & GCAD_PAD_R));
    CHECK(pad.trigger_l == 236 && pad.trigger_r == 237);
    report[7] = 26;
    report[8] = 255;
    CHECK(rthid_decode(&analog, report.data(), 9, &pad));
    CHECK(pad.trigger_l == 0 && pad.trigger_r == 254);

    const auto hat = Parse("VID=1234\nPID=5678\nPolltype=1\nDPAD=1\n"
                           "Up=0,00\nRightUp=0,01\nRight=0,02\nDownRight=0,03\n"
                           "Down=0,04\nDownLeft=0,05\nLeft=0,06\nUpLeft=0,07\n");
    const std::array<std::uint16_t, 9> directions = {
        GCAD_PAD_UP, GCAD_PAD_UP | GCAD_PAD_RIGHT, GCAD_PAD_RIGHT,
        GCAD_PAD_RIGHT | GCAD_PAD_DOWN, GCAD_PAD_DOWN, GCAD_PAD_DOWN | GCAD_PAD_LEFT,
        GCAD_PAD_LEFT, GCAD_PAD_LEFT | GCAD_PAD_UP, 0
    };
    for (unsigned i = 0; i < directions.size(); ++i) {
        report[0] = static_cast<std::uint8_t>(i | 0xA0);
        CHECK(rthid_decode(&hat, report.data(), 1, &pad));
        CHECK(pad.buttons == directions[i]);
    }
    const auto filtered = Parse(Config("MultiIn=1\nMultiInValue=AA\n"));
    report[0] = 0xAB;
    CHECK(!rthid_decode(&filtered, report.data(), 7, &pad));
    report[0] = 0xAA;
    CHECK(rthid_decode(&filtered, report.data(), 7, &pad));
    const auto rumble = Parse(Config("Rumble=1\nRumbleType=1\nRumbleDataLen=4\n"
                                     "RumbleDataOn=1,2,3,4\nRumbleDataOff=0,0,0,0\n"
                                     "RumbleTransferLen=2\nRumbleTransfers=2\n"));
    CHECK(rumble.rumble_on[3] == 4);
    CHECK(rumble.rumble_count == 2);
    Reject(Config("A=0,02\n"));
    Reject(Config("Unknown=1\n"));
    Reject(Config("MultiIn=4\n"));
    Reject(Config("Polltype=0\n"));
    Reject(Config("LAnalog=80\n"));
    Reject(Config("Rumble=1\nRumbleDataLen=81\n"));
    Reject(Config("Rumble=1\nRumbleDataLen=2\nRumbleDataOn=1,2,3\nRumbleDataOff=0,0\n"));
    Reject(Config("RumbleDataLen=1\n"));
    Reject(Config("[Second pad]\n"));
    Reject("VID=10000\nPID=1\nPolltype=1\n");
    Reject("VID=1\nPID=1\nPolltype=1\nA=0,100\n");
    Reject("VID=1\nPID=1\nPolltype=1\nA=0,1,2\n");
    Reject("VID=1\nPID=1\nPolltype=1\nStickX=0,80\n");
    Reject("VID=1\nPID=1\nPolltype=1\nStickX=0,0,-1\n");
    Reject("VID=1\nPID=1\nPolltype=1\nStickX=0,0,1001\n");
    Reject("VID=1\nPID=1\nPolltype=1\nStickX=0,0,\n");
    CHECK(Parse("VID=1\nPID=1\nPolltype=1\nDPAD=1\n").dpad_mask == 15);
    Reject("VID=1\nPID=1\nPolltype=1\nDPAD=\n");
    Reject(std::string(8193, '#'));
    Reject(std::string("VID=1\0PID=1", 11));
    Reject("VID=FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF\nPID=1\nPolltype=1\n");

    // Validate the binary boundary again inside the runtime, including
    // offsets in unused fields and the multiplication for rumble lengths.
    auto bad = mapping;
    bad.buttons[RTHID_POWER].offset = 128;
    CHECK(!rthid_valid(&bad));
    bad = mapping;
    bad.bytes--;
    CHECK(!rthid_valid(&bad));
    bad = mapping;
    bad.axes[0].scale = 0xFFFFFFFF;
    CHECK(!rthid_valid(&bad));
    bad = rumble;
    bad.rumble_count = 65535;
    CHECK(!rthid_valid(&bad));
    rthid_catalog catalog{};
    catalog.maps[0] = mapping;
    catalog.count = 1;
    CHECK(rthid_catalog_valid(&catalog));
    CHECK(rthid_find(&catalog, 0x12345678) == &catalog.maps[0]);
    catalog.known_count = 33;
    CHECK(!rthid_catalog_valid(&catalog));
    catalog.known_count = 0;
    catalog.count = 17;
    CHECK(!rthid_catalog_valid(&catalog));
}

struct Request {
    bool pending = false;
    gcad* driver = nullptr;
    std::uint32_t tag = 0, command = 0;
    void* output = nullptr;
    std::uint32_t bytes = 0;
    std::array<std::uint8_t, 128> payload{};
};

struct Bus {
    gcad_hub hub{};
    std::array<Request, GCAD_REQUESTS> requests{};
    unsigned finishes = 0;
    unsigned missing = 0;
    unsigned found = 0;
    unsigned lost = 0;
    struct Diagnostic { unsigned step, detail; int result; };
    std::vector<Diagnostic> diagnostics;
    std::vector<Request> outputs;
    unsigned refuse_slot = GCAD_REQUESTS;
    std::uint32_t clock = 100;

    explicit Bus(unsigned version = 5, const std::string& config = Config()) {
        hub.catalog.maps[0] = Parse(config);
        hub.catalog.count = 1;
        gcad_hub_init(&hub, this, 42, version, 1);
        gcad_tick(&hub.adapter, clock);
    }

    void Reply(unsigned slot, int result = 0) {
        const auto request = requests[slot];
        CHECK(request.pending);
        requests[slot].pending = false;
        gcad_on_reply(&hub.adapter, request.tag, result, clock);
    }

    static void Descriptors(std::uint8_t* data, unsigned start) {
        data[start] = 18;
        data[start + 1] = 1;
        data[start + 20] = 9;
        data[start + 21] = 2;
        data[start + 32] = 9;
        data[start + 33] = 4;
        data[start + 37] = 3;
        data[start + 44] = 7;
        data[start + 45] = 5;
        data[start + 46] = 0x83;
        data[start + 47] = 3;
        data[start + 49] = 64;
        data[start + 52] = 7;
        data[start + 53] = 5;
        data[start + 54] = 0x04;
        data[start + 55] = 3;
        data[start + 57] = 64;
    }

    void List(const std::vector<rthid_device>& devices, unsigned packet_bytes = 64) {
        auto* list = hub.adapter.change;
        std::memset(list, 0, sizeof(hub.adapter.change));
        if (hub.adapter.version == 5) {
            for (unsigned i = 0; i < devices.size(); ++i) {
                gcad_put32(list + i * 12, devices[i].id);
                gcad_put32(list + i * 12 + 4, devices[i].vid_pid);
            }
            Reply(GCAD_TAG_CHANGE, static_cast<int>(devices.size()));
        } else {
            unsigned offset = 0;
            for (const auto& device : devices) {
                gcad_put32(list + offset, 68);
                gcad_put32(list + offset + 4, device.id);
                Descriptors(list + offset, 8);
                list[offset + 8 + 49] = static_cast<std::uint8_t>(packet_bytes);
                gcad_put32(list + offset + 16, device.vid_pid);
                offset += 68;
            }
            gcad_put32(list + offset, 0xFFFFFFFF);
            Reply(GCAD_TAG_CHANGE);
        }
    }

    void Settle() {
        clock += 600;
        for (unsigned step = 0; step < 20; ++step) {
            gcad_tick(&hub.adapter, clock);
            for (unsigned slot = 0; slot < requests.size(); ++slot) {
                auto& request = requests[slot];
                if (!request.pending || (slot % GCAD_TAGS) == GCAD_TAG_IN) continue;
                if ((slot % GCAD_TAGS) == GCAD_TAG_CHANGE) {
                    if (request.command != GCAD_V5_ATTACH_FINISH) continue;
                    for (const auto& pad : hub.pads) CHECK(pad.link != GCAD_LINK_SETUP && pad.link != GCAD_LINK_SETTLE);
                    ++finishes;
                }
                if (hub.adapter.version == 5 && request.command == GCAD_V5_GET_DEVICE_INFO) {
                    Descriptors(static_cast<std::uint8_t*>(request.output), 36);
                }
                const bool wake = request.driver->mapping && (slot % GCAD_TAGS) == GCAD_TAG_SETUP &&
                                  request.driver->setup_step == GCAD_STEP_CTRL;
                if (wake) {
                    const auto* message = request.driver->out_msg;
                    const unsigned offset = hub.adapter.version == 5 ? 8 : 20;
                    CHECK(message[offset] == 0xA1 && message[offset + 1] == 1);
                    CHECK(message[offset + 2] == 3 && message[offset + 3] == 0xF2);
                    CHECK(hub.adapter.version == 4 ? message[27] == 17 : request.bytes == 17);
                    CHECK(!requests[(slot / GCAD_TAGS) * GCAD_TAGS + GCAD_TAG_IN].pending);
                }
                Reply(slot, wake ? 17 : 0);
            }
        }
    }

    void Report(unsigned pad, std::uint8_t button) {
        auto& driver = hub.pads[pad];
        std::memset(driver.in_data, 128, sizeof(driver.in_data));
        driver.in_data[0] = button;
        driver.in_data[1] = 0;
        driver.in_data[2] = 0;
        driver.in_data[4] = driver.in_data[6] = 127;
        Reply((pad + 1) * GCAD_TAGS + GCAD_TAG_IN, 64);
    }

    unsigned Count(unsigned step) const {
        unsigned count = 0;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.step == step) ++count;
        }
        return count;
    }
};

void LegacyConfigs() {
    // Field values from the two reported Nintendont configurations. Keep
    // these fixtures local: the tests do not depend on a reference checkout.
    const auto saitek = Parse("[Saitek P990]\nVID=06A3\nPID=040B\nPolltype=1\nDPAD=1\nDigitalLR=0\n"
        "Power=7,20\nA=6,4\nB=6,2\nX=7,1\nY=7,2\nZ=6,20\nL=6,40\nR=6,80\nS=7,10\n"
        "Left=5,7\nDown=5,5\nRight=5,3\nUp=5,1\nStickX=0\nStickY=1\nCStickX=2\nCStickY=3\n"
        "RAnalog=7,8\nLAnalog=7,4\n");
    CHECK(saitek.analog_l == 7 && saitek.analog_r == 7 && saitek.report_bytes == 8);
    CHECK(saitek.buttons[RTHID_RIGHT_UP].offset == 0 && saitek.buttons[RTHID_RIGHT_UP].mask == 0);
    CHECK(saitek.dpad_mask == 255);
    const auto gravis = Parse("[Gravis AfterShock]\nVID=047D\nPID=4006\nPolltype=1\nDPAD=1\n"
        "B=6,02\nA=6,04\nY=6,01\nX=6,08\nL=6,10\nZL=6,40\nZ=6,80\nR=6,20\nS=7,02\nPower=7,03\n"
        "Left=4,00\nDown=5,FF\nRight=4,FF\nUp=5,00\nStickX=0\nStickY=1\nCStickX=3\nCStickY=2\n"
        "DigitalLR=1\nLAnalog=0\nRAnalog=0\nRumble=1\nRumbleType=0\nRumbleDataLen=05\n"
        "RumbleDataOn=01,00,00,7F,7F\nRumbleDataOff=01,00,00,00,00\nRumbleTransferLen=05\nRumbleTransfers=1\n");
    CHECK(gravis.dpad_mask == 255 && gravis.rumble_bytes == 5);
    std::uint8_t report[8] = {128, 127, 128, 127, 0, 255, 4, 0};
    gcad_pad pad{};
    CHECK(rthid_decode(&gravis, report, sizeof(report), &pad));
    CHECK(pad.buttons == (GCAD_PAD_A | GCAD_PAD_LEFT | GCAD_PAD_DOWN));
    Reject("VID=1\nPID=1\nPolltype=1\nLAnalog=80,4\n");
    Reject("VID=1\nPID=1\nPolltype=1\nLAnalog=7,100\n");
}

void SetupDiagnostics(unsigned version) {
    const unsigned setup = GCAD_TAGS + GCAD_TAG_SETUP;
    const unsigned input = GCAD_TAGS + GCAD_TAG_IN;
    const unsigned output = GCAD_TAGS + GCAD_TAG_OUT;
    Bus bus(version, "VID=054C\nPID=0268\nPolltype=0\nA=2,40\nLAnalog=30\n");
    bus.hub.adapter.adapter_disabled = 1;
    bus.List({{11, 0x054C0268}, {12, GCAD_VID_PID}});
    bus.Settle();
    CHECK(bus.hub.adapter.dev_id == -1);
    CHECK(bus.hub.pads[0].link == GCAD_LINK_POLL);
    CHECK(bus.outputs.size() == 1);
    if (!bus.outputs.empty()) {
        const auto& wake = bus.outputs[0];
        CHECK(wake.bytes == 49 && wake.payload[0] == 1 && wake.payload[10] == 2);
        CHECK(wake.payload[2] == 255 && wake.payload[3] == 0 && wake.payload[4] == 255 && wake.payload[5] == 0);
        CHECK(wake.payload[11] == 255 && wake.payload[12] == 0x27 && wake.payload[15] == 0x32);
    }
    CHECK(bus.Count(GCAD_HID_WAKE) == 1 && bus.Count(GCAD_HID_OUTPUT) == 1 && bus.Count(GCAD_HID_POLL) == 1);
    bus.clock += 2100;
    gcad_tick(&bus.hub.adapter, bus.clock);
    gcad_tick(&bus.hub.adapter, bus.clock + 1);
    CHECK(bus.Count(GCAD_HID_SILENT) == 1);
    bus.Reply(input, 4);
    bus.Reply(input, 4);
    CHECK(bus.Count(GCAD_HID_SHORT) == 1);
    bus.hub.pads[0].in_data[2] = 0x40;
    bus.Reply(input, 49);
    CHECK(bus.Count(GCAD_HID_INPUT) == 1 && bus.requests[output].pending);
    CHECK(bus.requests[output].payload[10] == 2);
    bus.Reply(output, 49);
    CHECK(bus.Count(GCAD_HID_LED) == 1);
    bus.Reply(input, -7022);
    CHECK(bus.Count(GCAD_HID_POLL_ERROR) == 1);

    Bus small(version);
    small.List({{11, 0x12345678}}, 4);
    if (version == 5) {
        small.clock += 600;
        gcad_tick(&small.hub.adapter, small.clock);
        small.Reply(setup, -4); // already resumed: still request device info
        Bus::Descriptors(small.hub.pads[0].setup_out, 36);
        small.hub.pads[0].setup_out[36 + 49] = 4;
        small.Reply(setup);
    } else {
        small.hub.adapter.descriptors[0].packet_bytes = 4;
        gcad_refresh_hid(&small.hub.adapter, small.clock);
    }
    CHECK(small.Count(GCAD_HID_DESCRIPTORS) == 1);
    CHECK(!small.requests[input].pending);
    CHECK(small.hub.pads[0].link == GCAD_LINK_FAILED || small.hub.pads[0].link == GCAD_LINK_BUSY);
}

void SetupFailures() {
    const unsigned setup = GCAD_TAGS + GCAD_TAG_SETUP;
    for (unsigned stage = 0; stage < 5; ++stage) {
        Bus bus(5, "VID=054C\nPID=0268\nPolltype=0\nA=2,40\n");
        bus.List({{11, 0x054C0268}});
        if (stage == 0) bus.refuse_slot = setup;
        bus.clock += 600;
        gcad_tick(&bus.hub.adapter, bus.clock);
        if (stage == 0) {
            CHECK(bus.Count(GCAD_HID_RESUME) == 1);
            continue;
        }
        if (stage == 1) bus.refuse_slot = setup;
        bus.Reply(setup);
        if (stage == 1) {
            CHECK(bus.Count(GCAD_HID_INFO) == 1);
            continue;
        }
        Bus::Descriptors(bus.hub.pads[0].setup_out, 36);
        if (stage == 2) bus.refuse_slot = setup;
        bus.Reply(setup);
        if (stage == 2) {
            CHECK(bus.Count(GCAD_HID_WAKE) == 1);
            continue;
        }
        if (stage == 3) bus.refuse_slot = GCAD_TAGS + GCAD_TAG_OUT;
        bus.Reply(setup, 17);
        if (stage == 3) {
            CHECK(bus.Count(GCAD_HID_OUTPUT) == 1);
            continue;
        }
        bus.refuse_slot = GCAD_TAGS + GCAD_TAG_IN;
        bus.Reply(GCAD_TAGS + GCAD_TAG_OUT, 49);
        CHECK(bus.Count(GCAD_HID_POLL) == 1);
    }

    Bus waiting(5, "VID=054C\nPID=0268\nPolltype=0\nA=2,40\n");
    waiting.List({{11, 0x054C0268}});
    waiting.clock += 600;
    gcad_tick(&waiting.hub.adapter, waiting.clock);
    waiting.clock += 2100;
    gcad_tick(&waiting.hub.adapter, waiting.clock);
    gcad_tick(&waiting.hub.adapter, waiting.clock + 1);
    CHECK(waiting.Count(GCAD_HID_TIMEOUT) == 1);
    CHECK(waiting.diagnostics.back().detail == GCAD_STEP_RESUME);

    Bus failed_info;
    failed_info.List({{11, 0x12345678}});
    failed_info.clock += 600;
    gcad_tick(&failed_info.hub.adapter, failed_info.clock);
    failed_info.Reply(setup);
    failed_info.Reply(setup, -6);
    CHECK(failed_info.Count(GCAD_HID_INFO) == 1);
    CHECK(failed_info.hub.pads[0].link == GCAD_LINK_BUSY);

    Bus no_endpoints;
    no_endpoints.List({{11, 0x12345678}});
    no_endpoints.clock += 600;
    gcad_tick(&no_endpoints.hub.adapter, no_endpoints.clock);
    no_endpoints.Reply(setup);
    no_endpoints.Reply(setup); // all-zero device-info reply
    CHECK(no_endpoints.Count(GCAD_HID_DESCRIPTORS) == 1);
    CHECK(no_endpoints.hub.pads[0].link == GCAD_LINK_BUSY);
}

void SharedDriver(unsigned version) {
    Bus bus(version);
    bus.List({{11, 0x12345678}, {12, 0x12345678}, {13, 0x99990001}});
    bus.Settle();
    CHECK(bus.hub.pads[0].link == GCAD_LINK_POLL);
    CHECK(bus.hub.pads[1].link == GCAD_LINK_POLL);
    CHECK(bus.hub.pads[0].endpoint_in == 0x83);
    CHECK(bus.hub.pads[0].endpoint_out == 4);
    CHECK(bus.missing == 1 && bus.found == 2);
    CHECK(version == 4 || bus.finishes == 1);
    bus.Report(0, 1);
    bus.Report(1, 2);
    gcad_pad pads[4]{};
    CHECK(gcad_read_ports(&bus.hub.adapter, 1, pads, bus.clock) == 6);
    CHECK(pads[1].buttons == GCAD_PAD_A && pads[2].buttons == GCAD_PAD_B);
    // Adapter first, even when a native pad occupies its usual port.
    auto& adapter = bus.hub.adapter;
    adapter.link = GCAD_LINK_POLL;
    adapter.stale = 0;
    adapter.data_time = bus.clock;
    adapter.raw[1][0] = 0x10;
    adapter.raw[1][1] = 4;
    adapter.origin_valid[1] = 1;
    CHECK(gcad_read_ports(&adapter, 2, pads, bus.clock) == 13);
    CHECK(pads[0].buttons == GCAD_PAD_X);
    CHECK(pads[2].buttons == GCAD_PAD_A && pads[3].buttons == GCAD_PAD_B);
    gcad_rumble(&adapter, 0, 1);
    CHECK(adapter.rumble_want[1] == 1);
    adapter.link = GCAD_LINK_NONE;
    bus.List({{12, 0x12345678}, {13, 0x99990001}});
    CHECK(bus.lost == 1 && bus.missing == 1);
    // A delayed report from the unplugged link cannot resurrect it.
    bus.Reply(GCAD_TAGS + GCAD_TAG_IN, 64);
    CHECK(bus.hub.pads[0].stale);
    CHECK(gcad_read_ports(&adapter, 0, pads, bus.clock) == 1);
    CHECK(pads[0].buttons == GCAD_PAD_B);
    CHECK(gcad_read_ports(&adapter, 0, pads, bus.clock + GCAD_TIMEOUT_MS) == 0);
}

void CompositeV4() {
    Bus bus(4);
    bus.List({{11, 0x12345678}, {12, 0x12345678}, {13, 0x12345678}, {14, 0x12345678}});
    for (unsigned i = 0; i < 3; ++i) {
        auto& info = bus.hub.adapter.descriptors[i];
        info.interface_number = static_cast<std::uint8_t>(i);
        info.interface_class = 1;
    }
    gcad_refresh_hid(&bus.hub.adapter, bus.clock);
    bus.Settle();
    CHECK(bus.hub.adapter.excluded_count == 3);
    for (unsigned i = 0; i < 3; ++i) CHECK(bus.hub.pads[i].dev_id == -1);
    CHECK(bus.hub.pads[3].link == GCAD_LINK_POLL);
    // The same list does not bring the audio interfaces back.
    gcad_refresh_hid(&bus.hub.adapter, bus.clock);
    for (unsigned i = 0; i < 3; ++i) CHECK(bus.hub.pads[i].dev_id == -1);
    CHECK(bus.Count(GCAD_HID_INTERFACE) == 3);
    bus.List({{14, 0x12345678}});
    CHECK(bus.hub.adapter.excluded_count == 0);
}

void RumbleAndControl() {
    Bus bus(5, Config("Rumble=1\nRumbleType=1\nRumbleDataLen=4\nRumbleDataOn=AA,BB,CC,DD\n"
                      "RumbleDataOff=0,0,0,0\nRumbleTransferLen=2\nRumbleTransfers=2\n"));
    bus.List({{11, 0x12345678}});
    bus.Settle();
    bus.Report(0, 1);
    bus.Settle(); // complete the initial motor-off command
    gcad_pad pads[4]{};
    gcad_read_ports(&bus.hub.adapter, 0, pads, bus.clock);
    gcad_rumble(&bus.hub.adapter, 0, 1);
    bus.Report(0, 1);
    auto& out = bus.requests[GCAD_TAGS + GCAD_TAG_OUT];
    CHECK(out.pending && out.bytes == 2 && out.payload[0] == 0xAA && out.payload[1] == 0xBB);
    bus.Reply(GCAD_TAGS + GCAD_TAG_OUT);
    CHECK(out.pending && out.payload[0] == 0xCC && out.payload[1] == 0xDD);
    bus.Reply(GCAD_TAGS + GCAD_TAG_OUT);
    CHECK(bus.hub.pads[0].rumble_sent[0] == 1);

    Bus control(5, "VID=054C\nPID=0268\nPolltype=0\nA=2,40\n");
    control.List({{11, 0x054C0268}});
    control.Settle();
    CHECK(control.hub.pads[0].link == GCAD_LINK_POLL);
    const auto& in = control.requests[GCAD_TAGS + GCAD_TAG_IN];
    CHECK(in.pending && in.command == GCAD_V5_CONTROL && in.bytes == 49);
    control.hub.pads[0].in_data[2] = 0x40;
    control.Reply(GCAD_TAGS + GCAD_TAG_IN, 49);
    CHECK(gcad_read_ports(&control.hub.adapter, 0, pads, control.clock) == 1);
    CHECK(pads[0].buttons == GCAD_PAD_A);
}

}  // namespace

extern "C" {
int32_t gcad_env_ioctl(gcad* driver, uint32_t tag, uint32_t command, void*, uint32_t, void* out, uint32_t bytes) {
    auto& bus = *static_cast<Bus*>(driver->env_ctx);
    if (bus.refuse_slot == GCAD_REQUEST_SLOT(tag)) return -22;
    auto& request = bus.requests[GCAD_REQUEST_SLOT(tag)];
    CHECK(!request.pending);
    request = {};
    request.pending = true;
    request.driver = driver;
    request.tag = tag;
    request.command = command;
    request.output = out;
    request.bytes = bytes;
    if (driver->version == 4 && command == GCAD_V4_INTERRUPT_OUT) {
        request.bytes = gcad_get32(driver->out_msg + 24);
        std::memcpy(request.payload.data(), driver->out_data, request.bytes);
        bus.outputs.push_back(request);
    }
    return 0;
}

int32_t gcad_env_ioctlv(gcad* driver, uint32_t tag, uint32_t command, void*, uint32_t, void* data,
                       uint32_t bytes, int data_in) {
    const auto result = gcad_env_ioctl(driver, tag, command, nullptr, 0, data, bytes);
    if (result < 0) return result;
    auto& bus = *static_cast<Bus*>(driver->env_ctx);
    if (data_in && bytes) std::memcpy(bus.requests[GCAD_REQUEST_SLOT(tag)].payload.data(), data, bytes);
    if (data_in && command == GCAD_V5_INTERRUPT) bus.outputs.push_back(bus.requests[GCAD_REQUEST_SLOT(tag)]);
    return 0;
}
uint32_t gcad_env_address(gcad*, void*) { return 0; }
void gcad_env_flush(gcad*, void*, uint32_t) {}
void gcad_env_invalidate(gcad*, void*, uint32_t) {}
void gcad_env_event(gcad* driver, uint32_t code, uint32_t a, int32_t b) {
    auto& bus = *static_cast<Bus*>(driver->env_ctx);
    if (code == GCAD_EV_HID_MISSING) ++bus.missing;
    if (code == GCAD_EV_HID_FOUND) ++bus.found;
    if (code == GCAD_EV_HID_LOST) ++bus.lost;
    if (code == GCAD_EV_HID_DIAG) bus.diagnostics.push_back({a & 255u, a >> 8, b});
}
}

int main() {
    ParserAndReports();
    LegacyConfigs();
    SetupDiagnostics(4);
    SetupDiagnostics(5);
    SetupFailures();
    SharedDriver(4);
    SharedDriver(5);
    CompositeV4();
    RumbleAndControl();
    if (failures) std::cerr << failures << " failures\n";
    return failures ? 1 : 0;
}
