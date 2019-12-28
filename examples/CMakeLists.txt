cmake_minimum_required(VERSION 3.5)

# The examples aren't really tested, just compiled
add_executable(cat cat.c)
target_link_libraries(cat lfp::lfp)

add_executable(tif-cat tif-cat.c)
target_link_libraries(tif-cat lfp::lfp)
