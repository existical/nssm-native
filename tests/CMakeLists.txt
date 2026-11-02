# Compile the production code with test-only wrappers for the entry point and
# SCM/registry calls and allocations. No replacement APIs enter the shipped executable.
set(regression_sources ${nssm_sources})
list(REMOVE_ITEM regression_sources src/nssm.cpp src/env.cpp src/service.cpp src/registry.cpp src/settings.cpp src/hook.cpp src/io.cpp src/process.cpp src/gui.cpp src/event.cpp)
list(TRANSFORM regression_sources PREPEND "${PROJECT_SOURCE_DIR}/")
add_executable(nssm_regressions ${regression_sources}
  main.cpp nssm_entry.cpp environment.cpp hooks.cpp startup.cpp registry.cpp quoting.cpp dump_quoting.cpp parsing.cpp hook_lifecycle.cpp launch.cpp logger.cpp rotation.cpp process_tree.cpp process_live.cpp install.cpp install_gui.cpp token.cpp error_messages.cpp)
add_dependencies(nssm_regressions nssm_messages nssm_version)
foreach(property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES)
  get_target_property(value nssm ${property})
  set_property(TARGET nssm_regressions PROPERTY ${property} "${value}")
endforeach()
target_compile_features(nssm_regressions PRIVATE cxx_std_11)
target_compile_options(nssm_regressions PRIVATE /EHsc)
foreach(group environment hooks startup registry multisz quoting numeric affinity hook_lifecycle launch hook_live logger rotation process_identity process_live admin setting_ownership waits install_ownership debug_token error_messages)
  add_test(NAME nssm.${group} COMMAND nssm_regressions ${group})
  set_tests_properties(nssm.${group} PROPERTIES TIMEOUT 20)
endforeach()
set_tests_properties(nssm.launch nssm.hook_live nssm.logger nssm.process_live PROPERTIES TIMEOUT 60)
