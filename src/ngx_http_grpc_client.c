#include <dlfcn.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_lua_util.h>


#define NGX_HTTP_GRPC_CLIENT_STATE_OK         0
#define NGX_HTTP_GRPC_CLIENT_STATE_TIMEOUT    1

#define NGX_HTTP_GRPC_CLIENT_RES_TYPE_OK             0
#define NGX_HTTP_GRPC_CLIENT_RES_TYPE_ERR            1
#define NGX_HTTP_GRPC_CLIENT_RES_TYPE_OK_NOT_RES     2

#define NGX_HTTP_GRPC_CLIENT_STREAM_TYPE_UNARY                  0
#define NGX_HTTP_GRPC_CLIENT_STREAM_TYPE_CLIENT_STREAM          1
#define NGX_HTTP_GRPC_CLIENT_STREAM_TYPE_SERVER_STREAM          2
#define NGX_HTTP_GRPC_CLIENT_STREAM_TYPE_BIDIRECTIONAL_STREAM   3

typedef struct {
    ngx_str_t            engine_path;
    void                *engine;
    ngx_thread_pool_t   *thread_pool;
    ngx_thread_task_t   *task;
} ngx_http_grpc_cli_main_conf_t;


typedef struct {
    uint64_t        task_id;
    uint64_t        size;
    /*
     * We require this Nginx module runs on 64bit aligned machine, like x64 and arm64.
     * So that we can store the result types in the unused 7 bitfields
     * */
    u_char         *buf;
} ngx_http_grpc_cli_task_res_t;


typedef struct {
    void                         *engine_ctx;

    ngx_queue_t              opening;
    ngx_queue_t              free;

    unsigned                 free_delayed:1;
} ngx_http_grpc_cli_ctx_t;


typedef struct {
    ngx_http_request_t           *r;
    ngx_http_lua_co_ctx_t        *wait_co_ctx;

    void                         *prev_data;

    ngx_http_grpc_cli_task_res_t  res;
    int                           state;

    ngx_rbtree_node_t            *node;

    ngx_queue_t                   queue;
    ngx_http_grpc_cli_ctx_t      *parent;
    int                           type;
} ngx_http_grpc_cli_stream_ctx_t;


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


typedef struct {
    ngx_msec_t timeout;
} ngx_http_grpc_cli_call_opt_t;


#define must_resolve_symbol(hd, f) \
    f = dlsym(hd, #f); \
    err = dlerror(); \
    if (err != NULL) { \
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, \
                      "failed to resolve symbol: %s", err); \
        return NGX_ERROR; \
    }
#define ERR_BUF_LEN 512


extern ngx_module_t  ngx_http_grpc_client_module;

static ngx_int_t ngx_http_grpc_cli_init_worker(ngx_cycle_t *cycle);
static void ngx_http_grpc_cli_exit_worker(ngx_cycle_t *cycle);

static void *ngx_http_grpc_cli_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_grpc_cli_engine_path(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void *(*grpc_engine_connect) (unsigned char *, size_t *, const char *, int,
                                     void *);
static void (*grpc_engine_call)(unsigned char *, size_t *,
                                uint64_t, void *, const char *, int, const char *, int,
                                void *);
static void (*grpc_engine_new_stream)(unsigned char *, size_t *,
                                      uint64_t, void *, const char *, int, const char *, int,
                                      void *, int);
static void (*grpc_engine_stream_recv) (void *);
static void (*grpc_engine_stream_send) (void *, const char *, int);
static void (*grpc_engine_close) (void *);
static void (*grpc_engine_close_stream) (void *);
static void (*grpc_engine_free) (void *);
static void *(*grpc_engine_wait) (int *);


static ngx_rbtree_t       ngx_http_grpc_cli_ongoing_tasks;
static ngx_rbtree_node_t  ngx_http_grpc_cli_ongoing_task_sentinel;
static int                ngx_http_grpc_cli_ongoing_tasks_num;

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
                                          ngx_log_t *log, ngx_http_grpc_cli_task_res_t *res)
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

    ctx->res = *res;
    ngx_queue_insert_tail(&thctx->occupied, &ctx->queue);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, log, 0, "post finished task %uL, ctx:%p",
                   res->task_id, ctx);
}


