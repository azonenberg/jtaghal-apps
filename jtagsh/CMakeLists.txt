# CMake build script for jtagsh.

set(JTAGCLIENT_SOURCES main.cpp)

add_executable(jtagsh
	${JTAGCLIENT_SOURCES})
target_link_libraries(jtagsh jtaghal asan editline)
install(TARGETS jtagsh RUNTIME DESTINATION /usr/bin)

