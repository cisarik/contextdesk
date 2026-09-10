cmake_minimum_required(VERSION 3.25)
project(contextdeck VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/contextdeck-warnings.cmake")

find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_library(contextdeck_core STATIC
    src/core/ControlCatalog.cpp
    src/core/Resolver.cpp
    src/core/Persistence.cpp
)
target_include_directories(contextdeck_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(contextdeck_core PUBLIC Qt6::Core)

enable_testing()

add_executable(test_profile_resolver tests/unit/test_profile_resolver.cpp)
target_link_libraries(test_profile_resolver PRIVATE contextdeck_core Qt6::Test)
add_test(NAME test_profile_resolver COMMAND test_profile_resolver)

add_executable(test_profile_persistence tests/unit/test_profile_persistence.cpp)
target_link_libraries(test_profile_persistence PRIVATE contextdeck_core Qt6::Test)
add_test(NAME test_profile_persistence COMMAND test_profile_persistence)
