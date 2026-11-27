module;

#include <stormkit/core/try_expected.hpp>

#include "../contract_macro.hpp"

module lesserjoy;

import :transport.usb;
import :device;
import :hid;
import :log;
import :constants;

namespace stdv = std::views;

namespace lj::transport {
    using axis_type = u16;

    namespace {
        ////////////////////////////////////////
        ////////////////////////////////////////
        template<usb_context::joystick_type Type>
        auto calibrate_joystick(usb_context& usb, array_view<byte> bytes) noexcept -> void {
            static constexpr auto scale =
              [](axis_type x, axis_type rmin, axis_type rmax, axis_type tmin, axis_type tmax) noexcept -> axis_type {
                if (core::is(rmax, rmin)) return tmin;
                return (x - rmin) * (tmax - tmin) / (rmax - rmin) + tmin;
            };

            // axis data is 12 bit packed
            // extract x from first byte and lower part of second byte
            const auto x = init_by<axis_type>([bytes](auto& out) noexcept {
                out = 0;
                out |= as<u16>(bytes[0]);
                out |= (as<u16>(bytes[1] & 0xF_b) << 8);
            });

            // extract y from upper part of second byte and third byte
            const auto y = init_by<axis_type>([bytes](auto& out) noexcept {
                out = 0;
                out |= as<u16>((bytes[1] & 0xF0_b) >> 4);
                out |= (as<u16>(bytes[2]) << 4);
            });

            // calibrate joystick
            usb.update_calibration(Type, x, y);
            const auto joystick = usb.joystick_bounds(Type);

            // rescale x value from uncalibrated output to [0, 4095]
            const auto x_scaled = scale(x, joystick.min_x, joystick.max_x, 0, 4095);

            // rescale y value from uncalibrated output to [0, 4095]
            const auto y_scaled = [&joystick, &y] noexcept {
                if constexpr (Type == usb_context::joystick_type::left) return scale(y, joystick.min_y, joystick.max_y, 4095, 0);
                else
                    return scale(y, joystick.min_y, joystick.max_y, 0, 4095);
            }();

            bytes[0] = as<byte>((x_scaled >> 0) & 0xFF);
            bytes[1] = as<byte>(((x_scaled >> 8) & 0x0F) | ((y_scaled << 4) & 0xF0));
            bytes[2] = as<byte>((y_scaled >> 4) & 0xFF);
        }
    } // namespace

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::create(const device_context& ctx) -> system_result<ref_ptr<usb_context>> {
        const auto device = ctx.device();

        auto usb_device = wdk::WDFUSBDEVICE { nullptr };
        // Create and fill the usb sink configuration
        auto init_config = wdk::WDF_USB_DEVICE_CREATE_CONFIG {};
        wdk::WDF_USB_DEVICE_CREATE_CONFIG_INIT(&init_config, wdk::USBD_CLIENT_CONTRACT_VERSION_602);
        LoggedTry(lj::win_call(wdk::WdfUsbTargetDeviceCreateWithParameters,
                               ctx.device(),
                               &init_config,
                               wdk::WDF_NO_OBJECT_ATTRIBUTES,
                               &usb_device),
                  "Failed to create USB device context!");

        const auto interface_count = wdk::WdfUsbTargetDeviceGetNumInterfaces(usb_device);
        if (interface_count < 2) return { std::unexpected { error_code::from_ntstatus(wdk::STATUS_INVALID_DEVICE_STATE) } };

        // Retrieve usb descriptor
        auto usb_descriptor = wdk::USB_DEVICE_DESCRIPTOR {};
        wdk::WdfUsbTargetDeviceGetDeviceDescriptor(usb_device, &usb_descriptor);

        // Create and fill the usb interface configuration
        auto select_config = wdk::WDF_USB_DEVICE_SELECT_CONFIG_PARAMS {};
        wdk::WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_MULTIPLE_INTERFACES(&select_config, 0, nullptr);

        // Retrieve usb interfaces
        LoggedTry(lj::win_call(wdk::WdfUsbTargetDeviceSelectConfig, usb_device, wdk::WDF_NO_OBJECT_ATTRIBUTES, &select_config),
                  "Failed to configure USB device!");

        auto hid_endpoint     = endpoint {};
        auto command_endpoint = endpoint {};

        hid_endpoint.interface     = wdk::WdfUsbTargetDeviceGetInterface(usb_device, 0);
        command_endpoint.interface = wdk::WdfUsbTargetDeviceGetInterface(usb_device, 1);

        if (hid_endpoint.interface == nullptr or command_endpoint.interface == nullptr)
            return { std::unexpected { error_code::from_ntstatus(wdk::STATUS_INVALID_DEVICE_STATE) } };

        // retrieve pipe handles for USB I/O
        // hid_endpoint     => interrupt
        // command_endpoint => bulk
        auto endpoints = array { &hid_endpoint, &command_endpoint };
        for (const auto [i, endpoint_] : stdv::enumerate(endpoints)) {
            auto& endpoint = *endpoint_;

            for (auto pipe_index : range(wdk::WdfUsbInterfaceGetNumConfiguredPipes(endpoint.interface))) {
                auto pipe_info = wdk::WDF_USB_PIPE_INFORMATION {};
                wdk::WDF_USB_PIPE_INFORMATION_INIT(&pipe_info);

                const auto pipe = wdk::WdfUsbInterfaceGetConfiguredPipe(endpoint.interface, pipe_index, &pipe_info);

                // TODO investigate why this is necessery
                wdk::WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pipe);

                dlog("Pipe {}, Type: {} In: {} Out: {}",
                     pipe_index,
                     as<u8>(pipe_info.PipeType),
                     wdk::WdfUsbTargetPipeIsInEndpoint(pipe),
                     wdk::WdfUsbTargetPipeIsOutEndpoint(pipe));

                const auto type = (i == 0) ? wdk::WDF_USB_PIPE_TYPE::WdfUsbPipeTypeInterrupt
                                           : wdk::WDF_USB_PIPE_TYPE::WdfUsbPipeTypeBulk;

                if (pipe_info.PipeType == type and wdk::WdfUsbTargetPipeIsInEndpoint(pipe)) endpoint.in_pipe = pipe;
                if (pipe_info.PipeType == type and wdk::WdfUsbTargetPipeIsOutEndpoint(pipe)) endpoint.out_pipe = pipe;
            }

            if (not endpoint.in_pipe or not endpoint.out_pipe) {
                elog("Failed to get endpoint pipes! in_pipe: {}, out_pipe: {}",
                     static_cast<void*>(endpoint.in_pipe),
                     static_cast<void*>(endpoint.out_pipe));
                return std::unexpected { error_code::from_ntstatus(wdk::STATUS_INVALID_DEVICE_STATE) };
            }
        }

