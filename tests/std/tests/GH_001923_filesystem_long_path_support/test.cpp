// Copyright 2024 "Lennox" Shou Hao Ho
//
// Boost Software License - Version 1.0 - August 17th, 2003
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
//
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#define NOMINMAX

#ifdef _MSC_VER
// The annotation for function 'RegOpenKeyExW' on _Param_(3) does not apply to a value type
#   pragma warning(disable:6553)

#   pragma comment(lib, "Advapi32.lib")
#   pragma comment(lib, "Ntdll.lib")
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>

#ifdef USE_BOOST_FILESYSTEM
#   include <boost/filesystem.hpp>
#else
#   include <filesystem>
#endif

#include <Windows.h>

#define TEST_ASSERT_MSG(cond, msg) { if (!(cond)) throw test_assertion_failed{ msg, __LINE__ };  }
#define TEST_ASSERT(cond)          TEST_ASSERT_MSG(cond, #cond)
#define TEST_CASE(test_func)       { curr_test_name = #test_func; fixture.test(test_func, curr_test_name); }

extern "C" BOOL RtlGetVersion(PRTL_OSVERSIONINFOW);

#ifdef USE_BOOST_FILESYSTEM
namespace fs = boost::filesystem;
constexpr auto regular_file_type = fs::regular_file;
constexpr auto directory_file_type = fs::directory_file;
constexpr auto symlink_file_type = fs::symlink_file;
using err_code = boost::system::error_code;
#else
namespace fs = std::filesystem;
constexpr auto regular_file_type = fs::file_type::regular;
constexpr auto directory_file_type = fs::file_type::directory;
constexpr auto symlink_file_type = fs::file_type::symlink;
using err_code = std::error_code;
#endif

using path_chr = fs::path::value_type;
using path_str = fs::path::string_type;

constexpr std::size_t windows_max_path_len = 260;
// Minimum number of characters fixture::curr_short_test_dir will have
// before hitting windows_max_path_len
constexpr std::size_t short_test_path_reserved_len = 10;

//----- Support -----

class test_assertion_failed {
    std::string m_msg;

public:
    test_assertion_failed(std::string_view msg, long line)
    :m_msg{ ("Line " + std::to_string(line) + ": ").append(msg) }
    {}

    std::string_view message() const noexcept {
        return m_msg;
    }
};

class longpath_fixture {
    fs::path m_start_dir;

public:
    fs::path root_short_test_dir;
    fs::path root_long_test_base_dir;
    fs::path root_long_test_dir;
    fs::path curr_short_test_dir;
    fs::path curr_long_test_dir;

    longpath_fixture() {
        m_start_dir = fs::current_path();
        TEST_ASSERT_MSG(m_start_dir.is_absolute(), "current_path should return an absolute path");

        // Sorta moot, since CreateProcess doesn't even work if the cwd is a long path
        TEST_ASSERT_MSG(m_start_dir.native().size() < windows_max_path_len,
                        "This test suite should be started from a short path");

        constexpr std::string_view name_chars = "0123456789ABCDEF";

        std::random_device rd;
        std::minstd_rand eng{ rd() };
        std::uniform_int_distribution<std::size_t> dist{ 0, name_chars.size() - 1 };

        auto generate_subdirname = [&](std::size_t length) {
            path_str name(length, path_chr('a'));
            std::generate(name.begin(), name.end(),
                          [&](){ return path_chr(name_chars[dist(eng)]); });
            return name;
        };

        auto test_dir = fs::temp_directory_path();
        fs::create_directories(test_dir);
        test_dir = fs::absolute(test_dir);

        {
            root_short_test_dir = test_dir / generate_subdirname(8);
            TEST_ASSERT_MSG(root_short_test_dir.native().size() < windows_max_path_len,
                            "The short root test directory will exceed the windows long path limit. "
                            "Please start this test suite from a shorter path");

            std::wcerr << L"Creating short root directory " << root_short_test_dir << L"\n";
            fs::remove_all(root_short_test_dir);
            fs::create_directories(root_short_test_dir);
        }

        {
            root_long_test_base_dir = test_dir / generate_subdirname(32);
            root_long_test_dir = root_long_test_base_dir;

            while (root_long_test_dir.native().size() <= windows_max_path_len) {
                root_long_test_dir /= generate_subdirname(32);
            }

            std::wcerr << L"Creating long root directory " << root_long_test_dir << L"\n";
            fs::remove_all(root_long_test_dir);
            fs::create_directories(root_long_test_dir);
        }
    }

