#include <windows.h>

#include <string.h>

#include "test_assert.h"

static BOOL multi_string_contains(const char *buffer, const char *needle) {
    const char *cursor = buffer;
    while (*cursor) {
        if (strcmp(cursor, needle) == 0) {
            return TRUE;
        }
        cursor += strlen(cursor) + 1;
    }
    return FALSE;
}

static void write_ini_file(const char *path) {
    const char *contents =
        "[SectionOne]\r\n"
        "KeyOne=ValueOne\r\n"
        "KeyTwo=\"Quoted\"\r\n"
        "KeyThree=Value with spaces\r\n"
        "\r\n"
        "[Other]\r\n"
        "Alpha=beta\r\n";

    HANDLE handle = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    TEST_CHECK(handle != INVALID_HANDLE_VALUE);

    DWORD written = 0;
    BOOL ok = WriteFile(handle, contents, (DWORD)strlen(contents), &written, NULL);
    TEST_CHECK(ok);
    TEST_CHECK_EQ(strlen(contents), written);
    CloseHandle(handle);
}

static void test_values(const char *path) {
    char buffer[128];

    DWORD len = GetPrivateProfileStringA("SectionOne", "KeyOne", "Default", buffer, sizeof(buffer), path);
    TEST_CHECK_EQ(8, len);
    TEST_CHECK_STR_EQ("ValueOne", buffer);

    len = GetPrivateProfileStringA("sectionone", "keytwo", "Default", buffer, sizeof(buffer), path);
    TEST_CHECK_EQ(6, len);
    TEST_CHECK_STR_EQ("Quoted", buffer);

    len = GetPrivateProfileStringA("SectionOne", "Missing", "Fallback", buffer, sizeof(buffer), path);
    TEST_CHECK_EQ(8, len);
    TEST_CHECK_STR_EQ("Fallback", buffer);
}

static void test_enumeration(const char *path) {
    char buffer[256];

    DWORD len = GetPrivateProfileStringA(NULL, NULL, NULL, buffer, sizeof(buffer), path);
    TEST_CHECK(len > 0);
    TEST_CHECK(multi_string_contains(buffer, "SectionOne"));
    TEST_CHECK(multi_string_contains(buffer, "Other"));

    len = GetPrivateProfileStringA("SectionOne", NULL, NULL, buffer, sizeof(buffer), path);
    TEST_CHECK(len > 0);
    TEST_CHECK(multi_string_contains(buffer, "KeyOne"));
    TEST_CHECK(multi_string_contains(buffer, "KeyTwo"));
    TEST_CHECK(multi_string_contains(buffer, "KeyThree"));
}

static void test_missing_file(void) {
    char buffer[64];
    SetLastError(0xdeadbeef);
    DWORD len = GetPrivateProfileStringA("SectionOne", "KeyOne", "Default", buffer, sizeof(buffer),
                                         "Z:\\definitely_missing.ini");
    TEST_CHECK_EQ(7, len);
    TEST_CHECK_STR_EQ("Default", buffer);
    TEST_CHECK_EQ(ERROR_FILE_NOT_FOUND, GetLastError());
}

int main(void) {
    char temp_path[MAX_PATH];
    char temp_file[MAX_PATH];

    DWORD path_len = GetTempPathA(sizeof(temp_path), temp_path);
    TEST_CHECK(path_len > 0 && path_len < sizeof(temp_path));

    UINT unique = GetTempFileNameA(temp_path, "wbo", 0, temp_file);
    TEST_CHECK(unique != 0);

    write_ini_file(temp_file);
    test_values(temp_file);
    test_enumeration(temp_file);
    test_missing_file();

    DeleteFileA(temp_file);
    return 0;
}
