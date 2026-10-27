// Include the production mapper unchanged, with its command entry renamed.
// The child maps only the catalog fixture DLL into its own isolated process.
#define wmain ProductionMapperCommand
#include "../src/loader/loader.cpp"
#undef wmain
#include "loader_support.h"
#include <atomic>

namespace {
std::atomic<unsigned> cppExceptions{0};
LONG CALLBACK ObserveCatalogException(EXCEPTION_POINTERS* exception) {
    if (exception && exception->ExceptionRecord && exception->ExceptionRecord->ExceptionCode==0xE06D7363) {
        cppExceptions.fetch_add(1,std::memory_order_relaxed);
        std::fprintf(stderr,"MAPPED_CATALOG unexpected C++ exception\n");
        std::fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
LONG WINAPI QuietFailure(EXCEPTION_POINTERS*) { return EXCEPTION_EXECUTE_HANDLER; }
using FixtureBytes=std::vector<std::uint8_t>;
template<class T> void FixturePut(FixtureBytes& bytes,std::size_t at,T value) {
    if (at+sizeof(value)>bytes.size()) bytes.resize(at+sizeof(value));
    std::memcpy(bytes.data()+at,&value,sizeof(value));
}
FixtureBytes FixtureModel(const std::vector<std::string>& refs) {
    FixtureBytes bytes(36+refs.size()*16);
    FixturePut(bytes,4,std::uint16_t{12});FixturePut(bytes,6,std::uint16_t{1});
    FixturePut(bytes,8,std::uint32_t{8});FixturePut(bytes,12,std::uint32_t{1});
    std::memcpy(bytes.data()+16,"RERL",4);FixturePut(bytes,20,std::uint32_t{8});
    FixturePut(bytes,28,std::uint32_t{8});FixturePut(bytes,32,static_cast<std::uint32_t>(refs.size()));
    for (std::size_t i=0;i<refs.size();++i) {
        const auto field=36+i*16+8;
        FixturePut(bytes,field,static_cast<std::int64_t>(bytes.size()-field));
        bytes.insert(bytes.end(),refs[i].begin(),refs[i].end());bytes.push_back(0);
    }
    FixturePut(bytes,0,static_cast<std::uint32_t>(bytes.size()));
    FixturePut(bytes,24,static_cast<std::uint32_t>(bytes.size()-28));
    return bytes;
}
void FixtureWrite(const std::filesystem::path& path,const FixtureBytes& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if (!stream) Fail("Cannot write mapped catalog fixture");
}
}
int wmain(int argc,wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(&QuietFailure);
    try {
        if (argc==4 && std::wstring_view(argv[1])==L"--child") {
            const auto observer=AddVectoredExceptionHandler(1,&ObserveCatalogException);
            if (!observer) return 20;
            Arguments arguments;
            arguments.pid=GetCurrentProcessId();arguments.dll=argv[2];arguments.log=argv[3];
            const int result=Start(GetCurrentProcess(),arguments);
            RemoveVectoredExceptionHandler(observer);
            return result==0 && cppExceptions.load()==0 ? 0 : 21;
        }
        if (argc!=2 && argc!=4) return 22;
        const bool installed=argc==4 && std::wstring_view(argv[2])==L"--scan";
        if (argc==4 && !installed) return 23;
        const auto root=std::filesystem::path(CS2_WORKSPACE_ROOT)/L"logs"/L"mapped-catalog"/
            (L"run-"+std::to_wstring(GetCurrentProcessId()))/(installed ? L"installed-scan" : L"fixtures");
        std::filesystem::create_directories(root);
        if (installed) {
            const std::wstring value=argv[3];
            FixtureBytes bytes(value.size()*sizeof(wchar_t));
            std::memcpy(bytes.data(),value.data(),bytes.size());FixtureWrite(root/L"root.txt",bytes);
        } else {
            const std::string graph="animation/graphs/worldmodel/worldmodel.vnmgraph";
            FixtureWrite(root/L"valid/agents/valid/body.vmdl_c",FixtureModel({graph}));
            FixtureWrite(root/L"valid"/std::filesystem::path(graph+"_c"),{1});
            FixtureWrite(root/L"valid/agents/legacy/body.vmdl_c",FixtureModel({"characters/models/shared/animgraphs/player_ct.vanmgrph"}));
            FixtureWrite(root/L"valid/agents/bad/body.vmdl_c",{1,2,3});
            FixtureWrite(root/L"valid/agents/missing/body.vmdl_c",FixtureModel({graph,"materials/missing.vmat"}));
            FixtureWrite(root/L"bad-vpk/pak01_dir.vpk",{1,2,3});
        }
        wchar_t self[32768]{};
        if (!GetModuleFileNameW(nullptr,self,static_cast<DWORD>(std::size(self)))) return 24;
        auto command=loader::Quote(self)+L" --child "+loader::Quote(std::filesystem::absolute(argv[1]).wstring())+L" "+loader::Quote(root.wstring());
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
        if (!CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)) return 25;
        CloseHandle(child.hThread);
        const auto wait=WaitForSingleObject(child.hProcess,30000);
        DWORD result=1;
        const bool complete=wait==WAIT_OBJECT_0 && GetExitCodeProcess(child.hProcess,&result);
        CloseHandle(child.hProcess);
        std::printf("Production-mapped catalog child complete=%u exit=0x%08lx\n",complete ? 1u : 0u,result);
        // The child owns no game resources. It exits normally; no forced thread
        // shutdown or physical unmapping is performed, even after a timeout.
        return complete && result==0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"Mapped catalog check setup: %s\n",error.what());return 1;
    }
}
