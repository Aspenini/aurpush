local VERSION = "0.1.0"

set_project("aurpush")
set_version(VERSION)
set_languages("c++20")

add_rules("mode.debug", "mode.release")
add_includedirs("include", {public = true})
add_defines('AURPUSH_VERSION="' .. VERSION .. '"')
set_warnings("allextra")
add_cxflags("-Wpedantic")

target("aurpush_lib")
    set_kind("static")
    add_files("src/*.cpp|main.cpp")

target("aurpush")
    set_kind("binary")
    add_deps("aurpush_lib")
    add_files("src/main.cpp")

target("aurpush_tests")
    set_kind("binary")
    set_default(false)
    add_deps("aurpush_lib")
    add_files("tests/*.cpp")
    add_includedirs("tests")
    add_defines('AURPUSH_FAKE_DIR="' .. path.join(os.projectdir(), "tests", "fake") .. '"')
    add_tests("default")
