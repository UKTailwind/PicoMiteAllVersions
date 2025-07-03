#ifndef __PICOMITE_H
#define __PICOMITE_H

#ifdef __cplusplus

#include <cstddef>
#include "ff.h"

#define CombinedPtrBufSize 512

#ifndef XIP_BASE
#define XIP_BASE 0x10000000
#endif

template<typename T> class CombinedPtrT;

class CombinedPtr {
    union {
        unsigned char* c;
        FSIZE_t f;
    } p;
    static_assert(sizeof(unsigned char*) == sizeof(FSIZE_t), "Incompatible pointer and FSIZE_t sizes");
    static FSIZE_t buff_base_offset;
    static unsigned char buff[CombinedPtrBufSize]; // small buffer to read sd-card not a lot times
    static bool buff_dirty; // флаг, что буфер изменён
public:
    static void flush();

    // Конструкторы
    CombinedPtr() : p{nullptr} {}
    /*explicit*/
    CombinedPtr(unsigned char* ptr) : p{ptr} {}
    CombinedPtr(const unsigned char* ptr) : p{(unsigned char*)ptr} {}
    CombinedPtr(char* ptr) : p{(unsigned char*)ptr} {}
    CombinedPtr(const char* ptr) : p{(unsigned char*)ptr} {}
    CombinedPtr(void* ptr) : p{(unsigned char*)ptr} {}
    CombinedPtr(const void* ptr) : p{(unsigned char*)ptr} {}
    CombinedPtr(FSIZE_t off) { p.f = off; }
    CombinedPtr(const CombinedPtr& other) : p{other.p.c} {}
    CombinedPtr(std::nullptr_t) : p{nullptr} {}

    // TODO: assert for case not in RAM ?
    unsigned char* raw(int _case = 0) const;
    unsigned char* ram() const { return p.c; };
    bool is_ram() const { return p.f >= 0x11000000; };
    bool on_sd() const { return p.f < XIP_BASE; };

    // Оператор приведения
    explicit operator FSIZE_t() const { return p.f; }

    // Присваивание
    CombinedPtr& operator=(std::nullptr_t) { p.c = 0; return *this; }
    CombinedPtr& operator=(unsigned char* ptr) { p.c = ptr; return *this; }
    CombinedPtr& operator=(char* ptr) { p.c = (unsigned char*)ptr; return *this; }
    CombinedPtr& operator=(void* ptr) { p.c = (unsigned char*)ptr; return *this; }
    CombinedPtr& operator=(const CombinedPtr& other) { p.c = other.p.c; return *this; }

    // Разыменование
    unsigned char operator*();
    //unsigned char* operator->() const { return p.c; }

    // Индексация
    unsigned char operator[](std::ptrdiff_t i);

    // Инкремент / декремент
    CombinedPtr& operator++() { ++p.c; return *this; }        // префикс ++
    CombinedPtr operator++(int) { CombinedPtr tmp(*this); ++p.c; return tmp; }  // постфикс ++

    CombinedPtr& operator--() { --p.c; return *this; }        // префикс --
    CombinedPtr operator--(int) { CombinedPtr tmp(*this); --p.c; return tmp; }  // постфикс --

    // Арифметика указателей
    CombinedPtr operator+(std::ptrdiff_t i) const { return CombinedPtr(p.c + i); }
    CombinedPtr operator-(std::ptrdiff_t i) const { return CombinedPtr(p.c - i); }
    std::ptrdiff_t operator-(const CombinedPtr& other) const { return p.c - other.p.c; }

    CombinedPtr& operator+=(std::ptrdiff_t i) { p.c += i; return *this; }
    CombinedPtr& operator-=(std::ptrdiff_t i) { p.c -= i; return *this; }

    // Операторы сравнения
    bool operator==(const CombinedPtr& other) const { return p.c == other.p.c; }
    bool operator!=(const CombinedPtr& other) const { return p.c != other.p.c; }
    bool operator<(const CombinedPtr& other) const { return p.c < other.p.c; }
    bool operator<=(const CombinedPtr& other) const { return p.c <= other.p.c; }
    bool operator>(const CombinedPtr& other) const { return p.c > other.p.c; }
    bool operator>=(const CombinedPtr& other) const { return p.c >= other.p.c; }
    bool operator!=(std::nullptr_t) const {
        return p.c != nullptr;
    }
    bool operator==(std::nullptr_t) const {
        return p.c == nullptr;
    }

    unsigned int operator&(unsigned int x) { return p.f & x; }

    // Явное преобразование в bool (проверка на nullptr)
    explicit operator bool() const { return p.c != nullptr; }

    // CFunction (if it exists) starts on the next word address after the program
    CombinedPtr align() { return CombinedPtr((unsigned char *)((p.f + 0b11) & ~0b11)); }
    CombinedPtr& write_byte(uint8_t v);
    double as_double();
    long long as_i64a();

