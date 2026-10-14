# Asylum DLSS helper - a separate 64-bit build (the game and batman_asylum.dll are 32-bit).
#   cmake -S helper64 -B build64 -A x64 && cmake --build build64 --config RelWithDebInfo
cmake_minimum_required(VERSION 3.20)
project(aa_dlss64 CXX)
set(CMAKE_CXX_STANDARD 17)
# The installed NGX static library imports the DLL CRT. Match it explicitly.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "aa_dlss64 must be built 64-bit (-A x64)")
endif()
# NVIDIA NGX SDK 310.9.1 (from Luma), shared with AKN.
set(NGX "${CMAKE_CURRENT_SOURCE_DIR}/../../akvr_native/third_party/ngx")
add_executable(aa_dlss64 WIN32 aa_dlss64.cpp dlss_shared.h)
target_include_directories(aa_dlss64 PRIVATE "${NGX}/include")
target_include_directories(aa_dlss64 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../references/luma-framework-2026-10/Source/External/NVAPI")
target_link_libraries(aa_dlss64 PRIVATE d3d11 dxgi psapi version "${NGX}/lib/nvsdk_ngx_d.lib")
target_compile_definitions(aa_dlss64 PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

add_custom_command(TARGET aa_dlss64 POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:aa_dlss64>/CNN"
 COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/dlss_cnn/nvngx_dlss.dll" "$<TARGET_FILE_DIR:aa_dlss64>/CNN/nvngx_dlss.dll")
