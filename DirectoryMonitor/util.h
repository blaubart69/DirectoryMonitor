LastError* EnumDirRecurse(std::wstring* dir, WIN32_FIND_DATA* findData, const std::function<void(const std::wstring& fullEntryname, WIN32_FIND_DATA* findData)>& onFileEntry, LastError* err);
BOOL TryToSetPrivilege(LPCWSTR szPrivilege, BOOL bEnablePrivilege);
void walk_FILE_NOTIFY_INFORMATION(void* buf, size_t length, std::function<void(DWORD, std::wstring_view)> onEntry);
std::wstring FormatByteSize(size_t qdw);
void CloseHandle_mayBeNullOrInvalid(HANDLE h);
int localtime_as_str(WCHAR* buf, const size_t cchBuf);
bool ReadConsoleKey(HANDLE hStdin, WCHAR* key);