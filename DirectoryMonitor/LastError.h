#pragma once

class LastError
{
private:

	DWORD   _rc = 0;
	LPCWSTR _func = nullptr;

public:

	LastError() : _rc(0), _func(nullptr) {}

	bool failed() const { return _rc != 0; }

	void set(LPCWSTR errFunc)
	{
		_rc = ::GetLastError();
		_func = errFunc;
	}
	void set(LPCWSTR errFunc, DWORD selfSetLastError)
	{
		_rc = selfSetLastError;
		_func = errFunc;
	}

	DWORD code(void) { return _rc; }
	LPCWSTR func(void) { return _func == nullptr ? L"n/a" : _func; }
	void print()
	{
		fwprintf(stderr, L"E %d\t%s\n", _rc, func() );
	}


};

