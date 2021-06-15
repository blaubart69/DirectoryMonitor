LastError* EnumDirRecurse(std::wstring* dir, WIN32_FIND_DATA* findData, const std::function<void(const std::wstring& fullEntryname, WIN32_FIND_DATA* findData)>& onFileEntry, LastError* err);
BOOL TryToSetPrivilege(LPCWSTR szPrivilege, BOOL bEnablePrivilege);