    ~longpath_fixture() {
        err_code ec;

        fs::current_path(m_start_dir, ec);

        fs::remove_all(root_short_test_dir, ec);
        if (ec) {
            std::wcerr << L"Warning, failed to clean up short root directory " << root_short_test_dir << L" after test.\n";
        }

        fs::remove_all(root_long_test_base_dir, ec);
        if (ec) {
            std::wcerr << L"Warning, failed to clean up long root directory " << root_long_test_base_dir << L" after test.\n";
        }
    }

    longpath_fixture(const longpath_fixture&) = delete;
    longpath_fixture& operator=(const longpath_fixture&) = delete;

    template <typename TestFunc>
    void test(TestFunc test_func, std::string_view test_name) {
        TEST_ASSERT_MSG(!test_name.empty(), "test name must not be empty");
        curr_short_test_dir = root_short_test_dir / test_name;
        curr_long_test_dir = root_long_test_dir / test_name;

        TEST_ASSERT_MSG((curr_short_test_dir.native().size() + short_test_path_reserved_len) < windows_max_path_len,
                        "Test name is too long");

        std::wcerr << L"Creating short test directory " << curr_short_test_dir << L"\n";
        fs::remove_all(curr_short_test_dir);
        fs::create_directory(curr_short_test_dir);

        std::wcerr << L"Creating long test directory " << curr_long_test_dir << L"\n";
        fs::remove_all(curr_long_test_dir);
        fs::create_directory(curr_long_test_dir);
        
        fs::current_path(curr_short_test_dir);
        test_func(*this);
        fs::current_path(m_start_dir);
    }
};

class test_file {
    HANDLE m_hfile = INVALID_HANDLE_VALUE;

public:
    test_file(const fs::path& path) {
        // Would be great if fstream also supports long paths
        m_hfile = CreateFileW(path.c_str(), GENERIC_READ|GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        TEST_ASSERT_MSG(m_hfile != INVALID_HANDLE_VALUE, "Failed to create test file");

        DWORD written = 0;
        std::string_view dummy_str = "foobarbaz";
        auto write_res = WriteFile(m_hfile, dummy_str.data(), static_cast<DWORD>(dummy_str.size()),
                                   &written, nullptr);

        if (!write_res || written != dummy_str.size()) {
            CloseHandle(m_hfile);
            TEST_ASSERT_MSG(false, "Failed to write to dummy file");
        }
    }

    ~test_file() {
        CloseHandle(m_hfile);
    }

    test_file(const test_file&) = delete;
    test_file& operator=(const test_file&) = delete;
};

void check_os_support() {
    RTL_OSVERSIONINFOW info;
    info.dwOSVersionInfoSize = sizeof(info);

    RtlGetVersion(&info);

    TEST_ASSERT_MSG(info.dwMajorVersion >= 10,
                    "This test suite is not supported on platforms older than Windows 10 version 1607");

    if (info.dwMajorVersion == 10 && info.dwMinorVersion == 0) {
        // corresponds to Win 10 version 1607
        TEST_ASSERT_MSG(info.dwBuildNumber >= 14393,
                        "This test suite is not supported on platforms older than Windows 10 version 1607");
    }
}

void check_registry() {
    HKEY key;
    auto open_result = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                                     L"SYSTEM\\CurrentControlSet\\Control\\FileSystem",
                                     0, KEY_READ, &key);

