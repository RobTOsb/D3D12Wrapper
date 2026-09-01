add_executable(smoke_test smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE D3D12 d3d12 dxgi)

d3d12_deploy_runtime_dlls(smoke_test)
add_custom_command(TARGET smoke_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:smoke_test>/shaders"
    COMMENT "Copying smoke-test shaders")

add_test(NAME smoke COMMAND smoke_test WORKING_DIRECTORY $<TARGET_FILE_DIR:smoke_test>)
