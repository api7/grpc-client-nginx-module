/*
 * Copyright 2022 Shenzhen ZhiLiu Technology Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_stream.h>
#include <ngx_http_lua_util.h>
#include <ngx_stream_lua_util.h>


#ifndef NGX_GRPC_CLI_ENGINE_PATH
#define NGX_GRPC_CLI_ENGINE_PATH ""
#endif
#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)

#define NGX_GRPC_CLIENT_STATE_OK         0
#define NGX_GRPC_CLIENT_STATE_TIMEOUT    1

#define NGX_GRPC_CLIENT_RES_TYPE_OK             0
#define NGX_GRPC_CLIENT_RES_TYPE_ERR            1
#define NGX_GRPC_CLIENT_RES_TYPE_OK_NOT_RES     2

#define NGX_GRPC_CLIENT_STREAM_TYPE_UNARY                  0
#define NGX_GRPC_CLIENT_STREAM_TYPE_CLIENT_STREAM          1
#define NGX_GRPC_CLIENT_STREAM_TYPE_SERVER_STREAM          2
#define NGX_GRPC_CLIENT_STREAM_TYPE_BIDIRECTIONAL_STREAM   3

typedef struct {
    ngx_str_t            engine_path;
    void                *engine;
    ngx_thread_pool_t   *thread_pool;
    ngx_thread_task_t   *task;
} ngx_grpc_client_conf_t;


typedef struct {
    uint64_t        task_id;
    uint64_t        size;
    /*
     * We require this Nginx module runs on 64bit aligned machine, like x64 and arm64.
     * So that we can store the result types in the unused 7 bitfields
     * */
    u_char         *buf;
} ngx_grpc_cli_task_res_t;


typedef struct {
    void                         *engine_ctx;

    ngx_queue_t              opening;
    ngx_queue_t              free;

    unsigned                 free_delayed:1;
} ngx_grpc_cli_ctx_t;


typedef struct {
    void                         *r;
    void                         *wait_co_ctx;

    void                         *prev_data;

    ngx_grpc_cli_task_res_t       res;
    int                           state;

    ngx_rbtree_node_t            *node;

    ngx_queue_t                   queue;
    ngx_grpc_cli_ctx_t           *parent;
    int                           type;

    unsigned                      is_http:1;
} ngx_grpc_cli_stream_ctx_t;


typedef struct {
    ngx_queue_t                   queue;
    ngx_grpc_cli_task_res_t       res;
} ngx_grpc_cli_posted_event_ctx_t;


typedef struct {
    ngx_thread_task_t       *task;
    ngx_thread_pool_t       *thread_pool;

    ngx_grpc_cli_task_res_t *finished_tasks;
    int                      finished_task_num;

    ngx_event_t             *posted_ev;
    ngx_queue_t              occupied;
    ngx_queue_t              free;
} ngx_grpc_cli_thread_ctx_t;


typedef struct {
    ngx_msec_t timeout;
} ngx_grpc_cli_call_opt_t;


#define must_resolve_symbol(hd, f) \
    f = dlsym(hd, #f); \
    err = dlerror(); \
    if (err != NULL) { \
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, \
                      "failed to resolve symbol: %s", err); \
        return NGX_ERROR; \
    }


