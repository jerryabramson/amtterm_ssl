struct ctx;

extern int ssl_verbose;

extern struct ctx *sslinit(int fd,char *cacert, char *client_cert, char *client_key, char *passphrase);
extern void sslexit(struct ctx *ctx);
extern int sslready(struct ctx *ctx);
extern ssize_t sslread(struct ctx *ctx,void *buf,size_t count);
extern ssize_t sslwrite(struct ctx *ctx,const void *buf,size_t count);
