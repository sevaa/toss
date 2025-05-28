#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <map>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <termios.h>
#include "toss.h"
#include <string>
#include <sys/io.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>

using namespace std;

static const size_t
	DATAGRAM_SIZE = 512,
	V0_HEADER_SIZE = 20,
    MAX_MESSAGE_SIZE = 492;

static const int ver_ma = 1, ver_mi = 0, ver_bui = 0;

union datagram_t
{
    char data[DATAGRAM_SIZE];
    struct
    {
        unsigned version;
        uuid_t guid;
        char text[MAX_MESSAGE_SIZE];
    };
};

Mode g_mode = Mode::RECEIVE;
unsigned short g_port = 0;
uuid_t g_my_guid;

struct uuidkey_t
{
    uuid_t guid;
    uuidkey_t(const uuid_t &g)
    {
        memcpy(guid, g, sizeof(uuid_t));
    }

    bool operator<(const uuidkey_t &that) const
    {
        return uuid_compare(guid, that.guid) < 0;
    }
};
typedef map<uuidkey_t, string> peermap_t;

static int s_sock = -1;
static peermap_t s_peers;
static struct termios s_tios = {0, 0, 0, 0};
static bool s_ipv6 = true;

// Outgoing message
static string s_to, s_message;

extern bool av_start(int sock);
extern bool av_loop();
extern bool av_break();
extern void av_end();

extern void load_config();
extern void save_config();
extern bool config_get(const char *key, map<string, string>::const_iterator &it);
extern void config_set(const string &key, const string &value);

extern void cut_utf8(string &s, size_t max_len);

static struct global_guard_t
{
    ~global_guard_t()
    {
        if(s_sock != -1)
            close(s_sock);
        if(s_tios.c_iflag || s_tios.c_oflag || s_tios.c_cflag || s_tios.c_lflag)
            tcsetattr(0, TCSANOW, &s_tios);
    }
} s_guard;

static void ensure_guid()
{
    map<string, string>::const_iterator it;
    if(!config_get("MyGUID", it) || uuid_parse(it->second.c_str(), g_my_guid))
    {
        uuid_generate(g_my_guid);
        char s[50];
        uuid_unparse(g_my_guid, s);
        config_set("MyGUID", s);
    }
}

static bool is_really_v4(const uint8_t* addr)
{
    return !addr[0] && !addr[1] && !addr[2] && !addr[3] &&
        !addr[4] && !addr[5] && !addr[6] && !addr[7] &&
        !addr[8] && !addr[9] && addr[10] == 0xFF && addr[11] == 0xFF;
}

static void print_help()
{
    cout << "Seva Alekseyev, 2025" << endl <<
        "Version " << ver_ma << '.' << ver_mi << '.' << ver_bui << '.' << endl <<
        "You can find Toss for other platforms at their respective app stores (search for TossToSelf)." << endl <<
        "https://toss.jishop.com/" << endl << endl <<
        "\"toss\" advertises self and waits for incoming messages." << endl <<
        "\"toss l[ist]\" discovers running toss instances nearby and displays their names." << endl <<
        "\"toss s[end] <to> <message>\" sends the message to a nearby instance with name that matches or contains <to>, case insensitive." << endl;
}

void process_input()
{
    char c;
    if(read(0, &c, 1) == 1 && c == 'q')
        av_break();
}

