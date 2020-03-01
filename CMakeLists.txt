cmake_minimum_required(VERSION 3.9)
project(sql_query_parser C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

add_library(lib SHARED tsqlp.c tsqlp.h scanner.c lexer.c lexer.h)
set_target_properties(lib PROPERTIES OUTPUT_NAME "tsqlp")

if(CMAKE_BUILD_TYPE STREQUAL Debug)
    add_executable(test test.c)
    target_link_libraries(test lib criterion)
endif()

add_executable(main main.c)
target_link_libraries(main lib)
set_target_properties(main PROPERTIES OUTPUT_NAME "tsqlp")

install(TARGETS main DESTINATION bin)
install(TARGETS lib DESTINATION lib)
install(FILES lib/tsqlp.h DESTINATION include)
