#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>
#include <d3d11.h>
#include <dxgi.h>

#include "manual_map.h"
#include "loader_support.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#if !defined(_M_X64)
#error This mapper supports Windows AMD64 only.
#endif

namespace {
constexpr std::size_t kMaxImageBytes = 256u * 1024u * 1024u;
constexpr DWORD kStopWaitMilliseconds = 15000;

// References keep these import libraries loaded in the mapper. The mapper does
// not dynamically load a dependency into either process. Each resolved address
// is subsequently checked against an already loaded target module.
using Anchor = void (*)();
volatile Anchor gImportAnchors[] = {
    reinterpret_cast<Anchor>(&D3D11CreateDevice),
    reinterpret_cast<Anchor>(&CreateDXGIFactory),
};

[[noreturn]] void Fail(const std::string& message) { throw std::runtime_error(message); }
std::string LastError(const char* operation) {
    const auto code = GetLastError();
    return std::string(operation) + " failed (Win32 " + std::to_string(code) + ")";
}

struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

bool InRange(std::size_t offset, std::size_t length, std::size_t size) {
    return offset <= size && length <= size - offset;
}

template <typename T> T ReadObject(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (!InRange(offset, sizeof(T), bytes.size())) Fail("PE structure extends beyond available bytes");
    T result{};
    std::memcpy(&result, bytes.data() + offset, sizeof(result));
    return result;
}

std::wstring Lower(std::wstring value) {
    for (auto& c : value) c = static_cast<wchar_t>(towlower(c));
    return value;
}

std::wstring WidenAscii(const std::string& value) {
    std::wstring result;
    for (const auto c : value) {
        if (static_cast<unsigned char>(c) > 127) Fail("Non-ASCII import module name is unsupported");
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return result;
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) Fail("Cannot open DLL file");
    const auto length = stream.tellg();
    if (length <= 0 || static_cast<std::uint64_t>(length) > kMaxImageBytes) Fail("DLL file size is invalid or exceeds 256 MiB");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) Fail("Cannot read complete DLL file");
    return bytes;
}

struct PeImage {
    IMAGE_NT_HEADERS64 nt{};
    std::vector<IMAGE_SECTION_HEADER> sections;
    std::vector<std::uint8_t> image;

