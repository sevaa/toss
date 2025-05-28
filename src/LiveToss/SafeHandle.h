#pragma once

template<typename T, typename TClose>
class SafePtrHandle
{
	T h;
public:
	SafePtrHandle()
		:h(nullptr)
	{}

	SafePtrHandle(T h_)
		:h(h_)
	{}

	SafePtrHandle(const SafePtrHandle<T, TClose>&& that)
		:h(that.h)
	{
		that.h = nullptr;
	}

	~SafePtrHandle()
	{
		if (h != nullptr)
			TClose::close(h);
	}

	operator T() const
	{
		return h;
	}

	SafePtrHandle & operator=(T h_)
	{
		if (h != nullptr)
			TClose::close(h);
		h = h_;
		return *this;
	}

	explicit operator bool()
	{
		return h != nullptr;
	}

	bool operator !()
	{
		return h == nullptr;
	}

	T* operator &()
	{
		return &h;
	}

	void close()
	{
		if (h != nullptr)
			TClose::close(h);
		h = nullptr;
	}

private:
	SafePtrHandle(const SafePtrHandle &);
	SafePtrHandle &operator=(const SafePtrHandle &);
};

#ifdef _WINHTTPX_
struct CloseHINTERNET
{
	static void close(HINTERNET h) { WinHttpCloseHandle(h); }
};
typedef SafePtrHandle<HINTERNET, CloseHINTERNET> SafeInetHandle;
#endif

#ifdef _FILE_DEFINED
struct CloseFILE
{
	static void close(FILE *f) { fclose(f); }
};
typedef SafePtrHandle<FILE*, CloseFILE> SafeFILE;
#endif

struct CloseRegKey
{
	static void close(HKEY hk) { RegCloseKey(hk); }
};
typedef SafePtrHandle<HKEY, CloseRegKey> SafeRegKey;

struct CloseHANDLE
{
	static void close(HANDLE h) { CloseHandle(h); }
};
typedef SafePtrHandle<HANDLE, CloseHANDLE> SafeWin32Handle;

struct CloseMappedView
{
	static void close(void *p) { UnmapViewOfFile(p); }
};

//Proxy for combase
#ifdef STDMETHOD

struct FreeCoMemory
{
	static void close(void *p) { CoTaskMemFree(p);	}
};

template<typename T>
struct SafeCoMemory: public SafePtrHandle<T, FreeCoMemory>
{
public:
	SafeCoMemory()
		:SafePtrHandle<T, FreeCoMemory>()
	{
	}
private:
	SafeCoMemory(const SafeCoMemory<T>&);
	SafeCoMemory& operator=(const SafeCoMemory<T>&);
};

#endif