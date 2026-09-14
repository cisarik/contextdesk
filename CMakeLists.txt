cmake_minimum_required(VERSION 3.25)
project(contextdeck VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/contextdeck-warnings.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/contextdeck-tests.cmake")
include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Network Qml Quick QuickControls2 Svg Test)
find_package(KF6StatusNotifierItem REQUIRED)
find_package(KF6Kirigami REQUIRED)
find_package(KF6Screen REQUIRED)
find_package(KF6Service REQUIRED)
find_package(KF6KIO REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

add_library(contextdeck_core STATIC
    src/core/ControlCatalog.cpp
    src/core/Resolver.cpp
    src/core/Persistence.cpp
    src/core/persistence/JsonCommon.cpp
    src/core/persistence/KeysJson.cpp
    src/core/persistence/LightingJson.cpp
    src/core/persistence/MatchJson.cpp
    src/core/persistence/WorkspaceJson.cpp
    src/core/persistence/DocumentCodec.cpp
    src/core/ZoneMap.cpp
    src/workspace/WorkspacePlan.cpp
    src/workspace/WorkspaceCheckpoint.cpp
    src/workspace/PlacementResolver.cpp
)
target_include_directories(contextdeck_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(contextdeck_core PUBLIC Qt6::Core)

add_library(contextdeck_context STATIC
    src/context/ContextReceiver.cpp
    src/context/WorkspaceReceiver.cpp
    src/context/WorkspaceStateCodec.cpp
    src/context/InventoryPayload.cpp
    src/workspace/DesktopMutator.cpp
)
target_include_directories(contextdeck_context PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(contextdeck_context PUBLIC contextdeck_core Qt6::DBus)

add_library(contextdeck_launch STATIC
    src/workspace/ApplicationLauncher.cpp
)
target_include_directories(contextdeck_launch PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(contextdeck_launch PUBLIC contextdeck_core KF6::Service KF6::KIOGui)

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
    contextdeck_launch
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
        ui/WorkspacePage.qml
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
    # FakeGrabber, FakeSink, and FakeSource are selftest/unit-only; not the ARM path.
    src/broker/FakeGrabber.cpp
    src/broker/EvdevGrabber.cpp
    src/broker/EvdevSource.cpp
    src/broker/DeviceEnumerator.cpp
    src/broker/GrabbingSource.cpp
    src/broker/FakeSink.cpp
    src/broker/FakeSource.cpp
    src/broker/RealSink.cpp
    src/broker/ProductionRuntime.cpp
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
install(TARGETS contextdeck-broker
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT broker)
install(PROGRAMS packaging/systemd/contextdeck-sleep.sh
        DESTINATION lib/systemd/system-sleep
        RENAME contextdeck-broker
        COMPONENT broker)

enable_testing()

contextdeck_add_unit_test(test_profile_resolver
    LIBRARIES contextdeck_core Qt6::Test)

contextdeck_add_unit_test(test_profile_persistence
    LIBRARIES contextdeck_core Qt6::Test)

contextdeck_add_unit_test(test_workspace_plan
    LIBRARIES contextdeck_core Qt6::Test)

contextdeck_add_unit_test(test_openrgb_protocol
    LIBRARIES contextdeck_rgb Qt6::Test)

contextdeck_add_unit_test(test_broker_identity
    NO_AUTOMOC
    LIBRARIES contextdeck_broker_core)

contextdeck_add_unit_test(test_broker_ledger
    NO_AUTOMOC
    LIBRARIES contextdeck_broker_core)

contextdeck_add_unit_test(test_broker_forwarding
    NO_AUTOMOC
    LIBRARIES contextdeck_broker_core)

contextdeck_add_unit_test(test_broker_acquisition
    NO_AUTOMOC
    LIBRARIES contextdeck_broker_core)

contextdeck_add_unit_test(test_broker_watchdog
    NO_AUTOMOC
    LIBRARIES contextdeck_broker_core)

contextdeck_add_unit_test(test_broker_ipc
    NO_AUTOMOC
    LIBRARIES contextdeck_broker_core)

contextdeck_add_unit_test(test_broker_production
    NO_AUTOMOC
    LIBRARIES contextdeck_broker_core)

contextdeck_add_unit_test(test_udev_policy
    NO_AUTOMOC
    COMPILE_DEFINITIONS CONTEXTDECK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session REQUIRED)
contextdeck_add_unit_test(test_workspace_receiver
    DBUS_SESSION
    LIBRARIES contextdeck_context Qt6::Test Qt6::DBus)

contextdeck_add_unit_test(test_workspace_lighting
    DBUS_SESSION
    SOURCES
        tests/unit/test_workspace_lighting.cpp
        src/app/AppController.cpp
        src/app/BrokerIpcClient.cpp
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src/app"
    LIBRARIES
        contextdeck_core
        contextdeck_context
        contextdeck_launch
        contextdeck_rgb
        contextdeck_actions
        contextdeck_ipc
        Qt6::Test
        Qt6::DBus
        Qt6::Network
        Qt6::Gui
        KF6::ScreenDpms)

contextdeck_add_unit_test(test_placement_resolver
    LIBRARIES contextdeck_core Qt6::Test)

contextdeck_add_unit_test(test_workspace_mutator
    DBUS_SESSION
    LIBRARIES contextdeck_context Qt6::Test Qt6::DBus)

contextdeck_add_unit_test(test_application_launcher
    LIBRARIES contextdeck_launch contextdeck_core Qt6::Test)

add_test(NAME test_udev_verify
         COMMAND udevadm verify --resolve-names=never
                 "${CMAKE_SOURCE_DIR}/packaging/udev/61-contextdeck-input-guard.rules"
                 "${CMAKE_SOURCE_DIR}/packaging/udev/62-contextdeck-broker.rules"
                 "${CMAKE_SOURCE_DIR}/packaging/udev/99-contextdeck-broker-uinput.rules"
                 "${CMAKE_SOURCE_DIR}/packaging/udev/99-contextdeck-input-acl-guard.rules")

add_test(NAME test_trial_cutoff
         COMMAND bash "${CMAKE_SOURCE_DIR}/tests/unit/test_trial_cutoff.sh"
                 "${CMAKE_SOURCE_DIR}")

add_test(NAME test_sleep_hook
         COMMAND bash "${CMAKE_SOURCE_DIR}/tests/unit/test_sleep_hook.sh"
                 "${CMAKE_SOURCE_DIR}")

contextdeck_add_unit_test(test_broker_ipc_client
    SOURCES
        tests/unit/test_broker_ipc_client.cpp
        src/app/BrokerIpcClient.cpp
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src"
    LIBRARIES contextdeck_ipc Qt6::Test Qt6::Network
    COMMAND_ARGS -nocrashhandler)
