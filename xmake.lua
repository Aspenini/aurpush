local VERSION = "0.2.0"

set_project("aurpush")
set_version(VERSION)
set_languages("c++20")

add_rules("mode.debug", "mode.release")
add_includedirs("include", {public = true})
add_defines('AURPUSH_VERSION="' .. VERSION .. '"')

set_warnings("allextra")
add_cxflags("-Wpedantic", "-Wshadow", "-Wold-style-cast", "-Wnon-virtual-dtor",
            "-Wdouble-promotion", "-Wformat=2", {tools = {"gcc", "clang"}})

-- Opt in with `xmake f --werror=y`; kept off by default so a newer compiler's
-- fresh diagnostics cannot break someone's build.
option("werror")
    set_default(false)
    set_showmenu(true)
    set_description("Treat compiler warnings as errors")
option_end()

if has_config("werror") then
    add_cxflags("-Werror")
end

if not is_mode("release") then
    add_cxflags("-fsanitize=address,undefined", "-fno-omit-frame-pointer")
    add_ldflags("-fsanitize=address,undefined")
end

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
