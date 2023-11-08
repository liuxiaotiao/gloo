# Build the Rust library
#set(RUST_LIB_DIR "${CMAKE_SOURCE_DIR}/target/release")
#set(RUST_LIB "${RUST_LIB_DIR}/libdmludp.so")

#add_custom_command(
#   OUTPUT ${RUST_LIB}
#   COMMAND cargo build --release --features ffi
#   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/dmludp
#   COMMENT "Building Rust library"
#)

#add_custom_target(
	#rust_library_target
    #DEPENDS ${RUST_LIB}
    #)

# Add the C++ library called libdmludp
#add_library(dmludp SHARED dmludp.h)  # Change SHARED to STATIC for a static library

# Link the Rust library
#target_link_libraries(dmludp ${RUST_LIB})

# Specify the include directories
#target_include_directories(dmludp PUBLIC ${CMAKE_SOURCE_DIR})

# Ensure the C++ library is built after the Rust library
#add_dependencies(dmludp rust_library_target)

#
set(RUST_PROJECT_DIR "${PROJECT_SOURCE_DIR}/dmludp")

# Complie Rust project
execute_process(
    COMMAND cargo build --release --features ffi
    WORKING_DIRECTORY ${RUST_PROJECT_DIR}
    RESULT_VARIABLE CARGO_BUILD_RESULT
)

# Check Cargo is suceeful or not
if(CARGO_BUILD_RESULT EQUAL "0")
    message(STATUS "Cargo build succeeded.")
    #set(RUST_LIBRARY "${RUST_PROJECT_DIR}/target/release/libdmludp.so")
else()
    message(FATAL_ERROR "Cargo build failed.")
endif()
execute_process(
   COMMAND cp ${RUST_PROJECT_DIR}/target/release/libdmludp.so ../gloo
   WORKING_DIRECTORY ${RUST_PROJECT_DIR}
   RESULT_VARIABLE COPY_RESULT
)
if(COPY_RESULT EQUAL "0")
	message(STATUS "succeeded.")
else()
	message(FATAL_ERROR "failed.")
endif()
