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
    Reject("VID=1\nPID=1\nPolltype=1\nDPAD=1\n");
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

    void List(const std::vector<rthid_device>& devices) {
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
                Reply(slot);
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
};

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
    }
    return 0;
}

int32_t gcad_env_ioctlv(gcad* driver, uint32_t tag, uint32_t command, void*, uint32_t, void* data,
                       uint32_t bytes, int data_in) {
    gcad_env_ioctl(driver, tag, command, nullptr, 0, data, bytes);
    auto& bus = *static_cast<Bus*>(driver->env_ctx);
    if (data_in && bytes) std::memcpy(bus.requests[GCAD_REQUEST_SLOT(tag)].payload.data(), data, bytes);
    return 0;
}
uint32_t gcad_env_address(gcad*, void*) { return 0; }
void gcad_env_flush(gcad*, void*, uint32_t) {}
void gcad_env_invalidate(gcad*, void*, uint32_t) {}
void gcad_env_event(gcad* driver, uint32_t code, uint32_t, int32_t) {
    auto& bus = *static_cast<Bus*>(driver->env_ctx);
    if (code == GCAD_EV_HID_MISSING) ++bus.missing;
    if (code == GCAD_EV_HID_FOUND) ++bus.found;
    if (code == GCAD_EV_HID_LOST) ++bus.lost;
}
}

int main() {
    ParserAndReports();
    SharedDriver(4);
    SharedDriver(5);
    RumbleAndControl();
    if (failures) std::cerr << failures << " failures\n";
    return failures ? 1 : 0;
}
