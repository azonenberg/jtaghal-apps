# CMake build script for svfdumper.

set(SVFDUMPER_SOURCES main.cpp)

add_executable(svfdumper ${SVFDUMPER_SOURCES})
target_link_libraries(svfdumper jtaghal)
install(TARGETS svfdumper RUNTIME DESTINATION /usr/bin)