    explicit PeImage(const std::filesystem::path& path) {
        const auto raw = ReadFile(path);
        const auto dos = ReadObject<IMAGE_DOS_HEADER>(raw, 0);
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos))) Fail("Invalid DOS header");
        nt = ReadObject<IMAGE_NT_HEADERS64>(raw, static_cast<std::size_t>(dos.e_lfanew));
        if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
            !(nt.FileHeader.Characteristics & IMAGE_FILE_DLL)) Fail("Expected a native AMD64 PE32+ DLL");
        const auto& optional = nt.OptionalHeader;
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        const auto alignment = optional.SectionAlignment;
        const auto fileAlignment = optional.FileAlignment;
        if (!alignment || (alignment & (alignment - 1)) || alignment < system.dwPageSize ||
            !fileAlignment || (fileAlignment & (fileAlignment - 1)) || fileAlignment < 512 || fileAlignment > 65536 ||
            fileAlignment > alignment) Fail("Unsupported PE section/file alignment");
        if (!optional.SizeOfImage || optional.SizeOfImage > kMaxImageBytes || optional.SizeOfImage % alignment ||
            !optional.SizeOfHeaders || optional.SizeOfHeaders > optional.SizeOfImage ||
            optional.SizeOfHeaders > raw.size() || optional.SizeOfHeaders % fileAlignment ||
            optional.ImageBase % 65536 || optional.NumberOfRvaAndSizes > IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
            !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections > 96) Fail("Invalid PE image/header size");
        if (optional.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF) Fail("CFG-enabled DLL mapping is unsupported; build this DLL without /guard:cf");
        const auto sectionOffset = static_cast<std::size_t>(dos.e_lfanew) + sizeof(IMAGE_NT_HEADERS64);
        const auto sectionBytes = static_cast<std::size_t>(nt.FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
        if (!InRange(sectionOffset, sectionBytes, optional.SizeOfHeaders)) Fail("Section table exceeds PE headers");
        image.resize(optional.SizeOfImage, 0);
        std::memcpy(image.data(), raw.data(), optional.SizeOfHeaders);
        for (std::size_t i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            const auto section = ReadObject<IMAGE_SECTION_HEADER>(raw, sectionOffset + i * sizeof(IMAGE_SECTION_HEADER));
            const auto length = std::max(section.Misc.VirtualSize, section.SizeOfRawData);
            if (section.VirtualAddress % alignment || section.VirtualAddress < optional.SizeOfHeaders ||
                !InRange(section.VirtualAddress, length, image.size()) ||
                (section.SizeOfRawData && (section.PointerToRawData % fileAlignment ||
                    section.PointerToRawData < optional.SizeOfHeaders ||
                    !InRange(section.PointerToRawData, section.SizeOfRawData, raw.size())))) Fail("Invalid PE section bounds");
            for (const auto& existing : sections) {
                const auto existingLength = std::max(existing.Misc.VirtualSize, existing.SizeOfRawData);
                const auto end = static_cast<std::uint64_t>(section.VirtualAddress) + length;
                const auto existingEnd = static_cast<std::uint64_t>(existing.VirtualAddress) + existingLength;
                if (length && existingLength && section.VirtualAddress < existingEnd && existing.VirtualAddress < end) Fail("Overlapping PE sections are unsupported");
            }
            if (section.SizeOfRawData) std::memcpy(image.data() + section.VirtualAddress, raw.data() + section.PointerToRawData, section.SizeOfRawData);
            sections.push_back(section);
        }
        for (std::size_t i = 0; i < optional.NumberOfRvaAndSizes; ++i) {
            const auto directory = optional.DataDirectory[i];
            if (!directory.VirtualAddress && !directory.Size) continue;
            if (!directory.VirtualAddress || !directory.Size) Fail("Malformed PE data directory");
            const auto size = i == IMAGE_DIRECTORY_ENTRY_SECURITY ? raw.size() : image.size();
            if (!InRange(directory.VirtualAddress, directory.Size, size)) Fail("PE directory extends beyond file/image");
            if (i != IMAGE_DIRECTORY_ENTRY_SECURITY && !Backed(directory.VirtualAddress, directory.Size)) Fail("PE directory refers to an unmapped section gap");
        }
        if (Directory(IMAGE_DIRECTORY_ENTRY_TLS).VirtualAddress) Fail("Static TLS/TLS callbacks are unsupported by this mapper");
        if (Directory(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT).VirtualAddress) Fail("Delay-load imports are unsupported by this mapper");
        if (Directory(IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR).VirtualAddress) Fail("Managed DLLs are unsupported by this mapper");
        if (!optional.AddressOfEntryPoint || !Executable(optional.AddressOfEntryPoint)) Fail("DLL entrypoint is missing or outside executable sections");
    }

    IMAGE_DATA_DIRECTORY Directory(std::size_t index) const {
        return index < nt.OptionalHeader.NumberOfRvaAndSizes ? nt.OptionalHeader.DataDirectory[index] : IMAGE_DATA_DIRECTORY{};
    }
    bool Backed(std::size_t rva, std::size_t length) const {
        if (InRange(rva, length, nt.OptionalHeader.SizeOfHeaders)) return true;
        for (const auto& section : sections) {
            const auto size = std::max(section.Misc.VirtualSize, section.SizeOfRawData);
            if (rva >= section.VirtualAddress && InRange(rva - section.VirtualAddress, length, size)) return true;
        }
        return false;
    }
    bool Executable(std::size_t rva) const {
        for (const auto& section : sections) {
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && rva >= section.VirtualAddress &&
                rva - section.VirtualAddress < std::max(section.Misc.VirtualSize, section.SizeOfRawData)) return true;
        }
        return false;
    }
    template <typename T> T At(std::size_t rva) const {
        if (!Backed(rva, sizeof(T))) Fail("PE reference lies outside mapped section data");
        return ReadObject<T>(image, rva);
    }
    template <typename T> void Put(std::size_t rva, T value) {
        if (!Backed(rva, sizeof(T)) || !InRange(rva, sizeof(T), image.size())) Fail("PE write exceeds section data");
        std::memcpy(image.data() + rva, &value, sizeof(value));
    }
    std::string String(std::size_t rva, std::size_t maximum = 1024) const {
        std::string result;
        for (std::size_t i = 0; i < maximum; ++i) {
            const auto c = At<char>(rva + i);
            if (!c) return result;
            result.push_back(c);
        }
        Fail("Unterminated or excessive PE string");
    }
    DWORD ExportRva(const char* name) const {
        const auto directory = Directory(IMAGE_DIRECTORY_ENTRY_EXPORT);
        if (!directory.VirtualAddress || directory.Size < sizeof(IMAGE_EXPORT_DIRECTORY)) Fail("DLL has no valid export directory");
        const auto exports = At<IMAGE_EXPORT_DIRECTORY>(directory.VirtualAddress);
        if (exports.NumberOfNames > image.size() / sizeof(DWORD) || exports.NumberOfFunctions > image.size() / sizeof(DWORD) ||
            !Backed(exports.AddressOfNames, static_cast<std::size_t>(exports.NumberOfNames) * sizeof(DWORD)) ||
            !Backed(exports.AddressOfNameOrdinals, static_cast<std::size_t>(exports.NumberOfNames) * sizeof(WORD)) ||
            !Backed(exports.AddressOfFunctions, static_cast<std::size_t>(exports.NumberOfFunctions) * sizeof(DWORD))) Fail("Invalid export array bounds");
        for (std::size_t i = 0; i < exports.NumberOfNames; ++i) {
            if (String(At<DWORD>(static_cast<std::size_t>(exports.AddressOfNames) + i * sizeof(DWORD))) != name) continue;
            const auto ordinal = At<WORD>(static_cast<std::size_t>(exports.AddressOfNameOrdinals) + i * sizeof(WORD));
            if (ordinal >= exports.NumberOfFunctions) Fail("Invalid export ordinal index");
            const auto rva = At<DWORD>(static_cast<std::size_t>(exports.AddressOfFunctions) + ordinal * sizeof(DWORD));
            if (rva >= directory.VirtualAddress && rva - directory.VirtualAddress < directory.Size) Fail("Forwarded overlay exports are unsupported");
            if (!Executable(rva)) Fail("Overlay export lies outside executable sections");
            return rva;
        }
        Fail(std::string("Required DLL export is missing: ") + name);
    }
    void Relocate(std::uintptr_t base) {
        const auto delta = static_cast<std::uint64_t>(base) - nt.OptionalHeader.ImageBase;
        if (!delta) return;
        const auto directory = Directory(IMAGE_DIRECTORY_ENTRY_BASERELOC);
        if (!directory.VirtualAddress || (nt.FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED)) Fail("DLL cannot relocate to the allocated address");
        std::size_t offset = 0;
        while (offset < directory.Size) {
            if (directory.Size - offset < sizeof(IMAGE_BASE_RELOCATION)) Fail("Truncated relocation block");
            const auto blockRva = static_cast<std::size_t>(directory.VirtualAddress) + offset;
            const auto block = At<IMAGE_BASE_RELOCATION>(blockRva);
            if (block.SizeOfBlock < sizeof(block) || block.SizeOfBlock > directory.Size - offset ||
                (block.SizeOfBlock - sizeof(block)) % sizeof(WORD) || block.VirtualAddress % 4096) Fail("Invalid relocation block");
            const auto count = (block.SizeOfBlock - sizeof(block)) / sizeof(WORD);
            for (std::size_t i = 0; i < count; ++i) {
                const auto item = At<WORD>(blockRva + sizeof(block) + i * sizeof(WORD));
                const auto type = item >> 12;
                if (type == IMAGE_REL_BASED_ABSOLUTE) continue;
                if (type != IMAGE_REL_BASED_DIR64) Fail("Unsupported relocation type; expected AMD64 DIR64");
                const auto rva = static_cast<std::size_t>(block.VirtualAddress) + (item & 0xFFF);
                Put<std::uint64_t>(rva, At<std::uint64_t>(rva) + delta);
            }
            offset += block.SizeOfBlock;
        }
    }
    DWORD UnwindCount() const {
        const auto directory = Directory(IMAGE_DIRECTORY_ENTRY_EXCEPTION);
        if (!directory.VirtualAddress) return 0;
        if (directory.Size % sizeof(RUNTIME_FUNCTION)) Fail("Invalid AMD64 exception directory size");
        DWORD previousEnd = 0;
        for (std::size_t offset = 0; offset < directory.Size; offset += sizeof(RUNTIME_FUNCTION)) {
            const auto function = At<RUNTIME_FUNCTION>(static_cast<std::size_t>(directory.VirtualAddress) + offset);
            if (function.BeginAddress >= function.EndAddress || function.BeginAddress < previousEnd ||
                !Executable(function.BeginAddress) || !Executable(function.EndAddress - 1) ||
                !Backed(function.UnwindData, 4)) Fail("Invalid or unsorted AMD64 runtime function table");
            previousEnd = function.EndAddress;
        }
        return directory.Size / sizeof(RUNTIME_FUNCTION);
    }
};

