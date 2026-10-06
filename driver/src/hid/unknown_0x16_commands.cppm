export module lesserjoy:hid.unknown_0x16_commands;

import std;

import stormkit.core;

import :hid.command_ids;

using namespace stormkit;

export namespace lj::hid::unknown_0x16 {
    template<subcommand_id SUB_ID, command_data DATA = {}, auto FILL_PAYLOAD = monadic::noop()>
    using command = hid::command<command_id::UNKNOWN_0x16, SUB_ID, DATA, FILL_PAYLOAD>;

    using unknown_0x01_command = command<subcommand_id::UNKNOWN_0x01, command_data { .report_payload_length = 0x18 }>;
} // namespace lj::hid::unknown_0x16