#define ngx_cycle_get_main_conf(mod_name, cycle)                              \
    ((mod_name ## _conf_t *) ngx_get_conf(cycle->conf_ctx,                    \
                                          mod_name ## _module))


extern ngx_module_t  ngx_grpc_client_module;

static ngx_int_t ngx_grpc_cli_init_worker(ngx_cycle_t *cycle);
static void ngx_grpc_cli_exit_worker(ngx_cycle_t *cycle);

static void *ngx_grpc_cli_create_main_conf(ngx_cycle_t *cycle);

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
static void (*grpc_engine_stream_close_send) (void *);
static void (*grpc_engine_close) (void *);
static void (*grpc_engine_close_stream) (void *);
static void (*grpc_engine_free) (void *);
static void *(*grpc_engine_wait) (int *, int);


static ngx_rbtree_t       ngx_grpc_cli_ongoing_tasks;
static ngx_rbtree_node_t  ngx_grpc_cli_ongoing_task_sentinel;
static int                ngx_grpc_cli_ongoing_tasks_num;

static ngx_str_t thread_pool_name = ngx_string("grpc-client-nginx-module");
static ngx_str_t engine_path = ngx_string(STRINGIFY(NGX_GRPC_CLI_ENGINE_PATH));

static ngx_core_module_t ngx_grpc_cli_module_ctx = {
    ngx_string("grpc_client"),
    ngx_grpc_cli_create_main_conf,
    NULL,
};


ngx_module_t ngx_grpc_client_module = {
    NGX_MODULE_V1,
    &ngx_grpc_cli_module_ctx,       /* module context */
    NULL,                                /* module directives */
    NGX_CORE_MODULE,                     /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    ngx_grpc_cli_init_worker,       /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    NULL,                                /* exit process */
    ngx_grpc_cli_exit_worker,       /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_grpc_cli_create_main_conf(ngx_cycle_t *cycle)
{
    ngx_grpc_client_conf_t *conf;

    conf = ngx_pcalloc(cycle->pool, sizeof(ngx_grpc_client_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->engine_path = engine_path;

    return conf;
}


static void
ngx_grpc_cli_thread_handler(void *data, ngx_log_t *log)
{
    ngx_grpc_cli_thread_ctx_t     *thctx = data;
    ngx_int_t                     i;

    thctx->finished_tasks = grpc_engine_wait(&thctx->finished_task_num, 100);
    if (ngx_exiting || ngx_terminate) {
        for (i = 0; i < thctx->finished_task_num; i++) {
            ngx_grpc_cli_task_res_t *res = &thctx->finished_tasks[i];

            assert(res->buf != NULL);
            res->buf = (u_char *) ((int64_t) (res->buf) >> 3 << 3);
            grpc_engine_free(res->buf);
        }

        if (thctx->finished_tasks != NULL) {
            grpc_engine_free(thctx->finished_tasks);
        }
    }
}


static void
ngx_grpc_cli_insert_posted_event_ctx(ngx_grpc_cli_thread_ctx_t *thctx,
                                     ngx_log_t *log, ngx_grpc_cli_task_res_t *res)
{
    ngx_grpc_cli_posted_event_ctx_t        *ctx;

    if (!ngx_queue_empty(&thctx->free)) {
        ngx_queue_t         *q;

        q = ngx_queue_head(&thctx->free);
        ngx_queue_remove(q);
        ctx = ngx_queue_data(q, ngx_grpc_cli_posted_event_ctx_t, queue);

    } else {
        ctx = ngx_pcalloc(ngx_cycle->pool, sizeof(ngx_grpc_cli_posted_event_ctx_t));
        if (ctx == NULL) {
            ngx_log_error(NGX_LOG_ERR, log, 0, "no memory");
            return;
        }
    }

    ctx->res = *res;
    ngx_queue_insert_tail(&thctx->occupied, &ctx->queue);

    ngx_log_debug2(NGX_LOG_DEBUG_CORE, log, 0, "post finished task %uL, ctx:%p",
                   res->task_id, ctx);
}


static ngx_rbtree_node_t *
ngx_grpc_cli_lookup_task(ngx_rbtree_key_t key)
{
    ngx_rbtree_node_t    *node, *sentinel;

    node = ngx_grpc_cli_ongoing_tasks.root;
    sentinel = ngx_grpc_cli_ongoing_tasks.sentinel;

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
ngx_grpc_cli_task_num(void)
{
    return ngx_grpc_cli_ongoing_tasks_num;
}


static ngx_int_t
ngx_grpc_cli_keep_task(ngx_grpc_cli_stream_ctx_t *ctx, ngx_log_t *log)
{
    ngx_rbtree_node_t     *node;

    node = ngx_alloc(sizeof(ngx_rbtree_node_t), log);
    if (node == NULL) {
        return NGX_ERROR;
    }

    node->key = (ngx_rbtree_key_t) ctx;
    ngx_grpc_cli_ongoing_tasks_num++;

    ngx_log_debug2(NGX_LOG_DEBUG_CORE, log, 0, "increase ongoing tasks to %d, stream: %p",
                   ngx_grpc_cli_ongoing_tasks_num, ctx);

    ngx_rbtree_insert(&ngx_grpc_cli_ongoing_tasks, node);
    ctx->node = node;

    return NGX_OK;
}


static void
ngx_grpc_cli_unkeep_task(ngx_grpc_cli_stream_ctx_t *ctx)
{
    if (ctx->node == NULL) {
        return;
    }

    ngx_grpc_cli_ongoing_tasks_num--;

    ngx_log_debug2(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                   "decrease ongoing tasks to %d, stream: %p",
                   ngx_grpc_cli_ongoing_tasks_num, ctx);

    ngx_rbtree_delete(&ngx_grpc_cli_ongoing_tasks, ctx->node);
    ngx_free(ctx->node);
    ctx->node = NULL;
}


static void
ngx_grpc_cli_thread_event_handler(ngx_event_t *ev)
{
    int                               i;
    ngx_grpc_cli_thread_ctx_t        *thctx;
    ngx_thread_pool_t                *thread_pool;
    ngx_thread_task_t                *task;

    thctx = ev->data;

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0, "post %d finished task",
                   thctx->finished_task_num);

    for (i = 0; i < thctx->finished_task_num; i++) {
        ngx_grpc_cli_task_res_t *res = &thctx->finished_tasks[i];

        ngx_grpc_cli_insert_posted_event_ctx(thctx, ev->log, res);
    }

    if (thctx->finished_task_num > 0) {
        grpc_engine_free(thctx->finished_tasks);
        thctx->finished_tasks = NULL;
        ngx_post_event(thctx->posted_ev, &ngx_posted_events);
    }

    if (ngx_quit || ngx_exiting) {
        return;
    }

    if (ngx_grpc_cli_task_num() <= thctx->finished_task_num) {
        return;
    }

    /* still have tasks to wait */
    ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0, "post grpc client thread to wait %d tasks",
                   ngx_grpc_cli_task_num() - thctx->finished_task_num);

    thread_pool = thctx->thread_pool;
    task = thctx->task;

    if (ngx_thread_task_post(thread_pool, task) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, ev->log, 0,
                      "failed to wait gRPC engine: task post failed");
        return;
    }
}


static void
ngx_grpc_cli_put_res(ngx_grpc_cli_stream_ctx_t *sctx, lua_State *co)
{
    ngx_grpc_cli_task_res_t         *res;

    res = &sctx->res;

    if (sctx->state == NGX_GRPC_CLIENT_STATE_TIMEOUT) {
        lua_pushboolean(co, 0);
        lua_pushliteral(co, "timeout");

    } else {
        int type;

        assert(res->buf != NULL);
        type = (int64_t) (res->buf) & 7;
        res->buf = (u_char *) ((int64_t) (res->buf) >> 3 << 3);

        if (type == NGX_GRPC_CLIENT_RES_TYPE_OK) {
            lua_pushboolean(co, 1);
            lua_pushlstring(co, (const char *) res->buf, res->size);

        } else if (type == NGX_GRPC_CLIENT_RES_TYPE_OK_NOT_RES) {
            lua_pushboolean(co, 1);
            lua_pushnil(co);

        } else {
            lua_pushboolean(co, 0);
            lua_pushlstring(co, (const char *) res->buf, res->size);
        }

        grpc_engine_free(res->buf);
    }

    if (sctx->type == NGX_GRPC_CLIENT_STREAM_TYPE_UNARY) {
        ngx_grpc_cli_ctx_t              *ctx;

        /* reuse the sctx only when it is an unary stream */
        ctx = sctx->parent;
        ngx_queue_insert_head(&ctx->free, &sctx->queue);
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
    ngx_grpc_cli_stream_ctx_t            *sctx;

    lctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    if (lctx == NULL) {
        return NGX_ERROR;
    }

    sctx = lctx->cur_co_ctx->data;
    lctx->cur_co_ctx->data = sctx->prev_data;

    lctx->resume_handler = ngx_http_lua_wev_handler;

    c = r->connection;
    vm = ngx_http_lua_get_lua_vm(r, lctx);
    nreqs = c->requests;
    nret = 2;

    ngx_grpc_cli_put_res(sctx, lctx->cur_co_ctx->co);

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


static ngx_int_t
ngx_stream_grpc_cli_resume(ngx_stream_lua_request_t *r)
{
    lua_State                            *vm;
    ngx_connection_t                     *c;
    ngx_int_t                             rc;
    ngx_uint_t                            nreqs, nret;
    ngx_stream_lua_ctx_t                 *lctx;
    ngx_grpc_cli_stream_ctx_t            *sctx;

    lctx = ngx_stream_lua_get_module_ctx(r, ngx_stream_lua_module);
    if (lctx == NULL) {
        return NGX_ERROR;
    }

    sctx = lctx->cur_co_ctx->data;
    lctx->cur_co_ctx->data = sctx->prev_data;

    lctx->resume_handler = ngx_stream_lua_wev_handler;

    c = r->connection;
    vm = ngx_stream_lua_get_lua_vm(r, lctx);
    nreqs = c->requests;
    nret = 2;

    ngx_grpc_cli_put_res(sctx, lctx->cur_co_ctx->co);

    rc = ngx_stream_lua_run_thread(vm, r, lctx, nret);

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, r->connection->log, 0,
                   "lua run thread returned %d", rc);

    if (rc == NGX_AGAIN) {
        return ngx_stream_lua_run_posted_threads(c, vm, r, lctx, nreqs);
    }

    if (rc == NGX_DONE) {
        ngx_stream_lua_finalize_request(r, NGX_DONE);
        return ngx_stream_lua_run_posted_threads(c, vm, r, lctx, nreqs);
    }

    /* rc == NGX_ERROR || rc >= NGX_OK */

    if (lctx->entered_content_phase) {
        ngx_stream_lua_finalize_request(r, rc);
        return NGX_DONE;
    }

    return rc;
}


static void
ngx_grpc_cli_to_resume(ngx_grpc_cli_stream_ctx_t *ctx)
{
    ngx_connection_t                     *c;

    ngx_grpc_cli_unkeep_task(ctx);

    if (ctx->is_http) {
        ngx_http_request_t                   *r;
        ngx_http_lua_ctx_t                   *lctx;

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

    } else {
        ngx_stream_lua_request_t               *r;
        ngx_stream_lua_ctx_t                   *lctx;

        r = ctx->r;

        lctx = ngx_stream_lua_get_module_ctx(r, ngx_stream_lua_module);
        ngx_stream_lua_assert(lctx != NULL);

        lctx->cur_co_ctx = ctx->wait_co_ctx;
        ctx->wait_co_ctx = NULL;
        ctx->prev_data = lctx->cur_co_ctx->data;
        lctx->cur_co_ctx->data = ctx;

        if (lctx->entered_content_phase) {
            (void) ngx_stream_grpc_cli_resume(r);

        } else {
            lctx->resume_handler = ngx_stream_grpc_cli_resume;
            ngx_stream_lua_core_run_phases(r);
        }
    }
}


static void
ngx_grpc_cli_thread_post_event_handler(ngx_event_t *ev)
{
    ngx_grpc_cli_thread_ctx_t       *thctx = ev->data;
    ngx_queue_t                     *q;
    ngx_grpc_cli_stream_ctx_t       *ctx;
    ngx_grpc_cli_posted_event_ctx_t *posted_event_ctx;
    ngx_grpc_cli_task_res_t         *res;

    while (!ngx_queue_empty(&thctx->occupied)) {
        q = ngx_queue_head(&thctx->occupied);
        ngx_queue_remove(q);
        ngx_queue_insert_tail(&thctx->free, q);
        posted_event_ctx = ngx_queue_data(q, ngx_grpc_cli_posted_event_ctx_t, queue);

        res = &posted_event_ctx->res;

        /* remember to free the res.buf if the task is cancelled */
        if (ngx_grpc_cli_lookup_task(res->task_id) == NULL) {
            ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,
                           "finished task %uL is cancelled",
                           res->task_id);

            res->buf = (u_char *) ((int64_t) (res->buf) >> 3 << 3);
            grpc_engine_free(res->buf);
            continue;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, ev->log, 0, "resume finished task %uL, ctx:%p",
                       res->task_id, posted_event_ctx);

        ctx = (ngx_grpc_cli_stream_ctx_t *) res->task_id;
        ctx->res = *res;

        if (ctx->is_http) {
            ngx_http_lua_co_ctx_t *wait_co_ctx = ctx->wait_co_ctx;

            if (wait_co_ctx->sleep.timer_set) {
                ngx_del_timer(&wait_co_ctx->sleep);
            }

            wait_co_ctx->cleanup = NULL;

        } else {
            ngx_stream_lua_co_ctx_t *wait_co_ctx = ctx->wait_co_ctx;

            if (wait_co_ctx->sleep.timer_set) {
                ngx_del_timer(&wait_co_ctx->sleep);
            }

            wait_co_ctx->cleanup = NULL;

        }

        ngx_grpc_cli_to_resume(ctx);
    }
}


static void
ngx_grpc_cli_timeout_handler(ngx_event_t *ev)
{
    ngx_grpc_cli_stream_ctx_t     *ctx;

    ctx = ev->data;
    ctx->state = NGX_GRPC_CLIENT_STATE_TIMEOUT;

    if (ctx->is_http) {
        ngx_http_lua_co_ctx_t         *wait_co_ctx;

        wait_co_ctx = ctx->wait_co_ctx;
        wait_co_ctx->cleanup = NULL;
    } else {
        ngx_stream_lua_co_ctx_t         *wait_co_ctx;

        wait_co_ctx = ctx->wait_co_ctx;
        wait_co_ctx->cleanup = NULL;
    }


    ngx_log_debug2(NGX_LOG_DEBUG_CORE, ev->log, 0, "resume timeout task %uL, event:%p",
                   ctx, ev);

    ngx_grpc_cli_to_resume(ctx);
}


static void
ngx_http_grpc_cli_cleanup(void *data)
{
    ngx_http_lua_co_ctx_t         *wait_co_ctx = data;
    ngx_grpc_cli_stream_ctx_t     *ctx;

    ctx = wait_co_ctx->data;

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, "cleanup aborted task %uL", ctx);

    if (wait_co_ctx->sleep.timer_set) {
        ngx_del_timer(&wait_co_ctx->sleep);
    }

    ngx_grpc_cli_unkeep_task(ctx);
}


static void
ngx_stream_grpc_cli_cleanup(void *data)
{
    ngx_stream_lua_co_ctx_t         *wait_co_ctx = data;
    ngx_grpc_cli_stream_ctx_t       *ctx;

    ctx = wait_co_ctx->data;

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, "cleanup aborted task %uL", ctx);

    if (wait_co_ctx->sleep.timer_set) {
        ngx_del_timer(&wait_co_ctx->sleep);
    }

    ngx_grpc_cli_unkeep_task(ctx);
}


static ngx_int_t
ngx_grpc_cli_thread(ngx_grpc_client_conf_t *gccf, ngx_cycle_t *cycle)
{
    ngx_thread_pool_t                  *thread_pool;
    ngx_thread_task_t                  *task;
    ngx_grpc_cli_thread_ctx_t          *thctx;
    ngx_event_t                        *posted_ev;

    thread_pool = ngx_thread_pool_get(cycle, &thread_pool_name);

    task = ngx_thread_task_alloc(cycle->pool,
                                 sizeof(ngx_grpc_cli_thread_ctx_t));
    if (task == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "failed to init engine: no memory");
        return NGX_ERROR;
    }

    posted_ev = ngx_pcalloc(cycle->pool, sizeof(ngx_event_t));
    if (posted_ev == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "failed to init engine: no memory");
        return NGX_ERROR;
    }

    thctx = task->ctx;
    thctx->task = task;
    thctx->thread_pool = thread_pool;
    thctx->posted_ev = posted_ev;
    thctx->posted_ev->handler = ngx_grpc_cli_thread_post_event_handler;
    thctx->posted_ev->data = thctx;
    thctx->posted_ev->log = cycle->log;
    ngx_queue_init(&thctx->free);
    ngx_queue_init(&thctx->occupied);

    task->handler = ngx_grpc_cli_thread_handler;
    task->event.handler = ngx_grpc_cli_thread_event_handler;
    task->event.data = thctx;
    task->event.log = cycle->log;

    gccf->thread_pool = thread_pool;
    gccf->task = task;

    return NGX_OK;
}


