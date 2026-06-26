#pragma once
// Minimal Arduino String shim backed by std::string.
// Implements only the surface the ClockForge firmware uses: numeric/char-ptr
// construction, concatenation, length(), c_str(). Not a full Arduino String.
#include <string>
#include <cstdio>
#include <cstdint>
#include <cstring>

class String {
  public:
    std::string s;

    String() {}
    String(const char *cstr) : s(cstr ? cstr : "") {}
    String(const std::string &str) : s(str) {}
    String(const String &str) : s(str.s) {}
    String(char c) : s(1, c) {}

    // Integer constructors with optional base (Arduino: 10=DEC, 16=HEX, ...).
    String(int v, int base = 10) { fromInt((long)v, base); }
    String(unsigned int v, int base = 10) { fromUInt((unsigned long)v, base); }
    String(long v, int base = 10) { fromInt(v, base); }
    String(unsigned long v, int base = 10) { fromUInt(v, base); }

    // Float/double with optional decimal places (Arduino default = 2).
    String(double v, int decimals = 2) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%.*f", decimals, v);
        s = buf;
    }
    String(float v, int decimals = 2) : String((double)v, decimals) {}

    const char *c_str() const { return s.c_str(); }
    unsigned int length() const { return (unsigned int)s.length(); }
    void toCharArray(char *buf, unsigned int size) const {
        if (!buf || size == 0) return;
        unsigned int n = length();
        if (n > size - 1) n = size - 1;
        std::memcpy(buf, s.data(), n);
        buf[n] = 0;
    }
    char charAt(unsigned int i) const { return i < s.size() ? s[i] : 0; }
    char operator[](unsigned int i) const { return charAt(i); }
    bool operator==(const String &o) const { return s == o.s; }
    bool operator==(const char *o) const { return s == (o ? o : ""); }

    String &operator+=(const String &o) { s += o.s; return *this; }
    String &operator+=(const char *o) { s += (o ? o : ""); return *this; }
    String &operator+=(char c) { s += c; return *this; }

    String operator+(const String &o) const { String r(*this); r += o; return r; }
    String operator+(const char *o) const { String r(*this); r += o; return r; }
    String operator+(char c) const { String r(*this); r += c; return r; }

  private:
    void fromInt(long v, int base) {
        char buf[40];
        if (base == 16) snprintf(buf, sizeof(buf), "%lx", v);
        else if (base == 2) { binStr(buf, (unsigned long)v); }
        else snprintf(buf, sizeof(buf), "%ld", v);
        s = buf;
    }
    void fromUInt(unsigned long v, int base) {
        char buf[40];
        if (base == 16) snprintf(buf, sizeof(buf), "%lx", v);
        else if (base == 2) { binStr(buf, v); }
        else snprintf(buf, sizeof(buf), "%lu", v);
        s = buf;
    }
    static void binStr(char *buf, unsigned long v) {
        char tmp[40]; int n = 0;
        if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
        while (v) { tmp[n++] = '0' + (v & 1); v >>= 1; }
        int i = 0; while (n) buf[i++] = tmp[--n]; buf[i] = 0;
    }
};

inline String operator+(const char *lhs, const String &rhs) {
    return String(lhs) + rhs;
}
