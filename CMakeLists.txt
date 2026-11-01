cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build, for example cmake --preset windows-x64.")
endif()
project(nssm LANGUAGES CXX RC)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "NSSM currently requires Windows, MSVC and the Windows SDK.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated_dir}")

# Find mc.exe beside the selected SDK's resource compiler, including when
# configuring outside a Visual Studio developer shell.
get_filename_component(rc_dir "${CMAKE_RC_COMPILER}" DIRECTORY)
cmake_host_system_information(RESULT sdk_root QUERY WINDOWS_REGISTRY
  "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots" VALUE KitsRoot10 VIEW HOST)
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
  set(sdk_host_arch arm64)
elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
  set(sdk_host_arch x64)
else()
  set(sdk_host_arch x86)
endif()
find_program(NSSM_MESSAGE_COMPILER NAMES mc.exe mc
  HINTS "${rc_dir}"
    "${sdk_root}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/${sdk_host_arch}"
  REQUIRED)

set(message_outputs
  "${generated_dir}/messages.h"
  "${generated_dir}/messages.rc"
  "${generated_dir}/MSG00409.bin"
  "${generated_dir}/MSG0040C.bin"
  "${generated_dir}/MSG00410.bin")
add_custom_command(OUTPUT ${message_outputs}
  COMMAND "${NSSM_MESSAGE_COMPILER}" -u -U
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/messages.mc"
    -r "${generated_dir}" -h "${generated_dir}"
  DEPENDS resources/messages.mc
  COMMENT "Compiling translated Windows messages"
  VERBATIM)
add_custom_target(nssm_messages DEPENDS ${message_outputs})

# Recheck Git at build time; write the header only when its contents change.
add_custom_target(nssm_version
  COMMAND "${CMAKE_COMMAND}"
    "-DNSSM_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DNSSM_VERSION_HEADER=${generated_dir}/version.h"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.cmake"
  BYPRODUCTS "${generated_dir}/version.h"
  DEPENDS cmake/Version.cmake VERSION
  COMMENT "Updating NSSM build version"
  VERBATIM)

set(nssm_sources
  src/account.cpp
  src/console.cpp
  src/env.cpp
  src/event.cpp
  src/gui.cpp
  src/hook.cpp
  src/imports.cpp
  src/io.cpp
  src/nssm.cpp
  src/process.cpp
  src/registry.cpp
  src/service.cpp
  src/settings.cpp
  src/utf8.cpp
  resources/nssm.rc)
add_executable(nssm ${nssm_sources})
add_dependencies(nssm nssm_messages nssm_version)
target_include_directories(nssm PRIVATE src resources "${generated_dir}")
target_compile_features(nssm PRIVATE cxx_std_11)
target_compile_definitions(nssm PRIVATE WIN32 _CONSOLE UNICODE _UNICODE
  "$<$<CONFIG:Debug>:_DEBUG>"
  "$<$<NOT:$<CONFIG:Debug>>:NDEBUG>")
# Visual Studio does not reliably distinguish RC in language-dependent target
# definitions. Set the resource compiler's architecture on the source itself.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set_property(SOURCE resources/nssm.rc APPEND PROPERTY COMPILE_DEFINITIONS _WIN64)
endif()
target_compile_options(nssm PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:/W3;/GF;/Gy;/Zi;/we4302;/we4311;/we4312>")
target_link_options(nssm PRIVATE /DEBUG /DYNAMICBASE /NXCOMPAT
  "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF;/OPT:ICF>")
target_link_libraries(nssm PRIVATE
  kernel32 user32 gdi32 comdlg32 advapi32 shell32 ole32 oleaut32 uuid
  psapi shlwapi comctl32)
set_target_properties(nssm PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/$<CONFIG>"
  PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/$<CONFIG>"
  COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/obj/$<CONFIG>")
set_source_files_properties(resources/nssm.rc PROPERTIES OBJECT_DEPENDS
  "${generated_dir}/messages.rc;${generated_dir}/version.h;${CMAKE_CURRENT_SOURCE_DIR}/resources/nssm.ico;${CMAKE_CURRENT_SOURCE_DIR}/resources/resource.h")

include(CTest)
if(BUILD_TESTING)
  find_program(NSSM_POWERSHELL NAMES pwsh powershell REQUIRED)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(test_arch x64)
  else()
    set(test_arch x86)
  endif()
  add_test(NAME nssm.smoke
    COMMAND "${NSSM_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.ps1"
      -Executable "$<TARGET_FILE:nssm>" -Architecture "${test_arch}"
      -Configuration "$<CONFIG>" -VersionHeader "${generated_dir}/version.h")
  set_tests_properties(nssm.smoke PROPERTIES TIMEOUT 20)
  add_subdirectory(tests)
endif()