    TEST_ASSERT_MSG(open_result != ERROR_FILE_NOT_FOUND,
                    "The registry key HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\FileSystem does not exist on the host machine");
    TEST_ASSERT_MSG(open_result == ERROR_SUCCESS,
                    "Failed to query value of registry key HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\FileSystem");

    DWORD longpath_enabled = 0;
    auto buf_size = static_cast<DWORD>(sizeof(longpath_enabled));

    auto read_result = RegQueryValueExW(key, L"LongPathsEnabled", nullptr, nullptr,
                                        reinterpret_cast<LPBYTE>(&longpath_enabled),
                                       &buf_size);

    TEST_ASSERT_MSG(read_result != ERROR_FILE_NOT_FOUND, "The registry key LongPathsEnabled does not exist on the host machine");
    TEST_ASSERT_MSG(read_result == ERROR_SUCCESS, "Failed to query value of registry key LongPathsEnabled");

    TEST_ASSERT_MSG(longpath_enabled == 1, "The registry key LongPathsEnabled is not set to 1 on the host machine");
}

//----- Test Cases -----

void test_current_path(const longpath_fixture& fixture) {
    // chdir from a short path to a long path
    fs::current_path(fixture.curr_long_test_dir);
    TEST_ASSERT(fixture.curr_long_test_dir == fs::current_path());

    // chdir from a long path to a short path
    fs::current_path(fixture.curr_short_test_dir);
    TEST_ASSERT(fixture.curr_short_test_dir == fs::current_path());
}

void test_create_directory(const longpath_fixture& fixture) {
    // Create a long path dir from a short path cwd
    fs::create_directory(fixture.curr_long_test_dir / "foo");

    // Create a long path dir from a long path cwd
    fs::current_path(fixture.curr_long_test_dir);
    fs::create_directory("bar");

    // Create a short path dir from a long path cwd
    fs::create_directory(fixture.curr_short_test_dir / "baz");
}

template <typename Func>
void test_create_link(const longpath_fixture& fixture, Func func) {
    test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
    test_file file2{ fixture.curr_long_test_dir / "foo.txt" };

    // Create short links to long path targets from a short path cwd
    func(fixture.curr_long_test_dir / "foo.txt", "a");

    // Create long links to long path targets from a short path cwd
    func(fixture.curr_long_test_dir / "foo.txt", fixture.curr_long_test_dir / "b");

    // Create long links to short path targets from a short path cwd
    func("foo.txt", fixture.curr_long_test_dir / "c");

    fs::current_path(fixture.curr_long_test_dir);

    // Create short links to long path targets from a long path cwd
    func("foo.txt", fixture.curr_short_test_dir / "d");

    // Create long links to long path targets from a long path cwd
    func("foo.txt", "e");

    // Create long links to short path targets from a long path cwd
    func(fixture.curr_short_test_dir / "foo.txt", "f");
}

void test_create_hard_link(const longpath_fixture& fixture) {
    auto func = [](const fs::path& t, const fs::path& l) { fs::create_hard_link(t, l); };
    test_create_link(fixture, func);
}

void test_create_symlink(const longpath_fixture& fixture) {
    auto func = [](const fs::path& t, const fs::path& l) { fs::create_symlink(t, l); };
    test_create_link(fixture, func);
}

void test_create_directory_symlink(const longpath_fixture& fixture) {
    fs::create_directory(fixture.curr_short_test_dir / "foo");
    fs::create_directory(fixture.curr_long_test_dir / "foo");

    // Create short links to long path targets from a short path cwd
    fs::create_directory_symlink(fixture.curr_long_test_dir / "foo", "a");

    // Create long links to long path targets from a short path cwd
    fs::create_directory_symlink(fixture.curr_long_test_dir / "foo", fixture.curr_long_test_dir / "b");

    // Create long links to short path targets from a short path cwd
    fs::create_directory_symlink("foo", fixture.curr_long_test_dir / "c");

    fs::current_path(fixture.curr_long_test_dir);

    // Create short links to long path targets from a long path cwd
    fs::create_directory_symlink("foo", fixture.curr_short_test_dir / "d");

    // Create long links to long path targets from a long path cwd
    fs::create_directory_symlink("foo", "e");

    // Create long links to short path targets from a long path cwd
    fs::create_directory_symlink(fixture.curr_short_test_dir / "foo", "f");
}