static ngx_rbtree_node_t *
ngx_http_grpc_cli_lookup_task(ngx_rbtree_key_t key)
{
    ngx_rbtree_node_t    *node, *sentinel;

    node = ngx_http_grpc_cli_ongoing_tasks.root;
    sentinel = ngx_http_grpc_cli_ongoing_tasks.sentinel;

    while (node != sentinel) {
        if (key < node->key) {
            node = node->left;
            continue;
        }

        if (key > node->key) {
            node = node->right;
            continue;
        }

        return node;
    }

    return NULL;
}


static int
ngx_http_grpc_cli_task_num(void)
{
    return ngx_http_grpc_cli_ongoing_tasks_num;
}


static ngx_int_t
ngx_http_grpc_cli_keep_task(ngx_http_grpc_cli_stream_ctx_t *ctx, ngx_log_t *log)
{
    ngx_rbtree_node_t     *node;

    node = ngx_alloc(sizeof(ngx_rbtree_node_t), log);
    if (node == NULL) {
        return NGX_ERROR;
    }

    node->key = (ngx_rbtree_key_t) ctx;
    ngx_http_grpc_cli_ongoing_tasks_num++;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, log, 0, "increase ongoing tasks to %d, stream: %p",
                   ngx_http_grpc_cli_ongoing_tasks_num, ctx);

    ngx_rbtree_insert(&ngx_http_grpc_cli_ongoing_tasks, node);
    ctx->node = node;

    return NGX_OK;
}


static void
ngx_http_grpc_cli_unkeep_task(ngx_http_grpc_cli_stream_ctx_t *ctx)
{
    if (ctx->node == NULL) {
        return;
    }

    ngx_http_grpc_cli_ongoing_tasks_num--;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0,
                   "decrease ongoing tasks to %d, stream: %p",
                   ngx_http_grpc_cli_ongoing_tasks_num, ctx);

    ngx_rbtree_delete(&ngx_http_grpc_cli_ongoing_tasks, ctx->node);
    ngx_free(ctx->node);
    ctx->node = NULL;
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
        ngx_http_grpc_cli_task_res_t *res = &thctx->finished_tasks[i];

        ngx_http_grpc_cli_insert_posted_event_ctx(thctx, ev->log, res);
    }

    grpc_engine_free(thctx->finished_tasks);
    thctx->finished_tasks = NULL;

    ngx_post_event(thctx->posted_ev, &ngx_posted_events);

    if (ngx_quit || ngx_exiting) {
        return;
    }

    if (ngx_http_grpc_cli_task_num() <= thctx->finished_task_num) {
        return;
    }

    /* still have tasks to wait */
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ev->log, 0, "post grpc client thread to wait %d tasks",
                   ngx_http_grpc_cli_task_num() - thctx->finished_task_num);

    thread_pool = thctx->thread_pool;
    task = thctx->task;

    if (ngx_thread_task_post(thread_pool, task) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, ev->log, 0,
                      "failed to wait gRPC engine: task post failed");
        return;
    }
}