void receive_dgram()
{
    datagram_t dgram;
    union
    {
        sockaddr_in sa4;
        sockaddr_in6 sa6;
    } sa;
    socklen_t salen = s_ipv6 ? sizeof sa.sa6 : sizeof sa.sa4;
    size_t n = recvfrom(s_sock,  dgram.data, sizeof(dgram.data), 0, (sockaddr*)&sa, &salen);

    if(n >= V0_HEADER_SIZE && dgram.version == 0)
    {
        string from;
        peermap_t::iterator it;
        if((it = s_peers.find(dgram.guid)) != s_peers.end())
            from = it->second;
        else
        {
            char key[64] = "NameForGUID_";
            uuid_unparse(dgram.guid, key + strlen(key));
            map<string, string>::const_iterator it;
            if(config_get(key, it))
                from = it->second;
            else
            {
                ostringstream oss;
                if(s_ipv6)
                {
                    const uint8_t *a = sa.sa6.sin6_addr.s6_addr;
                    uint16_t port = ntohs(sa.sa6.sin6_port);
                    if(is_really_v4(a))
                        oss << a[12] << '.' << a[13] << '.' << a[14] << '.' << a[15] << ':' << port;
                    else
                    {
                        char s[64];
                        oss << '[' << inet_ntop(AF_INET6, a, s, sizeof s) << "]:" << port;
                    }
                }
                else
                {
                    const uint8_t *a = (const uint8_t*)&sa.sa4.sin_addr.s_addr;
                    oss << a[0] << '.' << a[1] << '.' << a[2] << '.' << a[3] << ':' << htons(sa.sa4.sin_port);
                }
                from = oss.str();
            }
        }
        cout << from << " says:" << endl;
        if(n > V0_HEADER_SIZE)
        {
            string s(dgram.text, n - V0_HEADER_SIZE);
            cout << s << endl << endl;
        }
    }
}

static void send_message_to(bool ipv6, const uint8_t *address, unsigned short port)
{
    datagram_t dgram = {0};
    memcpy(dgram.guid, g_my_guid, sizeof g_my_guid);
    memcpy(dgram.text, s_message.c_str(), s_message.size());

    union
    {
        sockaddr_in sa4;
        sockaddr_in6 sa6;
    } sa;
    socklen_t salen;
    memset(&sa, 0, sizeof sa);
    if(s_ipv6)
    {
        salen = sizeof sa.sa6;
        sa.sa6.sin6_family = AF_INET6;
        sa.sa6.sin6_port = htons(port);
        uint8_t *a = sa.sa6.sin6_addr.s6_addr;
        if(ipv6)
            memcpy(a, address, 16);
        else
        {
            a[10] = a[11] = 0xFF;
            memcpy(a + 12, address, 4);
        }
    }
    else
    {
        salen = sizeof sa.sa4;
        sa.sa4.sin_family = AF_INET;
        sa.sa4.sin_port = htons(port);
        sa.sa4.sin_addr.s_addr = *(uint32_t*)address;
    }
    sendto(s_sock, dgram.data, V0_HEADER_SIZE + s_message.size(), 0,
        (sockaddr*)&sa, salen);

    av_break();
}

void found_peer(const uuid_t &peer_guid, const string &name, bool ipv6, const uint8_t *address, unsigned short port)
{
    char key[64] = "NameForGUID_";
    uuid_unparse(peer_guid, key + strlen(key));
    config_set(key, name);

    if(g_mode == Mode::SEND && (s_ipv6 || !ipv6))
    {
        string::const_iterator it = search(name.begin(), name.end(),
            s_to.begin(), s_to.end(),
            [](unsigned char l, unsigned char r) { return toupper(l) == toupper(r); });
        if(it != name.end())
        {
            cout << "Found \"" << s_to << "\" as \"" << name << "\", sending." << endl;
            send_message_to(ipv6, address, port);
        }
    }
    else if(g_mode == Mode::LIST)
    {
        if(s_peers.find(peer_guid) == s_peers.end())
            cout << name << endl;
        s_peers[peer_guid] = name;
    }
}