struct TargetModule {
    std::wstring name;
    std::uintptr_t base;
    DWORD size;
};

class ImportResolver {
    HANDLE process_;
    std::vector<TargetModule> modules_;
    struct ExportInfo {
        IMAGE_DATA_DIRECTORY directory{};
        IMAGE_EXPORT_DIRECTORY exports{};
        std::vector<std::uint8_t> bytes;
        std::vector<DWORD> functions;
        std::vector<DWORD> names;
        std::vector<WORD> ordinals;
    };
    mutable std::unordered_map<std::uintptr_t, ExportInfo> exportCache_;

    void Read(const TargetModule& module, std::size_t rva, void* buffer, std::size_t size) const {
        if (!InRange(rva, size, module.size)) Fail("Target export reference exceeds module image");
        if (!size) return;
        SIZE_T read = 0;
        if (!ReadProcessMemory(process_, reinterpret_cast<void*>(module.base + rva), buffer, size, &read) || read != size) Fail(LastError("Read target export data"));
    }
    template <typename T> T ReadAt(const TargetModule& module, std::size_t rva) const {
        T value{};
        Read(module, rva, &value, sizeof(value));
        return value;
    }
    void ExportRead(const TargetModule& module, const ExportInfo& info, std::size_t rva, void* buffer, std::size_t size) const {
        if (!size) return;
        if (rva >= info.directory.VirtualAddress && InRange(rva - info.directory.VirtualAddress, size, info.bytes.size())) {
            std::memcpy(buffer, info.bytes.data() + rva - info.directory.VirtualAddress, size);
        } else Read(module, rva, buffer, size);
    }
    const ExportInfo& Exports(const TargetModule& module) const {
        if (const auto found = exportCache_.find(module.base); found != exportCache_.end()) return found->second;
        const auto dos = ReadAt<IMAGE_DOS_HEADER>(module, 0);
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) Fail("Invalid target dependency DOS header");
        const auto nt = ReadAt<IMAGE_NT_HEADERS64>(module, static_cast<std::size_t>(dos.e_lfanew));
        if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.OptionalHeader.SizeOfImage != module.size ||
            nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) Fail("Invalid target dependency image");
        ExportInfo info;
        info.directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (info.directory.Size < sizeof(IMAGE_EXPORT_DIRECTORY) || info.directory.Size > 16u * 1024u * 1024u ||
            !info.directory.VirtualAddress || !InRange(info.directory.VirtualAddress, info.directory.Size, module.size)) Fail("Invalid target dependency export directory");
        info.bytes.resize(info.directory.Size);
        Read(module, info.directory.VirtualAddress, info.bytes.data(), info.bytes.size());
        std::memcpy(&info.exports, info.bytes.data(), sizeof(info.exports));
        if (info.exports.NumberOfFunctions > module.size / sizeof(DWORD) || info.exports.NumberOfNames > module.size / sizeof(DWORD)) Fail("Excessive target dependency export count");
        info.functions.resize(info.exports.NumberOfFunctions);
        info.names.resize(info.exports.NumberOfNames);
        info.ordinals.resize(info.exports.NumberOfNames);
        ExportRead(module, info, info.exports.AddressOfFunctions, info.functions.data(), info.functions.size() * sizeof(DWORD));
        ExportRead(module, info, info.exports.AddressOfNames, info.names.data(), info.names.size() * sizeof(DWORD));
        ExportRead(module, info, info.exports.AddressOfNameOrdinals, info.ordinals.data(), info.ordinals.size() * sizeof(WORD));
        for (const auto ordinal : info.ordinals) if (ordinal >= info.functions.size()) Fail("Invalid target dependency ordinal index");
        return exportCache_.emplace(module.base, std::move(info)).first->second;
    }
    std::string ExportString(const TargetModule& module, const ExportInfo& info, std::size_t rva) const {
        std::string result;
        if (rva >= info.directory.VirtualAddress && rva - info.directory.VirtualAddress < info.bytes.size()) {
            const auto offset = rva - info.directory.VirtualAddress;
            const auto maximum = std::min<std::size_t>(1024, info.bytes.size() - offset);
            const auto begin = info.bytes.data() + offset;
            const auto end = std::find(begin, begin + maximum, static_cast<std::uint8_t>(0));
            if (end == begin + maximum) Fail("Unterminated target export string");
            return std::string(reinterpret_cast<const char*>(begin), reinterpret_cast<const char*>(end));
        }
        for (std::size_t i = 0; i < 1024; ++i) {
            const auto c = ReadAt<char>(module, rva + i);
            if (!c) return result;
            result.push_back(c);
        }
        Fail("Excessive target export string");
    }
    const TargetModule* FindModule(std::wstring name) const {
        name = Lower(std::move(name));
        if (name.find(L'.') == std::wstring::npos) name += L".dll";
        const auto found = std::find_if(modules_.begin(), modules_.end(), [&](const auto& module) { return module.name == name; });
        return found == modules_.end() ? nullptr : &*found;
    }
    std::uintptr_t Resolve(std::wstring moduleName, const std::string& name, DWORD ordinal, bool byOrdinal, unsigned depth,
        HMODULE localSource = nullptr, FARPROC localResolved = nullptr) const {
        if (depth >= 16) Fail("Dependency forwarder recursion exceeds 16 levels");
        const auto module = FindModule(moduleName);
        if (!module) {
            // API-set pseudo-modules have no image in the module snapshot. Use
            // only the already loaded local source to identify the actual host,
            // then parse that target host's own export table by name. Never reuse
            // a function RVA from a potentially different local DLL version.
            if (!localResolved && localSource) localResolved = GetProcAddress(localSource,
                byOrdinal ? reinterpret_cast<LPCSTR>(static_cast<std::uintptr_t>(ordinal)) : name.c_str());
            if (!localResolved) Fail("Import/forwarder module is not already loaded in target");
            HMODULE owner = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(localResolved), &owner)) Fail(LastError("Identify API-set host"));
            std::array<wchar_t, 32768> path{};
            const auto length = GetModuleFileNameW(owner, path.data(), static_cast<DWORD>(path.size()));
            if (!length || length == path.size()) Fail("Cannot identify API-set host path");
            const auto ownerName = std::filesystem::path(path.data()).filename().wstring();
            if (!FindModule(ownerName)) Fail("API-set host is not already loaded in target");
            if (byOrdinal || GetProcAddress(owner, name.c_str()) != localResolved) Fail("Cannot verify API-set host export name");
            return Resolve(ownerName, name, 0, false, depth + 1);
        }
        const auto& info = Exports(*module);
        std::size_t index = info.functions.size();
        if (byOrdinal) {
            if (ordinal >= info.exports.Base) index = static_cast<std::size_t>(ordinal - info.exports.Base);
        } else {
            for (std::size_t i = 0; i < info.names.size(); ++i) {
                if (ExportString(*module, info, info.names[i]) == name) { index = info.ordinals[i]; break; }
            }
        }
        if (index >= info.functions.size()) Fail("Requested export is missing from target dependency");
        const auto rva = info.functions[index];
        if (!rva || rva >= module->size) Fail("Invalid target dependency export RVA");
        if (rva >= info.directory.VirtualAddress && rva - info.directory.VirtualAddress < info.directory.Size) {
            const auto forwarder = ExportString(*module, info, rva);
            const auto dot = forwarder.find_last_of('.');
            if (dot == std::string::npos || dot == 0 || dot + 1 == forwarder.size()) Fail("Invalid target export forwarder");
            const auto forwardedName = forwarder.substr(dot + 1);
            const auto forwardedModule = WidenAscii(forwarder.substr(0, dot));
            if (!localResolved) {
                const auto localModule = GetModuleHandleW(module->name.c_str());
                if (localModule) localResolved = GetProcAddress(localModule,
                    byOrdinal ? reinterpret_cast<LPCSTR>(static_cast<std::uintptr_t>(ordinal)) : name.c_str());
            }
            if (forwardedName[0] == '#') {
                if (forwardedName.size() == 1 || forwardedName.find_first_not_of("0123456789", 1) != std::string::npos) Fail("Invalid forwarder ordinal");
                const auto forwardedOrdinal = std::stoul(forwardedName.substr(1));
                if (forwardedOrdinal > MAXDWORD) Fail("Forwarder ordinal exceeds DWORD");
                return Resolve(forwardedModule, "", static_cast<DWORD>(forwardedOrdinal), true, depth + 1, nullptr, localResolved);
            }
            return Resolve(forwardedModule, forwardedName, 0, false, depth + 1, nullptr, localResolved);
        }
        return module->base + rva;
    }
