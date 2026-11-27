module;

#include "../wdk/wdfobject_macros.hpp"

#include <stormkit/core/try_expected.hpp>

#include "../contract_macro.hpp"

export module lesserjoy:transport.usb;

import std;

import stormkit.core;

import wdk.wdf;
import wdk.usb;
import wdk.ntstatus;

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
            static constexpr auto transport = transport_type::usb;

            struct joystick {
                u16 min_x = 500;
                u16 max_x = 3500;
                u16 min_y = 500;
                u16 max_y = 3500;
            };

            enum class joystick_type : u8 {
                left  = 0,
                right = 1,
            };

            enum class endpoint_type : u8 {
                hid     = 0,
                command = 1,
            };

            usb_context(const usb_context&)                    = delete;
            auto operator=(const usb_context&) -> usb_context& = delete;

            usb_context(usb_context&&) noexcept;
            auto operator=(usb_context&&) noexcept -> usb_context&;

            static auto create(const device_context&) -> system_result<ref_ptr<usb_context>>;

            auto device_entry() noexcept -> system_result<void>;
            auto device_exit() const noexcept -> system_result<void>;

            auto send_data(array_view<const byte>, endpoint_type) const noexcept -> system_result<void>;
            auto send_data_sync(array_view<const byte>, endpoint_type) const noexcept -> system_result<void>;

            auto get_data_sync() const noexcept -> system_result<hid::command_report_buffer>;

            template<typename Command, bool Validate = true, typename... Ts>
            auto send_command(Ts&&... args) noexcept -> system_result<void>;

            auto send_control_request(byte, byte, byte = 0x00_b, array_view<const byte> = {}) noexcept -> system_result<void>;

            auto write_report_to(wdk::WDFREQUEST) const noexcept -> system_result<void>;

            auto update_calibration(joystick_type, u16 x, u16 y) noexcept -> void;
            auto joystick_bounds(joystick_type) const noexcept -> joystick;

            auto has_product_string() const noexcept -> bool;
            auto product_string() const noexcept -> const string&;

            auto has_serial_string() const noexcept -> bool;
            auto serial_string() const noexcept -> const string&;

            auto forward_request(wdk::WDFREQUEST) const noexcept -> system_result<void>;

          private:
            struct endpoint {
                wdk::WDFUSBINTERFACE interface = nullptr;

                wdk::WDFUSBPIPE in_pipe  = nullptr;
                wdk::WDFUSBPIPE out_pipe = nullptr;
            };

            usb_context(const device_context&,
                        wdk::WDFUSBDEVICE,
                        endpoint&&,
                        endpoint&&,
                        wdk::USB_DEVICE_DESCRIPTOR&&,
                        std::optional<string>,
                        std::optional<string>) noexcept;

            // EvtWdfUsbReaderCompletionRoutine
            static auto pipe_reader_completion(wdk::WDFUSBPIPE, wdk::WDFMEMORY, usize, wdk::WDFCONTEXT) noexcept -> void;
            // EvtWdfRequestCompletionRoutine
            static auto request_completion_routine(wdk::WDFREQUEST,
                                                   wdk::WDFIOTARGET,
                                                   wdk::PWDF_REQUEST_COMPLETION_PARAMS,
                                                   wdk::WDFCONTEXT) noexcept -> void;

            ref_ptr<const device_context> device_ctx_;

            wdk::WDFUSBDEVICE device_ = nullptr;

            endpoint hid_;
            endpoint command_;

            wdk::USB_DEVICE_DESCRIPTOR descriptor_ = {};

            array<joystick, 2> joysticks_ = {};

            locked<hid::input_report_buffer> last_input_report_;

            std::optional<string> product_string_;
            std::optional<string> serial_string_;
        };

        using Pusb_context = usb_context*;
        STORMKIT_PUSH_WARNINGS
#pragma clang diagnostic ignored "-Wduplicate-decl-specifier"
        WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(usb_context, get_usb_context)
        STORMKIT_POP_WARNINGS
    } // namespace transport
} // namespace lj

////////////////////////////////////////////////////////////////////
///                      IMPLEMENTATION                          ///
////////////////////////////////////////////////////////////////////

