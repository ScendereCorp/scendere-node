add_executable(scendere_rpc entry.cpp)

target_link_libraries(
  scendere_rpc
  rpc
  secure
  Boost::filesystem
  Boost::log_setup
  Boost::log
  Boost::program_options
  Boost::system
  Boost::thread
  Boost::boost)

target_compile_definitions(
  scendere_rpc
  PUBLIC -DACTIVE_NETWORK=${ACTIVE_NETWORK}
  PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
          -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

if((SCENDERE_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
  if(WIN32)
    install(TARGETS scendere_rpc RUNTIME DESTINATION .)
  else()
    install(TARGETS scendere_rpc RUNTIME DESTINATION ./bin)
  endif()
endif()
