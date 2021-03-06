﻿// created by i-saint
// distributed under Creative Commons Attribution (CC BY) license.
// https://github.com/i-saint/DynamicPatcher

#include "DynamicPatcher.h"
#include "dpInternal.h"
#pragma comment(lib, "dbghelp.lib")

dpMutex::ScopedLock::ScopedLock(dpMutex &v) : mutex(v) { mutex.lock(); }
dpMutex::ScopedLock::~ScopedLock() { mutex.unlock(); }
dpMutex::dpMutex() { ::InitializeCriticalSection(&m_cs); }
dpMutex::~dpMutex() { ::DeleteCriticalSection(&m_cs); }
void dpMutex::lock() { ::EnterCriticalSection(&m_cs); }
void dpMutex::unlock() { ::LeaveCriticalSection(&m_cs); }


template<size_t N>
inline int dpVSprintf(char (&buf)[N], const char *format, va_list vl)
{
    return _vsnprintf(buf, N, format, vl);
}

static const int DPRINTF_MES_LENGTH  = 4096;
void dpPrintV(const char* fmt, va_list vl)
{
    char buf[DPRINTF_MES_LENGTH];
    dpVSprintf(buf, fmt, vl);
    ::OutputDebugStringA(buf);
}

dpAPI void dpPrint(const char* fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    dpPrintV(fmt, vl);
    va_end(vl);
}

void dpPrintError(const char* fmt, ...)
{
    if((dpGetConfig().log_flags&dpE_LogError)==0) { return; }
    std::string format = std::string("dp error: ")+fmt; // OutputDebugStringA() は超遅いので dpPrint() 2 回呼ぶよりこうした方がまだ速い
    va_list vl;
    va_start(vl, fmt);
    dpPrintV(format.c_str(), vl);
    va_end(vl);
}

void dpPrintWarning(const char* fmt, ...)
{
    if((dpGetConfig().log_flags&dpE_LogWarning)==0) { return; }
    std::string format = std::string("dp warning: ")+fmt;
    va_list vl;
    va_start(vl, fmt);
    dpPrintV(format.c_str(), vl);
    va_end(vl);
}

void dpPrintInfo(const char* fmt, ...)
{
    if((dpGetConfig().log_flags&dpE_LogInfo)==0) { return; }
    std::string format = std::string("dp info: ")+fmt;
    va_list vl;
    va_start(vl, fmt);
    dpPrintV(format.c_str(), vl);
    va_end(vl);
}

void dpPrintDetail(const char* fmt, ...)
{
    if((dpGetConfig().log_flags&dpE_LogDetail)==0) { return; }
    std::string format = std::string("dp detail: ")+fmt;
    va_list vl;
    va_start(vl, fmt);
    dpPrintV(format.c_str(), vl);
    va_end(vl);
}

dpAPI bool dpDemangle(const char *mangled, char *demangled, size_t buflen)
{
    return ::UnDecorateSymbolName(mangled, demangled, (DWORD)buflen, UNDNAME_NAME_ONLY)!=0;
}



