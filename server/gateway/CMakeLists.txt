add_executable(gateway_server main.cc)

target_link_libraries(gateway_server
    PRIVATE
    spdlog::spdlog
    ${WORKFLOW_LIBRARIES}   
    workflow
    ${OpenSSL_LIBRARIES}
)