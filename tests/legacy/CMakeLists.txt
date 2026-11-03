cmake_minimum_required(VERSION 3.24)
project(nssm_legacy LANGUAGES CXX RC)
file(TO_CMAKE_PATH "${NSSM_LEGACY_SOURCE_DIR}" NSSM_LEGACY_SOURCE_DIR)
if(NOT WIN32 OR NOT MSVC OR NOT EXISTS "${NSSM_LEGACY_SOURCE_DIR}/nssm.cpp")
  message(FATAL_ERROR "Supply the extracted legacy source on Windows with MSVC.")
endif()
if(NOT NSSM_LEGACY_DATE MATCHES "^[0-9]+-[0-9]+-[0-9]+$")
  message(FATAL_ERROR "Supply the pinned legacy commit date.")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated}")
file(WRITE "${generated}/version.h"
  "#define NSSM_VERSION _T(\"2.24-103-gdee49fc\")\n#define NSSM_VERSIONINFO 2,24,103,0\n#define NSSM_DATE _T(\"${NSSM_LEGACY_DATE}\")\n#define NSSM_FILEFLAGS VS_FF_PRERELEASE\n#define NSSM_COPYRIGHT _T(\"Public Domain; Author Iain Patterson\")\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/toolchain.json"
  "{\"compiler\":\"MSVC ${CMAKE_CXX_COMPILER_VERSION}\",\"windows_sdk\":\"${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}\",\"cmake\":\"${CMAKE_VERSION}\"}\n")
get_filename_component(rc_dir "${CMAKE_RC_COMPILER}" DIRECTORY)
cmake_host_system_information(RESULT sdk_root QUERY WINDOWS_REGISTRY
  "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots" VALUE KitsRoot10 VIEW HOST)
find_program(message_compiler NAMES mc.exe mc HINTS "${rc_dir}"
  "${sdk_root}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64" REQUIRED)
set(message_outputs "${generated}/messages.h" "${generated}/messages.rc"
  "${generated}/MSG00409.bin" "${generated}/MSG0040C.bin" "${generated}/MSG00410.bin")
add_custom_command(OUTPUT ${message_outputs}
  COMMAND "${message_compiler}" -u -U "${NSSM_LEGACY_SOURCE_DIR}/messages.mc" -r "${generated}" -h "${generated}"
  DEPENDS "${NSSM_LEGACY_SOURCE_DIR}/messages.mc" VERBATIM)
add_custom_target(legacy_messages DEPENDS ${message_outputs})
set(sources account.cpp console.cpp env.cpp event.cpp gui.cpp hook.cpp imports.cpp
  io.cpp nssm.cpp process.cpp registry.cpp service.cpp settings.cpp utf8.cpp nssm.rc)
list(TRANSFORM sources PREPEND "${NSSM_LEGACY_SOURCE_DIR}/")
add_executable(nssm_legacy ${sources})
add_dependencies(nssm_legacy legacy_messages)
# Do not add the flat source root to /I: its io.h shadows the CRT's <io.h>.
# Quoted private headers resolve beside the unchanged original source files.
target_include_directories(nssm_legacy PRIVATE "${generated}")
target_compile_definitions(nssm_legacy PRIVATE WIN32 _CONSOLE UNICODE _UNICODE NDEBUG)
target_compile_options(nssm_legacy PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W3;/GF;/Gy;/Zi>")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set_property(SOURCE "${NSSM_LEGACY_SOURCE_DIR}/nssm.rc" APPEND PROPERTY COMPILE_DEFINITIONS _WIN64)
endif()
set_source_files_properties("${NSSM_LEGACY_SOURCE_DIR}/nssm.rc" PROPERTIES OBJECT_DEPENDS
  "${generated}/messages.rc;${generated}/version.h")
target_link_libraries(nssm_legacy PRIVATE kernel32 user32 gdi32 comdlg32 advapi32
  shell32 ole32 oleaut32 uuid psapi shlwapi comctl32)
target_link_options(nssm_legacy PRIVATE /DEBUG /DYNAMICBASE /NXCOMPAT /OPT:REF /OPT:ICF)
set_target_properties(nssm_legacy PROPERTIES OUTPUT_NAME nssm
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/$<CONFIG>"
  PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/$<CONFIG>")