void test_remove(const longpath_fixture& fixture) {
    {
        test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
        fs::create_directory(fixture.curr_short_test_dir / "bar");

        test_file file2{ fixture.curr_long_test_dir / "foo.txt" };
        test_file file3{ fixture.curr_long_test_dir / "bar.txt" };
        fs::create_directory(fixture.curr_long_test_dir / "baz");
        fs::create_directory(fixture.curr_long_test_dir / "qux");
    }

    // remove long path from short path cwd
    TEST_ASSERT(fs::remove(fixture.curr_long_test_dir / "foo.txt"));
    TEST_ASSERT(fs::remove(fixture.curr_long_test_dir / "baz"));

    fs::current_path(fixture.curr_long_test_dir);

    // remove long path from long path cwd
    TEST_ASSERT(fs::remove("bar.txt"));
    TEST_ASSERT(fs::remove("qux"));

    // remove short path from long path cwd
    TEST_ASSERT(fs::remove(fixture.curr_short_test_dir / "foo.txt"));
    TEST_ASSERT(fs::remove(fixture.curr_short_test_dir / "bar"));
}

void test_rename(const longpath_fixture& fixture) {
    {
        test_file file1{ fixture.curr_long_test_dir / "a1.txt" };
        fs::create_directory(fixture.curr_long_test_dir / "a2");
    }

    // rename long path to short path from short path cwd
    fs::rename(fixture.curr_long_test_dir / "a1.txt", "b1.txt");
    fs::rename(fixture.curr_long_test_dir / "a2", "b2");

    fs::current_path(fixture.curr_long_test_dir);

    // rename short path to long path from long path cwd
    fs::rename(fixture.curr_short_test_dir / "b1.txt", "c1.txt");
    fs::rename(fixture.curr_short_test_dir / "b2", "c2");

    // rename long path to long path from long path cwd
    fs::rename("c1.txt", "d1.txt");
    fs::rename("c2", "d2");

    // rename long path to short path from long path cwd
    fs::rename("d1.txt", fixture.curr_short_test_dir / "e1.txt");
    fs::rename("d2", fixture.curr_short_test_dir / "e2");
}

void test_absolute(const longpath_fixture& fixture) {
    const auto expected = fixture.curr_long_test_dir / "foo";
    fs::create_directory(expected);

    // get absolute path for a long path from a short path cwd
    auto actual = fs::absolute(fs::relative(expected));
    TEST_ASSERT(expected == actual ||
                (fixture.curr_short_test_dir / fs::relative(expected)) == actual);

    fs::current_path(fixture.curr_long_test_dir);

    // get absolute path for a long path from a long path cwd
    actual = fs::absolute("foo");
    TEST_ASSERT(expected == actual);

    // get absolute path for a short path from a long path cwd
    actual = fs::absolute(fs::relative(fixture.curr_short_test_dir));
    TEST_ASSERT(fixture.curr_short_test_dir == actual ||
                (fixture.curr_long_test_dir / fs::relative(fixture.curr_short_test_dir)) == actual);
}

void test_canonical(const longpath_fixture& fixture) {
    const auto dir = fixture.curr_long_test_dir / "foo";
    fs::create_directory(dir);
    const auto expected = fs::canonical(dir);

    // realpath for a long path from a short path cwd
    auto actual = fs::canonical(fs::relative(dir));
    TEST_ASSERT(expected == actual);

    fs::current_path(fixture.curr_long_test_dir);

    // realpath for a long path from a long path cwd
    actual = fs::canonical("foo");
    TEST_ASSERT(expected == actual);

    // realpath for a short path from a long path cwd
    actual = fs::canonical(fs::relative(fixture.curr_short_test_dir));
    TEST_ASSERT(fs::canonical(fixture.curr_short_test_dir) == actual);
}

