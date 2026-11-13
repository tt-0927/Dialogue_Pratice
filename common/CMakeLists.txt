add_library(dialogue_common STATIC
    Protocol.cpp
)

target_include_directories(dialogue_common PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party
)

target_compile_features(dialogue_common PUBLIC cxx_std_17)
