cmake_minimum_required(VERSION 3.10)

project(oprf C CXX)

# Targets oprf_server_(client|server)
foreach(_target
  oprf_server
  )
  add_executable(${_target}
    "${_target}.cpp")
  target_link_libraries(${_target}
    droidcrypto)
  set_target_properties(${_target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/..")
endforeach()
