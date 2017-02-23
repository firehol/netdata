#ifndef NETDATA_RRDPUSH_H
#define NETDATA_RRDPUSH_H

extern int rrdpush_enabled;
extern int rrdpush_exclusive;

extern int rrdpush_init();
extern void rrdset_done_push(RRDSET *st);
extern void *rrdpush_sender_thread(void *ptr);

extern int rrdpush_receiver_thread_spawn(RRDHOST *host, struct web_client *w, char *url);

#endif //NETDATA_RRDPUSH_H
