#ifndef NETDATA_REGISTRY_PERSON_H
#define NETDATA_REGISTRY_PERSON_H

#include "registry_internals.h"

// ----------------------------------------------------------------------------
// PERSON structures

// for each PERSON-URL pair we keep this
struct registry_person_url {
    REGISTRY_URL *url;          // de-duplicated URL
    REGISTRY_MACHINE *machine;  // link the MACHINE of this URL

    uint8_t flags;

    uint32_t first_t;           // the first time we saw this
    uint32_t last_t;            // the last time we saw this
    uint32_t usages;            // how many times this has been accessed

    char machine_name[1];       // the name of the machine, as known by the user
    // dynamically allocated to fit properly
};
typedef struct registry_person_url REGISTRY_PERSON_URL;

// A person
struct registry_person {
    char guid[GUID_LEN + 1];    // the person GUID

    DICTIONARY *person_urls;    // dictionary of PERSON_URL *

    uint32_t first_t;           // the first time we saw this
    uint32_t last_t;            // the last time we saw this
    uint32_t usages;            // how many times this has been accessed
};
typedef struct registry_person REGISTRY_PERSON;

extern REGISTRY_PERSON *registry_person_find(const char *person_guid);
extern REGISTRY_PERSON_URL *registry_person_url_allocate(REGISTRY_PERSON *p, REGISTRY_MACHINE *m, REGISTRY_URL *u, char *name, size_t namelen, time_t when);
extern REGISTRY_PERSON_URL *registry_person_url_reallocate(REGISTRY_PERSON *p, REGISTRY_MACHINE *m, REGISTRY_URL *u, char *name, size_t namelen, time_t when, REGISTRY_PERSON_URL *pu);
extern REGISTRY_PERSON *registry_person_allocate(const char *person_guid, time_t when);
extern REGISTRY_PERSON *registry_person_get(const char *person_guid, time_t when);
extern REGISTRY_PERSON_URL *registry_person_link_to_url(REGISTRY_PERSON *p, REGISTRY_MACHINE *m, REGISTRY_URL *u, char *name, size_t namelen, time_t when);

#endif //NETDATA_REGISTRY_PERSON_H
