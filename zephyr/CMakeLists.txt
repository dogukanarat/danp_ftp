if(CONFIG_DANP)
    zephyr_library()

    zephyr_library_sources(
        ../src/danp_ftp.c
    )
    zephyr_include_directories(
        ../include
        ../src
    )

    zephyr_library_link_libraries(
        osal
        danp
    )
endif()
