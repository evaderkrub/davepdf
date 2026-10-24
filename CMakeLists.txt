cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)
project(davepdf VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: the staged folder must run on a machine without a VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build directory under C:/buildfiles (see README.md)")
endif()
if(NOT WIN32)
  message(FATAL_ERROR "davepdf currently builds for Windows x64 only; the PDFium prebuilt is fetched per platform in CMakeLists.txt.")
endif()
include(FetchContent)
include(CTest)
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
# Pinned commits: SDL 3.2.28, Dear ImGui 1.92.9b docking branch.
FetchContent_Declare(SDL3 URL https://github.com/libsdl-org/SDL/archive/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540.tar.gz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/9acdfbf46810c0c74ab281ce04122c4149ae8bd1.tar.gz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# PDFium is Chromium's PDF engine. Building it from source needs depot_tools
# and an hour of machine time, so the pdfium-binaries project's prebuilt is
# used. It ships only as a DLL on Windows, which is staged next to the exe.
FetchContent_Declare(pdfium
  URL https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/8111/pdfium-win-x64.tgz
  URL_HASH SHA256=3452d11031050ec65e57f180215b57b9e6d465e3fbedafa8a77ad90885c7d23e
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(SDL3 imgui pdfium)
set(DAVEPDF_PDFIUM_VERSION "158.0.8111.0")
add_library(pdfium SHARED IMPORTED GLOBAL)
set_target_properties(pdfium PROPERTIES
  IMPORTED_LOCATION "${pdfium_SOURCE_DIR}/bin/pdfium.dll"
  IMPORTED_IMPLIB "${pdfium_SOURCE_DIR}/lib/pdfium.dll.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${pdfium_SOURCE_DIR}/include")

function(davepdf_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# Application state and logic: no ImGui, no SDL, no PDFium.
add_library(davepdf_core STATIC src/app/viewer.cpp src/app/settings.cpp src/app/sample_pdf.cpp src/app/recent_tree.cpp)
target_include_directories(davepdf_core PUBLIC src)
davepdf_warnings(davepdf_core)

# The PDFium wrapper: the only translation unit that includes fpdf*.h.
add_library(davepdf_pdf STATIC src/platform/pdf_engine.cpp)
target_link_libraries(davepdf_pdf PUBLIC davepdf_core pdfium)
target_compile_definitions(davepdf_pdf PRIVATE DAVEPDF_PDFIUM_VERSION="${DAVEPDF_PDFIUM_VERSION}")
davepdf_warnings(davepdf_pdf)

set(IMGUI_SOURCES ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
function(make_imgui target)
  add_library(${target} STATIC ${IMGUI_SOURCES})
  target_include_directories(${target} PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
  target_link_libraries(${target} PUBLIC SDL3::SDL3-static)
endfunction()
make_imgui(davepdf_imgui)

set(DAVEPDF_DESKTOP_SOURCES src/platform/desktop.cpp src/platform/host_windows.cpp src/platform/render_worker.cpp
  src/ui/workspace.cpp src/ui/canvas.cpp src/ui/panels.cpp src/ui/dialogs.cpp src/ui/fwTheme.cpp)
set(DAVEPDF_HOST_LIBRARIES comdlg32 shell32)

add_executable(davepdf WIN32 src/main.cpp ${DAVEPDF_DESKTOP_SOURCES} src/platform/app.rc)
target_link_libraries(davepdf PRIVATE davepdf_pdf davepdf_imgui ${DAVEPDF_HOST_LIBRARIES})
target_include_directories(davepdf PRIVATE assets/icons)
target_compile_definitions(davepdf PRIVATE DAVEPDF_VERSION="${PROJECT_VERSION}" DAVEPDF_PDFIUM_VERSION="${DAVEPDF_PDFIUM_VERSION}")
davepdf_warnings(davepdf)
set_target_properties(davepdf PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/portable/davepdf")
# Everything the executable needs at runtime is staged beside it.
add_custom_command(TARGET davepdf POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:davepdf>/assets"
  COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:davepdf>/assets/icons/IconsMaterialDesign.h" "$<TARGET_FILE_DIR:davepdf>/assets/icons/davepdf.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:davepdf>/pdfium.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${pdfium_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:davepdf>/assets/licenses/PDFium.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/README.md" "$<TARGET_FILE_DIR:davepdf>/README.md"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:davepdf>/LICENSE")
install(TARGETS davepdf RUNTIME DESTINATION .)
install(FILES "$<TARGET_FILE:pdfium>" README.md LICENSE DESTINATION .)
install(DIRECTORY assets DESTINATION . PATTERN "*.h" EXCLUDE PATTERN "davepdf.png" EXCLUDE)
install(FILES "${pdfium_SOURCE_DIR}/LICENSE" DESTINATION assets/licenses RENAME PDFium.txt)

if(BUILD_TESTING)
  # Tests run from C:/buildfiles, not the executable's folder, so asset and
  # DLL lookup is proven independent of the working directory.
  set(DAVEPDF_TEST_CWD "C:/buildfiles")
  add_executable(davepdf_app_tests tests/app_tests.cpp)
  target_link_libraries(davepdf_app_tests PRIVATE davepdf_core)
  davepdf_warnings(davepdf_app_tests)
  add_test(NAME app_logic COMMAND davepdf_app_tests WORKING_DIRECTORY "${DAVEPDF_TEST_CWD}")

  add_executable(davepdf_pdf_tests tests/pdf_tests.cpp)
  target_link_libraries(davepdf_pdf_tests PRIVATE davepdf_pdf)
  davepdf_warnings(davepdf_pdf_tests)
  add_custom_command(TARGET davepdf_pdf_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:davepdf_pdf_tests>/pdfium.dll")
  add_test(NAME pdf_engine COMMAND davepdf_pdf_tests WORKING_DIRECTORY "${DAVEPDF_TEST_CWD}")

  FetchContent_Declare(imgui_test_engine URL https://github.com/ocornut/imgui_test_engine/archive/fdc1cb0930fa9bc2bef1d884bf2c10b570d29170.tar.gz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(imgui_test_engine)
  make_imgui(davepdf_imgui_testing)
  target_compile_definitions(davepdf_imgui_testing PUBLIC IMGUI_ENABLE_TEST_ENGINE IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1 IMGUI_TEST_ENGINE_ENABLE_CAPTURE=0)
  file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/*.cpp")
  add_library(davepdf_test_engine STATIC ${ENGINE_SOURCES})
  target_include_directories(davepdf_test_engine PUBLIC ${imgui_test_engine_SOURCE_DIR})
  target_link_libraries(davepdf_test_engine PUBLIC davepdf_imgui_testing)
  target_compile_definitions(davepdf_test_engine PRIVATE _CRT_SECURE_NO_WARNINGS)
  add_executable(davepdf_ui_tests src/main.cpp ${DAVEPDF_DESKTOP_SOURCES} tests/ui_tests.cpp)
  target_compile_definitions(davepdf_ui_tests PRIVATE DAVEPDF_TESTING SDL_MAIN_HANDLED DAVEPDF_VERSION="${PROJECT_VERSION}" DAVEPDF_PDFIUM_VERSION="${DAVEPDF_PDFIUM_VERSION}")
  target_include_directories(davepdf_ui_tests PRIVATE assets/icons)
  target_link_libraries(davepdf_ui_tests PRIVATE davepdf_pdf davepdf_test_engine ${DAVEPDF_HOST_LIBRARIES})
  davepdf_warnings(davepdf_ui_tests)
  add_custom_command(TARGET davepdf_ui_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:davepdf_ui_tests>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:davepdf_ui_tests>/pdfium.dll")
  add_test(NAME ui_end_to_end COMMAND davepdf_ui_tests --test)
  set_tests_properties(ui_end_to_end PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE WORKING_DIRECTORY "${DAVEPDF_TEST_CWD}")
endif()