        // get product name if available
        auto product_string = std::optional<string> {};
        {
            auto       product_string_memory = wdk::WDFMEMORY {};
            const auto result                = lj::win_call(wdk::WdfUsbTargetDeviceAllocAndQueryString,
                                                            usb_device,
                                                            wdk::WDF_NO_OBJECT_ATTRIBUTES,
                                                            &product_string_memory,
                                                            nullptr,
                                                            usb_descriptor.iProduct,
                                                            0x409);

            if (result.has_value()) {
                auto size          = 0_usize;
                auto memory_buffer = wdk::WdfMemoryGetBuffer(product_string_memory, &size);
                product_string     = wide_to_ascii({ reinterpret_cast<const wchar_t*>(memory_buffer), (size / sizeof(wchar_t)) });
            } else
                lj::wlog("Failed to get product string from USB device!\n    reason: {}", result.error());
        }

        // get serial name if available
        auto serial_string = std::optional<string> {};
        {
            auto       serial_string_memory = wdk::WDFMEMORY {};
            const auto result               = lj::win_call(wdk::WdfUsbTargetDeviceAllocAndQueryString,
                                                           usb_device,
                                                           wdk::WDF_NO_OBJECT_ATTRIBUTES,
                                                           &serial_string_memory,
                                                           nullptr,
                                                           usb_descriptor.iSerialNumber,
                                                           0x409);
            if (result.has_value()) {
                auto size          = 0_usize;
                auto memory_buffer = wdk::WdfMemoryGetBuffer(serial_string_memory, &size);

                if (size > 0) {
                    auto serial_string_ = string {};
                    for (auto byte : array_view<const u8> { reinterpret_cast<const u8*>(memory_buffer), size }) {
                        serial_string_ += as<string>(byte, 16);
                        serial_string_ += ":";
                    }

                    serial_string_.pop_back();
                    serial_string = std::move(serial_string_);
                }
            } else
                lj::wlog("Failed to get serial string from USB device!\n    reason: {}", result.error());
        }

        // Create and fill the usb context attributes
        auto attributes = wdk::WDF_OBJECT_ATTRIBUTES {};
        wdk::WdfObjectAttributesInitContextType(attributes, _WDF_usb_context_TYPE_INFO);
        attributes.EvtDestroyCallback = destroy_ctx<transport::usb_context, get_usb_context>;

        auto usb_ctx = raw_ptr<transport::usb_context> { nullptr };
        CustomLoggedTry(lj::win_call(wdk::WdfObjectAllocateContext, usb_device, &attributes, std::out_ptr<void*>(usb_ctx)),
                        lj::elog,
                        "Usb context allocation failed!");

        WDK_ASSERT(usb_ctx != nullptr);

