cmake_minimum_required(VERSION 3.24)
project(cs2_menu_overlay LANGUAGES C CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Use Windows, MSVC and an x64 generator.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(FetchContent)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG f5befd2d29e66809cd1110a152e375a7f1981f06)
FetchContent_Declare(minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537)
FetchContent_MakeAvailable(imgui minhook)
add_library(imgui_dx11 STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui_dx11 PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_dx11 PUBLIC d3d11 dxgi d3dcompiler)
add_library(cs2_overlay SHARED src/overlay.cpp)
target_link_libraries(cs2_overlay PRIVATE imgui_dx11 minhook user32)
target_compile_definitions(cs2_overlay PRIVATE CS2_WORKSPACE_ROOT=L"${CMAKE_SOURCE_DIR}")
add_executable(mapper src/loader.cpp)
target_include_directories(mapper PRIVATE include)
target_link_libraries(mapper PRIVATE d3d11 dxgi)
foreach(target imgui_dx11 cs2_overlay mapper)
  target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(${target} PRIVATE /W4 /Zc:threadSafeInit-)
endforeach()
target_link_options(cs2_overlay PRIVATE /INCREMENTAL:NO)
add_custom_command(TARGET cs2_overlay POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cs2_overlay>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy "${imgui_SOURCE_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:cs2_overlay>/licenses/imgui-LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy "${minhook_SOURCE_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:cs2_overlay>/licenses/minhook-LICENSE.txt")
