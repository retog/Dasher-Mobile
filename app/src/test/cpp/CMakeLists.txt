# Host build of the native layer's tests: the app's C++ sources and DasherCore, with small
# stand-ins for the NDK headers.  Not part of the Gradle build; see README.md ("Native tests").
cmake_minimum_required(VERSION 3.22.1)
project("dasher_native_tests")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(APP_CPP ${CMAKE_CURRENT_SOURCE_DIR}/../../main/cpp)
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

set(DASHER_HAVE_OWN_FILEUTILS ON CACHE BOOL "" FORCE)
add_subdirectory(${REPO_ROOT}/third_party/DasherCore ${CMAKE_BINARY_DIR}/DasherCore EXCLUDE_FROM_ALL)

add_executable(dasher_native_tests
    native_tests.cpp
    asset_manager_stub.cpp
    ${APP_CPP}/AndroidDasherInterface.cpp
    ${APP_CPP}/AndroidFileUtils.cpp
    ${APP_CPP}/LanguageSet.cpp
    ${APP_CPP}/PersonalDictionary.cpp
    ${APP_CPP}/SymbolText.cpp
)
target_include_directories(dasher_native_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/android_stub
    ${APP_CPP}
)
target_compile_definitions(dasher_native_tests PRIVATE
    DASHER_TEST_ASSETS="${REPO_ROOT}/app/src/main/assets"
    DASHER_TEST_CORE_DATA="${REPO_ROOT}/third_party/DasherCore/Data"
)
target_link_libraries(dasher_native_tests DasherCore)

enable_testing()
add_test(NAME dasher_native_tests COMMAND dasher_native_tests)
