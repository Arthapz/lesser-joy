module;

#include <io.h>

#include <stormkit/core/contract_macro.hpp>
#include <stormkit/core/try_expected.hpp>

export module lj.cli.shell;

import std;

import stormkit.core;

import frozen;

import lj.cli.hid;
import lj.cli.log;

using namespace stormkit;

using namespace stormkit::literals;
using namespace std::literals;

export namespace lj {
    auto shell(dynarray<hid_device_entry> entries) -> system_result<void>;
} // namespace lj

////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
////////////////////////////////////////////////////////////////////

module: private;

namespace stdr = std::ranges;

using namespace std::literals;

namespace lj {
    struct state {
        array<char, 512>           command_buffer;
        dynarray<hid_device_entry> entries;
        defer_init<hid_device>     current_device;
    };

    auto print_help(state&) -> string;
    auto select_device(state&) -> string;
    auto send_output_report(state&) -> string;
    auto send_rumble_test(state&) -> string;
    auto show_devices(state&) -> string;

    namespace {
        struct command {
            string_view                         description;
            std23::function_ref<string(state&)> fn;
        };

        const auto commands = static_hash_map<frozen::string, command, 6> {
            { "help",             command { .description = "print help"sv, .fn = print_help }                             },
            { "devices",          command { .description = "show all available devices"sv, .fn = show_devices }           },
            { "select",           command { .description = "select current device"sv, .fn = select_device }               },
            { "send report",      command { .description = "send an output report"sv, .fn = send_output_report }          },
            { "send rumble test", command { .description = "send rumble test output reports"sv, .fn = send_rumble_test }  },
            { "exit",
             command { .description = "exit lesserjoy cli"sv, .fn = [](auto&&) static noexcept -> string { return {}; } } },
        };

        constinit auto help_string = string {};
    } // namespace

    auto print_help(state& state) -> string {
        if (stdr::empty(help_string)) {
            help_string = "available commands:";
            for (auto [name, command] : commands) {
                help_string += std::format("\n    - {: <30}{}",
                                           std::format("{}", green_text_style | string_view { name }),
                                           command.description);
            }
        }

        return help_string;
    }

    auto show_devices(state& state) -> string {
        auto out = "available devices:"s;
        auto i   = 0;
        for (const auto& entry : state.entries) {
            out += std::format("\n{} - {}\n    bus:              {}\n    serial number:    {}\n    release number:   {}\n    "
                               "interface number: {}\n    path:             {}",
                               green_text_style | i++,
                               entry.product_string,
                               yellow_text_style | entry.bus,
                               yellow_text_style | entry.serial_number,
                               yellow_text_style | entry.release_number,
                               yellow_text_style | entry.interface_number,
                               yellow_text_style | entry.path);
        }
        return out;
    }

    auto send_output_report(state& state) -> string {
        if (not state.current_device->is_open()) return std::format("{}", red_text_style | "no device selected!");

        return {};
    }

