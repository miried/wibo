#include "sysinfoapi.h"

#include "common.h"
#include "context.h"
#include "errors.h"
#include "internal.h"
#include "ntdll.h"
#include "timeutil.h"

#include <cstring>
#include <ctime>
#include <sys/time.h>

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace {

constexpr WORD PROCESSOR_ARCHITECTURE_INTEL = 0;
constexpr DWORD PROCESSOR_INTEL_PENTIUM = 586;

constexpr uint64_t kUnixTimeZero = 11644473600ULL * 10000000ULL;
constexpr DWORD kMajorVersion = 6;
constexpr DWORD kMinorVersion = 2;
constexpr DWORD kBuildNumber = 0;
constexpr uint64_t kVirtualAddressSpaceSize = 0x80000000ULL;

DWORD_PTR computeSystemProcessorMask(unsigned int cpuCount) {
	const auto maskWidth = static_cast<unsigned int>(sizeof(DWORD_PTR) * 8);
	if (cpuCount >= maskWidth) {
		return static_cast<DWORD_PTR>(~static_cast<DWORD_PTR>(0));
	}
	DWORD_PTR mask = (static_cast<DWORD_PTR>(1) << cpuCount) - 1;
	return mask == 0 ? 1 : mask;
}

struct HostMemoryInfo {
	uint64_t totalPhys = 0;
	uint64_t availPhys = 0;
	uint64_t totalSwap = 0;
	uint64_t availSwap = 0;
};

uint64_t clampToGuestSize(uint64_t value) {
	constexpr uint64_t kGuestMax = 0xFFFFFFFFULL;
	return value > kGuestMax ? kGuestMax : value;
}

uint64_t minU64(uint64_t a, uint64_t b) { return a < b ? a : b; }

HostMemoryInfo queryHostMemoryInfo() {
	HostMemoryInfo info{};

#ifdef __linux__
	struct sysinfo si {};
	if (sysinfo(&si) == 0) {
		const uint64_t unit = si.mem_unit ? static_cast<uint64_t>(si.mem_unit) : 1ULL;
		info.totalPhys = static_cast<uint64_t>(si.totalram) * unit;
		info.availPhys = static_cast<uint64_t>(si.freeram) * unit;
		info.totalSwap = static_cast<uint64_t>(si.totalswap) * unit;
		info.availSwap = static_cast<uint64_t>(si.freeswap) * unit;
	}
#endif

#ifdef __APPLE__
	uint64_t memsize = 0;
	size_t memsizeLen = sizeof(memsize);
	if (sysctlbyname("hw.memsize", &memsize, &memsizeLen, nullptr, 0) == 0) {
		info.totalPhys = memsize;
	}

	vm_size_t pageSize = 0;
	if (host_page_size(mach_host_self(), &pageSize) == KERN_SUCCESS) {
		vm_statistics64_data_t vmstat{};
		mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
		if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vmstat),
							  &count) == KERN_SUCCESS) {
			uint64_t freePages = static_cast<uint64_t>(vmstat.free_count) + static_cast<uint64_t>(vmstat.inactive_count);
			info.availPhys = freePages * static_cast<uint64_t>(pageSize);
		}
	}

	xsw_usage swap{};
	size_t swapLen = sizeof(swap);
	if (sysctlbyname("vm.swapusage", &swap, &swapLen, nullptr, 0) == 0) {
		info.totalSwap = swap.xsu_total;
		info.availSwap = swap.xsu_total - swap.xsu_used;
	}
