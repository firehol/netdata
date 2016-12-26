#ifndef NETDATA_PLUGIN_MACOS_H
#define NETDATA_PLUGIN_MACOS_H 1

void *macos_main(void *ptr);

extern int do_macos_sysctl(int update_every, usec_t dt);
extern int do_macos_mach_smi(int update_every, usec_t dt);

#endif /* NETDATA_PLUGIN_MACOS_H */
