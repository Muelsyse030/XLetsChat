add_executable(logic_server main.cc)

target_include_directories(logic_server PRIVATE ${PostgreSQL_INCLUDE_DIRS})

target_link_libraries(logic_server
    PRIVATE
    spdlog::spdlog
    ${WORKFLOW_LIBRARIES}
    workflow
    ${PostgreSQL_LIBRARIES}
)