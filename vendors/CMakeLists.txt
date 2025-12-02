if(NOT TARGET zlib)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory("zlib")
endif()