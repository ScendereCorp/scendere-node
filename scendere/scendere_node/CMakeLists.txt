add_executable(scendere_node daemon.cpp daemon.hpp entry.cpp)

target_link_libraries(
  scendere_node
  node
  rpc
  secure
  argon2
  Boost::boost
  ${PLATFORM_LIBS})

if(SCENDERE_STACKTRACE_BACKTRACE)
  target_link_libraries(scendere_node backtrace)
endif()

target_compile_definitions(
  scendere_node PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
                    -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

set_target_properties(
  scendere_node PROPERTIES COMPILE_FLAGS
                       "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")

add_custom_command(
  TARGET scendere_node
  POST_BUILD
  COMMAND scendere_node --generate_config node >
          ${PROJECT_BINARY_DIR}/config-node.toml.sample
  COMMAND scendere_node --generate_config rpc >
          ${PROJECT_BINARY_DIR}/config-rpc.toml.sample)

if((SCENDERE_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
  if(WIN32)
    install(TARGETS scendere_node RUNTIME DESTINATION .)
  else()
    install(TARGETS scendere_node RUNTIME DESTINATION ./bin)
  endif()
endif()
