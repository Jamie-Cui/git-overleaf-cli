if(NOT DEFINED COVERAGE_BINARY_DIR)
  message(FATAL_ERROR "COVERAGE_BINARY_DIR is required")
endif()

file(GLOB_RECURSE coverage_data "${COVERAGE_BINARY_DIR}/*.gcda")
if(coverage_data)
  file(REMOVE ${coverage_data})
endif()
