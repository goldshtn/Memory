#include <Windows.h>

#include <map>
#include <string>
#include <iostream>

using AllocatorFunc = void*(*)(size_t);

void* reserve(size_t size)
{
	return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
}

void* commit(size_t size)
{
	return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void touch(void* mem, size_t size)
{
	size_t const page_size = 4096;

	for (
		char* p = reinterpret_cast<char*>(mem);
		p < reinterpret_cast<char*>(mem) + size;
		p += page_size
		)
	{
		*p = 'a';
	}
}

void* commit_touch(size_t size)
{
	void* mem = commit(size);
	touch(mem, size);
	return mem;
}

void* unusable(size_t size)
{
	size_t const page_size = 4096;

	// VirtualAlloc guarantees that allocations are 64KB-aligned. To "leak" 60KB
	// of unusable memory, we need to allocate one page.

	size_t num_blocks = (size / 1024) / 60;
	for (size_t i = 0; i < num_blocks; ++i)
		VirtualAlloc(nullptr, page_size, MEM_RESERVE, PAGE_NOACCESS);

	return reinterpret_cast<void*>(1);
}

void* shareable(size_t size)
{
	DWORD size_high = static_cast<DWORD>((size & 0xffffffff00000000) >> 32);
	DWORD size_low = static_cast<DWORD>(size & 0x00000000ffffffff);

	auto mapping = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
		size_high, size_low, L"ShareableSection");

	return MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, size);
}

void* shareable_touch(size_t size)
{
	void* mem = shareable(size);
	touch(mem, size);
	return mem;
}

void* pool(size_t size)
{
	std::cerr << "*** pool allocator is not currently implemented\n";
	return nullptr;
}

void* nppool(size_t size)
{
	HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	HANDLE file = CreateFile(L"Memory.exe", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
	CreateIoCompletionPort(file, iocp, 0, 0);

	char buffer[100];
	OVERLAPPED ovl = { 0 };
	DWORD read;
	for (size_t i = 0; i < size; ++i)
	{
		// Each read queues a packet to the I/O completion port. These packets are
		// not retrieved by any threads, which creates an IRP leak (non-paged pool).
		ReadFile(file, buffer, ARRAYSIZE(buffer), &read, &ovl);
	}

	return reinterpret_cast<void*>(1);
}

int main(int argc, char* argv[])
{
	std::map<std::string, AllocatorFunc> allocators = {
		{ "reserve", reserve },
		{ "commit", commit },
		{ "commit_touch", commit_touch },
		{ "unusable", unusable },
		{ "shareable", shareable },
		{ "shareable_touch", shareable_touch },
		{ "pool", pool },
		{ "nppool", nppool }
	};

	size_t size;
	if (argc < 3 || allocators.count(argv[1]) == 0 || (size = static_cast<size_t>(atoll(argv[2]))) == 0)
	{
		std::cerr <<
			"\n"
			"Usage: Memory <option> <amount>\n"
			"\n"
			"  option -- reserve, commit, commit_touch, unusable,\n"
			"            shareable, shareable_touch, pool, nppool\n"
			"  amount -- megabytes to allocate for all options except\n"
			"            pool and nppool; number of kernel objects to\n"
			"            allocate for pool and nppool\n";
		return 1;
	}
	
	std::string allocator(argv[1]);
	if (allocator != "pool" && allocator != "nppool")
		size *= 1024 * 1024;

	std::cout << "Press ENTER to start\n";
	std::cin.get();

	void* mem = allocators[allocator](size);
	if (mem == nullptr)
		std::cerr << "*** Allocation failed\n";
	else
		std::cout << "Allocated at address " << std::hex << mem << '\n';

	std::cout << "Press ENTER to exit\n";
	std::cin.get();

	return 0;
}