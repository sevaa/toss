#include <uuid/uuid.h>

enum Mode {RECEIVE, LIST, SEND, };
extern Mode g_mode;
extern unsigned short g_port;
extern uuid_t g_my_guid;
extern int g_sock;

extern bool from_base64(const char *s, unsigned char *p, size_t &n);
extern void to_base64(const unsigned char* p, size_t l, char *s);
