#include "ape_server.h"
#include "JSON_parser.h"
#include "ape_transports.h"
#include "ape_modules.h"
#include "ape_base64.h"
#include "ape_sha1.h"
#include "ape_websocket.h"
#include "ape_ssl.h"
#include "ape_common_pattern.h"
#include "ape_timers.h"
#include "ape_json.h"
#include "ape_user.h"

#include <string.h>

#ifdef _HAVE_MSGPACK
 #include <msgpack.h>
#endif

int _co_event = 0, _disco_event = 0;

static int ape_server_http_ready(ape_client *client, ape_global *ape);

static struct _ape_transports_s {
    ape_transport_t type;
    size_t len;
    char path[12];
} ape_transports_s[] = {
    {APE_TRANSPORT_LP, CONST_STR_LEN2("/1/")},
    {APE_TRANSPORT_WS, CONST_STR_LEN2("/2/")},
    {APE_TRANSPORT_FT, CONST_STR_LEN2(APE_STATIC_URI)},
    {APE_TRANSPORT_NU, CONST_STR_LEN2("")}
};

static ape_transport_t ape_get_transport(buffer *path)
{
    int i;

    if (path->data == NULL || *path->data == '\0' || *path->data != '/') {
        return APE_TRANSPORT_NU;
    }

    for (i = 0; ape_transports_s[i].type != APE_TRANSPORT_NU; i++) {
        if (path->used - 1 < ape_transports_s[i].len) continue;
        
        if (ape_transports_s[i].len == 3 &&
            (*(uint32_t *)
             path->data & 0x00FFFFFF) == *(uint32_t *)
                                            ape_transports_s[i].path) {
            
            path->pos = ape_transports_s[i].len;
            
            return ape_transports_s[i].type;
        } else if (ape_transports_s[i].len > 3 &&
            strncmp(path->data, ape_transports_s[i].path,
                ape_transports_s[i].len) == 0) {
            
            path->pos = ape_transports_s[i].len;
            
            return ape_transports_s[i].type;
        }
    }

    return APE_TRANSPORT_NU;
}

static int ape_http_callback(void **ctx, callback_type type,
        int value, uint32_t step)
{
    ape_client *client = (ape_client *)ctx[0];
    ape_global *ape = (ape_global *)ctx[1];

    switch(type) {
    case HTTP_METHOD:
        switch(value) {
            case HTTP_GET:
                client->http.method = HTTP_GET;
                break;
            case HTTP_POST:
                client->http.method = HTTP_POST;
                break;
        }
        
        client->http.path         = buffer_new(256);
        client->http.headers.list = ape_array_new(16);
        client->http.headers.tkey = buffer_new(16);
        client->http.headers.tval = buffer_new(64);
        break;
    case HTTP_PATH_CHAR:
        buffer_append_char(client->http.path, (unsigned char)value);
        break;
    case HTTP_QS_CHAR:
        if (client->http.method == HTTP_GET &&
            APE_TRANSPORT_QS_ISJSON(client->http.transport) &&
            client->json.parser != NULL) {

            if (!JSON_parser_char(client->json.parser, (unsigned char)value)) {
                printf("Bad JSON\n");
            }
        } else {

            /* bufferize */
        }
        break;
    case HTTP_HEADER_KEYC:
        buffer_append_char(client->http.headers.tkey, (unsigned char)value);
        break;
    case HTTP_HEADER_VALC:
        buffer_append_char(client->http.headers.tval, (unsigned char)value);
        break;
    case HTTP_BODY_CHAR:
        if (APE_TRANSPORT_QS_ISJSON(client->http.transport) &&
            client->json.parser != NULL) {
            if (!JSON_parser_char(client->json.parser, (unsigned char)value)) {
                printf("Bad JSON\n");
            }
        }
        break;
    case HTTP_PATH_END:
        buffer_append_char(client->http.path, '\0');
        client->http.transport = ape_get_transport(client->http.path);

        if (APE_TRANSPORT_QS_ISJSON(client->http.transport)) {
            JSON_config config;
            init_JSON_config(&config);
            config.depth                  = 15;
            config.callback               = NULL;
            config.callback_ctx           = NULL;
            config.allow_comments         = 0;
            config.handle_floats_manually = 0;

            client->json.parser = new_JSON_parser(&config);
        }
        break;	
    case HTTP_VERSION_MINOR:
        /* fall through */
    case HTTP_VERSION_MAJOR:
        break;
    case HTTP_HEADER_KEY:
        break;
    case HTTP_HEADER_VAL:

        ape_array_add_b(client->http.headers.list,
                client->http.headers.tkey, client->http.headers.tval);
        client->http.headers.tkey   = buffer_new(16);
        client->http.headers.tval   = buffer_new(64);
        
        break;
    case HTTP_CL_VAL:
        break;
    case HTTP_HEADER_END:
        break;
    case HTTP_READY:
        buffer_destroy(client->http.headers.tkey);
        buffer_destroy(client->http.headers.tval);
        ape_server_http_ready(client, ape);
        break;
    default:
        break;
    }
    return 1;
}