#endif

	if (info.totalPhys == 0) {
		long pages = sysconf(_SC_PHYS_PAGES);
		long pageSize = sysconf(_SC_PAGE_SIZE);
		if (pages > 0 && pageSize > 0) {
			info.totalPhys = static_cast<uint64_t>(pages) * static_cast<uint64_t>(pageSize);
		}
	}

	if (info.availPhys == 0) {
#ifdef _SC_AVPHYS_PAGES
		long availPages = sysconf(_SC_AVPHYS_PAGES);
		long pageSize = sysconf(_SC_PAGE_SIZE);
		if (availPages > 0 && pageSize > 0) {
			info.availPhys = static_cast<uint64_t>(availPages) * static_cast<uint64_t>(pageSize);
		}
#endif
		if (info.availPhys == 0 && info.totalPhys != 0) {
			info.availPhys = info.totalPhys / 2;
		}
	}

	if (info.totalSwap == 0 && info.availSwap != 0) {
		info.totalSwap = info.availSwap;
	}
	if (info.availSwap == 0 && info.totalSwap != 0) {
		info.availSwap = info.totalSwap / 2;
	}

	info.availPhys = minU64(info.availPhys, info.totalPhys);
	info.availSwap = minU64(info.availSwap, info.totalSwap);
	return info;
}

} // namespace

namespace kernel32 {

void WINAPI GetSystemInfo(LPSYSTEM_INFO lpSystemInfo) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("GetSystemInfo(%p)\n", lpSystemInfo);
	if (!lpSystemInfo) {
		return;
	}

	std::memset(lpSystemInfo, 0, sizeof(*lpSystemInfo));
	lpSystemInfo->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
	lpSystemInfo->dwOemId = lpSystemInfo->wProcessorArchitecture;
	lpSystemInfo->dwProcessorType = PROCESSOR_INTEL_PENTIUM;
	lpSystemInfo->wProcessorLevel = 6; // Pentium

	long pageSize = sysconf(_SC_PAGESIZE);
	if (pageSize <= 0) {
		pageSize = 4096;
	}
	lpSystemInfo->dwPageSize = static_cast<DWORD>(pageSize);

	lpSystemInfo->lpMinimumApplicationAddress = toGuestPtr(reinterpret_cast<void *>(0x00010000));
#ifdef _WIN64
	lpSystemInfo->lpMaximumApplicationAddress = toGuestPtr(reinterpret_cast<void *>(0x00007FFFFFFEFFFFull));
#else
	lpSystemInfo->lpMaximumApplicationAddress = toGuestPtr(reinterpret_cast<void *>(0x7FFEFFFF));
#endif

	unsigned int cpuCount = 1;
	long reported = sysconf(_SC_NPROCESSORS_ONLN);
	if (reported > 0) {
		cpuCount = static_cast<unsigned int>(reported);
	}
	lpSystemInfo->dwNumberOfProcessors = cpuCount;
	lpSystemInfo->dwActiveProcessorMask = computeSystemProcessorMask(cpuCount);

	lpSystemInfo->dwAllocationGranularity = 0x10000;
}

void WINAPI GetSystemTime(LPSYSTEMTIME lpSystemTime) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("GetSystemTime(%p)\n", lpSystemTime);
	if (!lpSystemTime) {
		return;
	}

	time_t now = time(nullptr);
	struct tm tmUtc{};
#if defined(_GNU_SOURCE) || defined(__APPLE__)
	gmtime_r(&now, &tmUtc);
#else
	struct tm *tmp = gmtime(&now);
	if (!tmp) {
		return;
	}
	tmUtc = *tmp;
#endif

	lpSystemTime->wYear = static_cast<WORD>(tmUtc.tm_year + 1900);
	lpSystemTime->wMonth = static_cast<WORD>(tmUtc.tm_mon + 1);
	lpSystemTime->wDayOfWeek = static_cast<WORD>(tmUtc.tm_wday);
	lpSystemTime->wDay = static_cast<WORD>(tmUtc.tm_mday);
	lpSystemTime->wHour = static_cast<WORD>(tmUtc.tm_hour);
	lpSystemTime->wMinute = static_cast<WORD>(tmUtc.tm_min);
	lpSystemTime->wSecond = static_cast<WORD>(tmUtc.tm_sec);
	lpSystemTime->wMilliseconds = 0;
}