        new (usb_ctx) usb_context {
            ctx,
            usb_device,
            std::move(hid_endpoint),
            std::move(command_endpoint),
            std::move(usb_descriptor),
            std::move(product_string),
            std::move(serial_string)
        };

        return { ref_ptr { usb_ctx } };
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::pipe_reader_completion(wdk::WDFUSBPIPE, wdk::WDFMEMORY memory, usize count, wdk::WDFCONTEXT data) noexcept
      -> void {
        if (data == nullptr or count == 0 or count != hid::input_report_size) return;

        auto& usb    = *reinterpret_cast<usb_context*>(data);
        auto  report = hid::input_report_buffer {};
        LoggedTryOr(get_wdf_memory(memory, report), monadic::discard(), "Failed to get USB data!");

        // Left joystick
        // y axis of left joystick is inverted in uncalibrated data
        calibrate_joystick<usb_context::joystick_type::left>(usb, mutable_view_of(report).subspan(0x6, 0x3));
        // Right joystick
        calibrate_joystick<usb_context::joystick_type::right>(usb, mutable_view_of(report).subspan(0x9, 0x3));

        // Update input report
        usb.last_input_report_.assign(std::move(report));
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::request_completion_routine(wdk::WDFREQUEST request,
                                                 wdk::WDFIOTARGET,
                                                 wdk::PWDF_REQUEST_COMPLETION_PARAMS,
                                                 wdk::WDFCONTEXT) noexcept -> void {
        dlog("request {} completed", static_cast<void*>(request));
        wdk::WdfObjectDelete(request);
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::device_entry() noexcept -> system_result<void> {
        using enum hid::feature_select::feature_flag;

        static constexpr auto enabled_features = button_state | analog_sticks | rumble;

        // Send init sequence
        LoggedTry((send_command<hid::init::initialize_usb_command>()), "Failed to initialize USB link!");
        LoggedTry((send_command<hid::feature_select::set_feature_mask_command>(enabled_features)), "Failed to set feature mask!");
        LoggedTry((send_command<hid::feature_select::enable_features_command>(enabled_features)), "Failed to enable features!");
        LoggedTry((send_command<hid::leds::set_player_1_command>()), "failed to setup player led!");
        LoggedTry((send_command<hid::init::select_input_report_command>(hid::init::input_report_id::alt_procon_2)),
                  "Failed to select input report!");
        if (not send_command<hid::vibration::play_sample_command>(hid::vibration::sample::soft_click))
            elog("Failed to send rumble init sequence");

        ilog("{} initialized! (USB)", device_ctx_->product_string());

        // Prepare continuous USB reader
        auto config = wdk::WDF_USB_CONTINUOUS_READER_CONFIG {};
        wdk::WDF_USB_CONTINUOUS_READER_CONFIG_INIT(&config,
                                                   pipe_reader_completion,
                                                   reinterpret_cast<wdk::WDFCONTEXT>(this),
                                                   hid::input_report_size);
        LoggedTry(lj::win_call(wdk::WdfUsbTargetPipeConfigContinuousReader, hid_.in_pipe, &config),
                  "wdk::WdfUsbTargetPipeConfigContinuousReader failed!");

        // Start continuous USB reader
        auto io_target = wdk::WdfUsbTargetPipeGetIoTarget(hid_.in_pipe);
        LoggedTry(lj::win_call(wdk::WdfIoTargetStart, io_target), "Failed to start USB read pipe!");
        dlog("USB continuous reader started !");

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::device_exit() const noexcept -> system_result<void> {
        // Stop continueous reader
        auto io_target = wdk::WdfUsbTargetPipeGetIoTarget(hid_.in_pipe);
        wdk::WdfIoTargetStop(io_target, wdk::WdfIoTargetCancelSentIo);
        dlog("USB continuous reader stopped (hid)!");

        ilog("{} disconnected! (USB)", device_ctx_->product_string());

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::send_data(array_view<const byte> payload, endpoint_type endpoint) const noexcept -> system_result<void> {
        // Retrieve io target
        auto target = wdk::WdfUsbTargetDeviceGetIoTarget(device_);

        // Initialize request
        auto request = wdk::WDFREQUEST {};
        LoggedTry(lj::win_call(wdk::WdfRequestCreate, nullptr, target, &request), "wdk::WdfRequestCreate failed!");

        auto release = true;
        auto _       = at_scope_exit_do([request, &release] noexcept {
            if (release) wdk::WdfObjectDelete(request);
        });

        // Allocate request memory and fill it with payload
        LoggedTryTo(result, wdf_memory_allocate(stdr::size(payload), request), "Failed to allocate memory for USB send payload!");
        auto [memory, write_buffer] = std::move(result);
        stdr::copy(payload, stdr::begin(write_buffer));

        LoggedTry(lj::win_call(wdk::WdfUsbTargetPipeFormatRequestForWrite,
                               endpoint == endpoint_type::command ? command_.out_pipe : hid_.out_pipe,
                               request,
                               memory,
                               wdk::WDF_NO_HANDLE),
                  "wdk::WdfUsbTargetPipeFormatRequestForWrite failed!");

        // Setup completion callback
        wdk::WdfRequestSetCompletionRoutine(request, request_completion_routine, wdk::WDF_NO_HANDLE);

        // Send request
        if (wdk::WdfRequestSend(request, target, wdk::WDF_NO_SEND_OPTIONS) == win32::FALSE) {
            const auto status = wdk::WdfRequestGetStatus(request);
            dlog("wdk::WdfRequestSend failed!\n    reason: {:#x}", as<ulong>(status));
            return std::unexpected { error_code::from_ntstatus(status) };
        }

        release = false;
        dlog("request {}, {::#x} sent",
             static_cast<void*>(request),
             array_view<const u8> { reinterpret_cast<const u8*>(stdr::data(payload)), stdr::size(payload) });

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::send_data_sync(array_view<const byte> payload, endpoint_type endpoint) const noexcept
      -> system_result<void> {
        // Initialize and fill memory descriptor
        auto memory_descriptor = wdk::WDF_MEMORY_DESCRIPTOR {};
        wdk::WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memory_descriptor,
                                               std::bit_cast<void*>(stdr::data(payload)),
                                               stdr::size(payload));

        // Send request
        auto written = ulong { 0 };
        LoggedTry(lj::win_call(wdk::WdfUsbTargetPipeWriteSynchronously,
                               endpoint == endpoint_type::command ? command_.out_pipe : hid_.out_pipe,
                               nullptr,
                               nullptr,
                               &memory_descriptor,
                               &written),
                  "wdk::WdfUsbTargetPipeWriteSynchronously failed!");

        dlog("Sent {::#x}", array_view<const u8> { reinterpret_cast<const u8*>(stdr::data(payload)), stdr::size(payload) });

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::get_data_sync() const noexcept -> system_result<hid::command_report_buffer> {
        // Initialize and fill memory descriptor
        auto report            = hid::command_report_buffer {};
        auto memory_descriptor = wdk::WDF_MEMORY_DESCRIPTOR {};
        wdk::WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memory_descriptor, stdr::data(report), stdr::size(report));

        // Send request
        auto readed = ulong { 0 };
        LoggedTry(lj::win_call(wdk::WdfUsbTargetPipeReadSynchronously,
                               command_.in_pipe,
                               nullptr,
                               nullptr,
                               &memory_descriptor,
                               &readed),
                  "wdk::WdfUsbTargetPipeReadSynchronously failed! {}");

        dlog("Received {::#x}", array_view<const u8> { reinterpret_cast<const u8*>(stdr::data(report)), stdr::size(report) });

        return { std::move(report) };
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::send_control_request(byte request, byte value, byte index, array_view<const byte> data) noexcept
      -> system_result<void> {
        // Create and fill request send options
        auto options = wdk::WDF_REQUEST_SEND_OPTIONS {};
        wdk::WDF_REQUEST_SEND_OPTIONS_INIT(&options, wdk::WDF_REQUEST_SEND_OPTIONS_FLAGS::WDF_REQUEST_SEND_OPTION_TIMEOUT);
        wdk::WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, wdk::WDF_REL_TIMEOUT_IN_SEC(3));

        // Initialize control setup packet
        auto control_setup_packet = wdk::WDF_USB_CONTROL_SETUP_PACKET {};
        wdk::WDF_USB_CONTROL_SETUP_PACKET_INIT(&control_setup_packet,
                                               wdk::WDF_USB_BMREQUEST_DIRECTION::BmRequestHostToDevice,
                                               wdk::WDF_USB_BMREQUEST_RECIPIENT::BmRequestToDevice,
                                               as<u8>(request),
                                               as<u8>(value),
                                               as<u8>(index));

        // Initialize and fill memory descriptor
        auto memory_descriptor = wdk::WDF_MEMORY_DESCRIPTOR {};
        wdk::WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memory_descriptor, std::bit_cast<void*>(stdr::data(data)), stdr::size(data));

        // Send request
        LoggedTry(lj::win_call(wdk::WdfUsbTargetDeviceSendControlTransferSynchronously,
                               device_,
                               wdk::WDF_NO_HANDLE,
                               &options,
                               &control_setup_packet,
                               &memory_descriptor,
                               nullptr),
                  "wdk::WdfUsbTargetDeviceSendControlTransferSynchronously failed!");

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::write_report_to(wdk::WDFREQUEST request) const noexcept -> system_result<void> {
        return last_input_report_.read([&request](const auto& report) noexcept {
            return fill_wdf_request_memory(request, report);
        });
    }
} // namespace lj::transport
