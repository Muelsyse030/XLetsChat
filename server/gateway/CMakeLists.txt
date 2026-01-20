add_executable(gateway_server main.cc)

find_package(unofficial-uwebsockets CONFIG REQUIRED)

target_link_libraries(gateway_server
    PRIVATE
    spdlog::spdlog
    unofficial::uwebsockets::uwebsockets
    ${OpenSSL_LIBRARIES}      
    im_proto_lib              
    ${WORKFLOW_LIBRARIES}     
)