cmake_minimum_required(VERSION 3.20)
project(Cairn VERSION 0.2.0 LANGUAGES CXX RC)

# Two deliverables, built from one tree:
#   cairn           - the client DLL (loaded into Minecraft)
#   cairn_injector  - standalone injector EXE
# Windows / MSVC / x64 only.

if(NOT WIN32)
    message(FATAL_ERROR "Cairn builds on Windows only (MSVC, x64).")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Minecraft's JVM is 64-bit: configure an x64 build (use -A x64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(CAIRN_STATIC_CRT "Link the MSVC runtime statically (no VC++ redistributable needed)" ON)
if(CAIRN_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Let older third-party CMake files configure under newer CMake versions.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

if(MSVC)
    add_compile_options(/W3 /utf-8 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

# ── Third-party (fetched at configure time, pinned) ──────────
include(FetchContent)

FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        d94c64d32ea37bc4f5ee47d580709f70c6fb6080
)
FetchContent_MakeAvailable(minhook)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

# ── Shared UI / settings / sound (used by both targets) ──────
add_library(cairn_common STATIC
    src/shared/Brand.cpp
    src/shared/UiTheme.cpp
    src/shared/Widgets.cpp
    src/core/Settings.cpp
    src/core/Sound.cpp
)
target_include_directories(cairn_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(cairn_common PUBLIC CAIRN_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(cairn_common PUBLIC imgui winmm shell32 ole32)

# ── Version/icon resources ───────────────────────────────────
set(CAIRN_ICON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons")
set(CAIRN_VERSION_COMMA "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
configure_file(src/resources/cairn.rc.in   ${CMAKE_CURRENT_BINARY_DIR}/cairn.rc   @ONLY)
configure_file(src/resources/cairn_injector.rc.in ${CMAKE_CURRENT_BINARY_DIR}/cairn_injector.rc @ONLY)

# ── cairn.dll ─────────────────────────────────────────────
# JNI headers only. The DLL finds jvm.dll at runtime (GetProcAddress), so it does not link jvm.lib.
# CMake's FindJNI is deliberately not used: with a JAVA_HOME containing spaces/backslashes
# (e.g. C:\Program Files\...) it fails to parse its own search list on recent CMake versions.
if(NOT JAVA_HOME AND DEFINED ENV{JAVA_HOME})
    set(JAVA_HOME "$ENV{JAVA_HOME}")
endif()
if(NOT JAVA_HOME)
    message(FATAL_ERROR "JAVA_HOME is not set. Pass -DJAVA_HOME=<64-bit JDK folder> (scripts\\build.ps1 -JavaHome does this).")
endif()
file(TO_CMAKE_PATH "${JAVA_HOME}" CAIRN_JDK)          # forward slashes
if(NOT EXISTS "${CAIRN_JDK}/include/jni.h")
    message(FATAL_ERROR "No JNI headers at '${CAIRN_JDK}/include/jni.h'. JAVA_HOME must point at a JDK, not a JRE.")
endif()
if(NOT EXISTS "${CAIRN_JDK}/include/win32/jni_md.h")
    message(FATAL_ERROR "Missing '${CAIRN_JDK}/include/win32/jni_md.h'. A Windows JDK is required.")
endif()
set(CAIRN_JNI_INCLUDE_DIRS "${CAIRN_JDK}/include" "${CAIRN_JDK}/include/win32")
message(STATUS "JDK headers: ${CAIRN_JDK}/include")
find_package(OpenGL REQUIRED)

add_library(cairn SHARED
    src/DllMain.cpp
    src/core/Client.cpp
    src/core/JVM.cpp
    src/core/Hooks.cpp
    src/core/Profile.cpp
    src/modules/ModuleManager.cpp
    src/modules/Speed.cpp
    src/modules/Fly.cpp
    src/modules/NoFall.cpp
    src/modules/Sprint.cpp
    src/modules/KillAura.cpp
    src/modules/Reach.cpp
    src/modules/FullBright.cpp
    src/packet/PacketManager.cpp
    src/packet/TickSync.cpp
    src/menu/Menu.cpp
    src/menu/Theme.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/cairn.rc
)
target_include_directories(cairn PRIVATE ${CAIRN_JNI_INCLUDE_DIRS})
target_link_libraries(cairn PRIVATE
    cairn_common minhook imgui
    OpenGL::GL gdi32 user32 dwmapi windowscodecs ole32 uuid
)
set_target_properties(cairn PROPERTIES OUTPUT_NAME cairn SUFFIX .dll)

# ── cairn_injector.exe ───────────────────────────────────────────
add_executable(cairn_injector WIN32
    src/injector/main.cpp
    src/injector/InjectorCore.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/cairn_injector.rc
)
target_link_libraries(cairn_injector PRIVATE cairn_common imgui d3d11 dxgi winhttp bcrypt comdlg32 user32 gdi32 dwmapi)
set_target_properties(cairn_injector PROPERTIES OUTPUT_NAME CairnInjector)

# ── Portable tests (no Windows/JNI/ImGui dependencies) ───────
option(CAIRN_BUILD_TESTS "Build the portable unit tests" OFF)
if(CAIRN_BUILD_TESTS)
    add_executable(portable_tests tests/portable_tests.cpp)
    target_include_directories(portable_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

# ── Install layout: <prefix>\{bin,assets,config} + docs ──────
# Default prefix is ./dist inside the build tree; scripts\build.ps1 picks the output folder.
# Everything is in the "Cairn" component. Third-party projects fetched above (MinHook) ship their own install
# rules, which would drop include\, lib\ and share\ into the package; install with `--component Cairn` to skip them.
install(TARGETS cairn cairn_injector RUNTIME DESTINATION bin COMPONENT Cairn)
install(DIRECTORY assets/ DESTINATION assets COMPONENT Cairn PATTERN "*.py" EXCLUDE)
install(FILES README.md THIRD_PARTY_NOTICES.md DESTINATION . COMPONENT Cairn)
install(DIRECTORY docs/ DESTINATION docs COMPONENT Cairn)
# Never overwrite the user's edited config.
install(CODE "
    set(_dst \"\${CMAKE_INSTALL_PREFIX}/config/injector.ini\")
    if(NOT EXISTS \"\${_dst}\")
        file(INSTALL \"${CMAKE_CURRENT_SOURCE_DIR}/config/injector.ini\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/config\")
    else()
        message(STATUS \"Keeping existing \${_dst}\")
    endif()
" COMPONENT Cairn)
