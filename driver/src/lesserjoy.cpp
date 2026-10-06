module;

#include "windows.hpp"

#include <stormkit/core/try_expected.hpp>

export module lesserjoy;

import std;

import stormkit.core;
import stormkit.log;

import :device;
import :log;
import :wdf;

module: private;

using namespace stormkit;

namespace lj {
    EVT_WDF_OBJECT_CONTEXT_CLEANUP event_driver_cleanup;

    ////////////////////////////////////////
    ////////////////////////////////////////
    _Use_decl_annotations_ auto event_driver_cleanup(_In_ WDFOBJECT driver) -> void {
        lj::dlog("Driver successfully cleanup up!");
    }
} // namespace lj

#pragma code_seg("INIT")

auto logger = heap_ptr<lj::kernel_logger> {};

////////////////////////////////////////
////////////////////////////////////////
extern "C" __declspec(dllexport) auto APIENTRY DllMain(HMODULE module, DWORD, LPVOID) -> BOOL {
    if (not logger) logger = log::logger::allocate_logger_instance<lj::kernel_logger>();
#ifdef STORMKIT_DEBUG_MODE
    logger->set_severity_mask(logger->severity_mask() | log::severity::DEBUG);
#endif

    DisableThreadLibraryCalls(module);

    return TRUE;
}

extern "C" DRIVER_INITIALIZE DriverEntry;

////////////////////////////////////////
////////////////////////////////////////
extern "C" _Use_decl_annotations_ auto DriverEntry(_In_ PDRIVER_OBJECT driver_object, _In_ PUNICODE_STRING registry_path)
  -> NTSTATUS {
    lj::ilog("Initializing lesserjoy driver...");

    auto attributes = WDF_OBJECT_ATTRIBUTES {};
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = lj::event_driver_cleanup;

    auto config = WDF_DRIVER_CONFIG {};
    WDF_DRIVER_CONFIG_INIT(&config, lj::device_context::create);

    CustomLoggedTryOr(lj::win_call(WdfDriverCreate, driver_object, registry_path, &attributes, &config, WDF_NO_HANDLE),
                      monadic::unwrap(),
                      lj::elog,
                      "Failed to initialize lessjoy driver! {}");

    lj::ilog("Driver successfully initialized!");

    return 0;
}

#pragma code_seg()
