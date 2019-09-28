cmake_minimum_required(VERSION 3.3)
project(aquario)

add_executable(aquario aquario.c)
target_link_libraries(aquario gc)

add_subdirectory(gc)
