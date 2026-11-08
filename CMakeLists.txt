cmake_minimum_required(VERSION 3.21)
project(wifi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The exe builds straight into the plugin folder, because plugin.json names
# it as runtime.program. Keep the ra9fael.port / ra9fael.shell convention.
add_executable(wifi main.cpp wifi.rc)
target_compile_definitions(wifi PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# wlanapi: the whole plugin (WlanOpenHandle and friends). ole32: the GUIDs
# wlanapi.h declares but does not define. user32: clipboard for
# copyPassword/copyXml.
target_link_libraries(wifi PRIVATE wlanapi ole32 user32 gdi32 gdiplus)

if(MSVC)
    target_compile_options(wifi PRIVATE /utf-8 /W4 /permissive-)
    # Static CRT: no runtime DLL to load from the plugin folder.
    set_target_properties(wifi PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(wifi PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -O2)
    target_link_options(wifi PRIVATE -static -static-libgcc -static-libstdc++)
endif()

set_target_properties(wifi PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(wifi PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}")
endforeach()

# Pure-logic tests: logic.h needs only the SDK header and the C++17 library,
# so they build and run without a wireless adapter.
add_executable(wifi_tests tests/test_wifi.cpp)
target_compile_definitions(wifi_tests PRIVATE NOMINMAX)
target_include_directories(wifi_tests PRIVATE "${CMAKE_SOURCE_DIR}")
if(MSVC)
    target_compile_options(wifi_tests PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(wifi_tests PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -O2)
endif()
set_target_properties(wifi_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(wifi_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/tests")
endforeach()

enable_testing()
add_test(NAME wifi_logic COMMAND wifi_tests)

add_executable(wifi_qr_native_tests tests/test_qr_native.cpp)
target_compile_definitions(wifi_qr_native_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(wifi_qr_native_tests PRIVATE wlanapi ole32 user32 gdi32 gdiplus)
if(MSVC)
    target_compile_options(wifi_qr_native_tests PRIVATE /utf-8 /W4 /permissive-)
endif()
add_test(NAME wifi_qr_native COMMAND wifi_qr_native_tests)