    auto send_rumble_test(state& state) -> string {
        if (not state.current_device->is_open()) return std::format("{}", red_text_style | "no device selected!");

        const auto haptic_patterns = array {
            into<array>(as_bytes, { 0x93, 0x35, 0x36, 0x1c, 0x0d }), into<array>(as_bytes, { 0xa8, 0x29, 0xc5, 0xdc, 0x0c }),
            into<array>(as_bytes, { 0x75, 0x21, 0xb5, 0x5d, 0x13 }), into<array>(as_bytes, { 0x75, 0xf5, 0x70, 0x1e, 0x11 }),
            into<array>(as_bytes, { 0xba, 0x55, 0x40, 0x1e, 0x08 }), into<array>(as_bytes, { 0x90, 0x31, 0x10, 0x9e, 0x00 }),
            into<array>(as_bytes, { 0x90, 0x15, 0x10, 0x9e, 0x00 }), into<array>(as_bytes, { 0x90, 0x15, 0x10, 0x9e, 0x00 }),
            into<array>(as_bytes, { 0x90, 0x01, 0x10, 0x1e, 0x00 }), into<array>(as_bytes, { 0x90, 0x15, 0x10, 0x9e, 0x00 }),
            into<array>(as_bytes, { 0x75, 0x15, 0x73, 0x1e, 0x11 }), into<array>(as_bytes, { 0x7b, 0x95, 0x92, 0x5c, 0x13 }),
            into<array>(as_bytes, { 0x8d, 0xc5, 0xa1, 0x1b, 0x10 }), into<array>(as_bytes, { 0x7e, 0x31, 0xc1, 0xdc, 0x0b }),
            into<array>(as_bytes, { 0x6f, 0x2d, 0x31, 0xdc, 0x03 }), into<array>(as_bytes, { 0x75, 0x19, 0x41, 0x9b, 0x03 }),
            into<array>(as_bytes, { 0x6f, 0x15, 0xe1, 0xda, 0x02 }), into<array>(as_bytes, { 0x66, 0xf1, 0xe0, 0xda, 0x02 }),
            into<array>(as_bytes, { 0x63, 0xdd, 0x10, 0x5b, 0x02 }), into<array>(as_bytes, { 0x5a, 0xb9, 0x10, 0x5b, 0x02 }),
            into<array>(as_bytes, { 0x4e, 0x99, 0x50, 0x5a, 0x02 }), into<array>(as_bytes, { 0x45, 0x81, 0x20, 0x5a, 0x02 }),
            into<array>(as_bytes, { 0x48, 0x85, 0x50, 0x5a, 0x02 }), into<array>(as_bytes, { 0x4b, 0x85, 0x50, 0x5a, 0x02 }),
            into<array>(as_bytes, { 0x4b, 0x7d, 0x80, 0x5a, 0x02 }), into<array>(as_bytes, { 0x48, 0x71, 0x20, 0x5a, 0x02 }),
            into<array>(as_bytes, { 0x48, 0x71, 0xc0, 0x99, 0x02 }), into<array>(as_bytes, { 0x45, 0x65, 0x90, 0x99, 0x02 }),
            into<array>(as_bytes, { 0x42, 0x61, 0x90, 0x99, 0x02 }), into<array>(as_bytes, { 0x3c, 0x59, 0xd0, 0x98, 0x02 }),
            into<array>(as_bytes, { 0x36, 0x59, 0xa0, 0x98, 0x02 }), into<array>(as_bytes, { 0x30, 0x55, 0x70, 0x18, 0x02 }),
            into<array>(as_bytes, { 0x2a, 0x55, 0x70, 0x18, 0x02 }), into<array>(as_bytes, { 0x27, 0x4d, 0x70, 0x18, 0x02 }),
            into<array>(as_bytes, { 0x21, 0x4d, 0x70, 0x18, 0x02 }), into<array>(as_bytes, { 0x24, 0x45, 0x70, 0x18, 0x02 }),
            into<array>(as_bytes, { 0x2a, 0x45, 0xa0, 0x18, 0x02 }), into<array>(as_bytes, { 0x2d, 0x41, 0xa0, 0x58, 0x01 }),
            into<array>(as_bytes, { 0x36, 0x41, 0xf0, 0x59, 0x01 }), into<array>(as_bytes, { 0x39, 0x41, 0xf0, 0x59, 0x01 }),
            into<array>(as_bytes, { 0x3f, 0x39, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x39, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x39, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x31, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x31, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x31, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x2d, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x2d, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x2d, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x25, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x25, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x25, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x25, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x25, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x1d, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x1d, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x1d, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x1d, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x1d, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x1d, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x1d, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x19, 0xf0, 0x99, 0x00 }), into<array>(as_bytes, { 0x3f, 0x01, 0xf0, 0x19, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x01, 0xf0, 0x19, 0x00 }), into<array>(as_bytes, { 0x3f, 0x01, 0xf0, 0x19, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x01, 0xf0, 0x19, 0x00 }), into<array>(as_bytes, { 0x3f, 0x01, 0xf0, 0x19, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x01, 0xf0, 0x19, 0x00 }), into<array>(as_bytes, { 0x3f, 0x01, 0xf0, 0x19, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x01, 0xf0, 0x19, 0x00 }), into<array>(as_bytes, { 0x3f, 0x01, 0xf0, 0x19, 0x00 }),
            into<array>(as_bytes, { 0x3f, 0x01, 0xf0, 0x19, 0x00 }), into<array>(as_bytes, { 0x3f, 0x01, 0xf0, 0x19, 0x00 }),
            into<array>(as_bytes, { 0x00, 0x00, 0x00, 0x00, 0x00 })
        };

        auto counter = 0_u8;
        for (const auto& pattern : haptic_patterns) {
            auto report  = array<byte, 42> {};
            report[0x00] = 0x02_b;
            report[0x01] = 0x50_b | as<byte>(counter & 0xF); // left
            report[0x11] = report[1];                        // right

            for (auto i : range(stdr::size(pattern))) {
                report[0x02 + i] = pattern[i]; // left
                report[0x12 + i] = pattern[i]; // right
            }

            ilog("send output report {}", green_text_style | report);

            auto result = state.current_device->send_output_report(report);
            if (not result) {
                return std::format("{}",
                                   red_text_style
                                     | std::format("Failed to send output report {}, reason: {} {}",
                                                   report,
                                                   hid_error(ref_of(*state.current_device)),
                                                   error_code::from_win32()));
            }

            std::this_thread::sleep_for(1s);

            if (counter == 0x0F) counter = 0;
            else
                counter = (counter + 1);
        }

        return {};
    }