static ngx_int_t
ngx_grpc_cli_init_worker(ngx_cycle_t *cycle)
{
    char                          *err;
    ngx_grpc_client_conf_t        *gccf;

    gccf = ngx_cycle_get_main_conf(ngx_grpc_client, cycle);
    if (gccf == NULL) {
        /* only stream subsys is available */
        return NGX_OK;
    }

    if (gccf->engine_path.len == 0) {
        return NGX_OK;
    }

    if (ngx_thread_pool_get(cycle, &thread_pool_name) == NULL) {
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
    must_resolve_symbol(gccf->engine, grpc_engine_stream_close_send);
    must_resolve_symbol(gccf->engine, grpc_engine_wait);

    ngx_grpc_cli_ongoing_tasks_num = 0;
    ngx_rbtree_init(&ngx_grpc_cli_ongoing_tasks,
                    &ngx_grpc_cli_ongoing_task_sentinel, ngx_rbtree_insert_value);

    return ngx_grpc_cli_thread(gccf, cycle);
}


static void
ngx_grpc_cli_exit_worker(ngx_cycle_t *cycle)
{
    ngx_grpc_client_conf_t *gccf;

    gccf = ngx_cycle_get_main_conf(ngx_grpc_client, cycle);
    if (gccf == NULL) {
        /* only stream subsys is available */
        return;
    }

    if (gccf->engine != NULL) {
        dlclose(gccf->engine);
    }
}


int
ngx_grpc_cli_is_engine_inited(void)
{
    ngx_grpc_client_conf_t *gccf;

    gccf = ngx_cycle_get_main_conf(ngx_grpc_client, ngx_cycle);
    return gccf->engine != NULL;
}


static ngx_log_t *
ngx_grpc_cli_get_req_logger(void *r, bool is_http)
{
    if (is_http) {
        return ((ngx_http_request_t *) r)->connection->log;
    } else {
        return ((ngx_stream_lua_request_t *) r)->connection->log;
    }
}


void *
ngx_grpc_cli_connect(unsigned char *err_buf, size_t *err_len,
                     void *r, bool is_http,
                     const char *target_data, int target_len,
                     void *dial_opt)
{
    void                    *engine_ctx;
    ngx_grpc_cli_ctx_t      *ctx;
    ngx_log_t               *log;

    log = ngx_grpc_cli_get_req_logger(r, is_http);
    ctx = ngx_calloc(sizeof(ngx_grpc_cli_ctx_t), log);
    if (ctx == NULL) {
        return NULL;
    }

    ngx_queue_init(&ctx->free);
    ngx_queue_init(&ctx->opening);

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0,
                   "new gRPC ctx: %p", ctx);

    engine_ctx = grpc_engine_connect(err_buf, err_len, target_data, target_len, dial_opt);
    if (engine_ctx == NULL) {
        goto free_ctx;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0,
                   "create gRPC connection: %p", engine_ctx);

    ctx->engine_ctx = engine_ctx;
    return ctx;

free_ctx:
    ngx_free(ctx);
    return NULL;
}


