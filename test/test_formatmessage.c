#include <windows.h>

#include "test_assert.h"

#define FROM_STR (FORMAT_MESSAGE_FROM_STRING)
#define FROM_STR_ARR (FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY)

/*
 * Expected values below are copied from wine's own conformance suite
 * (dlls/kernel32/tests/format_msg.c), so they match real Windows behaviour.
 */

static void expect(DWORD flags, const char *src, const ULONG_PTR *args, const char *expected) {
	char out[256];
	memset(out, 'x', sizeof(out));
	DWORD r = FormatMessageA(flags, src, 0, 0, out, sizeof(out), (va_list *)args);
	size_t elen = strlen(expected);
	TEST_CHECK_MSG(r == (DWORD)elen, "src=[%s] expected r=%u got r=%u out=[%s]", src, (unsigned)elen, (unsigned)r, out);
	TEST_CHECK_MSG(memcmp(out, expected, elen + 1) == 0, "src=[%s] expected=[%s] got=[%s]", src, expected, out);
}

/* Variadic wrapper mirroring wine's "doit" helper: exercises the va_list path. */
static DWORD doit(DWORD flags, LPCSTR src, DWORD msgid, DWORD lang, LPSTR out, DWORD size, ...) {
	va_list list;
	va_start(list, size);
	DWORD r = FormatMessageA(flags, src, msgid, lang, out, size, &list);
	va_end(list);
	return r;
}

static void test_basic(void) {
	char out[256];
	memset(out, 'x', sizeof(out));
	DWORD r = FormatMessageA(FROM_STR, "test", 0, 0, out, sizeof(out), NULL);
	TEST_CHECK_EQ(4, r);
	TEST_CHECK_STR_EQ("test", out);
}

static void test_escapes(void) {
	ULONG_PTR one_arg[] = {0x1ab};
	expect(FROM_STR, " %%%% ", NULL, " %% ");
	expect(FROM_STR_ARR, " %.%. %1!d!", one_arg, " .. 427");
	expect(FROM_STR, "test%0test", NULL, "test");
	expect(FROM_STR, "yah%!%0   ", NULL, "yah!");
	expect(FROM_STR, "% %   ", NULL, "    ");
	expect(FROM_STR, "%n%r%t", NULL, "\r\n\r\t");
	expect(FROM_STR, "hi\n", NULL, "hi\r\n");
	expect(FROM_STR, "hi\r\n", NULL, "hi\r\n");
	expect(FROM_STR, "\r", NULL, "\r\n");
	expect(FROM_STR, "\r\r\n", NULL, "\r\n\r\n");
}

static void test_max_width(void) {
	DWORD flags = FROM_STR | FORMAT_MESSAGE_MAX_WIDTH_MASK;
	expect(flags, "hi\n", NULL, "hi ");
	expect(flags, "hi\r\n", NULL, "hi ");
	expect(flags, "\r", NULL, " ");
	expect(flags, "\r\r\n", NULL, "  ");
}

static void test_inserts_array(void) {
	ULONG_PTR s_test[] = {(ULONG_PTR) "test"};
	ULONG_PTR s_two[] = {(ULONG_PTR) "te", (ULONG_PTR) "st"};
	ULONG_PTR s_three[] = {(ULONG_PTR) "t", (ULONG_PTR) "s", (ULONG_PTR) "e"};
	ULONG_PTR s_t[] = {(ULONG_PTR) "t"};
	ULONG_PTR s_null[] = {(ULONG_PTR)0};
	ULONG_PTR d_123[] = {1, 2, 3};
	ULONG_PTR one[] = {1};
	ULONG_PTR eleven[] = {11};
	ULONG_PTR longhex[] = {0x1ab};
	ULONG_PTR star2[] = {6, 4, 2, 5, 3, 1};

	expect(FROM_STR_ARR, "%1", s_test, "test");
	expect(FROM_STR_ARR, "%1%2", s_two, "test");
	expect(FROM_STR_ARR, "%1%3%2%1", s_three, "test");
	expect(FROM_STR_ARR, "%1!s!", s_test, "test");
	expect(FROM_STR_ARR, "%1!3s!", s_t, "  t");
	expect(FROM_STR_ARR, "%1", s_null, "(null)");
	expect(FROM_STR_ARR, "%1!d!%2!d!%3!d!", d_123, "123");
	expect(FROM_STR_ARR, "%1!4d!", one, "   1");
	expect(FROM_STR_ARR, "%1!-4d!", one, "1   ");
	expect(FROM_STR_ARR, "%1!4d!", eleven, "  11");
	expect(FROM_STR_ARR, "%1!4x!", eleven, "   b");
	expect(FROM_STR_ARR, "%1!4X!", eleven, "   B");
	expect(FROM_STR_ARR, "%1!-4X!", eleven, "B   ");
	expect(FROM_STR_ARR, "%1!4X!", longhex, " 1AB");
	/* width/precision with '*' consumes extra array entries */
	expect(FROM_STR_ARR, "%1!*.*u!,%1!*.*u!", star2, "  0002, 00003");
	expect(FROM_STR_ARR, "%1!*.*u!,%4!*.*u!", star2, "  0002,  001");
}