public:
    ImportResolver(HANDLE process, DWORD pid) : process_(process) {
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
        if (snapshot.value == INVALID_HANDLE_VALUE) Fail(LastError("Module snapshot"));
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (!Module32FirstW(snapshot.value, &entry)) Fail(LastError("Module32FirstW"));
        do {
            modules_.push_back({Lower(entry.szModule), reinterpret_cast<std::uintptr_t>(entry.modBaseAddr), entry.modBaseSize});
        } while (Module32NextW(snapshot.value, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES) Fail(LastError("Module32NextW"));
    }

    std::uintptr_t Named(const wchar_t* module, const char* function) const {
        return Resolve(module, function, 0, false, 0, GetModuleHandleW(module));
    }

    void FixImports(PeImage& pe) const {
        const auto directory = pe.Directory(IMAGE_DIRECTORY_ENTRY_IMPORT);
        if (!directory.VirtualAddress) return;
        bool ended = false;
        for (std::size_t offset = 0; offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= directory.Size; offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
            const auto descriptor = pe.At<IMAGE_IMPORT_DESCRIPTOR>(static_cast<std::size_t>(directory.VirtualAddress) + offset);
            if (!descriptor.OriginalFirstThunk && !descriptor.TimeDateStamp && !descriptor.ForwarderChain && !descriptor.Name && !descriptor.FirstThunk) { ended = true; break; }
            if (!descriptor.Name || !descriptor.FirstThunk) Fail("Invalid import descriptor");
            const auto moduleName = pe.String(descriptor.Name, 260);
            const auto wideModule = WidenAscii(moduleName);
            const auto local = GetModuleHandleW(wideModule.c_str());
            const auto lookupRva = descriptor.OriginalFirstThunk ? descriptor.OriginalFirstThunk : descriptor.FirstThunk;
            bool thunkEnded = false;
            for (std::size_t i = 0; i < pe.image.size() / sizeof(ULONGLONG); ++i) {
                const auto thunk = pe.At<ULONGLONG>(static_cast<std::size_t>(lookupRva) + i * sizeof(ULONGLONG));
                if (!thunk) { thunkEnded = true; break; }
                std::string importName;
                std::uintptr_t address = 0;
                if (IMAGE_SNAP_BY_ORDINAL64(thunk)) {
                    if (thunk & ~(IMAGE_ORDINAL_FLAG64 | 0xFFFFull)) Fail("Invalid ordinal import bits");
                    const auto ordinal = static_cast<DWORD>(IMAGE_ORDINAL64(thunk));
                    importName = "#" + std::to_string(ordinal);
                    address = Resolve(wideModule, "", ordinal, true, 0, local);
                } else {
                    if (thunk > std::numeric_limits<DWORD>::max()) Fail("Invalid import name RVA");
                    pe.At<WORD>(static_cast<std::size_t>(thunk));
                    importName = pe.String(static_cast<std::size_t>(thunk) + sizeof(WORD));
                    address = Resolve(wideModule, importName, 0, false, 0, local);
                }
                pe.Put<std::uint64_t>(static_cast<std::size_t>(descriptor.FirstThunk) + i * sizeof(ULONGLONG), address);
            }
            if (!thunkEnded) Fail("Unterminated import thunk array");
        }
        if (!ended) Fail("Unterminated import descriptor array");
    }
};