// 位置指定版 VirtualAlloc()
// location より大きいアドレスの最寄りの位置にメモリを確保する。
void* dpAllocateForward(size_t size, void *location)
{
    if(size==0) { return NULL; }
    static size_t base = (size_t)location;

    // ドキュメントには、アドレス指定の VirtualAlloc() は指定先が既に予約されている場合最寄りの領域を返す、
    // と書いてるように見えるが、実際には NULL が返ってくるようにしか見えない。
    // なので成功するまでアドレスを進めつつリトライ…。
    void *ret = NULL;
    const size_t step = 0x10000; // 64kb
    for(size_t i=0; ret==NULL; ++i) {
        ret = ::VirtualAlloc((void*)((size_t)base+(step*i)), size, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    }
    return ret;
}

// 位置指定版 VirtualAlloc()
// location より小さいアドレスの最寄りの位置にメモリを確保する。
void* dpAllocateBackward(size_t size, void *location)
{
    if(size==0) { return NULL; }
    static size_t base = (size_t)location;

    void *ret = NULL;
    const size_t step = 0x10000; // 64kb
    for(size_t i=0; ret==NULL; ++i) {
        ret = ::VirtualAlloc((void*)((size_t)base-(step*i)), size, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    }
    return ret;
}

// exe がマップされている領域の後ろの最寄りの場所にメモリを確保する。
// jmp 命令などの移動量は x64 でも 32bit なため、32bit に収まらない距離を飛ぼうとした場合あらぬところに着地して死ぬ。
// そして new や malloc() だと 32bit に収まらない遥か彼方にメモリが確保されてしまうため、これが必要になる。
// .exe がマップされている領域を調べ、その近くに VirtualAlloc() するという内容。
void* dpAllocateModule(size_t size)
{
    return dpAllocateBackward(size, GetModuleHandleA(nullptr));
}

void dpDeallocate(void *location, size_t size)
{
    ::VirtualFree(location, size, MEM_RELEASE);
}

dpTime dpGetMTime(const char *path)
{
    union RetT
    {
        FILETIME filetime;
        dpTime qword;
    } ret;
    HANDLE h = ::CreateFileA(path, 0, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ::GetFileTime(h, NULL, NULL, &ret.filetime);
    ::CloseHandle(h);
    return ret.qword;
}

dpTime dpGetSystemTime()
{
    union RetT
    {
        FILETIME filetime;
        dpTime qword;
    } ret;
    SYSTEMTIME systime;
    ::GetSystemTime(&systime);
    ::SystemTimeToFileTime(&systime, &ret.filetime);
    return ret.qword;
}

bool dpCopyFile(const char *srcpath, const char *dstpath)
{
    return ::CopyFileA(srcpath, dstpath, FALSE)==TRUE;
}

bool dpWriteFile(const char *path, const void *data, size_t size)
{
    if(FILE *f=fopen(path, "wb")) {
        fwrite((const char*)data, 1, size, f);
        fclose(f);
        return true;
    }
    return false;
}

bool dpDeleteFile(const char *path)
{
    return ::DeleteFileA(path)==TRUE;
}

bool dpFileExists( const char *path )
{
    return ::GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES;
}

size_t dpSeparateDirFile(const char *path, std::string *dir, std::string *file)
{
    size_t f_len=0;
    size_t l = strlen(path);
    for(size_t i=0; i<l; ++i) {
        if(path[i]=='\\' || path[i]=='/') { f_len=i+1; }
    }
    if(dir)  { dir->insert(dir->end(), path, path+f_len); }
    if(file) { file->insert(file->end(), path+f_len, path+l); }
    return f_len;
}

size_t dpSeparateFileExt(const char *filename, std::string *file, std::string *ext)
{
    size_t dir_len=0;
    size_t l = strlen(filename);
    for(size_t i=0; i<l; ++i) {
        if(filename[i]=='.') { dir_len=i+1; }
    }
    if(file){ file->insert(file->end(), filename, filename+dir_len); }
    if(ext) { ext->insert(ext->end(), filename+dir_len, filename+l); }
    return dir_len;
}

size_t dpGetCurrentModulePath(char *buf, size_t buflen)
{
    HMODULE mod = 0;
    ::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)&dpGetCurrentModulePath, &mod);
    return ::GetModuleFileNameA(mod, buf, (DWORD)buflen);
}

size_t dpGetMainModulePath(char *buf, size_t buflen)
{
    HMODULE mod = ::GetModuleHandleA(nullptr);
    return ::GetModuleFileNameA(mod, buf, (DWORD)buflen);
}

void dpSanitizePath(std::string &path)
{
    dpEach(path, [](char &c){
        if(c=='/') { c='\\'; }
    });
}



dpSymbol::dpSymbol(const char *nam, void *addr, int fla, int sect, dpBinary *bin)
    : name(nam), address(addr), flags(fla), section(sect), binary(bin)
{}
dpSymbol::~dpSymbol()
{
    if((flags&dpE_NameNeedsDelete)!=0) {
        delete[] name;
    }
}
const dpSymbolS& dpSymbol::simplify() const { return (const dpSymbolS&)*this; }
bool dpSymbol::partialLink() { return binary->partialLink(section); }


dpSectionAllocator::dpSectionAllocator(void *data, size_t size)
    : m_data(data), m_size(size), m_used(0)
{}

void* dpSectionAllocator::allocate(size_t size, size_t align)
{
    size_t base = (size_t)m_data;
    size_t mask = align - 1;
    size_t aligned = (base + m_used + mask) & ~mask;
    if(aligned+size <= base+m_size) {
        m_used = (aligned+size) - base;
        return m_data==NULL ? NULL : (void*)aligned;
    }
    return NULL;
}

size_t dpSectionAllocator::getUsed() const { return m_used; }



class dpTrampolineAllocator::Page
{
public:
    struct Block {
        union {
            char data[block_size];
            Block *next;
        };
    };
    Page(void *base);
    ~Page();
    void* allocate();
    bool deallocate(void *v);
    bool isInsideMemory(void *p) const;
    bool isInsideJumpRange(void *p) const;

private:
    void *m_data;
    Block *m_freelist;
};

dpTrampolineAllocator::Page::Page(void *base)
    : m_data(nullptr), m_freelist(nullptr)
{
    m_data = dpAllocateBackward(page_size, base);
    m_freelist = (Block*)m_data;
    size_t n = page_size / block_size;
    for(size_t i=0; i<n-1; ++i) {
        m_freelist[i].next = m_freelist+i+1;
    }
    m_freelist[n-1].next = nullptr;
}

dpTrampolineAllocator::Page::~Page()
{
    dpDeallocate(m_data, page_size);
}

void* dpTrampolineAllocator::Page::allocate()
{
    void *ret = nullptr;
    if(m_freelist) {
        ret = m_freelist;
        m_freelist = m_freelist->next;
    }
    return ret;
}

bool dpTrampolineAllocator::Page::deallocate(void *v)
{
    if(v==nullptr) { return false; }
    bool ret = false;
    if(isInsideMemory(v)) {
        Block *b = (Block*)v;
        b->next = m_freelist;
        m_freelist = b;
        ret = true;
    }
    return ret;
}

bool dpTrampolineAllocator::Page::isInsideMemory(void *p) const
{
    size_t loc = (size_t)p;
    size_t base = (size_t)m_data;
    return loc>=base && loc<base+page_size;
}

bool dpTrampolineAllocator::Page::isInsideJumpRange( void *p ) const
{
    size_t loc = (size_t)p;
    size_t base = (size_t)m_data;
    size_t dist = base<loc ? loc-base : base-loc;
    return dist < 0x7fff0000;
}


dpTrampolineAllocator::dpTrampolineAllocator()
{
}

dpTrampolineAllocator::~dpTrampolineAllocator()
{
    dpEach(m_pages, [](Page *p){ delete p; });
    m_pages.clear();
}

void* dpTrampolineAllocator::allocate(void *location)
{
    void *ret = nullptr;
    if(Page *page=findCandidatePage(location)) {
        ret = page->allocate();
    }
    if(!ret) {
        Page *page = createPage(location);
        ret = page->allocate();
    }
    return ret;
}

bool dpTrampolineAllocator::deallocate(void *v)
{
    if(Page *page=findOwnerPage(v)) {
        return page->deallocate(v);
    }
    return false;
}

dpTrampolineAllocator::Page* dpTrampolineAllocator::createPage(void *location)
{
    Page *p = new Page(location);
    m_pages.push_back(p);
    return p;
}

dpTrampolineAllocator::Page* dpTrampolineAllocator::findOwnerPage(void *location)
{
    auto p = dpFind(m_pages, [=](const Page *p){ return p->isInsideMemory(location); });
    return p==m_pages.end() ? nullptr : *p;
}

dpTrampolineAllocator::Page* dpTrampolineAllocator::findCandidatePage(void *location)
{
    auto p = dpFind(m_pages, [=](const Page *p){ return p->isInsideJumpRange(location); });
    return p==m_pages.end() ? nullptr : *p;
}




template<size_t PageSize, size_t BlockSize>
class dpBlockAllocator<PageSize, BlockSize>::Page
{
public:
    struct Block {
        union {
            char data[block_size];
            Block *next;
        };
    };
    Page();
    ~Page();
    void* allocate();
    bool deallocate(void *v);
    bool isInsideMemory(void *p) const;

private:
    void *m_data;
    Block *m_freelist;
};

template<size_t PageSize, size_t BlockSize>
dpBlockAllocator<PageSize, BlockSize>::Page::Page()
    : m_data(nullptr), m_freelist(nullptr)
{
    m_data = malloc(page_size);
    m_freelist = (Block*)m_data;
    size_t n = page_size / block_size;
    for(size_t i=0; i<n-1; ++i) {
        m_freelist[i].next = m_freelist+i+1;
    }
    m_freelist[n-1].next = nullptr;
}

template<size_t PageSize, size_t BlockSize>
dpBlockAllocator<PageSize, BlockSize>::Page::~Page()
{
    free(m_data);
}

template<size_t PageSize, size_t BlockSize>
void* dpBlockAllocator<PageSize, BlockSize>::Page::allocate()
{
    void *ret = nullptr;
    if(m_freelist) {
        ret = m_freelist;
        m_freelist = m_freelist->next;
    }
    return ret;
}

template<size_t PageSize, size_t BlockSize>
bool dpBlockAllocator<PageSize, BlockSize>::Page::deallocate(void *v)
{
    if(v==nullptr) { return false; }
    bool ret = false;
    if(isInsideMemory(v)) {
        Block *b = (Block*)v;
        b->next = m_freelist;
        m_freelist = b;
        ret = true;
    }
    return ret;
}

template<size_t PageSize, size_t BlockSize>
bool dpBlockAllocator<PageSize, BlockSize>::Page::isInsideMemory(void *p) const
{
    size_t loc = (size_t)p;
    size_t base = (size_t)m_data;
    return loc>=base && loc<base+page_size;
}


template<size_t PageSize, size_t BlockSize>
dpBlockAllocator<PageSize, BlockSize>::dpBlockAllocator()
{
}

template<size_t PageSize, size_t BlockSize>
dpBlockAllocator<PageSize, BlockSize>::~dpBlockAllocator()
{
    dpEach(m_pages, [](Page *p){ delete p; });
    m_pages.clear();
}

template<size_t PageSize, size_t BlockSize>
void* dpBlockAllocator<PageSize, BlockSize>::allocate()
{
    for(size_t i=0; i<m_pages.size(); ++i) {
        if(void *ret=m_pages[i]->allocate()) {
            return ret;
        }
    }

    Page *p = new Page();
    m_pages.push_back(p);
    return p->allocate();
}

template<size_t PageSize, size_t BlockSize>
bool dpBlockAllocator<PageSize, BlockSize>::deallocate(void *v)
{
    auto p = dpFind(m_pages, [=](const Page *p){ return p->isInsideMemory(v); });
    if(p!=m_pages.end()) {
        (*p)->deallocate(v);
        return true;
    }
    return false;
}
template dpBlockAllocator<1024*256, sizeof(dpSymbol)>;


dpSymbolTable::dpSymbolTable() : m_partial_link(false)
{
}

void dpSymbolTable::addSymbol(dpSymbol *v)
{
    m_symbols.push_back(v);
}

void dpSymbolTable::merge(const dpSymbolTable &v)
{
    dpEach(v.m_symbols, [&](const dpSymbol *sym){
        m_symbols.push_back( const_cast<dpSymbol*>(sym) );
    });
    sort();
}

void dpSymbolTable::sort()
{
    std::sort(m_symbols.begin(), m_symbols.end(), dpLTPtr<dpSymbol>());
    m_symbols.erase(
        std::unique(m_symbols.begin(), m_symbols.end(), dpEQPtr<dpSymbol>()),
        m_symbols.end());
}

void dpSymbolTable::clear()
{
    m_symbols.clear();
}

void dpSymbolTable::enablePartialLink(bool v)
{
    m_partial_link = v;
}

size_t dpSymbolTable::getNumSymbols() const
{
    return m_symbols.size();
}

dpSymbol* dpSymbolTable::getSymbol(size_t i)
{
    dpSymbol *sym = m_symbols[i];
    return sym;
}

dpSymbol* dpSymbolTable::findSymbolByName(const char *name)
{
    auto p = std::lower_bound(m_symbols.begin(), m_symbols.end(), name,
        [](const dpSymbol *sym, const char *name){ return *sym<name; }
    );
    if(p!=m_symbols.end() && **p==name) {
        dpSymbol *sym = *p;
        if(m_partial_link) { sym->partialLink(); }
        return sym;
    }
    return nullptr;
}

dpSymbol* dpSymbolTable::findSymbolByAddress( void *addr )
{
    auto p = dpFind(m_symbols, [=](const dpSymbol *sym){ return sym->address==addr; });
    if(p!=m_symbols.end()) {
        dpSymbol *sym = *p;
        if(m_partial_link) { sym->partialLink(); }
        return sym;
    }
    return nullptr;
}