static bool create_socket()
{
    union
    {
        sockaddr_in sa4;
        sockaddr_in6 sa6;
    } sa;
    socklen_t salen;
    memset(&sa, 0, sizeof sa);

    s_sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if(s_sock != -1)
    {
        salen = sizeof sa.sa6;
        sa.sa6.sin6_family = AF_INET6;

        char opt;
        socklen_t sz = 1;
        if(getsockopt(s_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, &sz))
            return false;
        if(opt)
        {
            opt = 0;
            if(setsockopt(s_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof opt))
                return false;
        }
    }
    else
    {
        if(errno == EAFNOSUPPORT)
        {
            // Fallback to IPv4
            s_ipv6 = false;
            s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if(s_sock == -1)
                return false;

            salen = sizeof sa.sa4;
            sa.sa4.sin_family = AF_INET;
        }
        else
            return false;
    }
    
    unsigned short &port = s_ipv6 ? sa.sa6.sin6_port : sa.sa4.sin_port;
    map<string, string>::const_iterator it;
    if(config_get("Port", it))
    {
        g_port = (unsigned short)atoi(it->second.c_str());
        port = htons(g_port);
    }

    if(bind(s_sock, (sockaddr*)&sa, salen))
    {
        if(port && errno == EADDRINUSE)
        {
            port = 0; //Preferred port is in use - a choose new one
            if(bind(s_sock, (sockaddr*)&sa, salen))
                return false;
        }
        else
            return false;
    }

    if(!port)
    {
        getsockname(s_sock, (sockaddr*)&sa, &salen);
        g_port = ntohs(port);
        config_set("Port", to_string(g_port).c_str());
    }

    return true;
}

void timeout_elapsed()
{
    if(g_mode == Mode::LIST)
    {
        if(s_peers.size())
        {
            cout << "Found running Toss on the following devices:" << endl;
            for(const peermap_t::value_type &gn : s_peers)
                cout << gn.second << endl;
        }
        else
            cout << "No running Toss instances were found." << endl;
    }
    else if(g_mode == Mode::SEND)
        cout << "Peer \"" << s_to << "\" was not found." << endl;
    av_break();
}

static bool ensure_single_instance()
{
    string path(getenv("HOME"));
    path += "/.toss.pid";
    int fd = open(path.c_str(), O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR);
    if(fd == -1)
        return false;
    // fd intentionally leaked
    int r = flock(fd, LOCK_EX|LOCK_NB);
    return r == 0;
}

int main(int argc, char *argv[])
{
    cout << "Toss - sending small text strings between devices on the same network" << endl;

    const char *to;
    string message;
    if(argc > 1)
    {
        if(!strcmp(argv[1], "h") ||
            !strcmp(argv[1], "help") ||
            !strcmp(argv[1], "-h") ||
            !strcmp(argv[1], "--help") ||
            !strcmp(argv[1], "-?"))
        {
            print_help();
            return 0;
        }
        else if(!strcmp(argv[1], "l") || !strcmp(argv[1], "list"))
            g_mode = Mode::LIST;
        else if(!strcmp(argv[1], "s") || !strcmp(argv[1], "send"))
        {
            if(argc < 3)
            {
                cout << "For sending, toss needs the recipient's name:" << endl <<
                    "\"toss send <to> [<message>]\"" << endl <<
                    "The recipient name can be a substring, case insensitive." << endl;
                return 1;
            }
            s_to = argv[2];
            ostringstream oss;
            for(int i=3; i<argc; i++)
            {
                if(i > 3)
                    oss << ' ';
                oss << argv[i];
            }
            s_message = oss.str();
            cut_utf8(s_message, MAX_MESSAGE_SIZE);
            g_mode = Mode::SEND;
        }
    }

    cout << "Run \"toss -?\" for help" << endl << endl;

    load_config();
    ensure_guid();

    if(g_mode != Mode::LIST) // Send or receive mode
    {
        if(!ensure_single_instance())
        {
            cout << "An instance of toss is already running for the current user.\n";
            return 0;
        }

        if(!create_socket())
            return 1;
    }

    if(!av_start(s_sock))
    {
        cout << "Avahi not found." << endl;
        return 1;
    }

    if((g_mode == Mode::RECEIVE || g_mode == Mode::LIST) && isatty(0))
    {
        tcgetattr(0, &s_tios);
        struct termios tios = s_tios;
        tios.c_lflag &= ~(ICANON|ECHO); //Input by one character, no echo
        tcsetattr(0, TCSANOW, &tios);
    }

    av_loop();

    av_end();
    save_config();
    return 0;
}