class RemoteAllocation {
    HANDLE process_;
    bool retained_ = false;
public:
    void* address = nullptr;
    std::size_t size;
    RemoteAllocation(HANDLE process, std::size_t bytes, void* preferred = nullptr) : process_(process), size(bytes) {
        address = VirtualAllocEx(process_, preferred, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!address && preferred) address = VirtualAllocEx(process_, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!address) Fail(LastError("VirtualAllocEx"));
    }
    ~RemoteAllocation() { if (address && !retained_) VirtualFreeEx(process_, address, 0, MEM_RELEASE); }
    RemoteAllocation(const RemoteAllocation&) = delete;
    RemoteAllocation& operator=(const RemoteAllocation&) = delete;
    void Retain() { retained_ = true; }
    std::uintptr_t Base() const { return reinterpret_cast<std::uintptr_t>(address); }
    void Write(const void* source, std::size_t bytes) const {
        if (bytes > size) Fail("Remote write exceeds allocation");
        SIZE_T written = 0;
        if (!WriteProcessMemory(process_, address, source, bytes, &written) || written != bytes) Fail(LastError("WriteProcessMemory"));
    }
};

DWORD PageProtection(unsigned permissions) {
    const bool read = permissions & 1, write = permissions & 2, execute = permissions & 4;
    if (execute) return write ? PAGE_EXECUTE_READWRITE : read ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
    return write ? PAGE_READWRITE : read ? PAGE_READONLY : PAGE_NOACCESS;
}

void ProtectImage(HANDLE process, const RemoteAllocation& allocation, const PeImage& pe) {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto page = system.dwPageSize;
    std::vector<unsigned> permissions((pe.image.size() + page - 1) / page, 0);
    auto add = [&](std::size_t rva, std::size_t bytes, unsigned mask) {
        if (!bytes) return;
        for (auto index = rva / page; index <= (rva + bytes - 1) / page; ++index) permissions.at(index) |= mask;
    };
    add(0, pe.nt.OptionalHeader.SizeOfHeaders, 1);
    for (const auto& section : pe.sections) {
        unsigned mask = 0;
        if (section.Characteristics & IMAGE_SCN_MEM_READ) mask |= 1;
        if (section.Characteristics & IMAGE_SCN_MEM_WRITE) mask |= 2;
        if (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) mask |= 4;
        add(section.VirtualAddress, std::max(section.Misc.VirtualSize, section.SizeOfRawData), mask);
    }
    for (std::size_t first = 0; first < permissions.size();) {
        std::size_t end = first + 1;
        while (end < permissions.size() && permissions[end] == permissions[first]) ++end;
        DWORD old = 0;
        if (!VirtualProtectEx(process, reinterpret_cast<void*>(allocation.Base() + first * page), (end - first) * page, PageProtection(permissions[first]), &old)) Fail(LastError("VirtualProtectEx image"));
        first = end;
    }
}

