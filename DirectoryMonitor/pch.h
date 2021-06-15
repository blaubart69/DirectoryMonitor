#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shlwapi.h>
#include <cstdio>

#include <string>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <functional>
#include <atomic>

#include "LastError.h"
#include "util.h"
#include "WriteStdout.h"
#include "DirectoryMonitor.h"