/* TODO: If a close has already been sent : doesnt process (check the RFC) */
static void ape_server_on_ws_frame(ape_client *client, const unsigned char *data, ssize_t length, ape_global *ape)
{
#ifdef _HAVE_MSGPACK    
    if (client->serial_method == APE_CLIENT_SERIAL_MSGPACK) {
        int success;
        msgpack_unpacked msg;
        
        printf("We have %d data sized\n", length);
        
        msgpack_unpacked_init(&msg);
        success = msgpack_unpack_next(&msg, data, length, NULL);
        
        msgpack_object obj = msg.data;
        msgpack_object_print(stdout, obj);
        printf("\n");    
        
    } else if (client->serial_method == APE_CLIENT_SERIAL_JSON) {

#endif
        /*
        ape_ws_write(client->socket,
            (unsigned char *)data, length,
            APE_DATA_COPY);
            
            return;*/
        int i;
        
        json_context jcx = {
            .key_under   = 0,
            .start_depth = 0,
            .head        = NULL,
            .current_cx  = NULL
        };
        
        if (client->json.parser == NULL) {
            JSON_config config;
            init_JSON_config(&config);
            config.depth                  = 15;
            config.callback               = json_callback;
            config.callback_ctx           = &jcx;
            config.allow_comments         = 0;
            config.handle_floats_manually = 0;

            if ((client->json.parser = new_JSON_parser(&config)) == NULL) {
                ape_ws_write(client->socket,
                    (char *)CONST_STR_LEN(PATTERN_ERR_INTERNAL),
                    APE_DATA_GLOBAL_STATIC);
                return;
            }
        }
        
        for (i = 0; i < length; i++) {
            if (!JSON_parser_char(client->json.parser, data[i])) {   
                ape_ws_write(client->socket,
                    (char *)CONST_STR_LEN(PATTERN_ERR_BAD_JSON),
                    APE_DATA_GLOBAL_STATIC);
                    
                    return;
                break;
            }
        }
        if (!JSON_parser_done(client->json.parser)) {
            ape_ws_write(client->socket,
                (char *)CONST_STR_LEN(PATTERN_ERR_BAD_JSON),
                APE_DATA_GLOBAL_STATIC);
                
                return;            
        }
        
        ape_cmd_process_multi(jcx.head, client, ape);      
#ifdef _HAVE_MSGPACK
    }
#endif

	//ape_ws_close(client->socket);
	
	APE_EVENT(wsframe, client, data, length, ape);
}