void test_weakly_canonical(const longpath_fixture& fixture) {
    auto expected = fs::canonical(fixture.curr_long_test_dir) / "foo";

    // make canon for a long path from a short path cwd
    auto actual = fs::weakly_canonical(fs::relative(fixture.curr_long_test_dir) / "foo");
    TEST_ASSERT(expected == actual ||
                (fs::relative(fixture.curr_long_test_dir) / "foo") == actual);

    fs::current_path(fixture.curr_long_test_dir);

    // make canon for a long path from a long path cwd
    actual = fs::weakly_canonical("foo");
    expected = "foo";
    TEST_ASSERT(expected == actual);

    actual = fs::weakly_canonical(fs::relative(fixture.curr_short_test_dir) / "foo");
    expected = fs::canonical(fixture.curr_short_test_dir) / "foo";
    TEST_ASSERT(expected == actual ||
                (fs::relative(fixture.curr_short_test_dir) / "foo") == actual);
}

void test_copy_file(const longpath_fixture& fixture) {
    {
        test_file tfile{ fixture.curr_short_test_dir / "a.txt" };
    }

    // copy file with short path to long path from a short path cwd
    fs::copy_file("a.txt", fixture.curr_long_test_dir / "b.txt");

    // copy file with long path to long path from a short path cwd
    fs::copy_file(fixture.curr_long_test_dir / "b.txt", fixture.curr_long_test_dir / "c.txt");

    // copy file with long path to short path from a short path cwd
    fs::copy_file(fixture.curr_long_test_dir / "c.txt", "d.txt");

    fs::current_path(fixture.curr_long_test_dir);

    // copy file with short path to long path from a long path cwd
    fs::copy_file(fixture.curr_short_test_dir / "d.txt", "e.txt");

    // copy file with long path to long path from a long path cwd
    fs::copy_file("e.txt", "f.txt");

    // copy file with long path to short path from a long path cwd
    fs::copy_file("f.txt", fixture.curr_short_test_dir / "g.txt");
}

void test_is_empty(const longpath_fixture& fixture) {
    test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
    test_file file2{ fixture.curr_long_test_dir / "foo.txt" };

    // check file on long path from short path cwd
    TEST_ASSERT(!fs::is_empty(fixture.curr_long_test_dir));
    TEST_ASSERT(!fs::is_empty(fixture.curr_long_test_dir / "foo.txt"));

    // check file on long path from long path cwd
    fs::current_path(fixture.curr_long_test_dir);
    TEST_ASSERT(!fs::is_empty("."));
    TEST_ASSERT(!fs::is_empty("foo.txt"));

    // check file on short path from long path cwd
    TEST_ASSERT(!fs::is_empty(fixture.curr_short_test_dir));
    TEST_ASSERT(!fs::is_empty(fixture.curr_short_test_dir / "foo.txt"));
}

void test_file_size(const longpath_fixture& fixture) {
    test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
    test_file file2{ fixture.curr_long_test_dir / "foo.txt" };

    // check size on long path from short path cwd
    TEST_ASSERT(9 == fs::file_size(fixture.curr_long_test_dir / "foo.txt"));

    // check size on long path from long path cwd
    fs::current_path(fixture.curr_long_test_dir);
    TEST_ASSERT(9 == fs::file_size("foo.txt"));

    // check size on short path from long path cwd
    TEST_ASSERT(9 == fs::file_size(fixture.curr_short_test_dir / "foo.txt"));
}

