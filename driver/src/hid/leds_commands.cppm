export module lesserjoy:hid.leds_commands;

import std;

import stormkit.core;

import :hid.command_ids;

using namespace stormkit;

export namespace lj::hid::leds {
    template<subcommand_id SUB_ID, command_data DATA = {}, auto FILL_PAYLOAD = monadic::noop()>
    using command = hid::command<command_id::LEDS, SUB_ID, DATA, FILL_PAYLOAD>;

    using set_player_1_command = command<subcommand_id::SET_PLAYER_1>;
    using set_player_2_command = command<subcommand_id::SET_PLAYER_2>;
    using set_player_3_command = command<subcommand_id::SET_PLAYER_3>;
    using set_player_4_command = command<subcommand_id::SET_PLAYER_4>;
    using All_leds_on_command  = command<subcommand_id::ALL_LEDS_ON>;
    using All_leds_off_command = command<subcommand_id::ALL_LEDS_OFF>;
    using set_player_led_mask  = command<subcommand_id::SET_PLAYER_LED_MASK, command_data { .command_payload_length = 0x08 }>;
    using Flash_leds_command   = command<subcommand_id::FLASH_LEDS, command_data { .command_payload_length = 0x04 }>;
} // namespace lj::hid::leds
