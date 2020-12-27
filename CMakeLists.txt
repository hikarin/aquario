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
endif()

add_subdirectory(gc)

#Configuration for Test
add_executable(aq_test aquario.c)
target_compile_options(aq_test PUBLIC -D_TEST)
target_link_libraries(aq_test gc)

enable_testing()

macro(do_test gc gcname)
  file(STRINGS test/test.txt texts)
  foreach(text IN ITEMS ${texts})
    list(LENGTH text len)
    if(len LESS 3)
      continue()
    endif()
    list(GET text 0 name)
    list(GET text 1 value)
    list(GET text 2 result)
    add_test(
      NAME ${gcname}-${name}
	  COMMAND aq_test -GC ${gc} ${value} ${result}
    )
  endforeach()
endmacro()

do_test(copy Copying)
do_test(gen Generational)
do_test(mc MarkCompact)
do_test(ms MarkSweep)
do_test(ref ReferenceCounting)
