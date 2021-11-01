cmake_minimum_required ( VERSION 2.8.5 )
cmake_policy ( SET CMP0054 NEW )
project ( Bandwidth-Experiments )

include ( etc/build_defs.cmake )
include ( etc/build_type.cmake )
include ( etc/cflags.cmake )
include ( etc/clang_format.cmake )
include ( etc/clang_tidy.cmake )

include ( FindOpenSSL )

file ( GLOB SOURCE
  src/util/*
  src/net/*
  src/nat/*
)

include_directories ( src )

add_library( starter STATIC
  ${SOURCE}
)

set ( ALL_LIBS
  starter
  ${OPENSSL_LIBRARIES}
)

set ( STATIC_LINK_FLAGS dl z unwind lzma -static -Wl,-allow-multiple-definition
                        -Wl,--whole-archive -lpthread -Wl,--no-whole-archive
                        -s )


add_executable ( run-experiment src/frontend/run-experiment.cc )
target_link_libraries( run-experiment ${ALL_LIBS} )

add_executable ( lambdafunc src/frontend/lambdafunc.cc )
target_link_libraries( lambdafunc ${ALL_LIBS} ${STATIC_LINK_FLAGS} )



# Flags for building static binaries for AWS Lambda
# set ( STATIC_LINK_FLAGS dl z unwind lzma -static -Wl,-allow-multiple-definition
#                         -Wl,--whole-archive -lpthread -Wl,--no-whole-archive
#                         -s )
# if ( CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES ASan )
#   set( STATIC_LINK_FLAGS "" )
# endif()