namespace lj::transport {
    inline usb_context::usb_context(const device_context&        ctx,
                                    wdk::WDFUSBDEVICE            device,
                                    endpoint&&                   hid,
                                    endpoint&&                   command,
                                    wdk::USB_DEVICE_DESCRIPTOR&& descriptor,
                                    std::optional<string>        product_string,
                                    std::optional<string>        serial_string) noexcept
        : device_ctx_ { ctx },
          device_ { device },
          hid_ { std::move(hid) },
          command_ { std::move(command) },
          descriptor_ { std::move(descriptor) },
          product_string_ { std::move(product_string) },
          serial_string_ { std::move(serial_string) } {
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    inline usb_context::usb_context(usb_context&& other) noexcept
        : device_ctx_ { std::move(other.device_ctx_) },
          device_ { std::exchange(other.device_, nullptr) },
          hid_ { std::move(other.hid_) },
          command_ { std::move(other.command_) },
          descriptor_ { std::exchange(other.descriptor_, {}) },
          joysticks_ { std::move(other.joysticks_) },
          product_string_ { std::exchange(other.product_string_, std::nullopt) },
          serial_string_ { std::exchange(other.serial_string_, std::nullopt) } {
        {
            auto lock = std::scoped_lock { last_input_report_.mutex(), other.last_input_report_.mutex() };

            last_input_report_.unsafe() = std::move(other.last_input_report_.unsafe());
        }
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
        descriptor_     = std::exchange(other.descriptor_, {});
        joysticks_      = std::move(other.joysticks_);
        product_string_ = std::exchange(other.product_string_, std::nullopt);
        serial_string_  = std::exchange(other.serial_string_, std::nullopt);
        {
            auto lock = std::scoped_lock { last_input_report_.mutex(), other.last_input_report_.mutex() };

            last_input_report_.unsafe() = std::move(other.last_input_report_.unsafe());
        }

        return *this;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    inline auto usb_context::update_calibration(joystick_type type, u16 x, u16 y) noexcept -> void {
        const auto id = as<usize>(type);
        WDK_EXPECTS_VOID(id <= 1);

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
        WDK_EXPECTS_RETURN(id <= 1, {});

        return joysticks_[id];
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    template<typename Command, bool Validate = true, typename... Ts>
    auto usb_context::send_command(Ts&&... args) noexcept -> system_result<void> {
        if constexpr (sizeof...(args) == 0) {
            static constexpr auto command = Command::template make_command<transport>(std::forward<Ts>(args)...);
            Try(send_data(command, endpoint_type::command));
        } else {
            const auto command = Command::template make_command<transport>(std::forward<Ts>(args)...);
            Try(send_data(command, endpoint_type::command));
        }

        if constexpr (Validate) {
            TryTo(report_, get_data_sync());
            const auto report = array_view<const byte, Command::report_length> { stdr::data(report_), Command::report_length };

            if (not Command::template validate_report<transport>(report)) {
                const auto got      = array_view<const u8> { reinterpret_cast<const u8*>(stdr::data(report)),
                                                             stdr::size(Command::template report_header<transport>) };
                const auto expected = array_view<const u8> {
                    reinterpret_cast<const u8*>(stdr::data(Command::template report_header<transport>)),
                    stdr::size(Command::template report_header<transport>)
                };
                dlog("Report header bytes mismatch! got: {::#x}, expected: {::#x}!", got, expected);
                return std::unexpected<system_error2::nt_code> { wdk::STATUS_UNSUCCESSFUL };
            }
        }

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    auto usb_context::has_product_string() const noexcept -> bool {
        return product_string_ != std::nullopt;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    auto usb_context::product_string() const noexcept -> const string& {
        // WDK_EXPECTS_RETURN(has_product_string(), [] static noexcept {
        //     static const auto str = string { "" };
        //     return str;
        // }());

        return *product_string_;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    auto usb_context::has_serial_string() const noexcept -> bool {
        return serial_string_ != std::nullopt;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    STORMKIT_FORCE_INLINE
    auto usb_context::serial_string() const noexcept -> const string& {
        // WDK_EXPECTS_RETURN(has_serial_string(), [] static noexcept {
        //     static const auto str = string { "" };
        //     return str;
        // }());

        return *serial_string_;
    }
} // namespace lj::transport