    auto select_device(state& state) -> string {
        std::println("{}", show_devices(state));

        std::print("select > ");
        auto len = ::_read(get_stdin_fd(), stdr::data(state.command_buffer), stdr::size(state.command_buffer));
        if (len == 0 or len == 1) return std::format("{}", red_text_style | "invalid input!");

        auto input  = string_view { stdr::data(state.command_buffer), as<usize>(len - 1) };
        auto result = to<usize>(input, 10);
        if (not result) return std::format("{}", red_text_style | "invalid input!");

        auto device_index = *result;
        if (device_index >= stdr::size(state.entries)) return std::format("{}", red_text_style | "invalid input!");

        const auto& entry = state.entries[device_index];
        {
            auto result = hid_device::open(entry);
            if (not result) {
                return std::format("{}",
                                   red_text_style
                                     | std::format("failed to open device {} [{}], reason: {}",
                                                   entry.product_string,
                                                   entry.serial_number,
                                                   hid_error(ref_of(*state.current_device))));
            }

            state.current_device = std::move(*result);
        }

        return std::format("selected device {}", green_text_style | device_index);
    }

    auto shell(dynarray<hid_device_entry> entries) -> system_result<void> {
        EXPECTS(not stdr::empty(entries));

        auto state = lj::state { .entries = std::move(entries) };

        std::println("Entering shell...");

        auto current_command = string_view {};
        do {
            std::print("> ");
            auto len = ::_read(get_stdin_fd(), stdr::data(state.command_buffer), stdr::size(state.command_buffer));
            if (len == 0 or len == 1) continue;

            current_command = string_view { stdr::data(state.command_buffer), as<usize>(len) - 1 };
            if (commands.find(current_command) == stdr::cend(commands)) {
                std::println("{}", red_text_style | std::format("invalid command {}", current_command));
                current_command = "help";
            }

            const auto& fn     = commands.at(current_command).fn;
            auto        result = fn(state);
            if (not stdr::empty(result)) std::println("{}", result);
        } while (current_command != "exit");

        return {};
    }
} // namespace lj