static void test_inserts_valist(void) {
	char out[256];
	DWORD r;

	r = doit(FROM_STR, "%1!s!", 0, 0, out, sizeof(out), "test");
	TEST_CHECK_EQ(4, r);
	TEST_CHECK_STR_EQ("test", out);

	r = doit(FROM_STR, "%1", 0, 0, out, sizeof(out), "test");
	TEST_CHECK_EQ(4, r);
	TEST_CHECK_STR_EQ("test", out);

	r = doit(FROM_STR, "%1%2", 0, 0, out, sizeof(out), "te", "st");
	TEST_CHECK_EQ(4, r);
	TEST_CHECK_STR_EQ("test", out);

	/* ls / S / ws are unicode, narrowed back to ANSI */
	r = doit(FROM_STR, "%1!ls!", 0, 0, out, sizeof(out), L"test");
	TEST_CHECK_EQ(4, r);
	TEST_CHECK_STR_EQ("test", out);

	r = doit(FROM_STR, "%1!S!", 0, 0, out, sizeof(out), L"test");
	TEST_CHECK_EQ(4, r);
	TEST_CHECK_STR_EQ("test", out);

	/* characters */
	r = doit(FROM_STR, "%1!c!%2!c!%3!c!%1!c!", 0, 0, out, sizeof(out), 't', 'e', 's');
	TEST_CHECK_EQ(4, r);
	TEST_CHECK_STR_EQ("test", out);

	/* wide characters */
	r = doit(FROM_STR, "%1!lc!%2!lc!%3!lc!%1!lc!", 0, 0, out, sizeof(out), 't', 'e', 's');
	TEST_CHECK_EQ(4, r);
	TEST_CHECK_STR_EQ("test", out);

	r = doit(FROM_STR, "%1!d!%2!d!%3!d!", 0, 0, out, sizeof(out), 1, 2, 3);
	TEST_CHECK_EQ(3, r);
	TEST_CHECK_STR_EQ("123", out);

	r = doit(FROM_STR, "%1", 0, 0, out, sizeof(out), NULL);
	TEST_CHECK_EQ(6, r);
	TEST_CHECK_STR_EQ("(null)", out);

	/* width and precision, including '*' */
	r = doit(FROM_STR, "%1!3s!", 0, 0, out, sizeof(out), "t");
	TEST_CHECK_EQ(3, r);
	TEST_CHECK_STR_EQ("  t", out);

	r = doit(FROM_STR, "%1!*s!", 0, 0, out, sizeof(out), 4, "t");
	TEST_CHECK_EQ(4, r);
	TEST_CHECK_STR_EQ("   t", out);

	r = doit(FROM_STR, "%1!4.2u!", 0, 0, out, sizeof(out), 3);
	TEST_CHECK_EQ(4, r);
	TEST_CHECK_STR_EQ("  03", out);

	r = doit(FROM_STR, "%1!*.*u!", 0, 0, out, sizeof(out), 5, 3, 1);
	TEST_CHECK_EQ(5, r);
	TEST_CHECK_STR_EQ("  001", out);

	r = doit(FROM_STR, "%1!*.*u!,%1!*.*u!", 0, 0, out, sizeof(out), 5, 3, 1, 4, 2);
	TEST_CHECK_EQ(11, r);
	TEST_CHECK_STR_EQ("  001, 0002", out);
}

static void test_ignore_inserts(void) {
	DWORD flags = FROM_STR | FORMAT_MESSAGE_IGNORE_INSERTS;
	expect(flags, "test", NULL, "test");
	expect(flags, "test%0", NULL, "test");
	expect(flags, "test%0test", NULL, "test");
	expect(flags, "test%1%2!*.*s!%99", NULL, "test%1%2!*.*s!%99");
	expect(flags, "%%% %.%!", NULL, "%%% %.%!");
	expect(flags, "%n%r%t", NULL, "\r\n\r\t");
	expect(flags, "hi\n", NULL, "hi\r\n");
	expect(flags, "hi\r\n", NULL, "hi\r\n");
	expect(flags, "\r", NULL, "\r\n");
	expect(flags, "\r\r\n", NULL, "\r\n\r\n");

	flags |= FORMAT_MESSAGE_MAX_WIDTH_MASK;
	expect(flags, "hi\n", NULL, "hi ");
	expect(flags, "\r\r\n", NULL, "  ");
}

