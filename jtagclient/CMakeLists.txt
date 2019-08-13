# CMake build script for jtagclient.

set(JTAGCLIENT_SOURCES main.cpp)

add_executable(jtagclient
	${JTAGCLIENT_SOURCES})
target_link_libraries(jtagclient jtaghal asan ${PROTOBUF_LIBRARIES})
install(TARGETS jtagclient RUNTIME DESTINATION /usr/bin)

