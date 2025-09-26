#include <spdlog/sinks/basic_file_sink.h>
#include "version.h"
#include <SimpleIni.h>

static void setupLog() {
	auto logsFolder = SKSE::log::log_directory();
	if (!logsFolder) SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
	auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
	auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
	auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
	auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
	spdlog::set_default_logger(std::move(loggerPtr));
	spdlog::set_level(spdlog::level::trace);
	spdlog::flush_on(spdlog::level::trace);

	const char* runtimeString = "<Unknown>";
	switch (REL::Module::GetRuntime()) {
	case REL::Module::Runtime::AE:
		runtimeString = "AE";
		break;
	case REL::Module::Runtime::SE:
		runtimeString = "SE";
		break;
	case REL::Module::Runtime::VR:
		runtimeString = "VR";
		break;
	}

	SPDLOG_INFO("TabKeyStuckFix {} - {} - {}", TabKeyStuckFix_VERSION, runtimeString, REL::Module::get().version());

	CSimpleIniA ini;
	auto rc = ini.LoadFile(L"Data/SKSE/Plugins/TabKeyStuckFix.ini");
	if (rc < 0) {
		SPDLOG_ERROR("Failed to read settings from Data/SKSE/Plugins/TabKeyStuckFix.ini, error: {}", rc);
		return;
	}

	spdlog::level::level_enum logLevel = spdlog::level::trace;
	const char* logLevelString = ini.GetValue("General", "LogLevel", "Trace");
	if (0 == stricmp(logLevelString, "trace")) {
		logLevel = spdlog::level::trace;
	} else if (0 == stricmp(logLevelString, "debug")) {
		logLevel = spdlog::level::debug;
	} else if (0 == stricmp(logLevelString, "info")) {
		logLevel = spdlog::level::info;
	} else if (0 == stricmp(logLevelString, "warn")) {
		logLevel = spdlog::level::warn;
	} else if (0 == stricmp(logLevelString, "error")) {
		logLevel = spdlog::level::err;
	} else if (0 == stricmp(logLevelString, "critical")) {
		logLevel = spdlog::level::critical;
	} else if (0 == stricmp(logLevelString, "off")) {
		logLevel = spdlog::level::off;
	} else {
		SPDLOG_ERROR("Invalid value \"{}\" for [General] => LogLevel INI setting, will use \"Trace\" instead", logLevelString);
	}

	if (logLevel != spdlog::level::trace) {
		spdlog::set_level(logLevel);
		spdlog::flush_on(logLevel);
	}
}

namespace MY {
	class ButtonEvent : public RE::IDEvent {
	public:
		inline static constexpr auto RTTI = RE::RTTI_ButtonEvent;
		inline static constexpr auto VTABLE = RE::VTABLE_ButtonEvent;

		~ButtonEvent() override {};  // 00

		// members
		float value;         // 28
		float heldDownSecs;  // 2C
	};
	static_assert(sizeof(ButtonEvent) == 0x30);

	// declare our own class because NG version of it doesn't work correctly
	class BSInputEventQueue : public RE::BSTSingletonSDM<MY::BSInputEventQueue> {
	public:
		inline static constexpr std::uint8_t MAX_BUTTON_EVENTS = 10;
		inline static constexpr std::uint8_t MAX_CHAR_EVENTS = 5;
		inline static constexpr std::uint8_t MAX_MOUSE_EVENTS = 1;
		inline static constexpr std::uint8_t MAX_THUMBSTICK_EVENTS = 2;
		inline static constexpr std::uint8_t MAX_CONNECT_EVENTS = 1;
		inline static constexpr std::uint8_t MAX_KINECT_EVENTS = 1;

		static BSInputEventQueue* GetSingleton() {
			REL::Relocation<BSInputEventQueue**> singleton{ RELOCATION_ID(520856, 407374) };
			return *singleton;
		}

