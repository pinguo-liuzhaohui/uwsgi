#include <uwsgi.h>

#ifdef UWSGI_ROUTING

extern struct uwsgi_server uwsgi;

static int uwsgi_routing_func_http(struct wsgi_request *wsgi_req, struct uwsgi_route *ur) {

	// mark a route request
        wsgi_req->via = UWSGI_VIA_ROUTE;

	char **subject = (char **) (((char *)(wsgi_req))+ur->subject);
        uint16_t *subject_len = (uint16_t *)  (((char *)(wsgi_req))+ur->subject_len);

	struct uwsgi_buffer *ub_addr = uwsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data, ur->data_len);
	if (!ub_addr) return UWSGI_ROUTE_BREAK;

	struct uwsgi_buffer *ub_url = NULL;
	if (ur->data3_len) {
		ub_url = uwsgi_routing_translate(wsgi_req, ur, *subject, *subject_len, ur->data3, ur->data3_len);
        	if (!ub_url) {
			uwsgi_buffer_destroy(ub_addr);
			return UWSGI_ROUTE_BREAK;
		}
	}


	// convert the wsgi_request to an http proxy request
	struct uwsgi_buffer *ub = uwsgi_to_http(wsgi_req, ur->data2, ur->data2_len, ub_url ? ub_url->buf : NULL, ub_url ? ub_url->pos : 0);	
	if (!ub) {
		if (ub_url) uwsgi_buffer_destroy(ub_url);
		uwsgi_log("unable to generate http request for %s\n", ub_addr->buf);
		uwsgi_buffer_destroy(ub_addr);
                return UWSGI_ROUTE_NEXT;
	}

	if (ub_url) uwsgi_buffer_destroy(ub_url);

	// amount of body to send
	size_t remains = wsgi_req->post_cl - wsgi_req->proto_parser_remains;
	// append remaining body...
	if (wsgi_req->proto_parser_remains > 0) {
		if (uwsgi_buffer_append(ub, wsgi_req->proto_parser_remains_buf, wsgi_req->proto_parser_remains)) {
			uwsgi_buffer_destroy(ub);
			uwsgi_log("unable to generate http request for %s\n", ub_addr->buf);
			uwsgi_buffer_destroy(ub_addr);
               		return UWSGI_ROUTE_NEXT;
		}
		wsgi_req->proto_parser_remains = 0;
	}

	// ok now if have offload threads, directly use them
	if (!wsgi_req->post_file && !ur->custom && wsgi_req->socket->can_offload) {
		// append buffered body
		if (uwsgi.post_buffering > 0 && wsgi_req->post_cl > 0) {
			if (uwsgi_buffer_append(ub, wsgi_req->post_buffering_buf, wsgi_req->post_cl)) {
				uwsgi_buffer_destroy(ub);
				uwsgi_log("unable to generate http request for %s\n", ub_addr->buf);
				uwsgi_buffer_destroy(ub_addr);
               			return UWSGI_ROUTE_NEXT;
			}
		}
        	if (!uwsgi_offload_request_net_do(wsgi_req, ub_addr->buf, ub)) {
                	wsgi_req->via = UWSGI_VIA_OFFLOAD;
			wsgi_req->status = 202;
			uwsgi_buffer_destroy(ub_addr);
			return UWSGI_ROUTE_BREAK;
                }
	}

	if (uwsgi_proxy_nb(wsgi_req, ub_addr->buf, ub, remains, uwsgi.shared->options[UWSGI_OPTION_SOCKET_TIMEOUT])) {
		uwsgi_log("error routing request to http server %s\n", ub_addr->buf);
	}

	uwsgi_buffer_destroy(ub);
	uwsgi_buffer_destroy(ub_addr);

	return UWSGI_ROUTE_BREAK;

}

static int uwsgi_router_http(struct uwsgi_route *ur, char *args) {

	ur->func = uwsgi_routing_func_http;
	ur->data = (void *) args;
	ur->data_len = strlen(args);

	char *comma = strchr(ur->data, ',');
	if (comma) {
		*comma = 0;
		ur->data_len = strlen(ur->data);
		ur->data2 = comma+1;
		comma = strchr(ur->data2, ',');
		if (comma) {
			*comma = 0;
			ur->data3 = comma+1;
			ur->data3_len = strlen(ur->data3);
		}
		ur->data2_len = strlen(ur->data2);
	}
	return 0;
}

static int uwsgi_router_proxyhttp(struct uwsgi_route *ur, char *args) {
	ur->custom = 1;
	return uwsgi_router_http(ur, args);
}


static void router_http_register(void) {

	uwsgi_register_router("http", uwsgi_router_http);
	uwsgi_register_router("proxyhttp", uwsgi_router_proxyhttp);
}

struct uwsgi_plugin router_http_plugin = {
	.name = "router_http",
	.on_load = router_http_register,
};
#else
struct uwsgi_plugin router_http_plugin = {
	.name = "router_http",
};
#endif