   // friend class CombinedPtrI;
    template<typename T> friend class CombinedPtrT;
    template<typename T> CombinedPtr(const CombinedPtrT<T>& other);
    template<typename T> CombinedPtr& operator=(const CombinedPtrT<T>& other);
};

void sd_range_erase(FSIZE_t offset, FSIZE_t size);
void sd_range_program(FSIZE_t offset, CombinedPtr p, FSIZE_t size);

template<typename T>
class CombinedPtrT {
    CombinedPtr p;
public:
    CombinedPtrT() : p(nullptr) {}
    CombinedPtrT(T* ptr) : p(reinterpret_cast<uint8_t*>(ptr)) {}
    CombinedPtrT(const CombinedPtr& other) : p(other) {}
    CombinedPtrT(std::nullptr_t) : p(nullptr) {}

    T* raw(int _case) { return reinterpret_cast<T*>(p.raw(_case)); }

    CombinedPtrT& operator=(const CombinedPtr& other) { p = other; return *this; }
    CombinedPtrT& operator=(T* other) { p = reinterpret_cast<uint8_t*>(other); return *this; }

    inline T operator*() {
        if (!p.on_sd()) {
            return *reinterpret_cast<T*>(p.raw(300));
        }
        // SD-карта: читаем побайтово
        T val;
        for (size_t i = 0; i < sizeof(T); ++i)
            reinterpret_cast<uint8_t*>(&val)[i] = *(p + i);
        return val;
    }
    inline T operator[](std::ptrdiff_t i) {
        CombinedPtr base = p + i * sizeof(T);
        if (!p.on_sd()) {
            return *reinterpret_cast<T*>(base.raw(301));
        }
        T val = 0;
        for (size_t j = 0; j < sizeof(T); ++j)
            reinterpret_cast<uint8_t*>(&val)[j] = base[j];
        return val;
    }

    CombinedPtrT& operator++() { p += sizeof(T); return *this; }
    CombinedPtrT operator++(int) { CombinedPtrT tmp(*this); p += sizeof(T); return tmp; }

    CombinedPtrT& operator--() { p -= sizeof(T); return *this; }
    CombinedPtrT operator--(int) { CombinedPtrT tmp(*this); p -= sizeof(T); return tmp; }

    CombinedPtrT operator+(std::ptrdiff_t i) const { return CombinedPtrT(p + i * sizeof(T)); }
    CombinedPtrT operator-(std::ptrdiff_t i) const { return CombinedPtrT(p - i * sizeof(T)); }
    std::ptrdiff_t operator-(const CombinedPtrT& other) const { return (p - other.p) / sizeof(T); }

    CombinedPtrT& operator+=(std::ptrdiff_t i) { p += i * sizeof(T); return *this; }
    CombinedPtrT& operator-=(std::ptrdiff_t i) { p -= i * sizeof(T); return *this; }

    bool operator==(const CombinedPtrT& other) const { return p == other.p; }
    bool operator!=(const CombinedPtrT& other) const { return p != other.p; }
    bool operator<(const CombinedPtrT& other) const { return p < other.p; }
    bool operator<=(const CombinedPtrT& other) const { return p <= other.p; }
    bool operator>(const CombinedPtrT& other) const { return p > other.p; }
    bool operator>=(const CombinedPtrT& other) const { return p >= other.p; }

    explicit operator bool() const { return p.p.c != nullptr; }

    friend class CombinedPtr;
};

template<typename T>
CombinedPtr::CombinedPtr(const CombinedPtrT<T>& other) {
    this->p.c = other.p.p.c;
}

template<typename T>
CombinedPtr& CombinedPtr::operator=(const CombinedPtrT<T>& other) {
    this->p.c = other.p.p.c;
    return *this;
}

using CombinedPtrI = CombinedPtrT<int>;
using CombinedPtrLL = CombinedPtrT<long long>;
using CombinedPtrD = CombinedPtrT<double>;

size_t strlen(CombinedPtr src);
CombinedPtr strchr(CombinedPtr src, int ch);
void strcat(CombinedPtr dest, const char* src);
char *strcpy(char *dest, CombinedPtr src);
char *strncpy(char *dest, CombinedPtr src, size_t sz);
extern "C" bool str_equal2(CombinedPtr a, const unsigned char* b);
extern "C" int str_equal(const unsigned char* a, const unsigned char* b);
void strcat(char* dest, CombinedPtr src);
void *memcpy (uint8_t *dst, CombinedPtr src, size_t sz);
int strcasecmp(CombinedPtr s1, const char *s2);
int strncasecmp(CombinedPtr s1, const char *s2, size_t sz);

#endif // __cplusplus
#endif // __PICOMITE_H
