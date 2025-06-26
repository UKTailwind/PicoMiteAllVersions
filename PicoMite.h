#include <cstddef>
#include "ff.h"

#define CombinedPtrBufSize 32

class CombinedPtr {
    union {
        unsigned char* c;
        FSIZE_t f;
    } p;
    static_assert(sizeof(unsigned char*) == sizeof(FSIZE_t), "Incompatible pointer and FSIZE_t sizes");
    FSIZE_t buff_base_offset = (FSIZE_t)-1;
    unsigned char buff[CombinedPtrBufSize]; // small buffer to read sd-card not a lot times
public:
    // Конструкторы
    CombinedPtr() : p{nullptr} {}
    CombinedPtr(unsigned char* ptr) : p{ptr} {}
    CombinedPtr(FSIZE_t off) { p.f = off; }
    CombinedPtr(const CombinedPtr& other) : p{other.p.c} {}

    // Присваивание
    CombinedPtr& operator=(unsigned char* ptr) { p.c = ptr; return *this; }
    CombinedPtr& operator=(const CombinedPtr& other) { p.c = other.p.c; return *this; }

    // Разыменование
    unsigned char operator*();
    //unsigned char* operator->() const { return p.c; }

    // Индексация
    unsigned char& operator[](std::ptrdiff_t i) const { return p.c[i]; }

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

    // Получить "сырой" указатель
    unsigned char* get() const { return p.c; }

    // Явное преобразование в bool (проверка на nullptr)
    explicit operator bool() const { return p.c != nullptr; }
};
