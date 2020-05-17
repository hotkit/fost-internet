cmake_minimum_required(VERSION 3.10)
project(fost-internet-circleci)

## Opt in to tests
add_custom_target(check)
add_custom_target(stress)

add_subdirectory(boost)
add_subdirectory(crypto)
add_subdirectory(cord)
add_subdirectory(threading)
add_subdirectory(fost-base)
add_subdirectory(.. fost-internet)