// All instructions below are explicit AMD64 machine code, with no copied
// compiler-generated function, absolute instruction address, or hidden CRT use.
// The caller owns remote context and image allocations until process exit.
struct BootstrapContext {
    std::uint64_t imageBase;
    std::uint64_t entryPoint;
    std::uint64_t start;
    std::uint64_t logPath;
    std::uint64_t addFunctionTable;
    std::uint64_t functionTable;
    std::uint32_t functionCount;
    std::uint32_t status;
    std::uint32_t startResult;
    std::uint32_t unwindRegistered;
    std::uint64_t bootstrapFunctionTable;
    std::uint64_t bootstrapBase;
};
static_assert(sizeof(BootstrapContext) == 80);
static_assert(offsetof(BootstrapContext, status) == 52);
static_assert(offsetof(BootstrapContext, bootstrapFunctionTable) == 64);

struct Code {
    std::vector<std::uint8_t> bytes;
    void Emit(std::initializer_list<std::uint8_t> values) { bytes.insert(bytes.end(), values); }
    std::size_t Jump(std::uint8_t condition) {
        Emit({0x0F, condition, 0, 0, 0, 0});
        return bytes.size() - 4;
    }
    std::size_t JumpAlways() { Emit({0xE9, 0, 0, 0, 0}); return bytes.size() - 4; }
    void Patch(std::size_t displacement, std::size_t target) {
        const auto delta = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(displacement + 4);
        if (delta < INT32_MIN || delta > INT32_MAX) Fail("Bootstrap branch displacement overflow");
        const auto encoded = static_cast<std::int32_t>(delta);
        std::memcpy(bytes.data() + displacement, &encoded, sizeof(encoded));
    }
};

Code Bootstrap() {
    Code code;
    code.Emit({0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xCB}); // push rbx; sub rsp,32; mov rbx,rcx
    code.Emit({0x48, 0x8B, 0x4B, 0x40, 0xBA, 0x01, 0, 0, 0, 0x4C, 0x8B, 0x43, 0x48}); // rcx=bootstrapTable,edx=1,r8=bootstrapBase
    code.Emit({0xFF, 0x53, 0x20, 0x84, 0xC0}); // call addFunctionTable;test al,al
    const auto failedBootstrapUnwind = code.Jump(0x84);
    code.Emit({0x83, 0x7B, 0x30, 0x00}); // cmp dword ptr [rbx+48],0
    const auto noUnwind = code.Jump(0x84);
    code.Emit({0x48, 0x8B, 0x4B, 0x28, 0x8B, 0x53, 0x30, 0x4C, 0x8B, 0x03}); // rcx=table, edx=count,r8=base
    code.Emit({0xFF, 0x53, 0x20, 0x84, 0xC0}); // call [rbx+32]; test al,al
    const auto failedUnwind = code.Jump(0x84);
    code.Emit({0xC7, 0x43, 0x3C, 0x01, 0, 0, 0}); // unwindRegistered=1
    code.Patch(noUnwind, code.bytes.size());
    code.Emit({0x48, 0x8B, 0x0B, 0xBA, 0x01, 0, 0, 0, 0x45, 0x31, 0xC0}); // rcx=base,edx=DLL_PROCESS_ATTACH,r8d=0
    code.Emit({0xFF, 0x53, 0x08, 0x85, 0xC0}); // call entry; test eax,eax
    const auto failedAttach = code.Jump(0x84);
    code.Emit({0x48, 0x8B, 0x4B, 0x18, 0xFF, 0x53, 0x10, 0x89, 0x43, 0x38}); // rcx=log;call start;store startResult
    code.Emit({0xC7, 0x43, 0x34, 0x03, 0, 0, 0}); // status=3 (start returned)
    const auto done = code.JumpAlways();
    code.Patch(failedBootstrapUnwind, code.bytes.size());
    code.Patch(failedUnwind, code.bytes.size());
    code.Emit({0xC7, 0x43, 0x34, 0x01, 0, 0, 0}); // status=1 (unwind failed)
    const auto unwindDone = code.JumpAlways();
    code.Patch(failedAttach, code.bytes.size());
    code.Emit({0xC7, 0x43, 0x34, 0x02, 0, 0, 0}); // status=2 (attach rejected)
    code.Patch(done, code.bytes.size());
    code.Patch(unwindDone, code.bytes.size());
    code.Emit({0x8B, 0x43, 0x34, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3}); // eax=status;add rsp,32;pop rbx;ret
    return code;
}

void CheckArchitecture(HANDLE process) {
    USHORT processMachine = 0, nativeMachine = 0;
    if (!IsWow64Process2(process, &processMachine, &nativeMachine)) Fail(LastError("IsWow64Process2"));
    if (processMachine != IMAGE_FILE_MACHINE_UNKNOWN || nativeMachine != IMAGE_FILE_MACHINE_AMD64) Fail("Target must be a native AMD64 process");
}

