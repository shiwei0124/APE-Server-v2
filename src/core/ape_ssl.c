#include "ape_ssl.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

void ape_ssl_init()
{
	SSL_library_init();            
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
}

ape_ssl_t *ape_ssl_init_ctx()
{
	ape_ssl_t *ssl = NULL;
	SSL_CTX *ctx = SSL_CTX_new(SSLv23_method());
	
	if (ctx == NULL) {
		printf("Failed to init SSL ctx\n");
		return NULL;
	}
	
	ssl = malloc(sizeof(*ssl));
	ssl->ctx = ctx;
	ssl->con = NULL;

    SSL_CTX_set_options(ssl->ctx, SSL_OP_ALL);
	SSL_CTX_set_default_read_ahead(ssl->ctx, 1);
	//SSL_CTX_set_read_ahead(ssl->ctx, 1);
	
	if (SSL_CTX_use_certificate_chain_file(ssl->ctx, "/Users/anthonycatel/ape2/APE-Server-v2/etc/self.pem") == 0) {
		printf("Failed to load cert\n");
		SSL_CTX_free(ctx);
		free(ssl);
		return NULL;
	}
	if (SSL_CTX_use_PrivateKey_file(ssl->ctx, "/Users/anthonycatel/ape2/APE-Server-v2/etc/self.pem", SSL_FILETYPE_PEM) == 0) {
		printf("Failed to load private key\n");
		SSL_CTX_free(ctx);
		free(ssl);
		return NULL;		
	}
	
	printf("[SSL] New context\n");

	return ssl;
}

ape_ssl_t *ape_ssl_init_con(ape_ssl_t *parent, int fd)
{
	ape_ssl_t *ssl = NULL;
	SSL_CTX *ctx = parent->ctx;
	
	SSL *con = SSL_new(ctx);
	
	if (con == NULL) {
		return NULL;
	}
	
	SSL_set_accept_state(con);
	
	if (SSL_set_fd(con, fd) != 1) {
		printf("Failed to set fd on ssl\n");
		return NULL;
	}
	
	ssl = malloc(sizeof(*ssl));
	ssl->ctx = NULL;
	ssl->con = con;
	
	/* TODO: SSL_set_connect_state for SSL connection */

	return ssl;
}

int ape_ssl_read(ape_ssl_t *ssl, void *buf, int num)
{
	return SSL_read(ssl->con, buf, num);
}

void ape_ssl_destroy(ape_ssl_t *ssl)
{
	if (ssl == NULL) return;
	
	if (ssl->ctx != NULL)
		SSL_CTX_free(ssl->ctx);
	if (ssl->con != NULL)
		SSL_free(ssl->con);
	
	free(ssl);
}