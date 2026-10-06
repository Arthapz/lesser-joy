export module lesserjoy:hid.feature_select_commands;

import std;

import stormkit.core;

import :hid.command_ids;

using namespace stormkit;

export namespace lj::hid::feature_select {
    namespace fill_payload {
        inline constexpr auto get_feature_info = [](array_view<byte, 0x04> payload, feature_flag flags) static noexcept {
            payload[0] = as<byte>(flags);
        };

        inline constexpr auto set_feature_mask = [](array_view<byte, 0x04> payload, feature_flag flags) static noexcept {
            payload[0] = as<byte>(flags);
        };

        inline constexpr auto enable_features = [](array_view<byte, 0x04> payload, feature_flag flags) static noexcept {
            payload[0] = as<byte>(flags);
        };

        inline constexpr auto disable_features = [](array_view<byte, 0x04> payload, feature_flag flags) static noexcept {
            payload[0] = as<byte>(flags);
        };

        inline constexpr auto configure_features = [](array_view<byte, 0x0A> payload, feature_flag flags) static noexcept {
            payload[0] = as<byte>(flags);
        };
    } // namespace fill_payload

    template<subcommand_id SUB_ID, command_data DATA = {}, auto FILL_PAYLOAD = monadic::noop()>
    using command = hid::command<command_id::FEATURE_SELECT, SUB_ID, DATA, FILL_PAYLOAD>;

    using get_feature_info_command   = command<subcommand_id::GET_FEATURE_INFO,
                                               { .command_payload_length = 0x04, .report_payload_length = 0x0B },
                                               fill_payload::get_feature_info>;
    using set_feature_mask_command   = command<subcommand_id::SET_FEATURE_MASK,
                                               { .command_payload_length = 0x04, .report_payload_length = 0x04 },
                                               fill_payload::set_feature_mask>;
    using clear_feature_mask_command = command<subcommand_id::CLEAR_FEATURE_MASK,
                                               { .command_payload_length = 0x04, .report_payload_length = 0x04 }>;
    using enable_features_command    = command<subcommand_id::ENABLE_FEATURES,
                                               { .command_payload_length = 0x04, .report_payload_length = 0x04 },
                                               fill_payload::enable_features>;
    using disable_features_command   = command<subcommand_id::DISABLE_FEATURES,
                                               { .command_payload_length = 0x04, .report_payload_length = 0x04 },
                                               fill_payload::disable_features>;
    using configure_features_command = command<subcommand_id::CONFIGURE_FEATURES,
                                               { .command_payload_length = 0x0A, .report_payload_length = 0x28 },
                                               fill_payload::configure_features>;
} // namespace lj::hid::feature_select