struct Arguments {
    DWORD pid = 0;
    std::filesystem::path dll;
    std::filesystem::path log;
    std::uintptr_t base = 0;
    bool stop = false;
};

std::uint64_t Number(const std::wstring& text, int radix) {
    if (text.empty() || text[0] == L'-' || text[0] == L'+') Fail("Invalid numeric argument");
    std::size_t used = 0;
    const auto value = std::stoull(text, &used, radix);
    if (used != text.size()) Fail("Invalid numeric argument suffix");
    return value;
}

Arguments Parse(int argc, wchar_t** argv) {
    Arguments result;
    bool seenPid = false, seenDll = false, seenLog = false, seenBase = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if (argument == L"--stop") { if (result.stop) Fail("Duplicate --stop"); result.stop = true; continue; }
        if (i + 1 >= argc) Fail("Argument is missing its value");
        const std::wstring value = argv[++i];
        if (argument == L"--pid" && !seenPid) {
            const auto number = Number(value, 10);
            if (!number || number > MAXDWORD) Fail("PID is outside valid DWORD range");
            result.pid = static_cast<DWORD>(number); seenPid = true;
        } else if (argument == L"--dll" && !seenDll) { result.dll = value; seenDll = true; }
        else if (argument == L"--log" && !seenLog) { result.log = value; seenLog = true; }
        else if (argument == L"--base" && !seenBase) { result.base = static_cast<std::uintptr_t>(Number(value, 16)); seenBase = true; }
        else Fail("Unknown or duplicate command-line argument");
    }
    if (!result.pid || result.dll.empty() || !result.dll.is_absolute()) Fail("--pid and absolute --dll are required");
    if (result.stop) {
        if (!result.base || !result.log.empty()) Fail("Stop requires --base HEX and does not accept --log");
    } else {
        if (result.log.empty() || !result.log.is_absolute() || result.base) Fail("Start requires absolute --log and does not accept --base");
        if (!std::filesystem::is_directory(result.log.parent_path())) Fail("Log parent directory does not exist; create the project diagnostics directory first");
    }
    return result;
}

int Start(HANDLE process, const Arguments& arguments) {
    PeImage pe(arguments.dll);
    const auto startRva = pe.ExportRva(kOverlayStartExport);
    pe.ExportRva(kOverlayStopExport);
    const auto unwindCount = pe.UnwindCount();
    const ImportResolver resolver(process, arguments.pid);
    const auto addFunctionTable = resolver.Named(L"ntdll.dll", "RtlAddFunctionTable");
    RemoteAllocation mapped(process, pe.image.size(), reinterpret_cast<void*>(pe.nt.OptionalHeader.ImageBase));
    pe.Relocate(mapped.Base());
    resolver.FixImports(pe);
    mapped.Write(pe.image.data(), pe.image.size());
    ProtectImage(process, mapped, pe);
    if (!FlushInstructionCache(process, mapped.address, mapped.size)) Fail(LastError("FlushInstructionCache image"));
    const auto log = arguments.log.wstring();
    RemoteAllocation remoteLog(process, (log.size() + 1) * sizeof(wchar_t));
    remoteLog.Write(log.c_str(), (log.size() + 1) * sizeof(wchar_t));
    auto bootstrap = Bootstrap();
    const auto codeSize = static_cast<DWORD>(bootstrap.bytes.size());
    while (bootstrap.bytes.size() % 4) bootstrap.bytes.push_back(0);
    const auto bootstrapTableRva = static_cast<DWORD>(bootstrap.bytes.size());
    const auto bootstrapUnwindRva = bootstrapTableRva + static_cast<DWORD>(sizeof(RUNTIME_FUNCTION));
    const RUNTIME_FUNCTION bootstrapFunction{0, codeSize, bootstrapUnwindRva};
    const auto table = reinterpret_cast<const std::uint8_t*>(&bootstrapFunction);
    bootstrap.bytes.insert(bootstrap.bytes.end(), table, table + sizeof(bootstrapFunction));
    // UNWIND_INFO v1: prolog size 5, two codes in descending prolog order:
    // offset 5 UWOP_ALLOC_SMALL(opinfo=3 => 32 bytes), offset 1 UWOP_PUSH_NONVOL(RBX).
    bootstrap.Emit({1, 5, 2, 0, 5, 0x32, 1, 0x30});
    RemoteAllocation remoteCode(process, bootstrap.bytes.size());
    remoteCode.Write(bootstrap.bytes.data(), bootstrap.bytes.size());
    BootstrapContext context{
        mapped.Base(), mapped.Base() + pe.nt.OptionalHeader.AddressOfEntryPoint,
        mapped.Base() + startRva, remoteLog.Base(), addFunctionTable,
        unwindCount ? mapped.Base() + pe.Directory(IMAGE_DIRECTORY_ENTRY_EXCEPTION).VirtualAddress : 0,
        unwindCount, 0, 0, 0, remoteCode.Base() + bootstrapTableRva, remoteCode.Base(),
    };
    RemoteAllocation remoteContext(process, sizeof(context));
    remoteContext.Write(&context, sizeof(context));
    DWORD previous = 0;
    if (!VirtualProtectEx(process, remoteCode.address, remoteCode.size, PAGE_EXECUTE_READ, &previous)) Fail(LastError("VirtualProtectEx bootstrap"));
    if (!FlushInstructionCache(process, remoteCode.address, remoteCode.size)) Fail(LastError("FlushInstructionCache bootstrap"));
    std::cout << "Mapped image prepared at 0x" << std::hex << mapped.Base() << std::dec << "; dispatching bootstrap.\n";
    Handle thread(CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteCode.address), remoteContext.address, 0, nullptr));
    if (!thread.value) Fail(LastError("CreateRemoteThread bootstrap"));
    // From this point onward remote code may have registered unwind metadata,
    // initialized the CRT, or installed callbacks. Retain every allocation even
    // on failed startup or timeout. Never terminate the thread or detach/free.
    mapped.Retain(); remoteLog.Retain(); remoteContext.Retain(); remoteCode.Retain();
    const auto wait = loader::WaitForBootstrap(
        [&](DWORD milliseconds) { return WaitForSingleObject(thread.value, milliseconds); },
        [] { std::cout << "Bootstrap is still initializing after 15 seconds; waiting up to 60 seconds total.\n" << std::flush; });
    if (wait != WAIT_OBJECT_0) {
        if (wait == WAIT_TIMEOUT)
            std::cerr << "Bootstrap startup is pending after 60 seconds; initialization may still finish. Completion is unconfirmed.\n";
        else
            std::cerr << "Bootstrap wait failed (Win32 " << GetLastError() << "); completion is unconfirmed.\n";
        std::cerr << "All remote allocations remain resident; no thread was terminated. Restart CS2 normally before another load attempt.\n";
        return wait == WAIT_TIMEOUT ? loader::kMapperPending : loader::kMapperUnconfirmed;
    }
    DWORD exitCode = 0;
    if (!GetExitCodeThread(thread.value, &exitCode)) Fail(LastError("GetExitCodeThread bootstrap"));
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, remoteContext.address, &context, sizeof(context), &read) || read != sizeof(context)) Fail(LastError("Read bootstrap result"));
    std::cout << "Remote bootstrap status=" << context.status << ", thread exit=" << exitCode
        << ", unwind registered=" << context.unwindRegistered << ", OverlayStart result=" << context.startResult << ".\n";
    std::cout << "Image base: 0x" << std::hex << mapped.Base() << std::dec << ". Code remains resident until normal process exit.\n";
    if (context.status == 1) std::cerr << "RtlAddFunctionTable rejected a bootstrap/image exception table; entrypoint was not called.\n";
    else if (context.status == 2) std::cerr << "DLL CRT entrypoint returned FALSE; OverlayStart was not called.\n";
    else if (context.status != 3 || exitCode != 3) std::cerr << "Bootstrap did not complete normally; inspect the thread exit status.\n";
    return loader::BootstrapSucceeded(context.status, exitCode, context.startResult) ? 0 : 3;
}

