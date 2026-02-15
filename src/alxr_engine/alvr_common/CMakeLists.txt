include(FetchContent)
FetchContent_Declare(nanors
    GIT_REPOSITORY https://github.com/korejan/nanors.git
    GIT_TAG korejan/main
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(nanors)

file(GLOB_RECURSE ALVR_COMMON_HEADERS *.h)
file(GLOB_RECURSE ALVR_COMMON_SOURCE *.c *.cpp)

add_library(alvr_common STATIC
    ${ALVR_COMMON_HEADERS}
    ${ALVR_COMMON_SOURCE}
)
target_include_directories(alvr_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
if (MSVC)
    target_compile_definitions(alvr_common PRIVATE _CRT_SECURE_NO_WARNINGS PRIVATE _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
    target_compile_options(alvr_common PRIVATE /Zc:wchar_t /Zc:forScope /W3 /Zi)
endif()
target_link_libraries(alvr_common PUBLIC nanors::nanors)
