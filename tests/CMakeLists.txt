

FetchContent_Declare(
  ut
  GIT_REPOSITORY https://github.com/boost-ext/ut
  GIT_TAG        v1.1.9
  FIND_PACKAGE_ARGS
)

# This will try calling find_package() first for both dependencies
FetchContent_MakeAvailable(ut)
add_executable(proto_test proto_tests.cpp)
target_include_directories(proto_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(proto_test PRIVATE Boost::ut)

add_test(NAME proto_test 
         COMMAND proto_test 
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})