cmake_minimum_required(VERSION 3.30)
project(Http_Client C)

set(CMAKE_C_STANDARD 11)

add_executable(Http_Client client.c
        GPT.cpp)
