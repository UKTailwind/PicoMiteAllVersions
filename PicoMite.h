#ifndef __PICOMITE_H
#define __PICOMITE_H

#ifdef __cplusplus

#include <cstddef>
#include "ff.h"

#define CombinedPtrBufSize 32

//class CombinedPtrI;
template<typename T> class CombinedPtrT;

class CombinedPtr {
    union {
        unsigned char* c;
        FSIZE_t f;
    } p;
    static_assert(sizeof(unsigned char*) == sizeof(FSIZE_t), "Incompatible pointer and FSIZE_t sizes");
    static FSIZE_t buff_base_offset;
    static unsigned char buff[CombinedPtrBufSize]; // small buffer to read sd-card not a lot times
public:
    // Конструкторы
    CombinedPtr() : p{nullptr} {}
    /*explicit*/ CombinedPtr(unsigned char* ptr) : p{ptr} {}
    CombinedPtr(char* ptr) : p{(unsigned char*)ptr} {}
    CombinedPtr(void* ptr) : p{(unsigned char*)ptr} {}
    CombinedPtr(FSIZE_t off) { p.f = off; }
    CombinedPtr(const CombinedPtr& other) : p{other.p.c} {}
    CombinedPtr(std::nullptr_t) : p{nullptr} {}

    // TODO: assert for case not in RAM ?
    unsigned char* raw() const { return p.c; }

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
    CombinedPtr aligh() { return CombinedPtr((unsigned char *)((p.f + 0b11) & ~0b11)); }
    CombinedPtr& write_byte(uint8_t v);
    double as_double();
    long long as_i64a();

   // friend class CombinedPtrI;
    template<typename T> friend class CombinedPtrT;
    template<typename T> CombinedPtr(const CombinedPtrT<T>& other);
    template<typename T> CombinedPtr& operator=(const CombinedPtrT<T>& other);
};
/*
class CombinedPtrI {
    CombinedPtr p;
public:
    // Конструкторы
    CombinedPtrI() : p(nullptr) {}
    CombinedPtrI(int* ptr) : p((uint8_t*)ptr) {}
    CombinedPtrI(unsigned int* ptr) : p((uint8_t*)ptr) {}
    CombinedPtrI(const CombinedPtr& other) : p(other) {}
    CombinedPtrI(std::nullptr_t) : p(nullptr) {}

    // TODO: assert for case not in RAM ?
    unsigned int* raw() { return (unsigned int*)p.p.c; }

    // Оператор приведения
    explicit operator FSIZE_t() const { return p.p.f; }
        
    // Присваивание
    CombinedPtrI& operator=(const CombinedPtr& other) { p = other; return *this; }
    CombinedPtrI& operator=(int* other) { p.p.c = (uint8_t*)other ; return *this; }
    CombinedPtrI& operator=(unsigned int* other) { p.p.c = (uint8_t*)other ; return *this; }

    // Разыменование
    unsigned int operator*();
    //unsigned int* operator->() const;

    // Индексация
    unsigned int operator[](std::ptrdiff_t i);

    // Инкремент / декремент
    CombinedPtrI& operator++() { p.p.c += 4; return *this; }        // префикс ++
    CombinedPtrI operator++(int) { CombinedPtr tmp(p); p.p.c += 4; return tmp; }  // постфикс ++

    CombinedPtrI& operator--() { p.p.c -= 4; return *this; }        // префикс --
    CombinedPtrI operator--(int) { CombinedPtrI tmp(p); p.p.c -= 4; return tmp; }  // постфикс --

    // Арифметика указателей
    CombinedPtrI operator+(std::ptrdiff_t i) const { return CombinedPtrI(p + (i << 2)); }
    CombinedPtrI operator-(std::ptrdiff_t i) const { return CombinedPtrI(p - (i << 2)); }
    std::ptrdiff_t operator-(const CombinedPtrI& other) const { return p - other.p; }

    CombinedPtrI& operator+=(std::ptrdiff_t i) { p += i << 2; return *this; }
    CombinedPtrI& operator-=(std::ptrdiff_t i) { p -= i << 2; return *this; }

    // Операторы сравнения
    bool operator==(const CombinedPtrI& other) const { return p == other.p; }
    bool operator!=(const CombinedPtrI& other) const { return p != other.p; }
    bool operator<(const CombinedPtrI& other) const { return p < other.p; }
    bool operator<=(const CombinedPtrI& other) const { return p <= other.p; }
    bool operator>(const CombinedPtrI& other) const { return p > other.p; }
    bool operator>=(const CombinedPtrI& other) const { return p >= other.p; }

    // Явное преобразование в bool (проверка на nullptr)
    explicit operator bool() const { return p.p.c != nullptr; }

    friend class CombinedPtr;
};
*/
template<typename T>
class CombinedPtrT {
    CombinedPtr p;
public:
    CombinedPtrT() : p(nullptr) {}
    CombinedPtrT(T* ptr) : p(reinterpret_cast<uint8_t*>(ptr)) {}
    CombinedPtrT(const CombinedPtr& other) : p(other) {}
    CombinedPtrT(std::nullptr_t) : p(nullptr) {}

    T* raw() { return reinterpret_cast<T*>(p.raw()); }

    CombinedPtrT& operator=(const CombinedPtr& other) { p = other; return *this; }
    CombinedPtrT& operator=(T* other) { p = reinterpret_cast<uint8_t*>(other); return *this; }

    T operator*();
    T operator[](std::ptrdiff_t i);

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

    explicit operator bool() const { return p.raw() != nullptr; }

    friend class CombinedPtr;
};

using CombinedPtrI = CombinedPtrT<int>;
using CombinedPtrLL = CombinedPtrT<long long>;
using CombinedPtrD = CombinedPtrT<double>;

size_t strlen(CombinedPtr src);
CombinedPtr strchr(CombinedPtr src, int ch);
void strcat(CombinedPtr dest, const char* src);
char *strcpy(char *dest, CombinedPtr src);
char *strncpy(char *dest, CombinedPtr src, size_t sz);
inline static bool str_equal(CombinedPtr a, const unsigned char* b) {
    uint8_t len = *a;
    for (int i = 0; i < len; ++i) {
        if (b[i] == '\0' || (a + 1 + i).operator*() != b[i])
            return false;
    }
    return b[len] == '\0'; // строка b не должна быть длиннее a
}
void strcat(char* dest, CombinedPtr src);
void *memcpy (uint8_t *dst, CombinedPtr src, size_t sz);
int strcasecmp(CombinedPtr s1, const char *s2);
int strncasecmp(CombinedPtr s1, const char *s2, size_t sz);

#endif // __cplusplus
#endif // __PICOMITE_H
