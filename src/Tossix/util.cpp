#include <stdlib.h>
#include <string>

using namespace std;

//Filename-safe Base64 (-_ for the last two digits) with no padding
void to_base64(const unsigned char* p, size_t l, char *s)
{
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    size_t i;
    size_t mod = l % 3;
    size_t ll = l - mod;
    unsigned int triad;
    for(i = 0; i < ll; i += 3)
    {
        triad = (p[i] << 16) | (p[i + 1] << 8) | p[i + 2];
        *s++ = ALPHABET[(triad >> 18) & 0x3f];
        *s++ = ALPHABET[(triad >> 12) & 0x3f];
        *s++ = ALPHABET[(triad >> 6) & 0x3f];
        *s++ = ALPHABET[triad & 0x3f];
    }
    if(mod == 1)
    {
        *s++ = ALPHABET[(p[i] >> 2) & 0x3f];
        *s++ = ALPHABET[(p[i] << 4) & 0x3f];
    }
    if(mod == 2)
    {
        triad = (p[i] << 8) | p[i + 1];
        *s++ = ALPHABET[(triad >> 10) & 0x3f];
        *s++ = ALPHABET[(triad >> 4) & 0x3f];
        *s++ = ALPHABET[(triad << 2) & 0x3f];
    }
    *s = 0;
}

void from_base64(const unsigned char* p, size_t l, char *s)
{
    static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    size_t i;
    size_t mod = l % 3;
    size_t ll = l - mod;
    unsigned int triad;
    for(i = 0; i < ll; i += 3)
    {
        triad = (p[i] << 16) | (p[i + 1] << 8) | p[i + 2];
        *s++ = ALPHABET[(triad >> 18) & 0x3f];
        *s++ = ALPHABET[(triad >> 12) & 0x3f];
        *s++ = ALPHABET[(triad >> 6) & 0x3f];
        *s++ = ALPHABET[triad & 0x3f];
    }
    if(mod == 1)
    {
        *s++ = ALPHABET[(p[i] >> 2) & 0x3f];
        *s++ = ALPHABET[(p[i] << 4) & 0x3f];
    }
    if(mod == 2)
    {
        triad = (p[i] << 8) | p[i + 1];
        *s++ = ALPHABET[(triad >> 10) & 0x3f];
        *s++ = ALPHABET[(triad >> 4) & 0x3f];
        *s++ = ALPHABET[(triad << 2) & 0x3f];
    }
    *s = 0;
}

unsigned char from_base64_char(char c, bool *bad)
{
    if(c >= 'A' && c <= 'Z')
        return c - 'A';
    else if(c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    else if(c >= '0' && c <= '9')
        return c - '0' + 52;
    else if(c == '-')
        return 62;
    else if(c == '_')
        return 63;
    else
    {
        *bad = true;
        return 0;
    }
}

bool from_base64(const char* s, unsigned char* p, size_t &n)
{
    unsigned char u0, u1, u2, u3;
    bool bad = false;
    size_t w = 0;
    while(*s && s[1] && s[2] && s[3])
    {
        u0 = from_base64_char(*s++, &bad);
        u1 = from_base64_char(*s++, &bad);
        u2 = from_base64_char(*s++, &bad);
        u3 = from_base64_char(*s++, &bad);
        if(bad || n - w < 3)
            return false;
        *p++ = (u0 << 2) | (u1 >> 4);
        *p++ = (u1 << 4) | (u2 >> 2);
        *p++ = (u2 << 6) | u3;
        w += 3;
    }
    if(*s)
    {
        if(!s[1])
            return false;

        u0 = from_base64_char(*s++, &bad);
        u1 = from_base64_char(*s++, &bad);

        if(*s)
        {
            u2 = from_base64_char(*s, &bad);
            if(bad && n - w < 2)
                return false;
            *p++ = (u0 << 2) | (u1 >> 4);
            *p++ = (u1 << 4) | (u2 >> 2);
            w += 2;
        }
        else
        {
            if(bad && n - w < 1)
                return false;
            *p++ = (u0 << 2) | (u1 >> 4);
            ++w;
        }
    }
    n = w;
    return true;
}

// Cut the string, if necessary, to up to max_len characters
// on UTF-8 codepoint boundary.
// Can still mangle compound characters :(
// TODO: glyph boundary.
void cut_utf8(string& s, size_t max_len)
{
    const char *ps = s.c_str(), *p;
    size_t n = s.size();
    if(n > max_len)
    {
        const char *pe = ps + max_len;
        while((*pe & 0xC0) == 0x80)
            --pe;
        s.resize(pe - ps, 0);
    }
}
