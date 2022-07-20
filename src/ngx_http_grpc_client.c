#include <dlfcn.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#if (!NGX_THREADS)
#error "this module requires to be compiled with --with-thread option"
#endif


typedef struct {
    ngx_str_t            engine_path;
    void                *engine;
} ngx_http_grpc_cli_main_conf_t;


typedef struct {
    void                *engine_ctx;
} ngx_http_grpc_cli_ctx_t;


typedef struct {
    uint64_t        task_id;
    uint64_t        size;
    u_char         *buf;
} ngx_http_grpc_cli_task_res_t;


typedef struct {
    ngx_queue_t                   queue;
    ngx_http_grpc_cli_task_res_t  res;
} ngx_http_grpc_cli_posted_event_ctx_t;


typedef struct {
    ngx_thread_task_t       *task;
    ngx_thread_pool_t       *thread_pool;

    ngx_http_grpc_cli_task_res_t *finished_tasks;
    int                           finished_task_num;

    ngx_event_t             *posted_ev;
    ngx_queue_t              occupied;
    ngx_queue_t              free;
} ngx_http_grpc_cli_thread_ctx_t;


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
static void *(*grpc_engine_call)(char *, uint64_t, void *, const char *, int, const char *,
                                 int, int *);
static void (*grpc_engine_close) (void *);
static void (*grpc_engine_free) (void *);
static void *(*grpc_engine_wait) (int *);


static ngx_str_t thread_pool_name = ngx_string("grpc-client-nginx-module");

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


static void
ngx_http_grpc_cli_thread_handler(void *data, ngx_log_t *log)
{
    ngx_http_grpc_cli_thread_ctx_t     *thctx = data;

    thctx->finished_tasks = grpc_engine_wait(&thctx->finished_task_num);
}


static void
ngx_http_grpc_cli_insert_posted_event_ctx(ngx_http_grpc_cli_thread_ctx_t *thctx,
                                          ngx_log_t *log, ngx_http_grpc_cli_task_res_t res)
{
    ngx_http_grpc_cli_posted_event_ctx_t        *ctx;

    if (!ngx_queue_empty(&thctx->free)) {
        ngx_queue_t         *q;

        q = ngx_queue_head(&thctx->free);
        ngx_queue_remove(q);
        ctx = ngx_queue_data(q, ngx_http_grpc_cli_posted_event_ctx_t, queue);

    } else {
        ctx = ngx_pcalloc(ngx_cycle->pool, sizeof(ngx_http_grpc_cli_posted_event_ctx_t));
        if (ctx == NULL) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "no memory");
            return;
        }
    }

    ctx->res = res;
    ngx_queue_insert_tail(&thctx->occupied, &ctx->queue);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, log, 0, "post finished task %uL, ctx:%p",
                   res.task_id, ctx);
}


static void
ngx_http_grpc_cli_thread_event_handler(ngx_event_t *ev)
{
    int                               i;
    ngx_http_grpc_cli_thread_ctx_t   *thctx;
    ngx_thread_pool_t                 *thread_pool;
    ngx_thread_task_t                 *task;

    thctx = ev->data;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ev->log, 0, "post %d finished task",
                   thctx->finished_task_num);

    for (i = 0; i < thctx->finished_task_num; i++) {
        /* remember to free the res.buf if the task is cancelled */
        ngx_http_grpc_cli_insert_posted_event_ctx(thctx, ev->log, thctx->finished_tasks[i]);
        ngx_post_event(thctx->posted_ev, &ngx_posted_events);
    }

    grpc_engine_free(thctx->finished_tasks);
    thctx->finished_tasks = NULL;

    thread_pool = thctx->thread_pool;
    task = thctx->task;

    if (ngx_thread_task_post(thread_pool, task) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, ngx_cycle->log, 0,
                      "failed to wait gRPC engine: task post failed");
        return;
    }
}


static void
ngx_http_grpc_cli_thread_post_event_handler(ngx_event_t *ev)
{
    ngx_http_grpc_cli_thread_ctx_t       *thctx = ev->data;
    ngx_log_t                            *log = ev->log;
    ngx_queue_t                          *q;
    ngx_http_grpc_cli_ctx_t              *ctx;
    ngx_http_grpc_cli_posted_event_ctx_t *posted_event_ctx;

    if (ngx_queue_empty(&thctx->occupied)) {
        return;
    }

    q = ngx_queue_head(&thctx->occupied);
    ngx_queue_remove(q);
    ngx_queue_insert_tail(&thctx->free, q);
    posted_event_ctx = ngx_queue_data(q, ngx_http_grpc_cli_posted_event_ctx_t, queue);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, log, 0, "resume finished task %uL, ctx:%p",
                   posted_event_ctx->res.task_id, posted_event_ctx);

    ctx = (ngx_http_grpc_cli_ctx_t *) posted_event_ctx->res.task_id;

    grpc_engine_free(posted_event_ctx->res.buf);
}


