set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/pair.cc PROPERTIES COMPILE_FLAGS "-O2")

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    message(WARNING "Gloo TCP transport is supported only on Linux(uses epoll API)")
else()
    list(APPEND GLOO_TRANSPORT_SRCS
      "${CMAKE_CURRENT_SOURCE_DIR}/address.cc"
      "${CMAKE_CURRENT_SOURCE_DIR}/buffer.cc"
      "${CMAKE_CURRENT_SOURCE_DIR}/context.cc"
      "${CMAKE_CURRENT_SOURCE_DIR}/device.cc"
      "${CMAKE_CURRENT_SOURCE_DIR}/error.cc"
      "${CMAKE_CURRENT_SOURCE_DIR}/listener.cc"
      "${CMAKE_CURRENT_SOURCE_DIR}/loop.cc"
      "${CMAKE_CURRENT_SOURCE_DIR}/pair.cc"
      "${CMAKE_CURRENT_SOURCE_DIR}/socket.cc"
      "${CMAKE_CURRENT_SOURCE_DIR}/unbound_buffer.cc"
      )
    #foreach(src_file IN LISTS GLOO_TRANSPORT_SRCS)
     # set_source_files_properties(${src_file} PROPERTIES COMPILE_FLAGS "-fsanitize=address,undefined -g -fno-omit-frame-pointer")
   # endforeach()
    list(APPEND GLOO_TRANSPORT_HDRS
      "${CMAKE_CURRENT_SOURCE_DIR}/address.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/attr.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/buffer.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/context.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/device.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/error.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/helpers.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/listener.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/loop.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/pair.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/socket.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/unbound_buffer.h"
      #"${CMAKE_CURRENT_SOURCE_DIR}/dmludp_io.h"
      )
endif()

if(GLOO_HAVE_TRANSPORT_TCP_TLS)
    add_subdirectory(tls)
endif()

set(GLOO_TRANSPORT_SRCS ${GLOO_TRANSPORT_SRCS} PARENT_SCOPE)
set(GLOO_TRANSPORT_HDRS ${GLOO_TRANSPORT_HDRS} PARENT_SCOPE)
