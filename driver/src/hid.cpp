module;

#include "contract_macro.hpp"
#include <stormkit/core/try_expected.hpp>

module lesserjoy;

import :wdf;

using namespace stormkit::literals;

namespace lj::hid::ioctl {
    ////////////////////////////////////////
    ////////////////////////////////////////
    auto get_device_descriptor(wdk::WDFREQUEST request, const wdk::HID_DESCRIPTOR& descriptor) noexcept -> system_result<void> {
        Try(fill_wdf_request_memory(request, bytes_of(descriptor)));
        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto get_device_attributes(wdk::WDFREQUEST request, const wdk::HID_DEVICE_ATTRIBUTES& attributes) noexcept
      -> system_result<void> {
        Try(fill_wdf_request_memory(request, bytes_of(attributes)));
        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto get_report_descriptor(wdk::WDFREQUEST request, const report_descriptor& descriptor) noexcept -> system_result<void> {
        Try(fill_wdf_request_memory(request, bytes_of(descriptor)));
        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto read_report(wdk::WDFREQUEST request, const transport::usb_context& usb) noexcept -> system_result<void> {
        return usb.write_report_to(request);
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto write_report(wdk::WDFREQUEST request, array_view<const byte> report) noexcept -> system_result<void> {
        Try(fill_wdf_request_memory(request, report));

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto write_report(wdk::WDFREQUEST request, const transport::usb_context& ctx) noexcept -> system_result<void> {
        TryTo(report, get_wdf_request_memory(request));
        dlog("{}", report);

        Try(ctx.send_data(report, transport::usb_context::endpoint_type::hid));

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto get_string(wdk::WDFREQUEST request, string_view product_string, string_view serial_string) noexcept
      -> system_result<void> {
        WDK_EXPECTS(product_string[product_string.size()] == '\0');
        WDK_EXPECTS(serial_string[serial_string.size()] == '\0');

        auto raw_buffer  = raw_ptr<void> { nullptr };
        auto buffer_size = 0_usize;

        auto string_id = 0_u32;

        CustomLoggedTry(lj::win_call(wdk::WdfRequestRetrieveInputBuffer, request, sizeof(u32), &raw_buffer, &buffer_size),
                        elog,
                        "WdfRequestRetrieveInputBuffer failed!");

        if (raw_buffer != nullptr and buffer_size != 0) {
            string_id = (*reinterpret_cast<u32*>(raw_buffer)) & 0xFFFF;

            const auto is_serial = (string_id == 16 or string_id == 3); // HID_STRING_ID_ISERIALNUMBER
            auto       string    = wstring {};

            if (is_serial) string = ascii_to_wide(serial_string);
            else
                string = ascii_to_wide(product_string);

            // size + 1 to include \0
            auto string_as_bytes = array_view { reinterpret_cast<const byte*>(stdr::data(string)),
                                                (stdr::size(string) + 1) * sizeof(wchar_t) };

            Try(fill_wdf_request_memory(request, string_as_bytes));
        }

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto get_indexed_string(wdk::WDFREQUEST request, string_view product_string) noexcept -> system_result<void> {
        WDK_EXPECTS(product_string[product_string.size()] == '\0');

        const auto string = ascii_to_wide(product_string);

        // size + 1 to include \0
        auto string_as_bytes = array_view { reinterpret_cast<const byte*>(stdr::data(string)),
                                            (stdr::size(string) + 1) * sizeof(wchar_t) };

        Try(fill_wdf_request_memory(request, string_as_bytes));

        return {};
    }
} // namespace lj::hid::ioctl
