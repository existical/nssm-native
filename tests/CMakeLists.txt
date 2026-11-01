# Compile the production code with test-only wrappers for the entry point and
# SCM calls. No test switches or replacement APIs enter the shipped executable.
set(regression_sources ${nssm_sources})
list(REMOVE_ITEM regression_sources src/nssm.cpp src/env.cpp src/service.cpp)
list(TRANSFORM regression_sources PREPEND "${PROJECT_SOURCE_DIR}/")
add_executable(nssm_regressions ${regression_sources}
  main.cpp nssm_entry.cpp environment.cpp hooks.cpp startup.cpp)
add_dependencies(nssm_regressions nssm_messages nssm_version)
foreach(property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES)
  get_target_property(value nssm ${property})
  set_property(TARGET nssm_regressions PROPERTY ${property} "${value}")
endforeach()
target_compile_features(nssm_regressions PRIVATE cxx_std_11)
target_compile_options(nssm_regressions PRIVATE /EHsc)
foreach(group environment hooks startup)
  add_test(NAME nssm.${group} COMMAND nssm_regressions ${group})
  set_tests_properties(nssm.${group} PROPERTIES TIMEOUT 20)
endforeach()
