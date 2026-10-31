#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string_view>
#include <filesystem>
#include <memory>
#include <thread>
#include <chrono>

struct HandleDeleter {
    void operator()(HANDLE handle) const noexcept {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
        }
    }
};
using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

[[nodiscard]]
DWORD GetProcessIdByName(std::wstring_view processName) {
    UniqueHandle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W processEntry{};
    processEntry.dwSize = sizeof(processEntry);

    if (::Process32FirstW(snapshot.get(), &processEntry)) {
        do {
            if (::_wcsicmp(processEntry.szExeFile, processName.data()) == 0) {
                return processEntry.th32ProcessID;
            }
        } while (::Process32NextW(snapshot.get(), &processEntry));
    }

    return 0;
}

int main() {
    constexpr std::wstring_view targetProcess = L"...";
    const std::filesystem::path dllPath = R"(...)";

    if (!std::filesystem::exists(dllPath)) {
        std::cerr << "[Error] Target DLL not found at specified path.\n";
        return 1;
    }

    std::cout << "[Info] Waiting for " << std::filesystem::path(targetProcess).string() << "... ";

    DWORD processId = 0;
    while (processId == 0) {
        processId = GetProcessIdByName(targetProcess);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "Found! (PID: " << processId << ")\n";

    UniqueHandle processHandle(::OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId));
    if (!processHandle) {
        std::cerr << "[Error] Failed to open process. Try running as Administrator.\n";
        return 1;
    }

    const std::string dllPathStr = dllPath.string();
    const std::size_t allocationSize = dllPathStr.length() + 1;

    void* remoteMemory = ::VirtualAllocEx(processHandle.get(), nullptr, allocationSize,
                                          MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMemory) {
        std::cerr << "[Error] Failed to allocate virtual memory in target process.\n";
        return 1;
    }

    if (!::WriteProcessMemory(processHandle.get(), remoteMemory, dllPathStr.c_str(), allocationSize, nullptr)) {
        std::cerr << "[Error] Failed to write memory buffer to target process.\n";
        ::VirtualFreeEx(processHandle.get(), remoteMemory, 0, MEM_RELEASE);
        return 1;
    }

    const auto loadLibraryAddr = reinterpret_cast<LPTHREAD_START_ROUTINE>(::GetProcAddress(::GetModuleHandleA("kernel32.dll"), "LoadLibraryA"));

    UniqueHandle threadHandle(::CreateRemoteThread(processHandle.get(), nullptr, 0,
                                                  loadLibraryAddr, remoteMemory, 0, nullptr));
    if (!threadHandle) {
        std::cerr << "[Error] Failed to invoke remote execution thread.\n";
        ::VirtualFreeEx(processHandle.get(), remoteMemory, 0, MEM_RELEASE);
        return 1;
    }

    std::cout << "[Success] Module path successfully deployed.\n";

    return 0;
}