static ngx_int_t
ngx_http_grpc_cli_resume(ngx_http_request_t *r)
{
    lua_State                            *vm;
    ngx_connection_t                     *c;
    ngx_int_t                             rc;
    ngx_uint_t                            nreqs, nret;
    ngx_http_lua_ctx_t                   *lctx;
    ngx_http_grpc_cli_ctx_t              *ctx;
    ngx_http_grpc_cli_stream_ctx_t       *sctx;
    ngx_http_grpc_cli_task_res_t         *res;

    lctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    if (lctx == NULL) {
        return NGX_ERROR;
    }

    sctx = lctx->cur_co_ctx->data;
    lctx->cur_co_ctx->data = sctx->prev_data;
    res = &sctx->res;

    lctx->resume_handler = ngx_http_lua_wev_handler;

    c = r->connection;
    vm = ngx_http_lua_get_lua_vm(r, lctx);
    nreqs = c->requests;
    nret = 2;

    if (sctx->state == NGX_HTTP_GRPC_CLIENT_STATE_TIMEOUT) {
        lua_pushboolean(lctx->cur_co_ctx->co, 0);
        lua_pushliteral(lctx->cur_co_ctx->co, "timeout");

    } else {
        int type;

        ngx_http_lua_assert(res->buf != NULL);
        type = (int64_t) (res->buf) & 7;
        res->buf = (u_char *) ((int64_t) (res->buf) >> 3 << 3);

        if (type == NGX_HTTP_GRPC_CLIENT_RES_TYPE_OK) {
            lua_pushboolean(lctx->cur_co_ctx->co, 1);
            lua_pushlstring(lctx->cur_co_ctx->co, (const char *) res->buf, res->size);

        } else if (type == NGX_HTTP_GRPC_CLIENT_RES_TYPE_OK_NOT_RES) {
            lua_pushboolean(lctx->cur_co_ctx->co, 1);
            lua_pushnil(lctx->cur_co_ctx->co);

        } else {
            lua_pushboolean(lctx->cur_co_ctx->co, 0);
            lua_pushlstring(lctx->cur_co_ctx->co, (const char *) res->buf, res->size);
        }

        grpc_engine_free(res->buf);
    }

    if (sctx->type == NGX_HTTP_GRPC_CLIENT_STREAM_TYPE_UNARY) {
        /* reuse the sctx only when it is an unary stream */
        ctx = sctx->parent;
        ngx_queue_insert_head(&ctx->free, &sctx->queue);
    }

    rc = ngx_http_lua_run_thread(vm, r, lctx, nret);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "lua run thread returned %d", rc);

    if (rc == NGX_AGAIN) {
        return ngx_http_lua_run_posted_threads(c, vm, r, lctx, nreqs);
    }

    if (rc == NGX_DONE) {
        ngx_http_lua_finalize_request(r, NGX_DONE);
        return ngx_http_lua_run_posted_threads(c, vm, r, lctx, nreqs);
    }

    /* rc == NGX_ERROR || rc >= NGX_OK */

    if (lctx->entered_content_phase) {
        ngx_http_lua_finalize_request(r, rc);
        return NGX_DONE;
    }

    return rc;
}


static void
ngx_http_grpc_cli_to_resume(ngx_http_grpc_cli_stream_ctx_t *ctx)
{
    ngx_http_request_t                   *r;
    ngx_connection_t                     *c;
    ngx_http_lua_ctx_t                   *lctx;

    ngx_http_grpc_cli_unkeep_task(ctx);

    r = ctx->r;
    c = r->connection;

    lctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    ngx_http_lua_assert(lctx != NULL);

    lctx->cur_co_ctx = ctx->wait_co_ctx;
    ctx->wait_co_ctx = NULL;
    ctx->prev_data = lctx->cur_co_ctx->data;
    lctx->cur_co_ctx->data = ctx;

    if (lctx->entered_content_phase) {
        (void) ngx_http_grpc_cli_resume(r);

    } else {
        lctx->resume_handler = ngx_http_grpc_cli_resume;
        ngx_http_core_run_phases(r);
    }

    ngx_http_run_posted_requests(c);
}


static void
ngx_http_grpc_cli_thread_post_event_handler(ngx_event_t *ev)
{
    ngx_http_grpc_cli_thread_ctx_t       *thctx = ev->data;
    ngx_queue_t                          *q;
    ngx_http_grpc_cli_stream_ctx_t       *ctx;
    ngx_http_grpc_cli_posted_event_ctx_t *posted_event_ctx;
    ngx_http_grpc_cli_task_res_t         *res;

    while (!ngx_queue_empty(&thctx->occupied)) {
        q = ngx_queue_head(&thctx->occupied);
        ngx_queue_remove(q);
        ngx_queue_insert_tail(&thctx->free, q);
        posted_event_ctx = ngx_queue_data(q, ngx_http_grpc_cli_posted_event_ctx_t, queue);

        res = &posted_event_ctx->res;

        /* remember to free the res.buf if the task is cancelled */
        if (ngx_http_grpc_cli_lookup_task(res->task_id) == NULL) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ev->log, 0,
                           "finished task %uL is cancelled",
                           res->task_id);

            res->buf = (u_char *) ((int64_t) (res->buf) >> 3 << 3);
            grpc_engine_free(res->buf);
            continue;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ev->log, 0, "resume finished task %uL, ctx:%p",
                       res->task_id, posted_event_ctx);

        ctx = (ngx_http_grpc_cli_stream_ctx_t *) res->task_id;
        ctx->res = *res;

        if (ctx->wait_co_ctx->sleep.timer_set) {
            ngx_del_timer(&ctx->wait_co_ctx->sleep);
        }

        ctx->wait_co_ctx->cleanup = NULL;

        ngx_http_grpc_cli_to_resume(ctx);
    }
}


