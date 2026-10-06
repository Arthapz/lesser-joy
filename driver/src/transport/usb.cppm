module;

#include "../windows.hpp"

#include "usb.hpp"

#include <stormkit/core/contract_macro.hpp>
#include <stormkit/core/try_expected.hpp>

export module lesserjoy:transport.usb;

import std;

import stormkit.core;

import :wdf;
import :common;

using namespace stormkit;
using namespace stormkit::literals;

namespace stdr = std::ranges;

export namespace lj {
    class device_context;

    namespace transport {
        class usb_context {
          public:
            static constexpr auto TYPE = transport_type::USB;

            struct joystick {
                u16 min_x = 500;
                u16 max_x = 3500;
                u16 min_y = 500;
                u16 max_y = 3500;
            };

            enum class joystick_type : u8 {
                LEFT  = 0,
                RIGHT = 1,
            };

            usb_context(const usb_context&)                    = delete;
            auto operator=(const usb_context&) -> usb_context& = delete;

            usb_context(usb_context&&) noexcept;
            auto operator=(usb_context&&) noexcept -> usb_context&;

            static auto create(const device_context&) -> system_result<usb_context>;

            auto device_entry() noexcept -> system_result<void>;
            auto device_exit() const noexcept -> system_result<void>;

            auto send_data(array_view<const byte>) const noexcept -> system_result<void>;
            auto send_data_sync(array_view<const byte>) noexcept -> system_result<void>;

            auto get_data_sync() const noexcept -> system_result<hid::command_report_buffer>;

            template<typename Command, bool VALIDATE = true, typename... Ts>
            auto send_command(Ts&&... args) noexcept -> system_result<void>;

            auto send_control_request(byte, byte, byte = 0x00_b, array_view<const byte> = {}) noexcept -> system_result<void>;

            auto write_report_to(WDFREQUEST) const noexcept -> system_result<void>;

            auto update_calibration(joystick_type, u16 x, u16 y) noexcept -> void;
            auto joystick_bounds(joystick_type) const noexcept -> joystick;

          private:
            struct endpoint {
                WDFUSBINTERFACE interface = nullptr;

                WDFUSBPIPE in_pipe  = nullptr;
                WDFUSBPIPE out_pipe = nullptr;
            };

            usb_context(const device_context&, WDFUSBDEVICE, endpoint&&, endpoint&&, USB_DEVICE_DESCRIPTOR&&) noexcept;

            // EvtWdfUsbReaderCompletionRoutine
            static auto pipe_reader_completion(WDFUSBPIPE, WDFMEMORY, usize, WDFCONTEXT) noexcept -> void;
            // EvtWdfRequestCompletionRoutine
            static auto request_completion_routine(WDFREQUEST, WDFIOTARGET, PWDF_REQUEST_COMPLETION_PARAMS, WDFCONTEXT) noexcept
              -> void;

            ref_ptr<const device_context> device_ctx_;

            WDFUSBDEVICE device_ = nullptr;

            endpoint hid_;
            endpoint command_;

            USB_DEVICE_DESCRIPTOR descriptor_ = {};

            WDFMEMORY product_string_ = nullptr;

            array<joystick, 2> joysticks_;

            locked<hid::input_report_buffer> last_input_report_;
        };
    } // namespace transport
} // namespace lj

////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
////////////////////////////////////////////////////////////////////

namespace lj::transport {
    inline usb_context::usb_context(const device_context&   ctx,
                                    WDFUSBDEVICE            device,
                                    endpoint&&              hid,
                                    endpoint&&              command,
                                    USB_DEVICE_DESCRIPTOR&& descriptor) noexcept
        : device_ctx_ { ctx },
          device_ { device },
          hid_ { std::move(hid) },
          command_ { std::move(command) },
          descriptor_ { std::move(descriptor) } {
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    inline usb_context::usb_context(usb_context&& other) noexcept
        : device_ctx_ { std::move(other.device_ctx_) },
          device_ { std::exchange(other.device_, nullptr) },
          hid_ { std::move(other.hid_) },
          command_ { std::move(other.command_) },
          product_string_ { std::exchange(other.product_string_, nullptr) },
          joysticks_ { std::move(other.joysticks_) } {
        auto lock = std::scoped_lock { last_input_report_.mutex(), other.last_input_report_.mutex() };

        last_input_report_.unsafe() = std::move(other.last_input_report_.unsafe());
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    inline auto usb_context::operator=(usb_context&& other) noexcept -> usb_context& {
        if (this == &other) [[unlikely]]
            return *this;

        device_ctx_     = std::move(other.device_ctx_);
        device_         = std::exchange(other.device_, nullptr);
        hid_            = std::move(other.hid_);
        command_        = std::move(other.command_);
        product_string_ = std::exchange(other.product_string_, nullptr);
        joysticks_      = std::move(other.joysticks_);
        auto lock       = std::scoped_lock { last_input_report_.mutex(), other.last_input_report_.mutex() };

        last_input_report_.unsafe() = std::move(other.last_input_report_.unsafe());

        return *this;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    inline auto usb_context::update_calibration(joystick_type type, u16 x, u16 y) noexcept -> void {
        const auto id = as<usize>(type);
        EXPECTS(id <= 1);

        auto& joystick = joysticks_[id];
        joystick.min_x = std::min(x, joystick.min_x);
        joystick.max_x = std::max(x, joystick.max_x);
        joystick.min_y = std::min(y, joystick.min_y);
        joystick.max_y = std::max(y, joystick.max_y);
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    inline auto usb_context::joystick_bounds(joystick_type type) const noexcept -> joystick {
        const auto id = as<usize>(type);
        EXPECTS(id <= 1);

        return joysticks_[id];
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    template<typename Command, bool VALIDATE = true, typename... Ts>
    auto usb_context::send_command(Ts&&... args) noexcept -> system_result<void> {
        if constexpr (sizeof...(args) == 0) {
            static constexpr auto COMMAND = Command::template make_command<TYPE>(std::forward<Ts>(args)...);
            Try(send_data(COMMAND));
        } else {
            static const auto COMMAND = Command::template make_command<TYPE>(std::forward<Ts>(args)...);
            Try(send_data(COMMAND));
        }

        if constexpr (VALIDATE) {
            TryTo(report_, get_data_sync());
            const auto report = array_view<const byte, Command::REPORT_LENGTH> { stdr::data(report_), Command::REPORT_LENGTH };

            if (not Command::template validate_report<TYPE>(report)) {
                const auto got      = array_view<const u8> { reinterpret_cast<const u8*>(stdr::data(report)),
                                                             stdr::size(Command::template REPORT_HEADER<TYPE>) };
                const auto expected = array_view<const u8> {
                    reinterpret_cast<const u8*>(stdr::data(Command::template REPORT_HEADER<TYPE>)),
                    stdr::size(Command::template REPORT_HEADER<TYPE>)
                };
                dlog("Report header bytes mismatch! got: {::#x}, expected: {::#x}!", got, expected);
                return std::unexpected<system_error2::nt_code> { STATUS_UNSUCCESSFUL };
            }
        }

        return {};
    }
} // namespace lj::transport