void test_resize_file(const longpath_fixture& fixture) {
    {
        test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
        test_file file2{ fixture.curr_long_test_dir / "foo.txt" };
    }

    // resize file on long path from short path cwd
    fs::resize_file(fixture.curr_long_test_dir / "foo.txt", 7);

    // resize file on long path from long path cwd
    fs::current_path(fixture.curr_long_test_dir);
    fs::resize_file("foo.txt", 8);

    // resize file on short path from long path cwd
    fs::resize_file(fixture.curr_short_test_dir / "foo.txt", 5);
}

void test_last_write_time(const longpath_fixture& fixture) {
    test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
    test_file file2{ fixture.curr_long_test_dir / "foo.txt" };

    // write time on long path from short path cwd
    auto time = fs::last_write_time(fixture.curr_long_test_dir / "foo.txt");
    fs::last_write_time(fixture.curr_long_test_dir / "foo.txt", time);

    // write time on long path from long path cwd
    fs::current_path(fixture.curr_long_test_dir);
    time = fs::last_write_time("foo.txt");
    fs::last_write_time("foo.txt", time);

    // write time on short path from long path cwd
    time = fs::last_write_time(fixture.curr_short_test_dir / "foo.txt");
    fs::last_write_time(fixture.curr_short_test_dir / "foo.txt", time);
}

void test_permissions(const longpath_fixture& fixture) {
    constexpr auto rw = fs::perms::owner_read | fs::perms::owner_write;

    test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
    test_file file2{ fixture.curr_long_test_dir / "foo.txt" };

    // permissions on long path from short path cwd
    fs::permissions(fixture.curr_long_test_dir / "foo.txt", rw);

    // permissions on long path from long path cwd
    fs::current_path(fixture.curr_long_test_dir);
    fs::permissions("foo.txt", rw);

    // permissions on short path from long path cwd
    fs::permissions(fixture.curr_short_test_dir / "foo.txt", rw);
}

void test_status(const longpath_fixture& fixture) {
    test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
    test_file file2{ fixture.curr_long_test_dir / "foo.txt" };

    // check status on long path from short path cwd
    auto st = fs::status(fixture.curr_long_test_dir);
    TEST_ASSERT(st.type() == directory_file_type);
    st = fs::status(fixture.curr_long_test_dir / "foo.txt");
    TEST_ASSERT(st.type() == regular_file_type);

    fs::current_path(fixture.curr_long_test_dir);

    // check status on long path from long path cwd
    st = fs::status(".");
    TEST_ASSERT(st.type() == directory_file_type);
    st = fs::status("foo.txt");
    TEST_ASSERT(st.type() == regular_file_type);

    // check status on short path from long path cwd
    st = fs::status(fixture.curr_short_test_dir);
    TEST_ASSERT(st.type() == directory_file_type);
    st = fs::status(fixture.curr_short_test_dir / "foo.txt");
    TEST_ASSERT(st.type() == regular_file_type);
}

void test_symlink_status(const longpath_fixture& fixture) {
    test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
    test_file file2{ fixture.curr_long_test_dir / "foo.txt" };

    fs::create_symlink(fixture.curr_long_test_dir / "foo.txt", fixture.curr_short_test_dir / "a");
    fs::create_symlink(fixture.curr_short_test_dir, fixture.curr_long_test_dir / "b");

    // check status on long path from short path cwd
    auto st = fs::symlink_status(fixture.curr_long_test_dir / "b");
    TEST_ASSERT(st.type() == symlink_file_type);

    fs::current_path(fixture.curr_long_test_dir);

    // check status on long path from long path cwd
    st = fs::symlink_status("b");
    TEST_ASSERT(st.type() == symlink_file_type);

    // check status on short path from long path cwd
    st = fs::symlink_status(fixture.curr_short_test_dir / "a");
    TEST_ASSERT(st.type() == symlink_file_type);
}