static void expect_error_untouched(DWORD flags, const char *src, DWORD expectedError) {
	char out[16];
	memset(out, 'x', sizeof(out));
	SetLastError(0xdeadbeef);
	DWORD r = FormatMessageA(flags, src, 0, 0, out, sizeof(out), NULL);
	TEST_CHECK_MSG(r == 0, "src=[%s] expected r=0 got r=%u", src, (unsigned)r);
	TEST_CHECK_MSG(GetLastError() == expectedError, "src=[%s] expected err=%u got err=%u", src, (unsigned)expectedError,
				   (unsigned)GetLastError());
	for (int i = 0; i < (int)sizeof(out); i++) {
		TEST_CHECK_MSG(out[i] == 'x', "src=[%s] buffer modified at %d", src, i);
	}
}

static void expect_empty_untouched(DWORD flags, const char *src) {
	char out[16];
	memset(out, 'x', sizeof(out));
	DWORD r = FormatMessageA(flags, src, 0, 0, out, sizeof(out), NULL);
	TEST_CHECK_MSG(r == 0, "src=[%s] expected r=0 got r=%u", src, (unsigned)r);
	for (int i = 0; i < (int)sizeof(out); i++) {
		TEST_CHECK_MSG(out[i] == 'x', "src=[%s] buffer modified at %d", src, i);
	}
}

static void test_errors(void) {
	expect_empty_untouched(FROM_STR, "");
	expect_error_untouched(FROM_STR, "%", ERROR_INVALID_PARAMETER);
	expect_error_untouched(FROM_STR, "test%", ERROR_INVALID_PARAMETER);
	expect_error_untouched(FROM_STR, "%1", ERROR_INVALID_PARAMETER);
	expect_error_untouched(FROM_STR_ARR, "%1", ERROR_INVALID_PARAMETER);
	expect_empty_untouched(FROM_STR, "%0test");

	/* no source flag -> invalid parameter */
	expect_error_untouched(0, "test", ERROR_INVALID_PARAMETER);

	/* insufficient buffer */
	char out[4];
	memset(out, 'x', sizeof(out));
	SetLastError(0xdeadbeef);
	DWORD r = FormatMessageA(FROM_STR, "hello", 0, 0, out, sizeof(out), NULL);
	TEST_CHECK_EQ(0, r);
	TEST_CHECK_EQ(ERROR_INSUFFICIENT_BUFFER, GetLastError());
}

static void test_allocate_buffer(void) {
	char *buf = NULL;
	DWORD r = FormatMessageA(FROM_STR | FORMAT_MESSAGE_ALLOCATE_BUFFER, "hello world", 0, 0, (LPSTR)&buf, 0, NULL);
	TEST_CHECK_EQ(11, r);
	TEST_CHECK(buf != NULL);
	TEST_CHECK_STR_EQ("hello world", buf);
	LocalFree((HLOCAL)buf);

	ULONG_PTR args[] = {(ULONG_PTR) "planet"};
	buf = NULL;
	r = FormatMessageA(FROM_STR_ARR | FORMAT_MESSAGE_ALLOCATE_BUFFER, "hello %1!s!", 0, 0, (LPSTR)&buf, 0,
					   (va_list *)args);
	TEST_CHECK_EQ(12, r);
	TEST_CHECK(buf != NULL);
	TEST_CHECK_STR_EQ("hello planet", buf);
	LocalFree((HLOCAL)buf);
}

static void test_from_system(void) {
	char out[256];
	memset(out, 0, sizeof(out));
	/* wibo has no message tables, so the text differs from Windows; only assert
	 * the text-agnostic invariants that must hold on any platform. */
	DWORD r =
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, 2, 0, out, sizeof(out), NULL);
	TEST_CHECK(r > 0);
	TEST_CHECK_EQ('\0', out[r]);
	TEST_CHECK_EQ((int)r, (int)strlen(out));
}

int main(void) {
	test_basic();
	test_escapes();
	test_max_width();
	test_inserts_array();
	test_inserts_valist();
	test_ignore_inserts();
	test_errors();
	test_allocate_buffer();
	test_from_system();
	return 0;
}