		// members
		std::uint8_t       pad001;                                   // 001
		std::uint16_t      pad002;                                   // 002
		std::uint32_t      buttonEventCount;                         // 004
		std::uint32_t      charEventCount;                           // 008
		std::uint32_t      mouseEventCount;                          // 00C
		std::uint32_t      thumbstickEventCount;                     // 010
		std::uint32_t      connectEventCount;                        // 014
		std::uint32_t      kinectEventCount;                         // 018
		std::uint32_t      pad01C;                                   // 01C
		MY::ButtonEvent        buttonEvents[MAX_BUTTON_EVENTS];          // 020
		RE::CharEvent          charEvents[MAX_CHAR_EVENTS];              // 200
		RE::MouseMoveEvent     mouseEvents[MAX_MOUSE_EVENTS];            // 2A0
		RE::ThumbstickEvent    thumbstickEvents[MAX_THUMBSTICK_EVENTS];  // 2D0
		RE::DeviceConnectEvent connectEvents[MAX_CONNECT_EVENTS];        // 330
		RE::KinectEvent        kinectEvents[MAX_KINECT_EVENTS];          // 350
		RE::InputEvent* queueHead;                                // 380
		RE::InputEvent* queueTail;                                // 388

		void PushOntoInputQueue(RE::InputEvent* a_event) {
			if (!queueHead) {
				queueHead = a_event;
			}

			if (queueTail) {
				queueTail->next = a_event;
			}

			queueTail = a_event;
			queueTail->next = nullptr;
		}

		void AddButtonEvent(RE::INPUT_DEVICE a_device, std::int32_t a_id, float a_value, float a_duration) {
			if (buttonEventCount < MAX_BUTTON_EVENTS) {
				auto& cachedEvent = buttonEvents[buttonEventCount];
				cachedEvent.value = a_value;
				cachedEvent.heldDownSecs = a_duration;
				cachedEvent.device = a_device;
				cachedEvent.idCode = a_id;
				cachedEvent.userEvent = {};

				PushOntoInputQueue(&cachedEvent);
				++buttonEventCount;
			}
		}
	};
	static_assert(sizeof(MY::BSInputEventQueue) == 0x390);
}

static WNDPROC windowProcOriginal;
static LRESULT windowProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && wParam == VK_TAB) {
		// TODO: implement heldDownSecs?
		MY::BSInputEventQueue::GetSingleton()->AddButtonEvent(RE::INPUT_DEVICE::kKeyboard, RE::BSKeyboardDevice::Key::kTab, 0.0f, 0.0f);
        SPDLOG_TRACE("Added Tab key released event to BSInputEventQueue in response to {}", msg == WM_KEYUP ? "WM_KEYUP" : "WM_SYSKEYUP");
    }
	return windowProcOriginal(hwnd, msg, wParam, lParam);
}

static REL::Relocation<void(*)()> d3dInitOriginal;
static void d3dInitThunk() {
    SPDLOG_INFO("Hooking window proc...");

    d3dInitOriginal();

    const auto renderer = RE::BSGraphics::Renderer::GetSingleton();
    if (!renderer) {
        SPDLOG_ERROR("Failed to get renderer");
        return;
    }

    const auto swapChain = (IDXGISwapChain*)renderer->GetRuntimeData().renderWindows[0].swapChain;
    if (!swapChain) {
        SPDLOG_ERROR("Failed to get DXGI swap chain");
        return;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    HRESULT hr = swapChain->GetDesc(&desc);
    if (FAILED(hr)) {
        SPDLOG_ERROR("IDXGISwapChain::GetDesc({}) failed, HRESULT: {:08X}", (void*)swapChain, hr);
        return;
    }

    const auto device = (ID3D11Device*)renderer->GetRuntimeData().forwarder;
    const auto context = (ID3D11DeviceContext*)renderer->GetRuntimeData().context;

    windowProcOriginal = (WNDPROC)SetWindowLongPtrA(desc.OutputWindow, GWLP_WNDPROC, (LONG_PTR)windowProcThunk);
    if (!windowProcOriginal) {
        SPDLOG_ERROR("SetWindowLongPtrA({}) failed, error: {}", (void*)desc.OutputWindow, GetLastError());
        return;
    }

    SPDLOG_INFO("Hooked window proc");
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
    SKSE::Init(skse);
	setupLog();

	if (REL::Module::GetRuntime() == REL::Module::Runtime::VR) {
		SPDLOG_ERROR("This mod does not work for VR, skipping hook to prevent crash");
	} else {
		SPDLOG_INFO("Hooking d3dInit...");
		SKSE::AllocTrampoline(14);
		d3dInitOriginal = SKSE::GetTrampoline().write_call<5>(REL::VariantID(75595, 77226, 0xDC5530).address() + REL::VariantOffset(0x9, 0x275, 0x9).offset(), d3dInitThunk);
		SPDLOG_INFO("Hooked d3dInit");
	}

    return true;
}
