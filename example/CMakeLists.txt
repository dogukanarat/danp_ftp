# ==============================================================================
# Example Applications
# ==============================================================================
# This CMakeLists.txt builds example applications that demonstrate how to use
# the danp_ftp library.
#
# Users can:
# - Add new example applications by creating new executables
# - Modify existing examples to demonstrate different features
# - Choose whether to install examples (disabled by default)

# ==============================================================================
# Example: POSIX CLI Application
# ==============================================================================
# Build only on POSIX-like systems (excluding Zephyr).
if(UNIX AND NOT CMAKE_SYSTEM_NAME STREQUAL "Zephyr")
    # Create the CLI example executable
    add_executable(DanpFtpExample
        example.c
    )

    # Link with the danp_ftp library
    # Using the alias target ensures proper namespacing
    target_link_libraries(DanpFtpExample
        PRIVATE
            DanpFtp::DanpFtp
    )

    # Set C99 standard for the example
    target_compile_features(DanpFtpExample
        PRIVATE
            c_std_99
    )

    # Add compiler warnings for the example (same as library)
    target_compile_options(DanpFtpExample
        PRIVATE
            # GCC and Clang warnings
            $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:
                -Wall
                -Wextra
                -Wpedantic
            >
            # MSVC warnings
            $<$<C_COMPILER_ID:MSVC>:
                /W4
            >
    )
endif()

# ==============================================================================
# Example: Embedded-Safe Application
# ==============================================================================
add_executable(DanpFtpEmbeddedExample
    example_embedded.c
)

target_link_libraries(DanpFtpEmbeddedExample
    PRIVATE
        DanpFtp::DanpFtp
)

target_compile_features(DanpFtpEmbeddedExample
    PRIVATE
        c_std_99
)

target_compile_options(DanpFtpEmbeddedExample
    PRIVATE
        $<$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>>:
            -Wall
            -Wextra
            -Wpedantic
        >
        $<$<C_COMPILER_ID:MSVC>:
            /W4
        >
)

# ==============================================================================
# Adding New Examples (Template)
# ==============================================================================
# To add a new example application, copy and modify this template:
#
# add_executable(example_danp_ftp_myexample
#     myexample.c
# )
#
# target_link_libraries(example_danp_ftp_myexample
#     PRIVATE
#         DanpFtp::DanpFtp
# )
#
# target_compile_features(example_danp_ftp_myexample
#     PRIVATE
#         c_std_99
# )

# ==============================================================================
# Installation (Optional)
# ==============================================================================
# By default, examples are not installed to keep the installation clean.
# Uncomment the section below if you want to install example executables.

# install(
#     TARGETS DanpFtpExample DanpFtpEmbeddedExample
#     RUNTIME
#         DESTINATION ${CMAKE_INSTALL_BINDIR}
#         COMPONENT Examples
# )

# To install examples along with the library:
# cmake --install . --component Examples