void test_read_symlink(const longpath_fixture& fixture) {
    test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
    test_file file2{ fixture.curr_long_test_dir / "foo.txt" };

    fs::create_symlink(fixture.curr_long_test_dir / "foo.txt", "a");
    fs::create_symlink("foo.txt", fixture.curr_long_test_dir / "b");
    fs::create_symlink(fixture.curr_short_test_dir / "foo.txt", fixture.curr_long_test_dir / "c");

    // readlink from short path cwd
    TEST_ASSERT(fs::read_symlink("a") == fixture.curr_long_test_dir / "foo.txt");
    TEST_ASSERT(fs::read_symlink(fixture.curr_long_test_dir / "b") == "foo.txt");
    TEST_ASSERT(fs::read_symlink(fixture.curr_long_test_dir / "c") == (fixture.curr_short_test_dir / "foo.txt"));

    fs::current_path(fixture.curr_long_test_dir);

    // readlink from long path cwd
    TEST_ASSERT(fs::read_symlink(fixture.curr_short_test_dir / "a") == fixture.curr_long_test_dir / "foo.txt");
    TEST_ASSERT(fs::read_symlink("b") == "foo.txt");
    TEST_ASSERT(fs::read_symlink("c") == (fixture.curr_short_test_dir / "foo.txt"));
}

void test_hard_link_count(const longpath_fixture& fixture) {
    test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
    test_file file2{ fixture.curr_long_test_dir / "foo.txt" };

    fs::create_hard_link(fixture.curr_long_test_dir / "foo.txt", fixture.curr_short_test_dir / "a");
    fs::create_hard_link(fixture.curr_short_test_dir / "foo.txt", fixture.curr_long_test_dir / "b");

    TEST_ASSERT(fs::hard_link_count(fixture.curr_long_test_dir / "foo.txt") == 2);

    fs::current_path(fixture.curr_long_test_dir);

    TEST_ASSERT(fs::hard_link_count("foo.txt") == 2);
    TEST_ASSERT(fs::hard_link_count(fixture.curr_short_test_dir / "foo.txt") == 2);
}

void test_equivalent(const longpath_fixture& fixture) {
    test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
    test_file file2{ fixture.curr_long_test_dir / "foo.txt" };

    fs::create_symlink(fixture.curr_long_test_dir / "foo.txt", "a");
    fs::create_symlink("foo.txt", fixture.curr_long_test_dir / "b");
    fs::create_symlink(fixture.curr_short_test_dir / "foo.txt", fixture.curr_long_test_dir / "c");

    auto resolve_path = [](fs::path link) {
        auto target = fs::read_symlink(link);
        if (!target.is_absolute()) {
            target = fs::absolute(link.parent_path()) / target;
        }

        return target;
    };

    // test from short path cwd
    TEST_ASSERT(fs::equivalent(resolve_path("a"), fixture.curr_long_test_dir / "foo.txt"));
    TEST_ASSERT(fs::equivalent(resolve_path(fixture.curr_long_test_dir / "b"), fixture.curr_long_test_dir / "foo.txt"));
    TEST_ASSERT(fs::equivalent(resolve_path(fixture.curr_long_test_dir / "c"), fixture.curr_short_test_dir / "foo.txt"));

    fs::current_path(fixture.curr_long_test_dir);

    // test from long path cwd
    TEST_ASSERT(fs::equivalent(resolve_path(fixture.curr_short_test_dir / "a"), fixture.curr_long_test_dir / "foo.txt"));
    TEST_ASSERT(fs::equivalent(resolve_path("b"), "foo.txt"));
    TEST_ASSERT(fs::equivalent(resolve_path("c"), fixture.curr_short_test_dir / "foo.txt"));
}

