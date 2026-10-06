module;

#include "../windows.hpp"

#include "usb.hpp"

#include <stormkit/core/try_expected.hpp>

#undef move

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
        template<usb_context::joystick_type TYPE>
        auto calibrate_joystick(usb_context& usb, array_view<byte> bytes) noexcept -> void {
            static constexpr auto scale =
              [](axis_type x, axis_type rmin, axis_type rmax, axis_type tmin, axis_type tmax) noexcept -> axis_type {
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
            usb.update_calibration(TYPE, x, y);
            const auto joystick = usb.joystick_bounds(TYPE);

            // rescale x value from uncalibrated output to [0, 4095]
            const auto x_scaled = scale(x, joystick.min_x, joystick.max_x, 0, 4095);

            // rescale y value from uncalibrated output to [0, 4095]
            const auto y_scaled = [&joystick, &y] noexcept {
                if constexpr (TYPE == usb_context::joystick_type::LEFT) return scale(y, joystick.min_y, joystick.max_y, 4095, 0);
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
    auto usb_context::create(const device_context& ctx) -> system_result<usb_context> {
        const auto device = ctx.device();

        auto usb_device = WDFUSBDEVICE { nullptr };
        // Create and fill the usb sink configuration
        auto init_config = WDF_USB_DEVICE_CREATE_CONFIG {};
        WDF_USB_DEVICE_CREATE_CONFIG_INIT(&init_config, USBD_CLIENT_CONTRACT_VERSION_602);
        LoggedTry(lj::win_call(WdfUsbTargetDeviceCreateWithParameters,
                               ctx.device(),
                               &init_config,
                               WDF_NO_OBJECT_ATTRIBUTES,
                               &usb_device),
                  "Failed to create USB device context!");

        // Retrieve usb descriptor
        auto usb_descriptor = USB_DEVICE_DESCRIPTOR {};
        WdfUsbTargetDeviceGetDeviceDescriptor(usb_device, &usb_descriptor);

        // Create and fill the usb interface configuration
        auto select_config = WDF_USB_DEVICE_SELECT_CONFIG_PARAMS {};
        WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_MULTIPLE_INTERFACES(&select_config, 0, nullptr);

        // Retrieve usb interfaces
        LoggedTry(lj::win_call(WdfUsbTargetDeviceSelectConfig, usb_device, WDF_NO_OBJECT_ATTRIBUTES, &select_config),
                  "Failed to configure USB device!");

        auto hid_endpoint     = endpoint {};
        auto command_endpoint = endpoint {};

        hid_endpoint.interface     = WdfUsbTargetDeviceGetInterface(usb_device, 0);
        command_endpoint.interface = WdfUsbTargetDeviceGetInterface(usb_device, 1);

        // retrieve pipe handles for USB I/O
        // hid_endpoint     => interrupt
        // command_endpoint => bulk
        auto endpoints = array { &hid_endpoint, &command_endpoint };
        for (const auto [i, endpoint_] : stdv::enumerate(endpoints)) {
            auto& endpoint = *endpoint_;

            for (auto pipe_index : range(WdfUsbInterfaceGetNumConfiguredPipes(endpoint.interface))) {
                auto pipe_info = WDF_USB_PIPE_INFORMATION {};
                WDF_USB_PIPE_INFORMATION_INIT(&pipe_info);

                const auto pipe = WdfUsbInterfaceGetConfiguredPipe(endpoint.interface, pipe_index, &pipe_info);

                // TODO investigate why this is necessery
                WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pipe);

                dlog("Pipe {}, Type: {} In: {} Out: {}",
                     pipe_index,
                     as<u8>(pipe_info.PipeType),
                     WdfUsbTargetPipeIsInEndpoint(pipe),
                     WdfUsbTargetPipeIsOutEndpoint(pipe));

                const auto type = (i == 0) ? WdfUsbPipeTypeInterrupt : WdfUsbPipeTypeBulk;

                if (pipe_info.PipeType == type and WdfUsbTargetPipeIsInEndpoint(pipe)) endpoint.in_pipe = pipe;
                if (pipe_info.PipeType == type and WdfUsbTargetPipeIsOutEndpoint(pipe)) endpoint.out_pipe = pipe;
            }

            if (not endpoint.in_pipe or not endpoint.out_pipe) {
                elog("Failed to get endpoint pipes! in_pipe: {}, out_pipe: {}",
                     static_cast<void*>(endpoint.in_pipe),
                     static_cast<void*>(endpoint.out_pipe));
                return std::unexpected { error_code::from_ntstatus(STATUS_INVALID_DEVICE_STATE) };
            }
        }

        // // get product name if available
        // const auto result = lj::win_call(WdfUsbTargetDeviceAllocAndQueryString,
        //                                  usb.device,
        //                                  WDF_NO_OBJECT_ATTRIBUTES,
        //                                  &usb.product_string,
        //                                  nullptr,
        //                                  usb.descriptor.iProduct,
        //                                  0x409);
        // if (result.has_value()) {
        //     auto size          = 0_usize;
        //     auto memory_buffer = WdfMemoryGetBuffer(usb.product_string, &size);
        //     // ctx.product_string = wide_to_ascii({ reinterpret_cast<const wchar_t*>(memory_buffer), (size / sizeof(wchar_t))
        //     });
        // } else
        //     lj::wlog("Failed to get product string from USB device!\n    error: {}", result.error());

        return {
            usb_context { ctx, usb_device, std::move(hid_endpoint), std::move(command_endpoint), std::move(usb_descriptor) }
        };
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::pipe_reader_completion(WDFUSBPIPE, WDFMEMORY memory, usize count, WDFCONTEXT data) noexcept -> void {
        if (data == nullptr or count == 0 or count != hid::INPUT_REPORT_SIZE) return;

        auto& usb    = *reinterpret_cast<usb_context*>(data);
        auto  report = hid::input_report_buffer {};
        LoggedTryOr(get_wdf_memory(memory, report), monadic::discard(), "Failed to get USB data!");

        // Left joystick
        // y axis of left joystick is inverted in uncalibrated data
        calibrate_joystick<usb_context::joystick_type::LEFT>(usb, mutable_view_of(report).subspan(0x6, 0x3));
        // Right joystick
        calibrate_joystick<usb_context::joystick_type::RIGHT>(usb, mutable_view_of(report).subspan(0x9, 0x3));

        // Update input report
        usb.last_input_report_.assign(std::move(report));
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::request_completion_routine(WDFREQUEST request,
                                                 WDFIOTARGET,
                                                 PWDF_REQUEST_COMPLETION_PARAMS,
                                                 WDFCONTEXT) noexcept -> void {
        dlog("request {} completed", static_cast<void*>(request));
        WdfObjectDelete(request);
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::device_entry() noexcept -> system_result<void> {
        using enum hid::feature_select::feature_flag;

        constexpr auto ENABLED_FEATURES
          // = BUTTON_STATE | ANALOG_STICKS;
          = as<hid::feature_select::feature_flag>(0x27_u8);

        // Send init sequence
        // LoggedTry((hid::send_command_validate<hid::init::initialize_usb_command<transport_type::USB>>(*this)),
        //           "Failed to initialize USB link!");
        // LoggedTry((hid::send_command_validate<
        //             hid::feature_select::set_feature_mask_command<transport_type::USB>>(*this, ENABLED_FEATURES)),
        //           "Failed to set feature mask!");
        // LoggedTry((hid::send_command_validate<
        //             hid::feature_select::enable_features_command<transport_type::USB>>(*this, ENABLED_FEATURES)),
        //           "Failed to enable features!");
        // LoggedTry((hid::send_command_validate<hid::leds::set_player_1_command<transport_type::USB>>(*this)),
        //           "Failed to setup player LED!");
        // LoggedTry((hid::send_command_validate<
        //             hid::init::select_input_report_command<transport_type::USB>>(*this,
        //                                                                          hid::init::input_report_id::ALT_PROCON_2)),
        //           "Failed to select input report!");

        LoggedTry((send_command<hid::init::initialize_usb_command>()), "Failed to initialize USB link!");
        LoggedTry((send_command<hid::feature_select::set_feature_mask_command>(ENABLED_FEATURES)), "Failed to set feature mask!");
        LoggedTry((send_command<hid::feature_select::enable_features_command>(ENABLED_FEATURES)), "Failed to enable features!");
        LoggedTry((send_command<hid::leds::set_player_1_command>()), "Failed to setup player LED!");
        LoggedTry((send_command<hid::init::select_input_report_command>(hid::init::input_report_id::ALT_PROCON_2)),
                  "Failed to select input report!");

        ilog("{} initialized! (USB)", device_ctx_.get()->product_string());

        // Prepare continuous USB reader
        auto config = WDF_USB_CONTINUOUS_READER_CONFIG {};
        WDF_USB_CONTINUOUS_READER_CONFIG_INIT(&config,
                                              pipe_reader_completion,
                                              reinterpret_cast<WDFCONTEXT>(this),
                                              hid::INPUT_REPORT_SIZE);
        LoggedTry(lj::win_call(WdfUsbTargetPipeConfigContinuousReader, hid_.in_pipe, &config),
                  "WdfUsbTargetPipeConfigContinuousReader failed!");

        // Start continuous USB reader
        auto io_target = WdfUsbTargetPipeGetIoTarget(hid_.in_pipe);
        LoggedTry(lj::win_call(WdfIoTargetStart, io_target), "Failed to start USB read pipe!");
        dlog("USB continuous reader started !");

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::device_exit() const noexcept -> system_result<void> {
        // Stop continueous reader
        auto io_target = WdfUsbTargetPipeGetIoTarget(command_.out_pipe);
        WdfIoTargetStop(io_target, WdfIoTargetCancelSentIo);
        dlog("USB continuous reader stopped (command)!");

        io_target = WdfUsbTargetPipeGetIoTarget(hid_.in_pipe);
        WdfIoTargetStop(io_target, WdfIoTargetCancelSentIo);
        dlog("USB continuous reader stopped (hid)!");

        ilog("{} disconnected! (USB)", device_ctx_.get()->product_string());

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::send_data(array_view<const byte> payload) const noexcept -> system_result<void> {
        // Retrieve io target
        auto target = WdfUsbTargetDeviceGetIoTarget(device_);

        // Initialize request
        auto request = WDFREQUEST {};
        LoggedTry(lj::win_call(WdfRequestCreate, nullptr, target, &request), "WdfRequestCreate failed!");

        // Allocate request memory and fill it with payload
        // LoggedTryTo(result,
        //             wdf_memory_allocate(stdr::size(payload), request),
        //             elog,
        //             "Failed to allocate memory for USB send payload!");
        TryTo(result, wdf_memory_allocate(stdr::size(payload), request));
        auto [memory, write_buffer] = std::move(result);
        stdr::copy(payload, stdr::begin(write_buffer));

        CustomLoggedTry(lj::win_call(WdfUsbTargetPipeFormatRequestForWrite, command_.out_pipe, request, memory, WDF_NO_HANDLE),
                        dlog,
                        "WdfUsbTargetPipeFormatRequestForWrite failed!");

        // Setup completion callback
        WdfRequestSetCompletionRoutine(request, request_completion_routine, WDF_NO_HANDLE);

        // Send request
        if (WdfRequestSend(request, target, WDF_NO_SEND_OPTIONS) == FALSE) {
            const auto status = WdfRequestGetStatus(request);
            dlog("WdfRequestSend failed!\n    reason: {:#x}", as<ulong>(status));
            return std::unexpected { error_code::from_ntstatus(status) };
        }

        dlog("request {}, {::#x} sent",
             static_cast<void*>(request),
             array_view<const u8> { reinterpret_cast<const u8*>(stdr::data(payload)), stdr::size(payload) });

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::send_data_sync(array_view<const byte> payload) noexcept -> system_result<void> {
        // Initialize and fill memory descriptor
        auto memory_descriptor = WDF_MEMORY_DESCRIPTOR {};
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memory_descriptor, std::bit_cast<void*>(stdr::data(payload)), stdr::size(payload));

        // Send request
        auto written = ulong { 0 };
        CustomLoggedTry(lj::win_call(WdfUsbTargetPipeWriteSynchronously,
                                     command_.out_pipe,
                                     nullptr,
                                     nullptr,
                                     &memory_descriptor,
                                     &written),
                        dlog,
                        "WdfUsbTargetPipeWriteSynchronously failed!");

        dlog("Sent {::#x}", array_view<const u8> { reinterpret_cast<const u8*>(stdr::data(payload)), stdr::size(payload) });

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::get_data_sync() const noexcept -> system_result<hid::command_report_buffer> {
        // Initialize and fill memory descriptor
        auto report            = hid::command_report_buffer {};
        auto memory_descriptor = WDF_MEMORY_DESCRIPTOR {};
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memory_descriptor, stdr::data(report), stdr::size(report));

        // Send request
        auto readed = ulong { 0 };
        CustomLoggedTry(lj::win_call(WdfUsbTargetPipeReadSynchronously,
                                     command_.in_pipe,
                                     nullptr,
                                     nullptr,
                                     &memory_descriptor,
                                     &readed),
                        dlog,
                        "WdfUsbTargetPipeReadSynchronously failed! {}");

        dlog("Received {::#x}", array_view<const u8> { reinterpret_cast<const u8*>(stdr::data(report)), stdr::size(report) });

        return { std::move(report) };
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::send_control_request(byte request, byte value, byte index, array_view<const byte> data) noexcept
      -> system_result<void> {
        // Create and fill request send options
        auto options = WDF_REQUEST_SEND_OPTIONS {};
        WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
        WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(3));

        // Initialize control setup packet
        auto control_setup_packet = WDF_USB_CONTROL_SETUP_PACKET {};
        WDF_USB_CONTROL_SETUP_PACKET_INIT(&control_setup_packet,
                                          WDF_USB_BMREQUEST_DIRECTION::BmRequestHostToDevice,
                                          WDF_USB_BMREQUEST_RECIPIENT::BmRequestToDevice,
                                          as<u8>(request),
                                          as<u8>(value),
                                          as<u8>(index));

        // Initialize and fill memory descriptor
        auto memory_descriptor = WDF_MEMORY_DESCRIPTOR {};
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memory_descriptor, std::bit_cast<void*>(stdr::data(data)), stdr::size(data));

        // Send request
        CustomLoggedTry(lj::win_call(WdfUsbTargetDeviceSendControlTransferSynchronously,
                                     device_,
                                     WDF_NO_HANDLE,
                                     &options,
                                     &control_setup_packet,
                                     &memory_descriptor,
                                     nullptr),
                        dlog,
                        "WdfUsbTargetDeviceSendControlTransferSynchronously failed!");

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto usb_context::write_report_to(WDFREQUEST request) const noexcept -> system_result<void> {
        return last_input_report_.read([&request](const auto& report) noexcept {
            return fill_wdf_request_memory(request, report);
        });
    }
} // namespace lj::transport
