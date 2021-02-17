cmake_minimum_required(VERSION 3.3)

add_library(gc
  base.c
  copy.c
  generational.c
  markcompact.c
  marksweep.c
  reference_count.c
  rc_zct.c
)
