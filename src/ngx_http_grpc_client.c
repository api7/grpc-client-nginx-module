#include <dlfcn.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_str_t            engine_path;
    void                *engine;
} ngx_http_grpc_cli_main_conf_t;


#define must_resolve_symbol(hd, f) \
    f = dlsym(hd, #f); \
    err = dlerror(); \
    if (err != NULL) { \
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, \
                      "failed to resolve symbol: %s", err); \
        return NGX_ERROR; \
    }


extern ngx_module_t  ngx_http_grpc_client_module;

static ngx_int_t ngx_http_grpc_cli_init_worker(ngx_cycle_t *cycle);
static void ngx_http_grpc_cli_exit_worker(ngx_cycle_t *cycle);

static void *ngx_http_grpc_cli_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_grpc_cli_engine_path(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void *(*grpc_engine_connect) (char *, const char *, int);
static void *(*grpc_engine_call)(char *, void *, const char *, int, const char *, int, int*);
static void (*grpc_engine_close) (void *);
static void (*grpc_engine_free) (void *);


static ngx_command_t ngx_http_grpc_cli_cmds[] = {
    { ngx_string("grpc_client_engine_path"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_grpc_cli_engine_path,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    ngx_null_command
};


static ngx_http_module_t ngx_http_grpc_cli_module_ctx = {
    NULL,                                    /* preconfiguration */
    NULL,                                    /* postconfiguration */

    ngx_http_grpc_cli_create_main_conf,      /* create main configuration */
    NULL,                                    /* init main configuration */

    NULL,                                    /* create server configuration */
    NULL,                                    /* merge server configuration */

    NULL,                                    /* create location configuration */
    NULL                                     /* merge location configuration */
};


ngx_module_t ngx_http_grpc_client_module = {
    NGX_MODULE_V1,
    &ngx_http_grpc_cli_module_ctx,       /* module context */
    ngx_http_grpc_cli_cmds,              /* module directives */
    NGX_HTTP_MODULE,                     /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    ngx_http_grpc_cli_init_worker,       /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    NULL,                                /* exit process */
    ngx_http_grpc_cli_exit_worker,       /* exit master */
    NGX_MODULE_V1_PADDING
};


static char *
ngx_http_grpc_cli_engine_path(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_grpc_cli_main_conf_t *gccf = conf;

    ngx_str_t                         *value;

    if (gccf->engine_path.data != NULL) {
        return "is duplicate";
    }

    value = cf->args->elts;

    gccf->engine_path.data = ngx_palloc(cf->pool, value[1].len + 1);
    if (gccf->engine_path.data == NULL) {
        return "no memory";
    }

    gccf->engine_path.len = value[1].len + 1;
    ngx_memcpy(gccf->engine_path.data, value[1].data, value[1].len);
    gccf->engine_path.data[value[1].len] = '\0';

    return NGX_CONF_OK;
}


static void *
ngx_http_grpc_cli_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_grpc_cli_main_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_grpc_cli_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    return conf;
}


static ngx_int_t
ngx_http_grpc_cli_init_worker(ngx_cycle_t *cycle)
{
    char                          *err;
    ngx_http_grpc_cli_main_conf_t *gccf;

    gccf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_grpc_client_module);
    if (gccf->engine_path.data == NULL) {
        return NGX_OK;
    }

    dlerror();    /* Clear any existing error */

    /* we need to load the shared library in the worker process to work around
     * https://github.com/golang/go/issues/53806 */
    gccf->engine = dlopen((const char *) gccf->engine_path.data, RTLD_NOW);
    if (gccf->engine == NULL) {
        ngx_log_error(NGX_LOG_EMERG, ngx_cycle->log, 0, "failed to init engine: %s with %s",
                      dlerror(), gccf->engine_path.data);
        return NGX_ERROR;
    }

    must_resolve_symbol(gccf->engine, grpc_engine_connect);
    must_resolve_symbol(gccf->engine, grpc_engine_close);
    must_resolve_symbol(gccf->engine, grpc_engine_free);
    must_resolve_symbol(gccf->engine, grpc_engine_call);

    return NGX_OK;
}


static void ngx_http_grpc_cli_exit_worker(ngx_cycle_t *cycle)
{
    ngx_http_grpc_cli_main_conf_t *gccf;

    gccf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_grpc_client_module);
    if (gccf->engine != NULL) {
        dlclose(gccf->engine);
    }
}


int
ngx_http_grpc_cli_is_engine_inited(void)
{
    ngx_http_grpc_cli_main_conf_t *gccf;

    gccf = ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_grpc_client_module);
    return gccf->engine != NULL;
}


void *
ngx_http_grpc_cli_connect(char *err_buf, ngx_http_request_t *r,
                          const char *target_data, int target_len)
{
    void        *ctx;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "create gRPC connection");

    ctx = grpc_engine_connect(err_buf, target_data, target_len);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "new gRPC ctx: %p", ctx);

    return ctx;
}


void
ngx_http_grpc_cli_close(ngx_http_request_t *r, void *ctx)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "close gRPC connection");
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "free gRPC ctx: %p", ctx);

    grpc_engine_close(ctx);
}


int
ngx_http_grpc_cli_call(char *err_buf, void *ctx,
                       const char *method_data, int method_len,
                       const char *req_data, int req_len,
                       ngx_str_t *resp)
{
    int         resp_len;
    u_char     *out;

    out = grpc_engine_call(err_buf, ctx, method_data, method_len, req_data, req_len, &resp_len);
    if (out == NULL) {
        return NGX_ERROR;
    }

    resp->data = out;
    resp->len = resp_len;
    return NGX_OK;
}


void
ngx_http_grpc_cli_free(void *data)
{
    grpc_engine_free(data);
}
