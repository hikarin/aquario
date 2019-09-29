cmake_minimum_required(VERSION 3.3)
project(aquario C)

add_executable(aquario aquario.c)
target_link_libraries(aquario gc)

target_compile_options(aquario PUBLIC
  $<$<CONFIG:Release>:-O3>             # Release
  $<$<CONFIG:Debug>:-O0 -g>            # Debug
  $<$<CONFIG:RelWithDebgInfo>:-O3 -g>  # RelWithDebInfo
)

target_compile_definitions(aquario PUBLIC
  $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

if(MSVC)
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT aquario)
else()
  add_custom_target(debug
    COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Debug ${CMAKE_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
  )

  add_custom_target(release
    COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Release ${CMAKE_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all
  )
endif()

add_subdirectory(gc)
add_executable(testie test.c)

enable_testing()

add_test(
  NAME Int-Test1
  COMMAND testie 1 1
)

add_test(
  NAME Int-Test2
  COMMAND testie 1 2
)

add_test(
  NAME Int-Test3
  COMMAND testie 1 1
)
