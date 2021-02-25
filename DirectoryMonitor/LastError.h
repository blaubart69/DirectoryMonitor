#pragma once

class LastError
{
private:

	DWORD   _rc = 0;
	LPCWSTR _func = nullptr;
	std::wstring _param;

public:

	LastError() : _rc(0), _func(nullptr) {}
	LastError(LPCWSTR errFunc) 
	{
		this->set(errFunc);
	}

	bool failed() const { return _rc != 0; }

	LastError* set(LPCWSTR errFunc)
	{
		_rc = ::GetLastError();
		_func = errFunc;
		return this;
	}
	LastError* set(LPCWSTR errFunc, DWORD selfSetLastError)
	{
		_rc = selfSetLastError;
		_func = errFunc;
		return this;
	}
	LastError* set(LPCWSTR errFunc, const std::wstring& funcParam)
	{
		this->set(errFunc);
		this->_param.assign(funcParam);
		return this;
	}

	DWORD code(void) { return _rc; }
	//LPCWSTR func(void) { return _func == nullptr ? L"n/a" : _func; }
	void print()
	{
		fwprintf(stderr, L"E %d\t%s\t%s\n", 
			_rc
			, _func == nullptr  ? L"" : _func
			, _param.c_str());
	}


};

