cmake_minimum_required(VERSION 3.25)
project(contextdeck VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/contextdeck-warnings.cmake")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus Network Test)

add_library(contextdeck_core STATIC
    src/core/ControlCatalog.cpp
    src/core/Resolver.cpp
    src/core/Persistence.cpp
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

add_executable(contextdeck
    src/app/main.cpp
    src/app/SessionApplication.cpp
)
target_link_libraries(contextdeck PRIVATE contextdeck_context contextdeck_rgb Qt6::Widgets Qt6::Gui)

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
