#include <fstream>
#include <string>
#include <map>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

using namespace std;

static map<string, string> s_config;
static bool s_dirty = false;

static const char CONFIG_REL_PATH[] = "/.config/Seva/toss.conf";

void load_config()
{
    string path(getenv("HOME"));
    path += CONFIG_REL_PATH;
    ifstream ifst;
    ifst.open(path.c_str(), ifstream::in);
    if(ifst.is_open())
    {
        size_t pos;
        for(string s; getline(ifst, s);)
        {
            if(s.size() && (pos = s.find('=')) > 0)
            {
                string key(s, 0, pos), value(s, pos+1);
                s_config[key] = value;
            }
        }
    }
}

void save_config()
{
    if(s_dirty)
    {
        const char *home = getenv("HOME");
        string path(home);
        path += "/.config";
        if(access(path.c_str(), F_OK))
            mkdir(path.c_str(), 0700);
        path += "/Seva";
        if(access(path.c_str(), F_OK))
            mkdir(path.c_str(), 0700);
        path += "/toss.conf";

        ofstream ofst;
        ofst.open(path.c_str(), ofstream::out|ofstream::app);
        if(ofst.is_open())
        {
            for(const map<string, string>::value_type &kv : s_config)
                ofst << kv.first << '=' << kv.second << endl;
                s_dirty = false;
        }
    }
}

bool config_get(const char *key, map<string, string>::const_iterator &it)
{
    it = s_config.find(key);
    return it != s_config.end();
}

void config_set(const string &key, const string &value)
{
    map<string, string>::iterator it = s_config.find(key);
    if(it == s_config.end() || it->second != value)
    {
        s_config[key] = value;
        s_dirty = true;
    }
}