static void
ngx_http_grpc_cli_timeout_handler(ngx_event_t *ev)
{
    ngx_http_lua_co_ctx_t              *wait_co_ctx;
    ngx_http_grpc_cli_stream_ctx_t     *ctx;

    wait_co_ctx = ev->data;
    wait_co_ctx->cleanup = NULL;

    ctx = wait_co_ctx->data;
    ctx->state = NGX_HTTP_GRPC_CLIENT_STATE_TIMEOUT;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ev->log, 0, "resume timeout task %uL, event:%p",
                   ctx, ev);

    ngx_http_grpc_cli_to_resume(ctx);
}


static void
ngx_http_grpc_cli_cleanup(void *data)
{
    ngx_http_lua_co_ctx_t              *wait_co_ctx = data;
    ngx_http_grpc_cli_stream_ctx_t     *ctx;

    ctx = wait_co_ctx->data;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, "cleanup aborted task %uL", ctx);

    if (ctx->wait_co_ctx->sleep.timer_set) {
        ngx_del_timer(&ctx->wait_co_ctx->sleep);
    }

    ngx_http_grpc_cli_unkeep_task(ctx);
}


static ngx_int_t
ngx_http_grpc_cli_thread(ngx_http_grpc_cli_main_conf_t *gccf, ngx_cycle_t *cycle)
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

    gccf->thread_pool = thread_pool;
    gccf->task = task;

    return NGX_OK;
}


static ngx_int_t
ngx_http_grpc_cli_init_worker(ngx_cycle_t *cycle)
{
    char                          *err;
    ngx_http_grpc_cli_main_conf_t *gccf;

    gccf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_grpc_client_module);
    if (gccf == NULL) {
        /* only stream subsys is available */
        return NGX_OK;
    }

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
    must_resolve_symbol(gccf->engine, grpc_engine_close_stream);
    must_resolve_symbol(gccf->engine, grpc_engine_free);
    must_resolve_symbol(gccf->engine, grpc_engine_call);
    must_resolve_symbol(gccf->engine, grpc_engine_new_stream);
    must_resolve_symbol(gccf->engine, grpc_engine_stream_recv);
    must_resolve_symbol(gccf->engine, grpc_engine_stream_send);
    must_resolve_symbol(gccf->engine, grpc_engine_wait);

    ngx_http_grpc_cli_ongoing_tasks_num = 0;
    ngx_rbtree_init(&ngx_http_grpc_cli_ongoing_tasks,
                    &ngx_http_grpc_cli_ongoing_task_sentinel, ngx_rbtree_insert_value);

    return ngx_http_grpc_cli_thread(gccf, cycle);
}


static void
ngx_http_grpc_cli_exit_worker(ngx_cycle_t *cycle)
{
    ngx_http_grpc_cli_main_conf_t *gccf;

    gccf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_grpc_client_module);
    if (gccf == NULL) {
        /* only stream subsys is available */
        return;
    }

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
ngx_http_grpc_cli_connect(unsigned char *err_buf, size_t *err_len, ngx_http_request_t *r,
                          const char *target_data, int target_len,
                          void *dial_opt)
{
    void                    *engine_ctx;
    ngx_http_grpc_cli_ctx_t *ctx;

    ctx = ngx_calloc(sizeof(ngx_http_grpc_cli_ctx_t), r->connection->log);
    if (ctx == NULL) {
        return NULL;
    }

    ngx_queue_init(&ctx->free);
    ngx_queue_init(&ctx->opening);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "new gRPC ctx: %p", ctx);

    engine_ctx = grpc_engine_connect(err_buf, err_len, target_data, target_len, dial_opt);
    if (engine_ctx == NULL) {
        goto free_ctx;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "create gRPC connection: %p", engine_ctx);

    ctx->engine_ctx = engine_ctx;
    return ctx;

