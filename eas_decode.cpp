#include <time.h>

#include <pcre.h>

#include <string.h>

#include "eas_decode.h"

using namespace std;

static time_t mkgmtime(struct tm *tm)
{
    static const int mdays[13] = {
        0,31,59,90,120,151,181,212,243,273,304,334
    };
    return ((((((tm->tm_year - 70) * 365) + mdays[tm->tm_mon] + tm->tm_mday-1 + (tm->tm_year-68-1+(tm->tm_mon>=2))/4) * 24) + tm->tm_hour) * 60 + tm->tm_min) * 60 + tm->tm_sec;
}

struct Description {
    const char *abbr;
    const char *desc;
};

static const Description Originators[] = {
    {"CIV", "Civil authorities"},
    {"EAS", "Broadcast station or cable station"},
    {"PEP", "Primary Entry Point System"},
    {"WXR", "National Weather Service"},
};

static const Description Events[] = {
    {"ADR", "Administrative Message"},
    {"AVA", "Avalanche Watch"},
    {"AVW", "Avalanche Warning"},
    {"BZW", "Blizzard Warning"},
    {"CAE", "Child Abduction Emergency"},
    {"CDW", "Civil Danger Warning"},
    {"CEM", "Civil Emergency Message"},
    {"CFA", "Coastal Flood Watch"},
    {"CFW", "Coastal Flood Warning"},
    {"DMO", "Practice/Demo Warning"},
    {"DSW", "Dust Storm Warning"},
    {"EAN", "Emergency Action Notification"},
    {"EAT", "Emergency Action Termination"},
    {"EQW", "Earthquake Warning"},
    {"EVI", "Evacuation Immediate"},
    {"FFA", "Flash Flood Watch"},
    {"FFS", "Flash Flood Statement"},
    {"FFW", "Flash Flood Warning"},
    {"FLA", "Flood Watch"},
    {"FLS", "Flood Statement"},
    {"FLW", "Flood Warning"},
    {"FRW", "Fire Warning"},
    {"HLS", "Hurricane Statement"},
    {"HMW", "Hazardous Materials Warning"},
    {"HUA", "Hurricane Watch"},
    {"HUW", "Hurricane Warning"},
    {"HWA", "High Wind Watch"},
    {"HWW", "High Wind Warning"},
    {"LAE", "Local Area Emergency"},
    {"LEW", "Law Enforcement Warning"},
    {"NIC", "National Information Center"},
    {"NMN", "Network Message Notification"},
    {"NPT", "National Periodic Test"},
    {"NUW", "Nuclear Power Plant Warning"},
    {"RHW", "Radiological Hazard Warning"},
    {"RMT", "Required Monthly Test"},
    {"RWT", "Required Weekly Test"},
    {"SMW", "Special Marine Warning"},
    {"SPS", "Special Weather Statement"},
    {"SPW", "Shelter in Place Warning"},
    {"SVA", "Severe Thunderstorm Watch"},
    {"SVR", "Severe Thunderstorm Warning"},
    {"SVS", "Severe Weather Statement"},
    {"TOA", "Tornado Watch"},
    {"TOE", "911 Telephone Outage Emergency"},
    {"TOR", "Tornado Warning"},
    {"TRA", "Tropical Storm Watch"},
    {"TRW", "Tropical Storm Warning"},
    {"TSA", "Tsunami Watch"},
    {"TSW", "Tsunami Warning"},
    {"VOW", "Volcano Warning"},
    {"WSA", "Winter Storm Watch"},
    {"WSW", "Winter Storm Warning"},
};

static int strcompare(const void *p1, const void *p2)
{
    return strcmp((const char *)p1, ((const Description *)p2)->abbr);
}

static string getOriginatorDesc(const string &originator)
{
    Description *d = (Description *)bsearch(originator.c_str(), Originators, sizeof(Originators)/sizeof(Originators[0]), sizeof(Originators[0]), strcompare);
    if (d == NULL) {
        return string();
    }
    return d->desc;
}