void WINAPI GetLocalTime(LPSYSTEMTIME lpSystemTime) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("GetLocalTime(%p)\n", lpSystemTime);
	if (!lpSystemTime) {
		return;
	}

	time_t now = time(nullptr);
	struct tm tmLocal{};
#if defined(_GNU_SOURCE) || defined(__APPLE__)
	localtime_r(&now, &tmLocal);
#else
	struct tm *tmp = localtime(&now);
	if (!tmp) {
		return;
	}
	tmLocal = *tmp;
#endif

	lpSystemTime->wYear = static_cast<WORD>(tmLocal.tm_year + 1900);
	lpSystemTime->wMonth = static_cast<WORD>(tmLocal.tm_mon + 1);
	lpSystemTime->wDayOfWeek = static_cast<WORD>(tmLocal.tm_wday);
	lpSystemTime->wDay = static_cast<WORD>(tmLocal.tm_mday);
	lpSystemTime->wHour = static_cast<WORD>(tmLocal.tm_hour);
	lpSystemTime->wMinute = static_cast<WORD>(tmLocal.tm_min);
	lpSystemTime->wSecond = static_cast<WORD>(tmLocal.tm_sec);
	lpSystemTime->wMilliseconds = 0;
}

void WINAPI GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("GetSystemTimeAsFileTime(%p)\n", lpSystemTimeAsFileTime);
	if (!lpSystemTimeAsFileTime) {
		return;
	}

#if defined(CLOCK_REALTIME)
	struct timespec ts{};
	if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
		uint64_t ticks = kUnixTimeZero;
		ticks += static_cast<uint64_t>(ts.tv_sec) * 10000000ULL;
		ticks += static_cast<uint64_t>(ts.tv_nsec) / 100ULL;
		*lpSystemTimeAsFileTime = fileTimeFromDuration(ticks);
		return;
	}
#endif

	struct timeval tv{};
	if (gettimeofday(&tv, nullptr) == 0) {
		uint64_t ticks = kUnixTimeZero;
		ticks += static_cast<uint64_t>(tv.tv_sec) * 10000000ULL;
		ticks += static_cast<uint64_t>(tv.tv_usec) * 10ULL;
		*lpSystemTimeAsFileTime = fileTimeFromDuration(ticks);
		return;
	}

	const FILETIME fallback = {static_cast<DWORD>(kUnixTimeZero & 0xFFFFFFFFULL),
							   static_cast<DWORD>(kUnixTimeZero >> 32)};
	*lpSystemTimeAsFileTime = fallback;
}

DWORD WINAPI GetTickCount() {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("GetTickCount()\n");
#if defined(CLOCK_MONOTONIC)
	struct timespec ts{};
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		uint64_t milliseconds =
			static_cast<uint64_t>(ts.tv_sec) * 1000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
		DWORD result = static_cast<DWORD>(milliseconds & 0xFFFFFFFFULL);
		DEBUG_LOG(" -> %u\n", result);
		return result;
	}
#endif
	struct timeval tv{};
	if (gettimeofday(&tv, nullptr) == 0) {
		uint64_t milliseconds =
			static_cast<uint64_t>(tv.tv_sec) * 1000ULL + static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
		DWORD result = static_cast<DWORD>(milliseconds & 0xFFFFFFFFULL);
		DEBUG_LOG(" -> %u\n", result);
		return result;
	}
	DEBUG_LOG(" -> 0\n");
	return 0;
}

DWORD WINAPI GetVersion() {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("GetVersion()\n");
	return kMajorVersion | (kMinorVersion << 8) | (5 << 16) | (kBuildNumber << 24);
}