static int ape_server_http_ready(ape_client *client, ape_global *ape)
{
#define REQUEST_HEADER(header) ape_array_lookup(client->http.headers.list, CONST_STR_LEN(header))

	const buffer *host = REQUEST_HEADER("host");
	const buffer *upgrade = REQUEST_HEADER("upgrade");
	
	client->serial_method = APE_CLIENT_SERIAL_JSON;
	
    
    if (host != NULL) {
        /* /!\ the buffer is non null terminated */
    }
	if (upgrade &&
	     upgrade->used == (sizeof(" websocket") - 1) &&
	     strncmp(upgrade->data, " websocket", sizeof(" websocket") - 1) == 0) {
	        
		char *ws_computed_key;
		const buffer *ws_key = REQUEST_HEADER("Sec-WebSocket-Key");
        
        client->http.transport = APE_TRANSPORT_WS;
        
		if (ws_key) {
		    buffer *ws_proto = REQUEST_HEADER("Sec-WebSocket-Protocol");
		    
		    if (ws_proto != NULL) {
		        /* strtok needs a NULL-terminated string */
		    	buffer_append_char(ws_proto, '\0');
		    	
		        char *toksave, *token, *tproto = &ws_proto->data[1];
		        
		        while(1) {
		            token = strtok_r(tproto, ", ", &toksave);
                    
		            if (token == NULL) break;
#ifdef _HAVE_MSGPACK
		            if (strcasecmp(token, "msgpack.ape") == 0) {
		                client->serial_method = APE_CLIENT_SERIAL_MSGPACK;
		            }
#endif
		            tproto = NULL;
		        }
		    }
		    
			ws_computed_key = ape_ws_compute_key(&ws_key->data[1], ws_key->used-1);
			
			APE_socket_write(client->socket, CONST_STR_LEN(WEBSOCKET_HARDCODED_HEADERS), APE_DATA_STATIC);
			
			switch(client->serial_method) {
			    case APE_CLIENT_SERIAL_JSON:
			        APE_socket_write(client->socket, CONST_STR_LEN("Sec-WebSocket-Protocol: json.ape\r\n"), APE_DATA_STATIC);
			        break;
#ifdef _HAVE_MSGPACK
			    case APE_CLIENT_SERIAL_MSGPACK:
			        APE_socket_write(client->socket, CONST_STR_LEN("Sec-WebSocket-Protocol: msgpack.ape\r\n"), APE_DATA_STATIC);
			        break;
#endif			        
			}
			
			APE_socket_write(client->socket, CONST_STR_LEN("Sec-WebSocket-Accept: "), APE_DATA_STATIC);
			APE_socket_write(client->socket, ws_computed_key, strlen(ws_computed_key), APE_DATA_STATIC);
			/* TODO: check the origin */
			APE_socket_write(client->socket, CONST_STR_LEN("\r\nSec-WebSocket-Origin: 127.0.0.1\r\n\r\n"), APE_DATA_STATIC);
			client->socket->callbacks.on_read = ape_ws_process_frame;
			client->ws_state = malloc(sizeof(websocket_state));

			client->ws_state->step    = WS_STEP_START;
			client->ws_state->offset  = 0;
			client->ws_state->data    = NULL;
			client->ws_state->error   = 0;
			client->ws_state->key.pos = 0;
			client->ws_state->close_sent = 0;

			client->ws_state->frame_payload.start  = 0;
			client->ws_state->frame_payload.length = 0;
			client->ws_state->frame_payload.extended_length = 0;
			client->ws_state->data_pos  = 0;
			client->ws_state->frame_pos = 0;
			client->ws_state->on_frame = ape_server_on_ws_frame;
			
			return 1;
		}
	}

    switch(client->http.transport) {
    case APE_TRANSPORT_NU:
    case APE_TRANSPORT_FT:
    {
        APE_EVENT(request, client, ape);
        
        HTTP_PARSER_RESET(&client->http.parser);
        client->http.parser.callback = ape_http_callback;
        client->http.parser.ctx[0]   = client;
        client->http.parser.ctx[1]   = ape;
        client->http.method          = HTTP_GET;
        client->http.transport       = APE_TRANSPORT_NU;
        
        ape_array_destroy(client->http.headers.list);
        
        buffer_destroy(client->http.path);
        client->http.path = NULL;
        
		//APE_socket_write(client->socket, CONST_STR_LEN("HTTP/1.1 200 OK\n\n"), APE_DATA_STATIC);
		//APE_socket_write(client->socket, CONST_STR_LEN("<h1>Ho heil :)</h1>\n\n"), APE_DATA_STATIC);
		
		/*APE_socket_write(client->socket, fill, 20480, APE_DATA_STATIC);
		APE_socket_write(client->socket, fill, 20480, APE_DATA_STATIC);
		APE_socket_write(client->socket, fillb, 20480, APE_DATA_STATIC);
    */
		//#endif
		//APE_socket_shutdown(client->socket);
        
		//printf("Requested : %s\n", client->http.path->data);
		//APE_socket_write(client->socket, CONST_STR_LEN("HTTP/1.1 418 I'm a teapot\n\n"));
		//APE_sendfile(client->socket, client->http.path->data);
		//APE_socket_shutdown(client->socket);
        /*
        sprintf(fullpath, "%s%s", ((ape_server *)client->server->ctx)->chroot, client->http.path->data);
        APE_socket_write(client->socket, CONST_STR_LEN("HTTP/1.1 418 I'm a teapot\n\n"));
        APE_sendfile(client->socket, fullpath);
        APE_socket_shutdown(client->socket);
        printf("URL : %s\n", client->http.path->data);*/
        
        break;        
        
    }

    default:
        break;
    }

    return 0;
}