static string getEventDesc(const string &event)
{
    Description *d = (Description *)bsearch(event.c_str(), Events, sizeof(Events)/sizeof(Events[0]), sizeof(Events[0]), strcompare);
    if (d == NULL) {
        return string();
    }
    return d->desc;
}

struct County {
    int fips;
    const char *name;
};
static const County Counties[] = {
    #include "fips.inc"
};
static const char *Parts[] = {
    "",
    "Northwest ",
    "North ",
    "Northeast ",
    "West ",
    "Central ",
    "East ",
    "Southwest ",
    "South ",
    "Southeast ",
};

static int fipscompare(const void *p1, const void *p2)
{
    return *(int *)p1 - ((County *)p2)->fips;
}

static string getAreaDesc(const eas::Message::Area &area)
{
    int fips = 1000 * area.state + area.county;
    County *c = (County *)bsearch(&fips, Counties, sizeof(Counties)/sizeof(Counties[0]), sizeof(Counties[0]), fipscompare);
    if (c == NULL) {
        return string();
    }
    return string(Parts[area.part]) + c->name;
}

static string getSenderDesc(const string &sender)
{
    return string();
}

bool eas::Decode(const char *s, Message &message)
{
    const char *errptr;
    int erroffset;
    pcre *re = pcre_compile(
        "^ZCZC-(\\w+)-(\\w+)((?:-[^+-]+){1,31})\\+(\\d{2})(\\d{2})-(\\d{3})(\\d{2})(\\d{2})-([^-]+)-",
        //     1      2     3                     4       5        6       7       8        9
        0,
        &errptr,
        &erroffset,
        NULL);
    if (re == NULL) {
        return false;
    }
    int ovector[3*10];
    int r = pcre_exec(
        re,
        NULL,
        s,
        strlen(s),
        0,
        0,
        ovector,
        sizeof(ovector)/sizeof(ovector[0]));
    pcre_free(re);
    if (r < 0) {
        return false;
    }
    const char **matches;
    pcre_get_substring_list(s, ovector, r, &matches);
    int yday = atoi(matches[6]);
    time_t now = time(0);
    struct tm *tt;
    for (;;) {
        tt = gmtime(&now);
        if (1+tt->tm_yday == yday) {
            break;
        } else if (1+tt->tm_yday < yday) {
            now += 86400;
        } else if (1+tt->tm_yday > yday) {
            now -= 86400;
        }
    }
    tt->tm_hour = atoi(matches[7]);
    tt->tm_min = atoi(matches[8]);
    tt->tm_sec = 0;
    message.raw = s;
    message.originator = matches[1];
    message.originator_desc = getOriginatorDesc(message.originator);
    message.event = matches[2];
    message.event_desc = getEventDesc(message.event);
    message.areas.clear();
    string a;
    for (const char *p = matches[3]; ; p++) {
        if (*p == '-' || *p == 0) {
            if (*p == '-') {
                p++;
            }
            if (!a.empty()) {
                Message::Area area;
                area.code = a;
                if (a.length() == 6 && strspn(a.c_str(), "0123456789") == 6) {
                    area.part = a[0] - '0';
                    area.state = 10*(a[1] - '0') + (a[2] - '0');
                    area.county = 10*(10*(a[3] - '0') + (a[4] - '0')) + (a[5] - '0');
                }
                area.desc = getAreaDesc(area);
                message.areas.push_back(area);
            }
            a.erase();
            if (*p == 0) {
                break;
            }
        }
        a += *p;
    }
    message.issued = mkgmtime(tt);
    message.received = time(0);
    message.purge = message.issued + 60*(60*atoi(matches[4]) + atoi(matches[5]));
    message.sender = matches[9];
    message.sender_desc = getSenderDesc(message.sender);
    pcre_free_substring_list(matches);
    return true;
}
