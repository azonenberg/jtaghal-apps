# CMake build script for jtagclient.

set(JTAGCLIENT_SOURCES main.cpp)

add_executable(jtagclient
	${JTAGCLIENT_SOURCES})
target_link_libraries(jtagclient jtaghal asan)
install(TARGETS jtagclient RUNTIME DESTINATION /usr/bin)