free_ctx:
    ngx_free(ctx);
    return NULL;
}


static void
ngx_http_grpc_cli_free_conn(ngx_http_grpc_cli_ctx_t *ctx, ngx_log_t *log)
{
    while (!ngx_queue_empty(&ctx->free)) {
        ngx_queue_t                    *q;
        ngx_http_grpc_cli_stream_ctx_t *sctx;

        q = ngx_queue_last(&ctx->free);
        ngx_queue_remove(q);
        sctx = ngx_queue_data(q, ngx_http_grpc_cli_stream_ctx_t, queue);
        ngx_free(sctx);
    }

    ngx_free(ctx);
}


void
ngx_http_grpc_cli_close(ngx_http_grpc_cli_ctx_t *ctx, ngx_http_request_t *r)
{
    int                  gc;
    ngx_log_t           *log;
    void                *engine_ctx;

    if (r != NULL) {
        log = r->connection->log;
        gc = 0;
    } else {
        log = ngx_cycle->log;
        gc = 1;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, log, 0,
                   "close gRPC connection, gc:%d, ctx:%p, engine_ctx:%p",
                   gc, ctx, ctx->engine_ctx);

    if (ctx->engine_ctx != NULL) {
        engine_ctx = ctx->engine_ctx;
        grpc_engine_close(engine_ctx);
        ctx->engine_ctx = NULL;
    }

    if (!gc) {
        return;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "free gRPC ctx: %p", ctx);

    if (!ngx_queue_empty(&ctx->opening)) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "delay free of gRPC ctx:%p", ctx);

        ctx->free_delayed = 1;
        return;
    }

    ngx_http_grpc_cli_free_conn(ctx, log);
}


void
ngx_http_grpc_cli_free(void *data)
{
    grpc_engine_free(data);
}


static int
ngx_http_grpc_cli_schedule_task_thread_if_needed(ngx_http_grpc_cli_main_conf_t  *gccf)
{
    if (!gccf->task->event.active) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, "post grpc client thread");

        /* kick off background thread for the first task */
        if (ngx_thread_task_post(gccf->thread_pool, gccf->task) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static int
ngx_http_grpc_cli_kick_off_task(ngx_http_grpc_cli_main_conf_t  *gccf,
                                ngx_http_request_t *r, ngx_http_lua_ctx_t *lctx,
                                ngx_http_grpc_cli_stream_ctx_t *sctx,
                                ngx_http_grpc_cli_call_opt_t *call_opt)
{
    ngx_http_lua_co_ctx_t          *wait_co_ctx;

    if (ngx_http_grpc_cli_schedule_task_thread_if_needed(gccf) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_http_grpc_cli_keep_task(sctx, r->connection->log) != NGX_OK) {
        return NGX_DECLINED;
    }

    sctx->state = NGX_HTTP_GRPC_CLIENT_STATE_OK;

    wait_co_ctx = lctx->cur_co_ctx;
    sctx->wait_co_ctx = wait_co_ctx;

    wait_co_ctx->data = sctx;
    wait_co_ctx->sleep.handler = ngx_http_grpc_cli_timeout_handler;
    wait_co_ctx->sleep.data = wait_co_ctx;
    wait_co_ctx->sleep.log = r->connection->log;
    ngx_add_timer(&wait_co_ctx->sleep, call_opt->timeout);

    wait_co_ctx->cleanup = ngx_http_grpc_cli_cleanup;

    return NGX_OK;
}


void *
ngx_http_grpc_cli_new_stream(unsigned char *err_buf, size_t *err_len,
                             ngx_http_request_t *r, ngx_http_grpc_cli_ctx_t *ctx,
                             const char *method_data, int method_len,
                             const char *req_data, int req_len,
                             ngx_http_grpc_cli_call_opt_t *call_opt, int stream_type)
{
    ngx_int_t                       rc;
    void                           *engine_ctx;
    ngx_http_lua_ctx_t             *lctx;
    ngx_http_grpc_cli_main_conf_t  *gccf;
    ngx_http_grpc_cli_stream_ctx_t *sctx;

    gccf = ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_grpc_client_module);

    if (ctx->engine_ctx == NULL) {
        *err_len = ngx_snprintf(err_buf, *err_len, "closed") - err_buf;
        return NULL;
    }

    lctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    if (lctx == NULL) {
        *err_len = ngx_snprintf(err_buf, *err_len, "no request ctx found") - err_buf;
        return NULL;
    }

    rc = ngx_http_lua_ffi_check_context(lctx, NGX_HTTP_LUA_CONTEXT_YIELDABLE,
                                        err_buf, err_len);
    if (rc != NGX_OK) {
        return NULL;
    }

    if (!ngx_queue_empty(&ctx->free)) {
        ngx_queue_t         *q;

        q = ngx_queue_last(&ctx->free);
        ngx_queue_remove(q);
        sctx = ngx_queue_data(q, ngx_http_grpc_cli_stream_ctx_t, queue);
        ngx_memzero(sctx, sizeof(ngx_http_grpc_cli_stream_ctx_t));

    } else {
        sctx = ngx_calloc(sizeof(ngx_http_grpc_cli_stream_ctx_t), r->connection->log);
        if (sctx == NULL) {
            *err_len = ngx_snprintf(err_buf, *err_len, "no memory") - err_buf;
            return NULL;
        }
    }

    sctx->r = r;
    sctx->parent = ctx;
    sctx->type = stream_type;

    rc = ngx_http_grpc_cli_kick_off_task(gccf, r, lctx, sctx, call_opt);

    if (rc != NGX_OK) {
        ngx_queue_insert_head(&ctx->free, &sctx->queue);

        if (rc == NGX_ERROR) {
            *err_len = ngx_snprintf(err_buf, *err_len, "task post failed") - err_buf;
        } else {
            *err_len = ngx_snprintf(err_buf, *err_len, "no memory") - err_buf;
        }

        return NULL;
    }

    engine_ctx = ctx->engine_ctx;

    if (stream_type != NGX_HTTP_GRPC_CLIENT_STREAM_TYPE_UNARY) {
        grpc_engine_new_stream(err_buf, err_len, (uint64_t) sctx, engine_ctx,
                               method_data, method_len,
                               req_data, req_len, call_opt, stream_type);
        ngx_queue_insert_head(&ctx->opening, &sctx->queue);

    } else {
        grpc_engine_call(err_buf, err_len, (uint64_t) sctx, engine_ctx,
                         method_data, method_len,
                         req_data, req_len, call_opt);
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "yield gRPC ctx:%p, timeout:%M",
                   ctx, call_opt->timeout);

    return sctx;
}