static void
ngx_grpc_cli_free_conn(ngx_grpc_cli_ctx_t *ctx, ngx_log_t *log)
{
    while (!ngx_queue_empty(&ctx->free)) {
        ngx_queue_t                    *q;
        ngx_grpc_cli_stream_ctx_t *sctx;

        q = ngx_queue_last(&ctx->free);
        ngx_queue_remove(q);
        sctx = ngx_queue_data(q, ngx_grpc_cli_stream_ctx_t, queue);
        ngx_free(sctx);
    }

    ngx_free(ctx);
}


void
ngx_grpc_cli_close(ngx_grpc_cli_ctx_t *ctx, void *r, bool is_http)
{
    int                  gc;
    ngx_log_t           *log;
    void                *engine_ctx;

    if (r != NULL) {
        log = ngx_grpc_cli_get_req_logger(r, is_http);
        gc = 0;

    } else {
        log = ngx_cycle->log;
        gc = 1;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_CORE, log, 0,
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

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0, "free gRPC ctx: %p", ctx);

    if (!ngx_queue_empty(&ctx->opening)) {
        ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0, "delay free of gRPC ctx:%p", ctx);

        ctx->free_delayed = 1;
        return;
    }

    ngx_grpc_cli_free_conn(ctx, log);
}


static int
ngx_grpc_cli_schedule_task_thread_if_needed(ngx_grpc_client_conf_t  *gccf)
{
    if (!gccf->task->event.active) {
        ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, "post grpc client thread");

        /* kick off background thread for the first task */
        if (ngx_thread_task_post(gccf->thread_pool, gccf->task) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_grpc_cli_kick_off_task(unsigned char *err_buf, size_t *err_len,
                           void *r,
                           ngx_grpc_client_conf_t *gccf, ngx_grpc_cli_stream_ctx_t *sctx,
                           ngx_grpc_cli_call_opt_t *call_opt)
{
    ngx_int_t                       rc;
    ngx_log_t                      *log;
    ngx_http_lua_ctx_t             *hlctx = NULL;
    ngx_stream_lua_ctx_t           *slctx = NULL;
    ngx_http_request_t             *hr;
    ngx_stream_lua_request_t       *sr;

    if (sctx->is_http) {
        hr = r;
        log = hr->connection->log;

        hlctx = ngx_http_get_module_ctx(hr, ngx_http_lua_module);
        if (hlctx == NULL) {
            *err_len = ngx_snprintf(err_buf, *err_len, "no request ctx found") - err_buf;
            return NGX_ERROR;
        }

        rc = ngx_http_lua_ffi_check_context(hlctx, NGX_HTTP_LUA_CONTEXT_YIELDABLE,
                                            err_buf, err_len);
        if (rc != NGX_OK) {
            return NGX_ERROR;
        }

    } else {
        sr = r;
        log = sr->connection->log;

        slctx = ngx_stream_lua_get_module_ctx(sr, ngx_stream_lua_module);
        if (slctx == NULL) {
            *err_len = ngx_snprintf(err_buf, *err_len, "no request ctx found") - err_buf;
            return NGX_ERROR;
        }

        rc = ngx_stream_lua_ffi_check_context(slctx, NGX_STREAM_LUA_CONTEXT_PREREAD
                                                | NGX_STREAM_LUA_CONTEXT_CONTENT
                                                | NGX_STREAM_LUA_CONTEXT_TIMER
                                                | NGX_STREAM_LUA_CONTEXT_SSL_CLIENT_HELLO
                                                | NGX_STREAM_LUA_CONTEXT_SSL_CERT,
                                              err_buf, err_len);
        if (rc != NGX_OK) {
            return NGX_ERROR;
        }
    }

    if (ngx_grpc_cli_schedule_task_thread_if_needed(gccf) != NGX_OK) {
        *err_len = ngx_snprintf(err_buf, *err_len, "task post failed") - err_buf;
        return NGX_ERROR;
    }

    if (ngx_grpc_cli_keep_task(sctx, log) != NGX_OK) {
        *err_len = ngx_snprintf(err_buf, *err_len, "no memory") - err_buf;
        return NGX_ERROR;
    }

    sctx->state = NGX_GRPC_CLIENT_STATE_OK;

    if (sctx->is_http) {
        ngx_http_lua_co_ctx_t          *wait_co_ctx;
        ngx_http_lua_ctx_t             *lctx = hlctx;

        wait_co_ctx = lctx->cur_co_ctx;
        sctx->wait_co_ctx = wait_co_ctx;

        wait_co_ctx->data = sctx;
        wait_co_ctx->sleep.handler = ngx_grpc_cli_timeout_handler;
        wait_co_ctx->sleep.data = sctx;
        wait_co_ctx->sleep.log = log;
        ngx_add_timer(&wait_co_ctx->sleep, call_opt->timeout);

        wait_co_ctx->cleanup = ngx_http_grpc_cli_cleanup;

    } else {
        ngx_stream_lua_co_ctx_t          *wait_co_ctx;
        ngx_stream_lua_ctx_t             *lctx = slctx;

        wait_co_ctx = lctx->cur_co_ctx;
        sctx->wait_co_ctx = wait_co_ctx;

        wait_co_ctx->data = sctx;
        wait_co_ctx->sleep.handler = ngx_grpc_cli_timeout_handler;
        wait_co_ctx->sleep.data = sctx;
        wait_co_ctx->sleep.log = log;
        ngx_add_timer(&wait_co_ctx->sleep, call_opt->timeout);

        wait_co_ctx->cleanup = ngx_stream_grpc_cli_cleanup;
    }

    return NGX_OK;
}


void *
ngx_grpc_cli_new_stream(unsigned char *err_buf, size_t *err_len,
                        void *r, bool is_http,
                        ngx_grpc_cli_ctx_t *ctx,
                        const char *method_data, int method_len,
                        const char *req_data, int req_len,
                        ngx_grpc_cli_call_opt_t *call_opt, int stream_type)
{
    ngx_int_t                       rc;
    ngx_log_t                      *log;
    void                           *engine_ctx;

    ngx_grpc_client_conf_t  *gccf;
    ngx_grpc_cli_stream_ctx_t *sctx;

    gccf = ngx_cycle_get_main_conf(ngx_grpc_client, ngx_cycle);
    log = ngx_grpc_cli_get_req_logger(r, is_http);

    if (ctx->engine_ctx == NULL) {
        *err_len = ngx_snprintf(err_buf, *err_len, "closed") - err_buf;
        return NULL;
    }

    if (!ngx_queue_empty(&ctx->free)) {
        ngx_queue_t         *q;

        q = ngx_queue_last(&ctx->free);
        ngx_queue_remove(q);
        sctx = ngx_queue_data(q, ngx_grpc_cli_stream_ctx_t, queue);
        ngx_memzero(sctx, sizeof(ngx_grpc_cli_stream_ctx_t));

    } else {
        sctx = ngx_calloc(sizeof(ngx_grpc_cli_stream_ctx_t), ngx_cycle->log);
        if (sctx == NULL) {
            *err_len = ngx_snprintf(err_buf, *err_len, "no memory") - err_buf;
            return NULL;
        }
    }

    sctx->r = r;
    sctx->is_http = is_http ? 1 : 0;
    sctx->parent = ctx;
    sctx->type = stream_type;

    rc = ngx_grpc_cli_kick_off_task(err_buf, err_len, r, gccf, sctx, call_opt);
    if (rc != NGX_OK) {
        ngx_queue_insert_head(&ctx->free, &sctx->queue);
        return NULL;
    }

    engine_ctx = ctx->engine_ctx;

    if (stream_type != NGX_GRPC_CLIENT_STREAM_TYPE_UNARY) {
        grpc_engine_new_stream(err_buf, err_len, (uint64_t) sctx, engine_ctx,
                               method_data, method_len,
                               req_data, req_len, call_opt, stream_type);
        ngx_queue_insert_head(&ctx->opening, &sctx->queue);

    } else {
        grpc_engine_call(err_buf, err_len, (uint64_t) sctx, engine_ctx,
                         method_data, method_len,
                         req_data, req_len, call_opt);
    }

    ngx_log_debug2(NGX_LOG_DEBUG_CORE, log, 0, "yield gRPC ctx:%p, timeout:%M",
                   ctx, call_opt->timeout);

    return sctx;
}


int
ngx_grpc_cli_call(unsigned char *err_buf, size_t *err_len,
                  void *r, bool is_http,
                  ngx_grpc_cli_ctx_t *ctx,
                  const char *method_data, int method_len,
                  const char *req_data, int req_len,
                  ngx_grpc_cli_call_opt_t *call_opt,
                  ngx_str_t *blocking_call_output)
{
    if (blocking_call_output != NULL) {
        ngx_grpc_cli_stream_ctx_t   sctx;
        void                       *engine_ctx;
        int                         n, type;
        ngx_grpc_cli_task_res_t    *n_res, *res;

        if (ctx->engine_ctx == NULL) {
            *err_len = ngx_snprintf(err_buf, *err_len, "closed") - err_buf;
            return NGX_ERROR;
        }

        engine_ctx = ctx->engine_ctx;

        grpc_engine_call(err_buf, err_len, (uint64_t) &sctx, engine_ctx,
                         method_data, method_len,
                         req_data, req_len, call_opt);

        n_res = grpc_engine_wait(&n, 600 * 1000);
        if (n == 0) {
            *err_len = ngx_snprintf(err_buf, *err_len, "timeout") - err_buf;
            return NGX_ERROR;
        }

        res = &n_res[0];
        assert(res->buf != NULL);
        type = (int64_t) (res->buf) & 7;
        res->buf = (u_char *) ((int64_t) (res->buf) >> 3 << 3);

        if (type == NGX_GRPC_CLIENT_RES_TYPE_OK) {
            blocking_call_output->data = res->buf;
            blocking_call_output->len = res->size;
            /* free the buf after converting it to Lua str in Lua land */

        } else {
            assert(type == NGX_GRPC_CLIENT_RES_TYPE_ERR);
            *err_len = ngx_snprintf(err_buf, *err_len, "%*s", res->size, res->buf) - err_buf;
            grpc_engine_free(res->buf);
        }

        grpc_engine_free(n_res);

        return type == NGX_GRPC_CLIENT_RES_TYPE_ERR ? NGX_ERROR : NGX_OK;
    }

    ngx_grpc_cli_stream_ctx_t *sctx;

    sctx = ngx_grpc_cli_new_stream(err_buf, err_len, r, is_http, ctx,
                                   method_data, method_len, req_data, req_len,
                                   call_opt, NGX_GRPC_CLIENT_STREAM_TYPE_UNARY);
    if (sctx == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


void
ngx_grpc_cli_close_stream(ngx_grpc_cli_stream_ctx_t *sctx, void *r, bool is_http)
{
    int                      gc;
    ngx_log_t               *log;
    ngx_grpc_cli_ctx_t      *ctx;

    if (r != NULL) {
        log = ngx_grpc_cli_get_req_logger(r, is_http);
        gc = 0;

    } else {
        log = ngx_cycle->log;
        gc = 1;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_CORE, log, 0,
                   "close gRPC stream, gc:%d, ctx:%p", gc, sctx);

    grpc_engine_close_stream(sctx);

    if (!gc) {
        return;
    }

    ctx = sctx->parent;
    ngx_queue_remove(&sctx->queue);
    ngx_queue_insert_head(&ctx->free, &sctx->queue);
    sctx->r = NULL;

    ngx_log_debug2(NGX_LOG_DEBUG_CORE, log, 0, "free gRPC stream ctx:%p, gRPC ctx:%p",
                   sctx, ctx);

    if (ctx->free_delayed && ngx_queue_empty(&ctx->opening)) {
        ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0, "continue delayed free of gRPC ctx:%p", ctx);

        ngx_grpc_cli_free_conn(ctx, log);
    }
}


static ngx_int_t
ngx_grpc_cli_check_stream_status(unsigned char *err_buf, size_t *err_len,
                                 ngx_grpc_cli_stream_ctx_t *sctx, void *r)
{
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

    return NGX_OK;
}


int
ngx_grpc_cli_stream_recv(unsigned char *err_buf, size_t *err_len,
                         void *r, bool is_http,
                         ngx_grpc_cli_stream_ctx_t *sctx,
                         ngx_grpc_cli_call_opt_t *call_opt)
{
    ngx_int_t                       rc;
    ngx_log_t                      *log;
    ngx_grpc_client_conf_t         *gccf;

    gccf = ngx_cycle_get_main_conf(ngx_grpc_client, ngx_cycle);
    log = ngx_grpc_cli_get_req_logger(r, is_http);

    rc = ngx_grpc_cli_check_stream_status(err_buf, err_len, sctx, r);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    rc = ngx_grpc_cli_kick_off_task(err_buf, err_len, r, gccf, sctx, call_opt);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    grpc_engine_stream_recv(sctx);

    ngx_log_debug2(NGX_LOG_DEBUG_CORE, log, 0, "yield gRPC stream:%p, timeout:%M",
                   sctx, call_opt->timeout);

    return NGX_OK;
}


int
ngx_grpc_cli_stream_send(unsigned char *err_buf, size_t *err_len,
                         void *r, bool is_http,
                         ngx_grpc_cli_stream_ctx_t *sctx,
                         ngx_grpc_cli_call_opt_t *call_opt,
                         const char *req_data, int req_len)
{
    ngx_int_t                       rc;
    ngx_msec_t                      timeout;
    ngx_log_t                      *log;
    ngx_grpc_client_conf_t         *gccf;

    gccf = ngx_cycle_get_main_conf(ngx_grpc_client, ngx_cycle);
    log = ngx_grpc_cli_get_req_logger(r, is_http);

    rc = ngx_grpc_cli_check_stream_status(err_buf, err_len, sctx, r);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    rc = ngx_grpc_cli_kick_off_task(err_buf, err_len, r, gccf, sctx, call_opt);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    timeout = call_opt->timeout;

    grpc_engine_stream_send(sctx, req_data, req_len);

    ngx_log_debug2(NGX_LOG_DEBUG_CORE, log, 0, "yield gRPC stream:%p, timeout:%M",
                   sctx, timeout);

    return NGX_OK;
}


int
ngx_grpc_cli_stream_close_send(unsigned char *err_buf, size_t *err_len,
                               void *r, bool is_http,
                               ngx_grpc_cli_stream_ctx_t *sctx,
                               ngx_grpc_cli_call_opt_t *call_opt)
{
    ngx_int_t                       rc;
    ngx_msec_t                      timeout;
    ngx_log_t                      *log;
    ngx_grpc_client_conf_t         *gccf;

    gccf = ngx_cycle_get_main_conf(ngx_grpc_client, ngx_cycle);
    log = ngx_grpc_cli_get_req_logger(r, is_http);

    rc = ngx_grpc_cli_check_stream_status(err_buf, err_len, sctx, r);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    rc = ngx_grpc_cli_kick_off_task(err_buf, err_len, r, gccf, sctx, call_opt);
    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    timeout = call_opt->timeout;

    grpc_engine_stream_close_send(sctx);

    ngx_log_debug2(NGX_LOG_DEBUG_CORE, log, 0, "yield gRPC stream:%p, timeout:%M",
                   sctx, timeout);

    return NGX_OK;
}


void
ngx_grpc_cli_free_buf(const unsigned char *buf)
{
    grpc_engine_free((void *) buf);
}
