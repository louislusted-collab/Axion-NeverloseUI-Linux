#include "itrace.h"

#ifdef __linux__

#include "../../utilities/memory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

namespace
{
using NativeTraceFn = bool(*)(void*, void*, const Vector_t*, const Vector_t*, void*, void*);
using EntityHandleFn = std::uint32_t(*)(C_BaseEntity*);
using CollisionIdFn = std::uint16_t(*)(C_BaseEntity*);

struct NativeTraceAbi
{
	std::uint8_t* base = nullptr;
	NativeTraceFn trace = nullptr;
	EntityHandleFn entityHandle = nullptr;
	CollisionIdFn collisionId = nullptr;
	void* filterVtable = nullptr;
	void** managerStorage = nullptr;
	void* validatedManager = nullptr;
	void* validatedPhysicsQuery = nullptr;
	bool resolved = false;
};

NativeTraceAbi nativeAbi{};
std::once_flag resolveOnce;
std::atomic<bool> nativeSelfTestPassed{false};
std::atomic_flag nativeSelfTestRunning = ATOMIC_FLAG_INIT;
std::atomic<std::int64_t> nextNativeSelfTestMs{0};
std::atomic<std::uint64_t> rejectedNativeTraceCalls{0};

void TraceLog(const char* format, ...)
{
	if (FILE* output = std::fopen("/tmp/cs2_trace_debug.log", "a"))
	{
		va_list args;
		va_start(args, format);
		std::vfprintf(output, format, args);
		va_end(args);
		std::fputc('\n', output);
		std::fclose(output);
	}
}

bool AddressHasPermissions(const void* address, std::size_t size, char permission)
{
	if (address == nullptr || size == 0)
		return false;
	const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(address);
	if (begin + size < begin)
		return false;
	FILE* maps = std::fopen("/proc/self/maps", "r");
	if (maps == nullptr)
		return false;
	char line[512];
	bool valid = false;
	while (std::fgets(line, sizeof(line), maps) != nullptr)
	{
		unsigned long long mapBegin = 0, mapEnd = 0;
		char permissions[5]{};
		if (std::sscanf(line, "%llx-%llx %4s", &mapBegin, &mapEnd, permissions) != 3)
			continue;
		if (begin >= mapBegin && begin + size <= mapEnd &&
			std::strchr(permissions, permission) != nullptr)
		{
			valid = true;
			break;
		}
	}
	std::fclose(maps);
	return valid;
}

bool SafeAddressRead(const void* address, void* output, std::size_t size)
{
	if (address == nullptr || output == nullptr || size == 0)
		return false;
	iovec local{output, size};
	iovec remote{const_cast<void*>(address), size};
	const long read = ::syscall(SYS_process_vm_readv, ::getpid(), &local, 1UL,
		&remote, 1UL, 0UL);
	return read == static_cast<long>(size);
}

bool MatchBytes(const std::uint8_t* address, const std::uint8_t* bytes, std::size_t size)
{
	return AddressHasPermissions(address, size, 'r') && std::memcmp(address, bytes, size) == 0;
}

std::uint8_t* ResolveLeaTarget(std::uint8_t* instruction)
{
	if (instruction == nullptr || instruction[0] != 0x48 || instruction[1] != 0x8D ||
		instruction[2] != 0x05)
		return nullptr;
	std::int32_t displacement = 0;
	std::memcpy(&displacement, instruction + 3, sizeof(displacement));
	return instruction + 7 + displacement;
}

void ResolveNativeAbi()
{
	nativeAbi.base = static_cast<std::uint8_t*>(MEM::GetModuleBaseHandle(CLIENT_DLL));
	if (nativeAbi.base == nullptr)
	{
		TraceLog("[trace] gate CLOSED: libclient base missing");
		return;
	}

	// Build dc8a23833b40d5eced92e34487dc9061a72d6de7. All offsets and
	// independent call-site references must agree before any native call.
	auto* traceAddress = nativeAbi.base + 0x169F200;
	static constexpr std::uint8_t tracePrologue[]{
		0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x48, 0x89, 0xCF,
		0x41, 0x56, 0x49, 0x89, 0xF6, 0x41, 0x55, 0x4D, 0x89,
		0xC5, 0x41, 0x54, 0x49, 0x89, 0xD4, 0x53, 0x4C, 0x89, 0xCB};
	if (!MatchBytes(traceAddress, tracePrologue, sizeof(tracePrologue)) ||
		MEM::FindPattern(CLIENT_DLL,
			"55 48 89 E5 41 57 48 89 CF 41 56 49 89 F6 41 55 4D 89 C5 41 54 49 89 D4 53 4C 89 CB 48 81 EC B8 24 00 00") != traceAddress)
	{
		TraceLog("[trace] gate CLOSED: trace wrapper mismatch base=%p expected=%p",
			nativeAbi.base, traceAddress);
		return;
	}

	auto* entityHandleAddress = nativeAbi.base + 0x169DA20;
	auto* collisionIdAddress = nativeAbi.base + 0x169DA80;
	static constexpr std::uint8_t entityHandlePrologue[]{
		0x48, 0x85, 0xFF, 0x74, 0x3B, 0x48, 0x8B, 0x57, 0x10,
		0xB8, 0xFF, 0xFF, 0xFF, 0xFF};
	static constexpr std::uint8_t collisionIdPrologue[]{
		0x48, 0x85, 0xFF, 0x74, 0x4B, 0x55, 0x48, 0x89, 0xE5,
		0x41, 0x54};
	if (!MatchBytes(entityHandleAddress, entityHandlePrologue, sizeof(entityHandlePrologue)) ||
		!MatchBytes(collisionIdAddress, collisionIdPrologue, sizeof(collisionIdPrologue)))
	{
		TraceLog("[trace] gate CLOSED: filter helper mismatch");
		return;
	}

	auto* managerReference = nativeAbi.base + 0xF110FF;
	auto* vtableReference = nativeAbi.base + 0xF110A1;
	auto* managerStorage = ResolveLeaTarget(managerReference);
	auto* filterVtable = ResolveLeaTarget(vtableReference);
	if (managerStorage != nativeAbi.base + 0x455EBF0 ||
		filterVtable != nativeAbi.base + 0x42C0F78 ||
		!AddressHasPermissions(managerStorage, sizeof(void*), 'r') ||
		!AddressHasPermissions(filterVtable, sizeof(void*) * 2, 'r'))
	{
		TraceLog("[trace] gate CLOSED: manager/filter references mismatch manager=%p vtable=%p",
			managerStorage, filterVtable);
		return;
	}
	void* firstFilterFunction = nullptr;
	std::memcpy(&firstFilterFunction, filterVtable, sizeof(firstFilterFunction));
	if (!AddressHasPermissions(firstFilterFunction, 1, 'x'))
	{
		TraceLog("[trace] gate CLOSED: filter vtable is not executable");
		return;
	}

	nativeAbi.trace = reinterpret_cast<NativeTraceFn>(traceAddress);
	nativeAbi.entityHandle = reinterpret_cast<EntityHandleFn>(entityHandleAddress);
	nativeAbi.collisionId = reinterpret_cast<CollisionIdFn>(collisionIdAddress);
	nativeAbi.filterVtable = filterVtable;
	nativeAbi.managerStorage = reinterpret_cast<void**>(managerStorage);
	nativeAbi.resolved = true;
	TraceLog("[trace] ABI resolved wrapper=%p manager_storage=%p filter_vtable=%p",
		traceAddress, managerStorage, filterVtable);
}

bool VectorFinite(const Vector_t& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void* GetValidatedManager()
{
	void* manager = nullptr;
	std::memcpy(&manager, nativeAbi.managerStorage, sizeof(manager));
	if (manager == nativeAbi.validatedManager &&
		nativeAbi.validatedPhysicsQuery != nullptr)
		return manager;
	if (!AddressHasPermissions(manager, sizeof(void*), 'r'))
		return nullptr;
	void* physicsQuery = nullptr;
	std::memcpy(&physicsQuery, manager, sizeof(physicsQuery));
	if (!AddressHasPermissions(physicsQuery, sizeof(void*), 'r'))
		return nullptr;
	nativeAbi.validatedManager = manager;
	nativeAbi.validatedPhysicsQuery = physicsQuery;
	TraceLog("[trace] manager validated manager=%p physics_query=%p", manager, physicsQuery);
	return manager;
}

bool ExecuteNativeTrace(const Vector_t& start, const Vector_t& end,
	const Vector_t& mins, const Vector_t& maxs, bool hull, C_BaseEntity* skip,
	std::uint64_t mask, TRACE::NativeResult& result)
{
	result = {};
	result.start = start;
	result.end = end;
	if (!nativeAbi.resolved || nativeAbi.managerStorage == nullptr ||
		!VectorFinite(start) || !VectorFinite(end) || !VectorFinite(mins) ||
		!VectorFinite(maxs) || mask == 0)
		return false;

	void* manager = GetValidatedManager();
	if (manager == nullptr)
		return false;

	alignas(16) std::array<std::byte, 0x30> ray{};
	if (hull)
	{
		std::memcpy(ray.data(), &mins, sizeof(Vector_t));
		std::memcpy(ray.data() + 0x0C, &maxs, sizeof(Vector_t));
		ray[0x28] = std::byte{2};
	}

	alignas(16) std::array<std::byte, 0x40> filter{};
	std::memcpy(filter.data(), &nativeAbi.filterVtable, sizeof(nativeAbi.filterVtable));
	std::memcpy(filter.data() + 0x08, &mask, sizeof(mask));
	std::memset(filter.data() + 0x20, 0xFF, 0x10);
	const std::uint64_t collisionDefaults = 0x0100FFFF00000000ULL;
	std::memcpy(filter.data() + 0x30, &collisionDefaults, sizeof(collisionDefaults));
	const std::uint16_t filterTail = 0x4900;
	std::memcpy(filter.data() + 0x38, &filterTail, sizeof(filterTail));
	filter[0x37] = std::byte{0x0F};
	filter[0x38] = std::byte{0x03};
	filter[0x3A] = std::byte{0};
	const std::uint32_t skipHandle = nativeAbi.entityHandle(skip);
	const std::uint16_t collisionId = nativeAbi.collisionId(skip);
	std::memcpy(filter.data() + 0x20, &skipHandle, sizeof(skipHandle));
	std::memcpy(filter.data() + 0x30, &collisionId, sizeof(collisionId));

	alignas(16) std::array<std::byte, 0xC0> nativeResult{};
	const bool wrapperHit = nativeAbi.trace(manager, ray.data(), &start, &end,
		filter.data(), nativeResult.data());
	std::memcpy(&result.surface, nativeResult.data(), sizeof(result.surface));
	std::memcpy(&result.entity, nativeResult.data() + 0x08, sizeof(result.entity));
	void* hitboxData = nullptr;
	std::memcpy(&hitboxData, nativeResult.data() + 0x10, sizeof(hitboxData));
	if (hitboxData != nullptr)
	{
		int hitGroup = -1;
		if (SafeAddressRead(static_cast<const std::byte*>(hitboxData) + 0x38,
				&hitGroup, sizeof(hitGroup)) && hitGroup >= 0 && hitGroup <= 10)
			result.hitGroup = hitGroup;
	}
	// The current build's surface-property record is 0x20 bytes. Its loader at
	// libclient+0x1682200 writes bulletPenetrationDistanceModifier to +0x08,
	// bulletPenetrationDamageModifier to +0x0C and gamematerial to +0x14.
	// Keep these values independently range-gated so a changed trace result or
	// surface layout closes ballistics without affecting ordinary visibility.
	std::array<std::byte, 0x16> surfaceRecord{};
	if (SafeAddressRead(result.surface, surfaceRecord.data(), surfaceRecord.size()))
	{
		std::memcpy(&result.penetrationModifier,
			surfaceRecord.data() + 0x08,
			sizeof(result.penetrationModifier));
		std::memcpy(&result.damageModifier,
			surfaceRecord.data() + 0x0C,
			sizeof(result.damageModifier));
		std::memcpy(&result.material,
			surfaceRecord.data() + 0x14,
			sizeof(result.material));
		result.surfaceValid = std::isfinite(result.penetrationModifier) &&
			std::isfinite(result.damageModifier) &&
			result.penetrationModifier >= 0.05f && result.penetrationModifier <= 10.f &&
			result.damageModifier >= 0.f && result.damageModifier <= 2.f;
		if (!result.surfaceValid)
		{
			result.penetrationModifier = 0.f;
			result.damageModifier = 0.f;
			result.material = 0;
		}
	}
	std::array<float, 3> vectorBytes{};
	std::memcpy(vectorBytes.data(), nativeResult.data() + 0x78, sizeof(vectorBytes));
	result.start = Vector_t(vectorBytes.data());
	std::memcpy(vectorBytes.data(), nativeResult.data() + 0x84, sizeof(vectorBytes));
	result.end = Vector_t(vectorBytes.data());
	std::memcpy(vectorBytes.data(), nativeResult.data() + 0x90, sizeof(vectorBytes));
	result.normal = Vector_t(vectorBytes.data());
	std::memcpy(&result.fraction, nativeResult.data() + 0xAC, sizeof(result.fraction));
	result.allSolid = nativeResult[0xBB] != std::byte{0};
	if (!VectorFinite(result.start) || !VectorFinite(result.end) ||
		!VectorFinite(result.normal) || !std::isfinite(result.fraction) ||
		result.fraction < 0.0f || result.fraction > 1.0001f)
	{
		TraceLog("[trace] invalid result fraction=%f wrapper_hit=%d", result.fraction, wrapperHit);
		result = {};
		return false;
	}
	result.fraction = std::clamp(result.fraction, 0.0f, 1.0f);
	result.hit = wrapperHit || result.allSolid || result.fraction < 0.9999f;
	return true;
}
}

bool TRACE::NativeReady()
{
	std::call_once(resolveOnce, ResolveNativeAbi);
	if (!nativeAbi.resolved)
		return false;
	if (nativeSelfTestPassed.load(std::memory_order_acquire))
		return true;
	const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	if (now < nextNativeSelfTestMs.load(std::memory_order_relaxed))
		return false;
	if (nativeSelfTestRunning.test_and_set(std::memory_order_acquire))
		return false;
	NativeResult selfTest{};
	const Vector_t zero{};
	const bool passed = ExecuteNativeTrace(zero, zero, zero, zero, false, nullptr,
		kNativeSolidMask, selfTest);
	if (passed)
	{
		nativeSelfTestPassed.store(true, std::memory_order_release);
		TraceLog("[trace] self-test PASSED fraction=%.4f hit=%d solid=%d",
			selfTest.fraction, selfTest.hit, selfTest.allSolid);
	}
	else
	{
		// Loading screens can temporarily expose the manager before its physics
		// query is usable. Retry at a low rate instead of invoking the native ABI
		// once per rendered frame while the gate remains closed.
		nextNativeSelfTestMs.store(now + 2000, std::memory_order_relaxed);
	}
	nativeSelfTestRunning.clear(std::memory_order_release);
	return passed;
}

bool TRACE::NativeLine(const Vector_t& start, const Vector_t& end,
	C_BaseEntity* skip, NativeResult& result, std::uint64_t mask)
{
	if (!NativeReady())
	{
		RejectNativeCall("NativeLine");
		result = {};
		return false;
	}
	return ExecuteNativeTrace(start, end, {}, {}, false, skip, mask, result);
}

bool TRACE::NativeHull(const Vector_t& start, const Vector_t& end,
	const Vector_t& mins, const Vector_t& maxs, C_BaseEntity* skip,
	NativeResult& result, std::uint64_t mask)
{
	if (!NativeReady())
	{
		RejectNativeCall("NativeHull");
		result = {};
		return false;
	}
	return ExecuteNativeTrace(start, end, mins, maxs, true, skip, mask, result);
}

void TRACE::RejectNativeCall(const char* operation)
{
	const std::uint64_t count = rejectedNativeTraceCalls.fetch_add(1, std::memory_order_relaxed) + 1;
	static std::atomic_flag logged = ATOMIC_FLAG_INIT;
	if (logged.test_and_set(std::memory_order_relaxed))
		return;
	TraceLog("[trace] native trace gate CLOSED operation=%s rejected=%llu",
		operation != nullptr ? operation : "unknown",
		static_cast<unsigned long long>(count));
}

std::uint64_t TRACE::RejectedNativeCalls()
{
	return rejectedNativeTraceCalls.load(std::memory_order_relaxed);
}

#else

bool TRACE::NativeReady()
{
	return true;
}

bool TRACE::NativeLine(const Vector_t&, const Vector_t&, C_BaseEntity*, NativeResult&, std::uint64_t)
{
	return false;
}

bool TRACE::NativeHull(const Vector_t&, const Vector_t&, const Vector_t&, const Vector_t&,
	C_BaseEntity*, NativeResult&, std::uint64_t)
{
	return false;
}

void TRACE::RejectNativeCall(const char*)
{
}

std::uint64_t TRACE::RejectedNativeCalls()
{
	return 0;
}

#endif
