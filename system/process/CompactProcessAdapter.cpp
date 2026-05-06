#include "system/process/CompactProcessAdapter.h"

#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace gsm::system {
namespace {

std::string quoteForDisplay(const std::string& value)
{
    if (value.find_first_of(" \t\"") == std::string::npos) {
        return value;
    }

    std::string quoted = "\"";
    for (char character : value) {
        if (character == '"') {
            quoted += "\\\"";
        } else {
            quoted += character;
        }
    }
    quoted += "\"";
    return quoted;
}

std::string algorithmArgument(gsm::core::CompressionAlgorithm algorithm)
{
    return "/exe:" + gsm::core::toString(algorithm);
}

} // namespace

CompactCommand CompactProcessAdapter::buildCompressCommand(
    const gsm::system::Path& targetPath,
    gsm::core::CompressionAlgorithm algorithm) const
{
    CompactCommand command;
    command.arguments = {
        "/c",
        "/s",
        "/a",
        "/i",
        algorithmArgument(algorithm),
        gsm::system::normalizePath(targetPath) + "\\*"
    };
    return command;
}

CompactCommand CompactProcessAdapter::buildRestoreCommand(const gsm::system::Path& targetPath) const
{
    CompactCommand command;
    command.arguments = {
        "/u",
        "/s",
        gsm::system::normalizePath(targetPath) + "\\*"
    };
    return command;
}

ProcessResult CompactProcessAdapter::run(const CompactCommand& command, std::function<void(const std::string&)> onOutput) const
{
    ProcessResult result;
    const std::string displayCommand = toDisplayString(command);
    
    // We construct the command line explicitly executing cmd.exe so we can redirect stderr 2>&1
    std::string fullCommandLine = "cmd.exe /c \"\"" + command.executable + "\"";
    for (const std::string& arg : command.arguments) {
        fullCommandLine += " \"" + arg + "\"";
    }
    fullCommandLine += " 2>&1\"";

    HANDLE hReadPipe = NULL;
    HANDLE hWritePipe = NULL;

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &saAttr, 0)) {
        result.output = "Failed to create pipe.";
        return result;
    }
    if (!SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0)) {
        result.output = "Failed to set handle information.";
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return result;
    }

    STARTUPINFOA siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFOA));
    siStartInfo.cb = sizeof(STARTUPINFOA);
    siStartInfo.hStdError = hWritePipe;
    siStartInfo.hStdOutput = hWritePipe;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    siStartInfo.wShowWindow = SW_HIDE; // Avoid opening a visible console window

    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    std::vector<char> cmdLineMutable(fullCommandLine.begin(), fullCommandLine.end());
    cmdLineMutable.push_back('\0');

    BOOL bSuccess = CreateProcessA(
        NULL,
        cmdLineMutable.data(),
        NULL,
        NULL,
        TRUE,
        0,
        NULL,
        NULL,
        &siStartInfo,
        &piProcInfo
    );

    CloseHandle(hWritePipe); // Important to close our copy of the write end so ReadFile returns when the child exits

    if (!bSuccess) {
        result.output = "Failed to start process via CreateProcessA.";
        CloseHandle(hReadPipe);
        return result;
    }

    DWORD dwRead;
    CHAR chBuf[4096];
    bSuccess = FALSE;

    for (;;) {
        bSuccess = ReadFile(hReadPipe, chBuf, sizeof(chBuf) - 1, &dwRead, NULL);
        if (!bSuccess || dwRead == 0) break;
        chBuf[dwRead] = '\0';
        std::string chunk(chBuf);
        result.output += chunk;
        if (onOutput) {
            onOutput(chunk);
        }
    }

    WaitForSingleObject(piProcInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(piProcInfo.hProcess, &exitCode);
    result.exitCode = exitCode;

    CloseHandle(piProcInfo.hProcess);
    CloseHandle(piProcInfo.hThread);
    CloseHandle(hReadPipe);

    return result;
}

std::string toDisplayString(const CompactCommand& command)
{
    std::ostringstream stream;
    stream << quoteForDisplay(command.executable);

    for (const std::string& argument : command.arguments) {
        stream << ' ' << quoteForDisplay(argument);
    }

    return stream.str();
} // namespace

CompactOutputMetrics CompactProcessAdapter::parseCompressOutput(const std::string& output)
{
    (void)output;
    CompactOutputMetrics metrics;
    metrics.parsed = false;
    return metrics;
}

} // namespace gsm::system