BOOL WINAPI GetVersionExA(LPOSVERSIONINFOA lpVersionInformation) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("GetVersionExA(%p)\n", lpVersionInformation);
	if (!lpVersionInformation) {
		setLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	DWORD size = lpVersionInformation->dwOSVersionInfoSize;
	if (size < sizeof(OSVERSIONINFOA)) {
		setLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	DWORD requestSize = (size >= sizeof(OSVERSIONINFOEXA)) ? sizeof(OSVERSIONINFOEXW) : sizeof(OSVERSIONINFOW);
	OSVERSIONINFOEXW wideInfo{};
	wideInfo.dwOSVersionInfoSize = requestSize;
	NTSTATUS status = ntdll::RtlGetVersion(reinterpret_cast<PRTL_OSVERSIONINFOW>(&wideInfo));
	if (status != STATUS_SUCCESS) {
		setLastError(wibo::winErrorFromNtStatus(status));
		return FALSE;
	}

	std::memset(lpVersionInformation, 0, size);
	lpVersionInformation->dwOSVersionInfoSize = size;
	lpVersionInformation->dwMajorVersion = wideInfo.dwMajorVersion;
	lpVersionInformation->dwMinorVersion = wideInfo.dwMinorVersion;
	lpVersionInformation->dwBuildNumber = wideInfo.dwBuildNumber;
	lpVersionInformation->dwPlatformId = wideInfo.dwPlatformId;

	if (size >= sizeof(OSVERSIONINFOEXA)) {
		auto extended = reinterpret_cast<OSVERSIONINFOEXA *>(lpVersionInformation);
		extended->wServicePackMajor = wideInfo.wServicePackMajor;
		extended->wServicePackMinor = wideInfo.wServicePackMinor;
		extended->wSuiteMask = wideInfo.wSuiteMask;
		extended->wProductType = wideInfo.wProductType;
		extended->wReserved = wideInfo.wReserved;
	}

	return TRUE;
}

BOOL WINAPI GetVersionExW(LPOSVERSIONINFOW lpVersionInformation) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("GetVersionExW(%p)\n", lpVersionInformation);
	if (!lpVersionInformation) {
		setLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	DWORD size = lpVersionInformation->dwOSVersionInfoSize;
	if (size < sizeof(OSVERSIONINFOW)) {
		setLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	NTSTATUS status = ntdll::RtlGetVersion(reinterpret_cast<PRTL_OSVERSIONINFOW>(lpVersionInformation));
	if (status != STATUS_SUCCESS) {
		setLastError(wibo::winErrorFromNtStatus(status));
		return FALSE;
	}

	lpVersionInformation->dwOSVersionInfoSize = size;
	return TRUE;
}

void WINAPI GlobalMemoryStatus(LPMEMORYSTATUS lpBuffer) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("GlobalMemoryStatus(%p)\n", lpBuffer);
	if (!lpBuffer) {
		return;
	}

	const HostMemoryInfo info = queryHostMemoryInfo();
	const uint64_t totalPhys = info.totalPhys;
	const uint64_t availPhys = minU64(info.availPhys, info.totalPhys);
	const uint64_t totalPageFile = info.totalPhys + info.totalSwap;
	const uint64_t availPageFile = info.availPhys + info.availSwap;

	DWORD memoryLoad = 0;
	if (totalPhys > 0) {
		const uint64_t used = totalPhys - availPhys;
		memoryLoad = static_cast<DWORD>((used * 100ULL) / totalPhys);
		if (memoryLoad > 100) {
			memoryLoad = 100;
		}
	}

	lpBuffer->dwLength = sizeof(*lpBuffer);
	lpBuffer->dwMemoryLoad = memoryLoad;
	lpBuffer->dwTotalPhys = static_cast<SIZE_T>(clampToGuestSize(totalPhys));
	lpBuffer->dwAvailPhys = static_cast<SIZE_T>(clampToGuestSize(availPhys));
	lpBuffer->dwTotalPageFile = static_cast<SIZE_T>(clampToGuestSize(totalPageFile));
	lpBuffer->dwAvailPageFile = static_cast<SIZE_T>(clampToGuestSize(availPageFile));
	lpBuffer->dwTotalVirtual = static_cast<SIZE_T>(clampToGuestSize(kVirtualAddressSpaceSize));
	lpBuffer->dwAvailVirtual = static_cast<SIZE_T>(clampToGuestSize(kVirtualAddressSpaceSize));
}

} // namespace kernel32
