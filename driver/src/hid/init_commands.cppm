export module lesserjoy:hid.init_commands;

import std;

import stormkit.core;

import :hid.command_ids;

using namespace stormkit;
using namespace stormkit::literals;

export namespace lj::hid::init {
    namespace fill_payload {
        inline constexpr auto bt_wake = [](array_view<byte, 0x04> payload, bool enabled = true) static noexcept {
            payload[0] = (enabled) ? 0x01_b : 0x00_b;
        };

        inline constexpr auto enable_usb_hid_report = [](array_view<byte, 0x04> payload, bool enabled = true) static noexcept {
            payload[0] = (enabled) ? 0x01_b : 0x00_b;
        };
        inline constexpr auto select_input_report = [](array_view<byte, 0x04> payload, input_report_id id) static noexcept {
            payload[0] = as<byte>(id);
        };

        inline constexpr auto unknown_0x0C = [](array_view<byte, 0x04> payload) static noexcept { payload[0] = 0x01_b; };

        inline constexpr auto initialize_usb = [](array_view<byte, 0x08> payload) static noexcept {
            payload[0] = 0x01_b;
            stdr::copy(into<array>(as_bytes, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }), stdr::begin(payload) + 2);
        };
    } // namespace fill_payload

    template<subcommand_id SUB_ID, command_data DATA = {}, auto FILL_PAYLOAD = monadic::noop()>
    using command = hid::command<command_id::INIT, SUB_ID, DATA, FILL_PAYLOAD>;

    using bt_wake_command               = command<subcommand_id::BT_WAKE,
                                                  command_data { .command_payload_length = 0x04 },
                                                  fill_payload::bt_wake>;
    using bt_cancel_command             = command<subcommand_id::BT_CANCEL>;
    using enable_usb_hid_report_command = command<subcommand_id::ENABLE_USB_HID_REPORT,
                                                  command_data { .command_payload_length = 0x04, .report_payload_length = 0x04 },
                                                  fill_payload::enable_usb_hid_report>;
    using unknown_0x04_command          = command<subcommand_id::UNKNOWN_0x04>;
    using unknown_0x05_command          = command<subcommand_id::UNKNOWN_0x05>;
    using unknown_0x06_command          = command<subcommand_id::UNKNOWN_0x06>;
    using send_pairing_info_command  = command<subcommand_id::SEND_PAIRING_INFO, command_data { .command_payload_length = 0x16 }>;
    using clear_pairing_info_command = command<subcommand_id::CLEAR_PAIRING_INFO>;
    using store_pairing_command      = command<subcommand_id::STORE_PAIRING_INFO>;
    using select_input_report_command = command<subcommand_id::SELECT_INPUT_REPORT,
                                                command_data { .command_payload_length = 0x04 },
                                                fill_payload::select_input_report>;
    using unknown_0x0C_command        = command<subcommand_id::UNKNOWN_0x0C,
                                                command_data { .command_payload_length = 0x04 },
                                                fill_payload::unknown_0x0C>;
    using initialize_usb_command      = command<subcommand_id::INITIALIZE_USB,
                                                command_data { .command_payload_length = 0x08, .report_payload_length = 0x04 },
                                                fill_payload::initialize_usb>;
    using unknown_0x0F_command        = command<subcommand_id::UNKNOWN_0x0F, command_data { .report_payload_length = 0x04 }>;
} // namespace lj::hid::init
