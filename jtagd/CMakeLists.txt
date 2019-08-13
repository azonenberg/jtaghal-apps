# CMake build script for jtagd.

set(PROTOBUF_DIR ${CMAKE_BINARY_DIR}/protobufs)
include_directories(${PROTOBUF_DIR})

set(JTAGD_SOURCES main.cpp ConnectionThread.cpp XvcdConnectionThread.cpp)

add_executable(jtagd
	${JTAGD_SOURCES})
target_link_libraries(jtagd jtaghal ${PROTOBUF_LIBRARIES})
target_include_directories(jtagd
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS jtagd RUNTIME DESTINATION /usr/bin)

