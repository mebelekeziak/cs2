// Catalog-only fixture: exercises the production mapper/CRT bootstrap without
// CS2, native hooks or entity access. All work stays outside DllMain.
#include <Windows.h>
#include "model_catalog.h"
#include <cstdio>
#include <filesystem>
#include <string>

extern "C" __declspec(dllexport) DWORD WINAPI OverlayStart(const wchar_t* rootText) {
    const std::filesystem::path root(rootText);
    std::string error;
    if (root.filename()==L"installed-scan") {
        // Optional installed root is read from a workspace-owned UTF-16 file.
        const auto pathFile=root/L"root.txt";
        const auto file=CreateFileW(pathFile.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (file==INVALID_HANDLE_VALUE) return 10;
        wchar_t path[32768]{}; DWORD read=0;
        const bool copied=ReadFile(file,path,sizeof(path)-sizeof(wchar_t),&read,nullptr)!=FALSE;
        CloseHandle(file);
        if (!copied || read%sizeof(wchar_t) || !ModelCatalogDiscover(path,error)) return 11;
        const auto catalog=ModelCatalogSnapshot();
        std::printf("MAPPED_CATALOG installed entries=%zu\n",catalog ? catalog->size() : 0);
        std::fflush(stdout);
        return catalog && !catalog->empty() ? 0 : 12;
    }
    if (!ModelCatalogDiscover(root/L"valid",error)) return 1;
    const auto catalog=ModelCatalogSnapshot();
    if (!catalog || catalog->size()!=1 || (*catalog)[0].path!="agents/valid/body.vmdl") return 2;
    if (ModelCatalogDiscover(root/L"bad-vpk",error) || error.empty() || ModelCatalogSnapshot()!=catalog) return 3;
    if (ModelCatalogDiscover(root/L"missing",error) || error.empty() || ModelCatalogSnapshot()!=catalog) return 4;
    std::printf("MAPPED_CATALOG valid=1 malformed/legacy/missing rejected; failed scans retain snapshot\n");
    std::fflush(stdout);
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI OverlayRequestStop(void*) { return 0; }
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID) { return TRUE; }
