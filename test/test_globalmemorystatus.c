#include <windows.h>

#include "test_assert.h"

static void test_globalmemorystatus_basic(void) {
    MEMORYSTATUS status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatus(&status);

    TEST_CHECK_EQ(sizeof(status), status.dwLength);
    TEST_CHECK(status.dwMemoryLoad <= 100);
    TEST_CHECK(status.dwTotalPhys >= status.dwAvailPhys);
    TEST_CHECK(status.dwTotalPageFile >= status.dwAvailPageFile);
    TEST_CHECK(status.dwTotalVirtual >= status.dwAvailVirtual);
    TEST_CHECK(status.dwTotalPhys > 0);
    TEST_CHECK(status.dwTotalVirtual > 0);
}

int main(void) {
    test_globalmemorystatus_basic();
    return 0;
}
