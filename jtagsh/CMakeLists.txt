# CMake build script for jtagsh.

add_executable(jtagsh
	main.cpp
	commands.cpp)
target_link_libraries(jtagsh jtaghal asan edit ${PROTOBUF_LIBRARIES})
install(TARGETS jtagsh RUNTIME DESTINATION /usr/bin)

