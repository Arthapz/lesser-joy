module;

#include <stormkit/core/try_expected.hpp>

#include "contract_macro.hpp"
#include "wdk/wdfobject_macros.hpp"

module lesserjoy;

import :wdf;
import :log;
import :hid;
import :transport.usb;

using namespace stormkit;
using namespace stormkit::literals;

namespace stdr = std::ranges;

namespace lj {
    ////////////////////////////////////////
    ////////////////////////////////////////
    auto device_context::create(wdk::WDFDRIVER driver, wdk::PWDFDEVICE_INIT device_init) -> wdk::NTSTATUS {
        // initialize filter
        wdk::WdfFdoInitSetFilter(device_init);

        // Create and fill the PnP power callbacks configuration
        auto power_callbacks = wdk::WDF_PNPPOWER_EVENT_CALLBACKS {};
        wdk::WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&power_callbacks);
        power_callbacks.EvtDevicePrepareHardware = prepare_hardware;
        power_callbacks.EvtDeviceReleaseHardware = release_hardware;
        power_callbacks.EvtDeviceD0Entry         = device_entry;
        power_callbacks.EvtDeviceD0Exit          = device_exit;

        // set the Pnp power callback
        wdk::WdfDeviceInitSetPnpPowerEventCallbacks(device_init, &power_callbacks);

        // Create and fill the device context attributes
        auto attributes = wdk::WDF_OBJECT_ATTRIBUTES {};
        wdk::WdfObjectAttributesInitContextType(attributes, _WDF_device_context_TYPE_INFO);
        attributes.EvtDestroyCallback = destroy_ctx<device_context, get_device_context>;

        // create the actual device
        auto device = wdk::WDFDEVICE {};
        LoggedTryOr(lj::win_call(wdk::WdfDeviceCreate, &device_init, &attributes, &device),
                    monadic::map(monadic::unwrap(), monadic::as<wdk::NTSTATUS>()),
                    "Failed to create device!");

        // construct the device class into the raw memory that wdk::WDF allocated
        // via placement new
        auto device_ctx = get_device_context(device);
        WDK_ASSERT_NTSTATUS(device_ctx != nullptr);
        new (device_ctx) device_context { device };

        // create and expose device hid interface
        LoggedTryOr(lj::win_call(wdk::WdfDeviceCreateDeviceInterface, device, &dev_interface_hid_guid, nullptr),
                    monadic::map(monadic::unwrap(), monadic::as<wdk::NTSTATUS>()),
                    "Failed to expose device interface!");
        LoggedTryOr(lj::win_call(queue_context::create, device),
                    monadic::map(monadic::unwrap(), monadic::as<wdk::NTSTATUS>()),
                    "Failed to create queue context!");

