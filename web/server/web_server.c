// SPDX-License-Identifier: GPL-3.0-or-later

#define WEB_SERVER_INTERNALS 1
#include "web_server.h"

WEB_SERVER_MODE web_server_mode = WEB_SERVER_MODE_STATIC_THREADED;

// --------------------------------------------------------------------------------------

WEB_SERVER_MODE web_server_mode_id(const char *mode) {
    if(!strcmp(mode, "none"))
        return WEB_SERVER_MODE_NONE;
    else
        return WEB_SERVER_MODE_STATIC_THREADED;

}

const char *web_server_mode_name(WEB_SERVER_MODE id) {
    switch(id) {
        case WEB_SERVER_MODE_NONE:
            return "none";
        default:
        case WEB_SERVER_MODE_STATIC_THREADED:
            return "static-threaded";
    }
}

// --------------------------------------------------------------------------------------
// API sockets

LISTEN_SOCKETS api_sockets = {
		.config          = &netdata_config,
		.config_section  = CONFIG_SECTION_WEB,
		.default_bind_to = "*",
		.default_port    = API_LISTEN_PORT,
		.backlog         = API_LISTEN_BACKLOG
};

void debug_sockets() {
	BUFFER *wb = buffer_create(256 * sizeof(char));
	int i;

	for(i = 0 ; i < (int)api_sockets.opened ; i++) {
		buffer_strcat(wb, (api_sockets.fds_acl_flags[i] & WEB_CLIENT_ACL_NOCHECK)?"NONE ":"");
		buffer_strcat(wb, (api_sockets.fds_acl_flags[i] & WEB_CLIENT_ACL_DASHBOARD)?"dashboard ":"");
		buffer_strcat(wb, (api_sockets.fds_acl_flags[i] & WEB_CLIENT_ACL_REGISTRY)?"registry ":"");
		buffer_strcat(wb, (api_sockets.fds_acl_flags[i] & WEB_CLIENT_ACL_BADGE)?"badges ":"");
		buffer_strcat(wb, (api_sockets.fds_acl_flags[i] & WEB_CLIENT_ACL_MGMT)?"management ":"");
		buffer_strcat(wb, (api_sockets.fds_acl_flags[i] & WEB_CLIENT_ACL_STREAMING)?"streaming ":"");
		buffer_strcat(wb, (api_sockets.fds_acl_flags[i] & WEB_CLIENT_ACL_NETDATACONF)?"netdata.conf ":"");
		debug(D_WEB_CLIENT, "Socket fd %d name '%s' acl_flags: %s",
			  i,
			  api_sockets.fds_names[i],
			  buffer_tostring(wb));
		buffer_reset(wb);
	}
	buffer_free(wb);
}

void api_listen_sockets_setup(void) {
	int socks = listen_sockets_setup(&api_sockets);

	if(!socks)
		fatal("LISTENER: Cannot listen on any API socket. Exiting...");

	if(unlikely(debug_flags & D_WEB_CLIENT))
		debug_sockets();

	return;
}


// --------------------------------------------------------------------------------------
// access lists

SIMPLE_PATTERN *web_allow_connections_from = NULL;

// WEB_CLIENT_ACL
SIMPLE_PATTERN *web_allow_dashboard_from = NULL;
SIMPLE_PATTERN *web_allow_registry_from = NULL;
SIMPLE_PATTERN *web_allow_badges_from = NULL;
SIMPLE_PATTERN *web_allow_mgmt_from = NULL;
SIMPLE_PATTERN *web_allow_streaming_from = NULL;
SIMPLE_PATTERN *web_allow_netdataconf_from = NULL;

void web_client_update_acl_matches(struct web_client *w) {
    w->acl = WEB_CLIENT_ACL_NONE;

    if(!web_allow_dashboard_from || simple_pattern_matches(web_allow_dashboard_from, w->client_ip))
        w->acl |= WEB_CLIENT_ACL_DASHBOARD;

    if(!web_allow_registry_from || simple_pattern_matches(web_allow_registry_from, w->client_ip))
        w->acl |= WEB_CLIENT_ACL_REGISTRY;

    if(!web_allow_badges_from || simple_pattern_matches(web_allow_badges_from, w->client_ip))
        w->acl |= WEB_CLIENT_ACL_BADGE;

    if(!web_allow_mgmt_from || simple_pattern_matches(web_allow_mgmt_from, w->client_ip))
        w->acl |= WEB_CLIENT_ACL_MGMT;

    if(!web_allow_streaming_from || simple_pattern_matches(web_allow_streaming_from, w->client_ip))
        w->acl |= WEB_CLIENT_ACL_STREAMING;

    if(!web_allow_netdataconf_from || simple_pattern_matches(web_allow_netdataconf_from, w->client_ip))
        w->acl |= WEB_CLIENT_ACL_NETDATACONF;

    w->acl &= w->port_acl;
}


// --------------------------------------------------------------------------------------

void web_server_log_connection(struct web_client *w, const char *msg) {
    log_access("%llu: %d '[%s]:%s' '%s'", w->id, gettid(), w->client_ip, w->client_port, msg);
}

// --------------------------------------------------------------------------------------

void web_client_initialize_connection(struct web_client *w) {
    int flag = 1;

    if(unlikely(web_client_check_tcp(w) && setsockopt(w->ifd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)) != 0))
        debug(D_WEB_CLIENT, "%llu: failed to enable TCP_NODELAY on socket fd %d.", w->id, w->ifd);

    flag = 1;
    if(unlikely(setsockopt(w->ifd, SOL_SOCKET, SO_KEEPALIVE, (char *) &flag, sizeof(int)) != 0))
        debug(D_WEB_CLIENT, "%llu: failed to enable SO_KEEPALIVE on socket fd %d.", w->id, w->ifd);

    web_client_update_acl_matches(w);

    w->origin[0] = '*'; w->origin[1] = '\0';
    w->cookie1[0] = '\0'; w->cookie2[0] = '\0';
    freez(w->user_agent); w->user_agent = NULL;

    web_client_enable_wait_receive(w);

    web_server_log_connection(w, "CONNECTED");

    web_client_cache_verify(0);
#ifdef ENABLE_HTTPS
	int sslerrno;
	w->accepted = 0;
	if ( netdata_ctx )
	{
		if ( (w->ssl = SSL_new(netdata_ctx) ) )
		{
			SSL_set_accept_state(w->ssl);
			if ( SSL_set_fd(w->ssl, w->ifd) == 1 )
			{
				/*
				if ( (w->sbio = BIO_new_socket(w->ifd,BIO_NOCLOSE) ) )
				{
					SSL_set_bio(w->ssl,w->sbio,w->sbio);
				}
				else
				{
			        error("Failed to set BIO new socket on socket fd %d.", w->ifd);
				}
				*/
				w->accepted = security_process_accept(w->ssl,w->ifd);
				if ( w->accepted == -1 )
				{
				    WEB_CLIENT_IS_DEAD(w);
				}
			}
			else
			{
	        	error("Failed to set the socket to the SSL on socket fd %d.", w->ifd);
        		WEB_CLIENT_IS_DEAD(w);
			}
		}
		else
		{
	        error("Failed to create SSL context on socket fd %d.", w->ifd);
        	WEB_CLIENT_IS_DEAD(w);
		}
	}
#endif
}
