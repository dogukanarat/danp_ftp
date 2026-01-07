if(CONFIG_DANP)
    zephyr_library()

    zephyr_library_sources(
        ../src/danp_ftp.c
        ../src/danp_ftp_service.c
    )

    zephyr_library_sources_ifdef(CONFIG_SHELL
         ../src/arch/zephyr/danp_ftp_service_shell.c
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