int
ngx_http_grpc_cli_call(unsigned char *err_buf, size_t *err_len,
                       ngx_http_request_t *r, ngx_http_grpc_cli_ctx_t *ctx,
                       const char *method_data, int method_len,
                       const char *req_data, int req_len,
                       ngx_http_grpc_cli_call_opt_t *call_opt)
{
    ngx_http_grpc_cli_stream_ctx_t *sctx;

    sctx = ngx_http_grpc_cli_new_stream(err_buf, err_len, r, ctx,
                                        method_data, method_len, req_data, req_len,
                                        call_opt, NGX_HTTP_GRPC_CLIENT_STREAM_TYPE_UNARY);
    if (sctx == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


void
ngx_http_grpc_cli_close_stream(ngx_http_grpc_cli_stream_ctx_t *sctx, ngx_http_request_t *r)
{
    int                      gc;
    ngx_log_t               *log;
    ngx_http_grpc_cli_ctx_t *ctx;

    if (r != NULL) {
        log = r->connection->log;
        gc = 0;
    } else {
        log = ngx_cycle->log;
        gc = 1;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, log, 0,
                   "close gRPC stream, gc:%d, ctx:%p", gc, sctx);

    grpc_engine_close_stream(sctx);

    if (!gc) {
        return;
    }

    ctx = sctx->parent;
    ngx_queue_remove(&sctx->queue);
    ngx_queue_insert_head(&ctx->free, &sctx->queue);
    sctx->r = NULL;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, log, 0, "free gRPC stream ctx:%p, gRPC ctx:%p",
                   sctx, ctx);

    if (ctx->free_delayed && ngx_queue_empty(&ctx->opening)) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0, "continue delayed free of gRPC ctx:%p", ctx);

        ngx_http_grpc_cli_free_conn(ctx, log);
    }
}


