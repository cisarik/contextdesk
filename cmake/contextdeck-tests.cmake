# Shared registration for executable unit tests. add_test COMMAND spellings
# stay byte-stable with the pre-helper CMakeLists.txt forms.

function(contextdeck_add_unit_test name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG
        "NO_AUTOMOC;DBUS_SESSION"
        "ENVIRONMENT"
        "SOURCES;LIBRARIES;COMPILE_DEFINITIONS;INCLUDE_DIRECTORIES;COMMAND_ARGS"
    )

    if(NOT ARG_SOURCES)
        set(ARG_SOURCES "tests/unit/${name}.cpp")
    endif()

    add_executable(${name} ${ARG_SOURCES})

    if(ARG_INCLUDE_DIRECTORIES)
        target_include_directories(${name} PRIVATE ${ARG_INCLUDE_DIRECTORIES})
    endif()

    if(ARG_LIBRARIES)
        target_link_libraries(${name} PRIVATE ${ARG_LIBRARIES})
    endif()

    if(ARG_COMPILE_DEFINITIONS)
        target_compile_definitions(${name} PRIVATE ${ARG_COMPILE_DEFINITIONS})
    endif()

    if(ARG_NO_AUTOMOC)
        set_target_properties(${name} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    endif()

    if(ARG_DBUS_SESSION)
        add_test(NAME ${name}
                 COMMAND "${DBUS_RUN_SESSION_EXECUTABLE}" -- $<TARGET_FILE:${name}>)
        if(NOT ARG_ENVIRONMENT)
            set(ARG_ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_DISABLE_SESSION_MANAGER=1")
        endif()
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "${ARG_ENVIRONMENT}")
    else()
        add_test(NAME ${name} COMMAND ${name} ${ARG_COMMAND_ARGS})
        if(ARG_ENVIRONMENT)
            set_tests_properties(${name} PROPERTIES ENVIRONMENT "${ARG_ENVIRONMENT}")
        endif()
    endif()
endfunction()
