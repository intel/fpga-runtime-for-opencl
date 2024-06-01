if (NOT WIN32)
    add_library(glibc_wrap STATIC glibc_wrap.c)
    # Ensure position-independent code is generated
    set_target_properties(glibc_wrap PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(glibc_wrap PRIVATE ${CMAKE_SOURCE_DIR}/src)
    # Specify the wrapped functions on the interface link options.
    # This causes libraries linking against this one to use the wrapped 
    # functions rather than those in a potentially too new glibc for
    # our minimum supported OS. See glibc_wrap.c for details.
    target_link_libraries(glibc_wrap INTERFACE ${GLIBC_WRAP_FUNCTIONS})
    
    # Link the math library for debian-11-arm-dev
    target_link_libraries(glibc_wrap INTERFACE m)

    install(TARGETS glibc_wrap DESTINATION ${CMAKE_SOURCE_DIR}/glibc_wrap)
endif()
