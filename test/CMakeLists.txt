include_directories(..)
set(CMAKE_CXX_FLAGS "-g -Wall -Wextra -pthread")

add_executable(test_empty test_empty.c)
add_executable(chaos chaos.cpp)

add_test(test_empty test_empty)

set_tests_properties(test_empty PROPERTIES TIMEOUT 1)
