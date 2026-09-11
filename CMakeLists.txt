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
    src/app/ChordRecorder.cpp
)
target_link_libraries(contextdeck PRIVATE
    contextdeck_context
    contextdeck_rgb
    contextdeck_actions
    Qt6::Widgets
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
    KF6::StatusNotifierItem
    KF6::Kirigami
)

qt_add_qml_module(contextdeck
    URI io.github.cisarik.ContextDeck
    VERSION 1.0
    QML_FILES
        ui/Main.qml
        ui/OverviewPage.qml
        ui/ProfilesPage.qml
        ui/ControlsPage.qml
        ui/DiagnosticsPage.qml
        ui/LightingPresetEditor.qml
    NO_RESOURCE_TARGET_PATH
)

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