        return wdk::STATUS_SUCCESS;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto device_context::prepare_hardware() noexcept -> system_result<void> {
        // currently we only support the pro controller
        // so it's hardcoded
        static constexpr auto default_controller = controllers_type.at("pro_controller");

        vendor_id_  = default_controller.vid;
        product_id_ = default_controller.pid;

        hid_attributes_.Size      = sizeof(wdk::HID_DEVICE_ATTRIBUTES);
        hid_attributes_.VendorID  = default_controller.vid;
        hid_attributes_.ProductID = default_controller.pid;
        hid_descriptor_           = hid::default_descriptor;
        report_descriptor_        = hid::default_report_descriptor;
        product_string_           = default_controller.product_string;

        // set BusReportedDeviceDesc so joy.cpl shows the profile name
        // DEVPKEY_Device_BusReportedDeviceDesc = {540b947e-8b40-45bc-a8a2-6a0b894cbda2}
        constexpr auto bus_desc_key = wdk::DEVPROPKEY {
            { 0x540b947e, 0x8b40, 0x45bc, { 0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2 } },
            4
        };

        auto prop_data = wdk::WDF_DEVICE_PROPERTY_DATA {};
        wdk::WDF_DEVICE_PROPERTY_DATA_INIT(&prop_data, &bus_desc_key);
        prop_data.Lcid = 0x00; // LOCALE_NEUTRAL
        auto _         = wdk::WdfDeviceAssignProperty(device_,
                                                      &prop_data,
                                                      wdk::DEVPROP_TYPE_STRING,
                                                      stdr::size(product_string_) + 1, // +1 to take account of null terminator
                                                      stdr::data(product_string_));

        // initialize usb context
        LoggedTryTo(transport, transport::usb_context::create(*this), "Failed to allocate usb context!");
        transport_ = std::move(transport);

        lj::ilog("{} attached (USB), PID: {:#x}, VID: {:#x}", product_string_, vendor_id_, product_id_);

        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto device_context::release_hardware() noexcept -> system_result<void> {
        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto device_context::device_entry() noexcept -> system_result<void> {
        if (is_usb()) { LoggedTry(usb_ctx().device_entry(), "Device entry failed!"); }
        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto device_context::device_exit() noexcept -> system_result<void> {
        if (is_usb()) { LoggedTry(usb_ctx().device_exit(), "Device exit failed!"); }
        return {};
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto device_context::prepare_hardware(wdk::WDFDEVICE device, wdk::WDFCMRESLIST, wdk::WDFCMRESLIST) noexcept -> wdk::NTSTATUS {
        auto ctx = get_device_context(device);
        WDK_EXPECTS_NTSTATUS(ctx != nullptr);

        LoggedTryOr(ctx->prepare_hardware(),
                    monadic::map(monadic::unwrap(), monadic::as<wdk::NTSTATUS>()),
                    "Device prepare hardware failed!");

        return wdk::STATUS_SUCCESS;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto device_context::release_hardware(wdk::WDFDEVICE device, wdk::WDFCMRESLIST) noexcept -> wdk::NTSTATUS {
        auto ctx = get_device_context(device);
        WDK_EXPECTS_NTSTATUS(ctx != nullptr);

        CustomLoggedTryOr(ctx->release_hardware(),
                          monadic::map(monadic::unwrap(), monadic::as<wdk::NTSTATUS>()),
                          lj::elog,
                          "Device release hardware failed!");

        return wdk::STATUS_SUCCESS;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto device_context::device_entry(wdk::WDFDEVICE device, wdk::WDF_POWER_DEVICE_STATE) noexcept -> wdk::NTSTATUS {
        auto ctx = get_device_context(device);
        WDK_EXPECTS_NTSTATUS(ctx != nullptr);

        CustomLoggedTryOr(ctx->device_entry(),
                          monadic::map(monadic::unwrap(), monadic::as<wdk::NTSTATUS>()),
                          lj::elog,
                          "Device entry failed!");

        return wdk::STATUS_SUCCESS;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto device_context::device_exit(wdk::WDFDEVICE device, wdk::WDF_POWER_DEVICE_STATE) noexcept -> wdk::NTSTATUS {
        auto ctx = get_device_context(device);
        WDK_EXPECTS_NTSTATUS(ctx != nullptr);

        CustomLoggedTryOr(ctx->device_exit(),
                          monadic::map(monadic::unwrap(), monadic::as<wdk::NTSTATUS>()),
                          lj::elog,
                          "Device exit failed!");

        return wdk::STATUS_SUCCESS;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto queue_context::create(wdk::WDFDEVICE device) noexcept -> wdk::NTSTATUS {
        // Create and fill the io queue configuration and attributes
        auto config = wdk::WDF_IO_QUEUE_CONFIG {};
        wdk::WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&config, wdk::WDF_IO_QUEUE_DISPATCH_TYPE::WdfIoQueueDispatchParallel);
        config.PowerManaged       = wdk::WDF_TRI_STATE::WdfTrue;
        config.EvtIoDeviceControl = io_control;

        // Create and fill the queue context attributes
        auto attributes = wdk::WDF_OBJECT_ATTRIBUTES {};
        wdk::WdfObjectAttributesInitContextType(attributes, _WDF_queue_context_TYPE_INFO);
        attributes.ParentObject       = device;
        attributes.EvtDestroyCallback = destroy_ctx<queue_context, get_queue_context>;

        // create the actual io queue
        auto queue = wdk::WDFQUEUE { nullptr };
        CustomLoggedTryOr(lj::win_call(wdk::WdfIoQueueCreate, device, &config, &attributes, &queue),
                          monadic::map(monadic::unwrap(), monadic::as<wdk::NTSTATUS>()),
                          lj::elog,
                          "Failed to create io queue!");

        // construct the device class into the raw memory that wdk::WDF allocated
        // via placement new
        auto queue_ctx = get_queue_context(queue);
        WDK_ASSERT_NTSTATUS(queue_ctx != nullptr);
        new (queue_ctx) queue_context { get_device_context(device), queue };

        return wdk::STATUS_SUCCESS;
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto queue_context::io_control(wdk::WDFQUEUE   queue,
                                   wdk::WDFREQUEST request,
                                   usize           output_buffer_size,
                                   usize           input_buffer_size,
                                   u32             io_control_code,
                                   tag) const noexcept -> void {
        auto request_completed = true;
        auto status            = wdk::NTSTATUS { wdk::STATUS_SUCCESS };

        auto _ = at_scope_exit_do([&request_completed, request, &status] noexcept {
            if (request_completed) wdk::WdfRequestComplete(request, status);
        });

        const auto update_status = [&status](auto&& error) noexcept { status = error.value(); };

        const auto& device_ctx = *device_ctx_;

        switch (io_control_code) {
            case wdk::IOCTL_HID_GET_DEVICE_DESCRIPTOR: {
                dlog("IOCTL_HID_GET_DEVICE_DESCRIPTOR");
                CustomLoggedTryOr(hid::ioctl::get_device_descriptor(request, device_ctx.hid_descriptor()),
                                  update_status,
                                  lj::elog,
                                  "IOCTL Failed to get device descriptor!");
            } break;

            case wdk::IOCTL_HID_GET_DEVICE_ATTRIBUTES: {
                dlog("IOCTL_HID_GET_DEVICE_ATTRIBUTES");
                CustomLoggedTryOr(hid::ioctl::get_device_attributes(request, device_ctx.hid_attributes()),
                                  update_status,
                                  lj::elog,
                                  "IOCTL Failed to get device attributes!");
            } break;

            case wdk::IOCTL_HID_GET_REPORT_DESCRIPTOR: {
                dlog("IOCTL_HID_GET_REPORT_DESCRIPTOR");
                CustomLoggedTryOr(hid::ioctl::get_report_descriptor(request, device_ctx.report_descriptor()),
                                  update_status,
                                  lj::elog,
                                  "IOCTL Failed to get report descriptor!");
            } break;

            case wdk::IOCTL_HID_GET_INPUT_REPORT: {
                wlog("IOCTL_HID_GET_INPUT_REPORT not supported");
                status = wdk::STATUS_NOT_IMPLEMENTED;
            } break;

            case wdk::IOCTL_HID_READ_REPORT: {
                request_completed = false;
                if (device_ctx.is_usb()) {
                    CustomLoggedTryOr(hid::ioctl::read_report(request, device_ctx.usb_ctx()),
                                      update_status,
                                      lj::elog,
                                      "OCTL Failed to read report!");
                    request_completed = true;
                }
            } break;

            case wdk::IOCTL_HID_WRITE_REPORT: {
                CustomLoggedTryOr(hid::ioctl::write_report(request, device_ctx.usb_ctx()),
                                  update_status,
                                  lj::elog,
                                  "IOCTL Failed to write report!");
            } break;

            case wdk::IOCTL_HID_GET_STRING: {
                const auto& product_string = [&device_ctx] noexcept -> const string& {
                    const auto& ctx = device_ctx.usb_ctx();
                    if (ctx.has_product_string()) return ctx.product_string();

                    return device_ctx.product_string();
                }();

                const auto& serial_string = [&device_ctx] noexcept -> const string& {
                    const auto& ctx = device_ctx.usb_ctx();
                    if (ctx.has_serial_string()) return ctx.serial_string();
                    else {
                        static constinit auto empty_serial = string { "NO SERIAL" };
                        return empty_serial;
                    }
                    std::unreachable();
                }();

                CustomLoggedTryOr(hid::ioctl::get_string(request, product_string, serial_string),
                                  update_status,
                                  lj::elog,
                                  "IOCTL Failed to get string!");
            } break;

            case wdk::IOCTL_HID_GET_INDEXED_STRING: {
                const auto& product_string = [&device_ctx] noexcept -> const string& {
                    const auto& ctx = device_ctx.usb_ctx();
                    if (ctx.has_product_string()) return ctx.product_string();

                    return device_ctx.product_string();
                }();

                CustomLoggedTryOr(hid::ioctl::get_indexed_string(request, product_string),
                                  update_status,
                                  lj::elog,
                                  "IOCTL Failed to get indexed string!");
            } break;

            case wdk::IOCTL_HID_DEVICERESET_NOTIFICATION: {
                wlog("IOCTL_HID_DEVICERESET_NOTIFICATION not supported");
                status = wdk::STATUS_NOT_IMPLEMENTED;
            } break;

            case wdk::IOCTL_HID_ACTIVATE_DEVICE: {
                // wlog("IOCTL_HID_ACTIVATE_DEVICE not supported!");
                // status = wdk::STATUS_NOT_IMPLEMENTED;
            } break;

            case wdk::IOCTL_HID_DEACTIVATE_DEVICE: {
                wlog("IOCTL_HID_DEACTIVATE_DEVICE not supported!");
                status = wdk::STATUS_NOT_IMPLEMENTED;
            } break;

            case wdk::IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST: {
                wlog("IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST not supported!");
                status = wdk::STATUS_NOT_IMPLEMENTED;
            } break;

            case wdk::IOCTL_UMDF_GET_PHYSICAL_DESCRIPTOR: {
                wlog("IOCTL_UMDF_GET_PHYSICAL_DESCRIPTOR not supported");
                status = wdk::STATUS_NOT_IMPLEMENTED;
            } break;

            case wdk::IOCTL_UMDF_HID_GET_FEATURE: {
                wlog("IOCTL_UMDF_HID_GET_FEATURE not supported");
                status = wdk::STATUS_NOT_IMPLEMENTED;
            } break;

            case wdk::IOCTL_UMDF_HID_GET_INPUT_REPORT: {
                request_completed = false;
                if (device_ctx.is_usb()) {
                    CustomLoggedTryOr(hid::ioctl::read_report(request, device_ctx.usb_ctx()),
                                      update_status,
                                      lj::elog,
                                      "OCTL Failed to read report!");
                    request_completed = true;
                }
            } break;

            case wdk::IOCTL_UMDF_HID_SET_FEATURE: {
                wlog("IOCTL_UMDF_HID_SET_FEATURE not supported");
                status = wdk::STATUS_NOT_IMPLEMENTED;
            } break;

            case wdk::IOCTL_UMDF_HID_SET_OUTPUT_REPORT: {
                CustomLoggedTryOr(hid::ioctl::write_report(request, device_ctx.usb_ctx()),
                                  update_status,
                                  lj::elog,
                                  "IOCTL Failed to write report!");
            } break;

            default: {
                status = wdk::STATUS_NOT_IMPLEMENTED;
            } break;
        }
    }

    ////////////////////////////////////////
    ////////////////////////////////////////
    auto queue_context::io_control(wdk::WDFQUEUE   queue,
                                   wdk::WDFREQUEST request,
                                   usize           output_buffer_size,
                                   usize           input_buffer_size,
                                   win32::ULONG    io_control_code) noexcept -> void {
        auto queue_ctx = get_queue_context(queue);
        WDK_EXPECTS_VOID(queue_ctx != nullptr);

        queue_ctx->io_control(queue, request, output_buffer_size, input_buffer_size, as<u32>(io_control_code), tag {});
    }
} // namespace lj