int
ngx_http_grpc_cli_stream_recv(unsigned char *err_buf, size_t *err_len,
                              ngx_http_request_t *r, ngx_http_grpc_cli_stream_ctx_t *sctx,
                              ngx_http_grpc_cli_call_opt_t *call_opt)
{
    ngx_int_t                       rc;
    ngx_msec_t                      timeout;
    ngx_http_lua_ctx_t             *lctx;
    ngx_http_grpc_cli_main_conf_t  *gccf;

    gccf = ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_grpc_client_module);

    if (sctx->r == NULL) {
        *err_len = ngx_snprintf(err_buf, *err_len, "closed") - err_buf;
        return NGX_ERROR;
    }

    if (sctx->r != r) {
        *err_len = ngx_snprintf(err_buf, *err_len, "bad request") - err_buf;
        return NGX_ERROR;
    }

    if (sctx->wait_co_ctx != NULL) {
        *err_len = ngx_snprintf(err_buf, *err_len, "busy waiting") - err_buf;
        return NGX_ERROR;
    }

    lctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    if (lctx == NULL) {
        *err_len = ngx_snprintf(err_buf, *err_len, "no request ctx found") - err_buf;
        return NGX_ERROR;
    }

    rc = ngx_http_lua_ffi_check_context(lctx, NGX_HTTP_LUA_CONTEXT_YIELDABLE,
                                        err_buf, err_len);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    timeout = call_opt->timeout;

    rc = ngx_http_grpc_cli_kick_off_task(gccf, r, lctx, sctx, call_opt);

    if (rc == NGX_ERROR) {
        *err_len = ngx_snprintf(err_buf, *err_len, "task post failed") - err_buf;
        return NGX_ERROR;
    }

    if (rc == NGX_DECLINED) {
        *err_len = ngx_snprintf(err_buf, *err_len, "no memory") - err_buf;
        return NGX_ERROR;
    }

    grpc_engine_stream_recv(sctx);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "yield gRPC stream:%p, timeout:%M",
                   sctx, timeout);

    return NGX_OK;
}


int
ngx_http_grpc_cli_stream_send(unsigned char *err_buf, size_t *err_len,
                              ngx_http_request_t *r, ngx_http_grpc_cli_stream_ctx_t *sctx,
                              ngx_http_grpc_cli_call_opt_t *call_opt,
                              const char *req_data, int req_len)
{
    ngx_int_t                       rc;
    ngx_msec_t                      timeout;
    ngx_http_lua_ctx_t             *lctx;
    ngx_http_grpc_cli_main_conf_t  *gccf;

    gccf = ngx_http_cycle_get_module_main_conf(ngx_cycle, ngx_http_grpc_client_module);

    if (sctx->r == NULL) {
        *err_len = ngx_snprintf(err_buf, *err_len, "closed") - err_buf;
        return NGX_ERROR;
    }

    if (sctx->r != r) {
        *err_len = ngx_snprintf(err_buf, *err_len, "bad request") - err_buf;
        return NGX_ERROR;
    }

    if (sctx->wait_co_ctx != NULL) {
        *err_len = ngx_snprintf(err_buf, *err_len, "busy waiting") - err_buf;
        return NGX_ERROR;
    }

    lctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    if (lctx == NULL) {
        *err_len = ngx_snprintf(err_buf, *err_len, "no request ctx found") - err_buf;
        return NGX_ERROR;
    }

    rc = ngx_http_lua_ffi_check_context(lctx, NGX_HTTP_LUA_CONTEXT_YIELDABLE,
                                        err_buf, err_len);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    rc = ngx_http_grpc_cli_kick_off_task(gccf, r, lctx, sctx, call_opt);

    if (rc == NGX_ERROR) {
        *err_len = ngx_snprintf(err_buf, *err_len, "task post failed") - err_buf;
        return NGX_ERROR;
    }

    if (rc == NGX_DECLINED) {
        *err_len = ngx_snprintf(err_buf, *err_len, "no memory") - err_buf;
        return NGX_ERROR;
    }

    timeout = call_opt->timeout;

    grpc_engine_stream_send(sctx, req_data, req_len);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "yield gRPC stream:%p, timeout:%M",
                   sctx, timeout);

    return NGX_OK;
}