void test_recursive_directory_iterator(const longpath_fixture& fixture) {
    test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
    test_file file2{ fixture.curr_short_test_dir / "bar.txt" };
    test_file file3{ fixture.curr_long_test_dir / "foo.txt" };
    test_file file4{ fixture.curr_long_test_dir / "bar.txt" };

    std::size_t count = 0;

    // iterate on long path from short path cwd
    for (const auto& entry : fs::recursive_directory_iterator{ fixture.curr_long_test_dir }) {
        TEST_ASSERT(entry.is_regular_file());
        ++count;
    }
    TEST_ASSERT(count == 2);

    // iterate on long path from long path cwd
    for (const auto& entry : fs::recursive_directory_iterator{ "." }) {
        TEST_ASSERT(entry.is_regular_file());
        ++count;
    }
    TEST_ASSERT(count == 4);

    // iterate on short path from long path cwd
    for (const auto& entry : fs::recursive_directory_iterator{ fixture.curr_long_test_dir }) {
        TEST_ASSERT(entry.is_regular_file());
        ++count;
    }
    TEST_ASSERT(count == 6);
}

void test_directory_iterator(const longpath_fixture& fixture) {
    test_file file1{ fixture.curr_short_test_dir / "foo.txt" };
    test_file file2{ fixture.curr_short_test_dir / "bar.txt" };
    test_file file3{ fixture.curr_long_test_dir / "foo.txt" };
    test_file file4{ fixture.curr_long_test_dir / "bar.txt" };

    std::size_t count = 0;

    // iterate on long path from short path cwd
    for (const auto& entry : fs::directory_iterator{ fixture.curr_long_test_dir }) {
        TEST_ASSERT(entry.is_regular_file());
        ++count;
    }
    TEST_ASSERT(count == 2);

    // iterate on long path from long path cwd
    for (const auto& entry : fs::directory_iterator{ "." }) {
        TEST_ASSERT(entry.is_regular_file());
        ++count;
    }
    TEST_ASSERT(count == 4);

    // iterate on short path from long path cwd
    for (const auto& entry : fs::directory_iterator{ fixture.curr_long_test_dir }) {
        TEST_ASSERT(entry.is_regular_file());
        ++count;
    }
    TEST_ASSERT(count == 6);
}

//----- Main -----

int main() {
    std::string_view curr_test_name = "setup";

    try {
        // https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation?tabs=registry#enable-long-paths-in-windows-10-version-1607-and-later
        check_os_support();
        check_registry();

        curr_test_name = "fixture_initialisation";
        longpath_fixture fixture;

        TEST_CASE(test_current_path);
        TEST_CASE(test_create_directory);
        TEST_CASE(test_create_hard_link);
        TEST_CASE(test_create_symlink);
        TEST_CASE(test_create_directory_symlink);
        TEST_CASE(test_remove);
        TEST_CASE(test_rename);
        TEST_CASE(test_absolute);
        TEST_CASE(test_canonical);
        TEST_CASE(test_weakly_canonical);
        TEST_CASE(test_copy_file);
        TEST_CASE(test_is_empty);
        TEST_CASE(test_file_size);
        TEST_CASE(test_resize_file);
        TEST_CASE(test_last_write_time);
        TEST_CASE(test_permissions);
        TEST_CASE(test_status);
        TEST_CASE(test_symlink_status);
        TEST_CASE(test_read_symlink);
        TEST_CASE(test_hard_link_count);
        TEST_CASE(test_equivalent);

        TEST_CASE(test_directory_iterator);
        TEST_CASE(test_recursive_directory_iterator);

        return 0;
    }
    catch (const test_assertion_failed& err) {
        std::cerr << "Failing test: " << curr_test_name << "\n"
                  << "\t" << err.message() << "\n";
        return 1;
    }
    catch (const fs::filesystem_error& err) {
        std::cerr << "Failing test: " << curr_test_name << "\n";

        std::wcerr << L"FS Exception: " << err.what() << L"\n"
                   << L"\tPath 1: " << err.path1() << L"\n"
                   << L"\tPath 2: " << err.path2() << L"\n";
        return 2;
    }
    catch (const std::exception& err) {
        std::cerr << "Failing test: " << curr_test_name << "\n";
        std::wcerr << L"Other Exception: " << err.what() << L"\n";
        return 3;
    }
}