static void ape_server_on_read(ape_socket *socket_client, ape_global *ape)
{
    int i;
    //printf("data used : %d\n", socket_client->data_in.used);
    /* TODO : implement duff device here (speedup !)*/

    for (i = 0; i < socket_client->data_in.used; i++) {

        if (!parse_http_char(&APE_CLIENT(socket_client)->http.parser,
                socket_client->data_in.data[i])) {
					printf("Failed at %d %c\n", i, socket_client->data_in.data[i]);
					printf("next %c\n", socket_client->data_in.data[i+1]);
            // TODO : graceful shutdown
            shutdown(socket_client->s.fd, 2);
            break;
        }
        if (socket_client->states.state != APE_SOCKET_ST_ONLINE) {
            break;
        }
        //printf("%c", socket_client->data_in.data[i]);
    }
    //printf("\n");
}

static void ape_server_on_connect(ape_socket *socket_server, ape_socket *socket_client, ape_global *ape)
{
    ape_client *client;

    client          = malloc(sizeof(*client)); /* setup the client struct */
    client->socket  = socket_client;
    client->server  = socket_server;
    
    client->user_session = NULL;

    socket_client->_ctx = client; /* link the socket to the client struct */

    HTTP_PARSER_RESET(&client->http.parser);

    client->http.parser.callback = ape_http_callback;
    client->http.parser.ctx[0]   = client;
    client->http.parser.ctx[1]   = ape;
    client->http.method          = HTTP_GET;
    client->http.transport       = APE_TRANSPORT_NU;
    client->http.path            = NULL;
    client->http.headers.list    = NULL;

	client->ws_state			 = NULL;

    client->json.parser = NULL;
    
    _co_event++;
    
}

static void ape_server_on_disconnect(ape_socket *socket_client, ape_global *ape)
{

    buffer_destroy(APE_CLIENT(socket_client)->http.path);
    APE_CLIENT(socket_client)->http.path = NULL;
    
    /* TODO clean headers */
    
    ape_dispatch_async(free, socket_client->_ctx);

    _disco_event++;
    
    //printf("Client has disconnected\n");

} /* ape_socket object is released after this call */

ape_server *ape_server_init(ape_cfg_server_t *conf, ape_global *ape)
{
    ape_socket *socket;
    ape_server *server;
    
    uint16_t port;
    char *local_ip, *cert, *key;
    
    port = conf->port;
    local_ip = conf->ip;
    cert = conf->SSL.cert_path;
    key  = conf->SSL.key_path;

    if ((socket = APE_socket_new((cert != NULL &&
        conf->SSL.enabled ? APE_SOCKET_PT_SSL : APE_SOCKET_PT_TCP), 0, ape)) == NULL ||
        APE_socket_listen(socket, port, local_ip) != 0) {

        printf("[Server] Failed to initialize %s:%d\n", local_ip, port);
        APE_socket_destroy(socket);
        return NULL;
    }

    server          = malloc(sizeof(*server));
    server->socket  = socket;

    if (*local_ip == '*' || *local_ip == '\0') {
        strcpy(server->ip, "0.0.0.0");
    } else {
        strncpy(server->ip, local_ip, 15);
    }
    server->ip[15]  = '\0';
    server->port    = port;

    socket->callbacks.on_read       = ape_server_on_read;
    socket->callbacks.on_connect    = ape_server_on_connect;
    socket->callbacks.on_disconnect = ape_server_on_disconnect;
    socket->_ctx                    = server; /* link the socket to the server struct */
	
	if (APE_SOCKET_ISSECURE(socket)) {
		if ((socket->SSL.ssl = ape_ssl_init_ctx(cert, key)) == NULL) {
		    APE_socket_destroy(socket);
		    printf("[Server] Failed to start %s:%d (Failed to init SSL)\n", server->ip, server->port);
		    free(server);
		    return NULL;
		}
	}

    printf("[Server] Starting %s:%d %s\n", server->ip, server->port, (APE_SOCKET_ISSECURE(socket) ? "[SSL server]" : ""));

    return server;
}

// vim: ts=4 sts=4 sw=4 et