static ngx_int_t
ngx_http_grpc_cli_thread(ngx_cycle_t *cycle)
{
    ngx_thread_pool_t                  *thread_pool;
    ngx_thread_task_t                  *task;
    ngx_http_grpc_cli_thread_ctx_t     *thctx;

    thread_pool = ngx_thread_pool_get(cycle, &thread_pool_name);
    if (thread_pool == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "failed to init engine: missing thread pool %V", &thread_pool_name);
        return NGX_ERROR;
    }

    task = ngx_thread_task_alloc(cycle->pool,
                                 sizeof(ngx_http_grpc_cli_thread_ctx_t));
    if (task == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "failed to init engine: no memory");
        return NGX_ERROR;
    }

    thctx = task->ctx;
    thctx->task = task;
    thctx->thread_pool = thread_pool;
    thctx->posted_ev = ngx_pcalloc(cycle->pool, sizeof(ngx_event_t));
    thctx->posted_ev->handler = ngx_http_grpc_cli_thread_post_event_handler;
    thctx->posted_ev->data = thctx;
    thctx->posted_ev->log = cycle->log;
    ngx_queue_init(&thctx->free);
    ngx_queue_init(&thctx->occupied);

    task->handler = ngx_http_grpc_cli_thread_handler;
    task->event.handler = ngx_http_grpc_cli_thread_event_handler;
    task->event.data = thctx;
    task->event.log = cycle->log;

    if (ngx_thread_task_post(thread_pool, task) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "failed to init engine: task post failed");
        return NGX_ERROR;
    }

    return NGX_OK;
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
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "failed to init engine: %s with %s",
                      dlerror(), gccf->engine_path.data);
        return NGX_ERROR;
    }

    must_resolve_symbol(gccf->engine, grpc_engine_connect);
    must_resolve_symbol(gccf->engine, grpc_engine_close);
    must_resolve_symbol(gccf->engine, grpc_engine_free);
    must_resolve_symbol(gccf->engine, grpc_engine_call);
    must_resolve_symbol(gccf->engine, grpc_engine_wait);

    return ngx_http_grpc_cli_thread(cycle);
}


static void
ngx_http_grpc_cli_exit_worker(ngx_cycle_t *cycle)
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
    void                    *engine_ctx;
    ngx_http_grpc_cli_ctx_t *ctx;

    ctx = ngx_calloc(sizeof(ngx_http_grpc_cli_ctx_t), r->connection->log);
    if (ctx == NULL) {
        return NULL;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "create gRPC connection");

    engine_ctx = grpc_engine_connect(err_buf, target_data, target_len);
    if (engine_ctx == NULL) {
        goto free_ctx;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "new gRPC ctx: %p", engine_ctx);

    ctx->engine_ctx = engine_ctx;
    return ctx;

free_ctx:
    ngx_free(ctx);
    return NULL;
}


void
ngx_http_grpc_cli_close(ngx_http_request_t *r, ngx_http_grpc_cli_ctx_t *ctx)
{
    ngx_log_t           *log;
    void                *engine_ctx;

    if (r == NULL) {
        log = ngx_cycle->log;
    } else {
        log = r->connection->log;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "close gRPC connection");

    engine_ctx = ctx->engine_ctx;
    ngx_free(ctx);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "free gRPC ctx: %p", engine_ctx);

    grpc_engine_close(engine_ctx);
}


int
ngx_http_grpc_cli_call(char *err_buf, ngx_http_grpc_cli_ctx_t *ctx,
                       const char *method_data, int method_len,
                       const char *req_data, int req_len,
                       ngx_str_t *resp)
{
    int                  resp_len;
    u_char              *out;
    void                *engine_ctx;

    engine_ctx = ctx->engine_ctx;
    out = grpc_engine_call(err_buf, (uint64_t) ctx, engine_ctx,
                           method_data, method_len,
                           req_data, req_len,
                           &resp_len);
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
