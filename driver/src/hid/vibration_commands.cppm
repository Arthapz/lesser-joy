export module lesserjoy:hid.vibration_commands;

import std;

import stormkit.core;

import :hid.command_ids;

using namespace stormkit;

export namespace lj::hid::vibration {
    namespace fill_payload {
        inline constexpr auto play_sample = [](array_view<byte, 0x04> payload, vibration::sample sample) static noexcept {
            payload[0] = as<byte>(sample);
        };

        inline constexpr auto send_data = [](array_view<byte, 0x14> payload) static noexcept {};
    } // namespace fill_payload

    template<subcommand_id Sub_id, command_data Data = {}, auto Fill_payload = monadic::noop()>
    using command = hid::command<command_id::vibration, Sub_id, Data, Fill_payload>;

    using unknown_0x01_command = command<subcommand_id::unknown_0x01>;
    using play_sample_command  = command<subcommand_id::play_sample,
                                         command_data { .command_payload_length = 0x04 },
                                         fill_payload::play_sample>;
    using unknown_0x03_command = command<subcommand_id::unknown_0x03>;
    using unknown_0x04_command = command<subcommand_id::unknown_0x04>;
    using unknown_0x05_command = command<subcommand_id::unknown_0x05>;
    using unknown_0x06_command = command<subcommand_id::unknown_0x06>;
    using unknown_0x07_command = command<subcommand_id::unknown_0x07>;
    using send_data_command    = command<subcommand_id::send_data,
                                         command_data { .command_payload_length = 0x14 },
                                         fill_payload::send_data>;
    using unknown_0x09_command = command<subcommand_id::unknown_0x09>;
} // namespace lj::hid::vibration