int Stop(HANDLE process, const Arguments& arguments) {
    const PeImage pe(arguments.dll);
    const auto stopRva = pe.ExportRva(kOverlayStopExport);
    if (arguments.base > std::numeric_limits<std::uintptr_t>::max() - pe.image.size()) Fail("Mapped base range overflows");
    // Confirm the supplied base still contains our exact image headers before
    // calling an address derived from the on-disk DLL.
    std::vector<std::uint8_t> headers(pe.nt.OptionalHeader.SizeOfHeaders);
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<void*>(arguments.base), headers.data(), headers.size(), &read) || read != headers.size()) Fail(LastError("Read mapped image headers"));
    if (!std::equal(headers.begin(), headers.end(), pe.image.begin())) Fail("Supplied base headers do not match the supplied DLL");
    MEMORY_BASIC_INFORMATION memory{};
    const auto functionAddress = arguments.base + stopRva;
    if (!VirtualQueryEx(process, reinterpret_cast<void*>(functionAddress), &memory, sizeof(memory))) Fail(LastError("VirtualQueryEx stop export"));
    const auto protection = memory.Protect & 0xFF;
    if (memory.State != MEM_COMMIT || (memory.Protect & PAGE_GUARD) ||
        (protection != PAGE_EXECUTE && protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)) Fail("Mapped stop export memory is not executable");
    Handle thread(CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(functionAddress), nullptr, 0, nullptr));
    if (!thread.value) Fail(LastError("CreateRemoteThread stop"));
    const auto wait = WaitForSingleObject(thread.value, kStopWaitMilliseconds);
    if (wait != WAIT_OBJECT_0) {
        std::cerr << "Stop request " << (wait == WAIT_TIMEOUT ? "timed out" : "wait failed") << "; thread and mapped image were not terminated or unloaded.\n";
        return 2;
    }
    DWORD result = 0;
    if (!GetExitCodeThread(thread.value, &result)) Fail(LastError("GetExitCodeThread stop"));
    std::cout << "OverlayRequestStop result=" << result << ". Stop is cooperative; consult the DLL log for completion.\n"
        << "Stopped and resident is not physically unloaded. Code and unwind metadata remain until normal process exit.\n";
    return result == ERROR_SUCCESS ? 0 : 3;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const auto arguments = Parse(argc, argv);
        Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
            PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD | SYNCHRONIZE, FALSE, arguments.pid));
        if (!process.value) Fail(LastError("OpenProcess"));
        CheckArchitecture(process.value);
        return arguments.stop ? Stop(process.value, arguments) : Start(process.value, arguments);
    } catch (const std::exception& error) {
        std::cerr << "Mapper: " << error.what() << "\n"
            << "Usage: mapper.exe --pid N --dll ABSOLUTE_DLL --log ABSOLUTE_LOG\n"
            << "       mapper.exe --stop --pid N --base HEX --dll SAME_ABSOLUTE_DLL\n";
        return 1;
    }
}
