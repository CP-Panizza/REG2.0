cmake_minimum_required(VERSION 3.12)
project(RegistCenter2_0)

set(CMAKE_CXX_STANDARD 11)
IF (CMAKE_SYSTEM_NAME MATCHES "Windows")
    link_libraries("D:/MinGW64/x86_64-w64-mingw32/lib/libws2_32.a")
ELSE ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
ENDIF ()
add_executable(RegistCenter2_0
        main.cpp
        socket_header.h
        Service.cpp
        Service.h
        util.cpp
        util.h
        RWLock.hpp)