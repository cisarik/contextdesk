cmake_minimum_required(VERSION 3.25)
project(contextdeck VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/contextdeck-warnings.cmake")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Network Qml Quick QuickControls2 Svg Test)
find_package(KF6StatusNotifierItem REQUIRED)
find_package(KF6Kirigami REQUIRED)
find_package(KF6Screen REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

add_library(contextdeck_core STATIC
    src/core/ControlCatalog.cpp
    src/core/Resolver.cpp
    src/core/Persistence.cpp
    src/core/ZoneMap.cpp
)
target_include_directories(contextdeck_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(contextdeck_core PUBLIC Qt6::Core)

add_library(contextdeck_context STATIC
    src/context/ContextReceiver.cpp
)
target_include_directories(contextdeck_context PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(contextdeck_context PUBLIC contextdeck_core Qt6::DBus)

add_library(contextdeck_rgb STATIC
    src/rgb/OpenRgbProtocol.cpp
    src/rgb/OpenRgbClient.cpp
)
target_include_directories(contextdeck_rgb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(contextdeck_rgb PUBLIC contextdeck_core Qt6::Network)

add_library(contextdeck_actions STATIC
    src/actions/PowerActions.cpp
)
target_include_directories(contextdeck_actions PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(contextdeck_actions PUBLIC Qt6::DBus Qt6::Gui KF6::ScreenDpms)

add_executable(contextdeck
    src/app/main.cpp
    src/app/SessionApplication.cpp
    src/app/AppController.cpp
    src/app/TrayController.cpp
    src/app/SettingsHost.cpp
    src/app/BrokerIpcClient.cpp
)
target_link_libraries(contextdeck PRIVATE
    contextdeck_context
    contextdeck_rgb
    contextdeck_actions
    contextdeck_ipc
    Qt6::Widgets
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
    KF6::StatusNotifierItem
    KF6::Kirigami
)
# qt_add_qml_module emits #include <ChordRecorder.h>; the header lives in src/app/.
target_include_directories(contextdeck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/app")

# QML lives under ui/; without this, implicit import is that directory and C++
# types on the module URI (ChordRecorder) are invisible.
qt_policy(SET QTP0004 NEW)

qt_add_qml_module(contextdeck
    URI io.github.cisarik.ContextDeck
    VERSION 1.0
    SOURCES
        src/app/ChordRecorder.h
        src/app/ChordRecorder.cpp
    QML_FILES
        ui/Main.qml
        ui/OverviewPage.qml
        ui/ZoneHero.qml
        ui/ColorsPage.qml
        ui/ApplicationsPage.qml
        ui/ControlsPage.qml
        ui/DiagnosticsPage.qml
        ui/LightingPresetEditor.qml
    NO_RESOURCE_TARGET_PATH
)

add_library(contextdeck_ipc STATIC
    src/broker/IpcProtocol.cpp
)
target_include_directories(contextdeck_ipc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(contextdeck_ipc PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(contextdeck_broker_core STATIC
    src/broker/Logger.cpp
    src/broker/IdentityMatcher.cpp
    src/broker/KeyLedger.cpp
    src/broker/Acquisition.cpp
    src/broker/FakeGrabber.cpp
    src/broker/EvdevGrabber.cpp
    src/broker/GrabbingSource.cpp
    src/broker/FakeSink.cpp
    src/broker/FakeSource.cpp
    src/broker/RealSink.cpp
    src/broker/ForwardingEngine.cpp
    src/broker/Watchdog.cpp
    src/broker/EventLoop.cpp
    src/broker/IdleWait.cpp
    src/broker/Selftest.cpp
    src/broker/WatchdogSelftest.cpp
    src/broker/SessionIpc.cpp
)
target_include_directories(contextdeck_broker_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(contextdeck_broker_core PUBLIC contextdeck_ipc PkgConfig::LIBEVDEV PkgConfig::LIBUDEV PkgConfig::LIBSYSTEMD)

add_executable(contextdeck-broker src/broker/main.cpp)
target_compile_definitions(contextdeck-broker PRIVATE CONTEXTDECK_VERSION="${PROJECT_VERSION}")
target_link_libraries(contextdeck-broker PRIVATE contextdeck_broker_core)
set_target_properties(contextdeck_broker_core contextdeck-broker PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

enable_testing()

add_executable(test_profile_resolver tests/unit/test_profile_resolver.cpp)
target_link_libraries(test_profile_resolver PRIVATE contextdeck_core Qt6::Test)
add_test(NAME test_profile_resolver COMMAND test_profile_resolver)

add_executable(test_profile_persistence tests/unit/test_profile_persistence.cpp)
target_link_libraries(test_profile_persistence PRIVATE contextdeck_core Qt6::Test)
add_test(NAME test_profile_persistence COMMAND test_profile_persistence)

add_executable(test_openrgb_protocol tests/unit/test_openrgb_protocol.cpp)
target_link_libraries(test_openrgb_protocol PRIVATE contextdeck_rgb Qt6::Test)
add_test(NAME test_openrgb_protocol COMMAND test_openrgb_protocol)

add_executable(test_broker_identity tests/unit/test_broker_identity.cpp)
target_link_libraries(test_broker_identity PRIVATE contextdeck_broker_core)
set_target_properties(test_broker_identity PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME test_broker_identity COMMAND test_broker_identity)

add_executable(test_broker_ledger tests/unit/test_broker_ledger.cpp)
target_link_libraries(test_broker_ledger PRIVATE contextdeck_broker_core)
set_target_properties(test_broker_ledger PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME test_broker_ledger COMMAND test_broker_ledger)

add_executable(test_broker_forwarding tests/unit/test_broker_forwarding.cpp)
target_link_libraries(test_broker_forwarding PRIVATE contextdeck_broker_core)
set_target_properties(test_broker_forwarding PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME test_broker_forwarding COMMAND test_broker_forwarding)

add_executable(test_broker_acquisition tests/unit/test_broker_acquisition.cpp)
target_link_libraries(test_broker_acquisition PRIVATE contextdeck_broker_core)
set_target_properties(test_broker_acquisition PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME test_broker_acquisition COMMAND test_broker_acquisition)

add_executable(test_broker_watchdog tests/unit/test_broker_watchdog.cpp)
target_link_libraries(test_broker_watchdog PRIVATE contextdeck_broker_core)
set_target_properties(test_broker_watchdog PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME test_broker_watchdog COMMAND test_broker_watchdog)

add_executable(test_broker_ipc tests/unit/test_broker_ipc.cpp)
target_link_libraries(test_broker_ipc PRIVATE contextdeck_broker_core)
set_target_properties(test_broker_ipc PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_test(NAME test_broker_ipc COMMAND test_broker_ipc)
