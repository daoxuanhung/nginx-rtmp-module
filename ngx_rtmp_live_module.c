
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_live_module.h"
#include "ngx_rtmp_cmd_module.h"
#include "ngx_rtmp_codec_module.h"


static ngx_rtmp_publish_pt              next_publish;
static ngx_rtmp_play_pt                 next_play;
static ngx_rtmp_close_stream_pt         next_close_stream;
static ngx_rtmp_pause_pt                next_pause;
static ngx_rtmp_stream_begin_pt         next_stream_begin;
static ngx_rtmp_stream_eof_pt           next_stream_eof;
static ngx_rtmp_disconnect_pt           next_disconnect;


static ngx_int_t ngx_rtmp_live_postconfiguration(ngx_conf_t *cf);
static void * ngx_rtmp_live_create_app_conf(ngx_conf_t *cf);
static char * ngx_rtmp_live_merge_app_conf(ngx_conf_t *cf,
       void *parent, void *child);
static char *ngx_rtmp_live_set_msec_slot(ngx_conf_t *cf, ngx_command_t *cmd,
       void *conf);
static void ngx_rtmp_live_idle(ngx_event_t *pev);
static void ngx_rtmp_live_reconnect_timeout(ngx_event_t *pev);
static void ngx_rtmp_live_restart_subscribers(ngx_rtmp_live_stream_t *stream);
static ngx_int_t ngx_rtmp_live_disconnect(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_live_disconnect_init(ngx_rtmp_session_t *s, 
                                               ngx_rtmp_header_t *h, 
                                               ngx_chain_t *in);
static void ngx_rtmp_live_start(ngx_rtmp_session_t *s);
static void ngx_rtmp_live_stop(ngx_rtmp_session_t *s);


static ngx_command_t  ngx_rtmp_live_commands[] = {

    { ngx_string("live"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, live),
      NULL },

    { ngx_string("stream_buckets"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, nbuckets),
      NULL },

    { ngx_string("buffer"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, buflen),
      NULL },

    { ngx_string("sync"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_rtmp_live_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, sync),
      NULL },

    { ngx_string("interleave"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, interleave),
      NULL },

    { ngx_string("wait_key"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, wait_key),
      NULL },

    { ngx_string("wait_video"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, wait_video),
      NULL },

    { ngx_string("publish_notify"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, publish_notify),
      NULL },

    { ngx_string("play_restart"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, play_restart),
      NULL },

    { ngx_string("idle_streams"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, idle_streams),
      NULL },

    { ngx_string("drop_idle_publisher"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_rtmp_live_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, idle_timeout),
      NULL },

    { ngx_string("keep_connections"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, keep_connections),
      NULL },

    { ngx_string("reconnect_timeout"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_rtmp_live_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_live_app_conf_t, reconnect_timeout),
      NULL },

      ngx_null_command
};


static ngx_rtmp_module_t  ngx_rtmp_live_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_rtmp_live_postconfiguration,        /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_rtmp_live_create_app_conf,          /* create app configuration */
    ngx_rtmp_live_merge_app_conf            /* merge app configuration */
};


ngx_module_t  ngx_rtmp_live_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_live_module_ctx,              /* module context */
    ngx_rtmp_live_commands,                 /* module directives */
    NGX_RTMP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_rtmp_live_create_app_conf(ngx_conf_t *cf)
{
    ngx_rtmp_live_app_conf_t      *lacf;

    lacf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_live_app_conf_t));
    if (lacf == NULL) {
        return NULL;
    }

    lacf->live = NGX_CONF_UNSET;
    lacf->nbuckets = NGX_CONF_UNSET;
    lacf->buflen = NGX_CONF_UNSET_MSEC;
    lacf->sync = NGX_CONF_UNSET_MSEC;
    lacf->idle_timeout = NGX_CONF_UNSET_MSEC;
    lacf->interleave = NGX_CONF_UNSET;
    lacf->wait_key = NGX_CONF_UNSET;
    lacf->wait_video = NGX_CONF_UNSET;
    lacf->publish_notify = NGX_CONF_UNSET;
    lacf->play_restart = NGX_CONF_UNSET;
    lacf->idle_streams = NGX_CONF_UNSET;
    lacf->keep_connections = NGX_CONF_UNSET;
    lacf->reconnect_timeout = NGX_CONF_UNSET_MSEC;

    return lacf;
}


static char *
ngx_rtmp_live_merge_app_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtmp_live_app_conf_t *prev = parent;
    ngx_rtmp_live_app_conf_t *conf = child;

    ngx_conf_merge_value(conf->live, prev->live, 0);
    ngx_conf_merge_value(conf->nbuckets, prev->nbuckets, 1024);
    ngx_conf_merge_msec_value(conf->buflen, prev->buflen, 0);
    ngx_conf_merge_msec_value(conf->sync, prev->sync, 300);
    ngx_conf_merge_msec_value(conf->idle_timeout, prev->idle_timeout, 0);
    ngx_conf_merge_value(conf->interleave, prev->interleave, 0);
    ngx_conf_merge_value(conf->wait_key, prev->wait_key, 1);
    ngx_conf_merge_value(conf->wait_video, prev->wait_video, 0);
    ngx_conf_merge_value(conf->publish_notify, prev->publish_notify, 0);
    ngx_conf_merge_value(conf->play_restart, prev->play_restart, 0);
    ngx_conf_merge_value(conf->idle_streams, prev->idle_streams, 1);
    ngx_conf_merge_value(conf->keep_connections, prev->keep_connections, 0);
    ngx_conf_merge_msec_value(conf->reconnect_timeout, prev->reconnect_timeout, 30000);

    conf->pool = ngx_create_pool(4096, &cf->cycle->new_log);
    if (conf->pool == NULL) {
        return NGX_CONF_ERROR;
    }

    conf->streams = ngx_pcalloc(cf->pool,
            sizeof(ngx_rtmp_live_stream_t *) * conf->nbuckets);

    return NGX_CONF_OK;
}


static char *
ngx_rtmp_live_set_msec_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                       *p = conf;
    ngx_str_t                  *value;
    ngx_msec_t                 *msp;

    msp = (ngx_msec_t *) (p + cmd->offset);

    value = cf->args->elts;

    if (value[1].len == sizeof("off") - 1 &&
        ngx_strncasecmp(value[1].data, (u_char *) "off", value[1].len) == 0)
    {
        *msp = 0;
        return NGX_CONF_OK;
    }

    return ngx_conf_set_msec_slot(cf, cmd, conf);
}


static void
ngx_rtmp_live_reconnect_timeout(ngx_event_t *pev)
{
    ngx_connection_t               *c;
    ngx_rtmp_live_stream_t         *stream;
    ngx_rtmp_live_ctx_t            *pctx, *next_pctx;
    ngx_rtmp_session_t             *ss;
    ngx_uint_t                      subscriber_count = 0;

    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                  "live: reconnect_timeout event fired, timer=%p", pev);

    c = pev->data;
    if (c == NULL) {
        ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                      "live: reconnect_timeout: connection is NULL");
        return;
    }

    stream = c->data;
    if (stream == NULL) {
        ngx_log_error(NGX_LOG_WARN, c->log, 0,
                      "live: reconnect_timeout: stream is NULL");
        return;
    }

    ngx_log_error(NGX_LOG_INFO, c->log, 0,
                  "live: reconnect timeout for stream '%s', active=%d, publisher_disconnected=%d, keep_subscribers=%d", 
                  stream->name, stream->active, stream->publisher_disconnected, stream->keep_subscribers);

    /* Close all subscriber connections */
    for (pctx = stream->ctx; pctx; pctx = next_pctx) {
        next_pctx = pctx->next;
        subscriber_count++;
        
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                      "live: checking subscriber #%d, publishing=%d, session=%p", 
                      subscriber_count, pctx->publishing, pctx->session);
        
        if (pctx->publishing == 0 && pctx->session) {
            ss = pctx->session;
            if (ss->connection) {
                ngx_log_error(NGX_LOG_INFO, ss->connection->log, 0,
                              "live: closing subscriber #%d after reconnect timeout, session=%p, connection=%p",
                              subscriber_count, ss, ss->connection);
                ngx_rtmp_finalize_session(ss);
            } else {
                ngx_log_error(NGX_LOG_WARN, c->log, 0,
                              "live: subscriber #%d has no connection, session=%p",
                              subscriber_count, ss);
            }
        } else {
            ngx_log_error(NGX_LOG_INFO, c->log, 0,
                          "live: skipping subscriber #%d (publishing=%d, session=%p)",
                          subscriber_count, pctx->publishing, pctx->session);
        }
    }

    ngx_log_error(NGX_LOG_INFO, c->log, 0,
                  "live: reconnect timeout finished for stream '%s', processed %d subscribers", 
                  stream->name, subscriber_count);

    stream->publisher_disconnected = 0;
    stream->keep_subscribers = 0;

    ngx_log_error(NGX_LOG_INFO, c->log, 0,
                  "live: stream '%s' flags reset - publisher_disconnected=0, keep_subscribers=0", 
                  stream->name);
}


static void
ngx_rtmp_live_restart_subscribers(ngx_rtmp_live_stream_t *stream)
{
    ngx_rtmp_live_ctx_t            *pctx;
    ngx_rtmp_session_t             *ss;
    ngx_uint_t                      subscriber_count = 0;

    if (stream == NULL || !stream->active) {
        ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                      "live: restart_subscribers called with stream=%p, active=%d", 
                      stream, stream ? stream->active : -1);
        return;
    }

    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                  "live: restarting subscribers for stream '%s', active=%d", 
                  stream->name, stream->active);

    /* Restart all subscribers */
    for (pctx = stream->ctx; pctx; pctx = pctx->next) {
        subscriber_count++;
        
        ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                      "live: checking subscriber #%d for restart, publishing=%d, session=%p", 
                      subscriber_count, pctx->publishing, pctx->session);
        
        if (pctx->publishing == 0 && pctx->session) {
            ss = pctx->session;
            if (ss->connection) {
                ngx_log_error(NGX_LOG_INFO, ss->connection->log, 0,
                              "live: restarting subscriber #%d, session=%p, connection=%p",
                              subscriber_count, ss, ss->connection);
                ngx_rtmp_live_start(ss);
            } else {
                ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                              "live: subscriber #%d has no connection, session=%p",
                              subscriber_count, ss);
            }
        } else {
            ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                          "live: skipping subscriber #%d for restart (publishing=%d, session=%p)",
                          subscriber_count, pctx->publishing, pctx->session);
        }
    }

    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                  "live: finished restarting %d subscribers for stream '%s'", 
                  subscriber_count, stream->name);
}


static ngx_rtmp_live_stream_t **
ngx_rtmp_live_get_stream(ngx_rtmp_session_t *s, u_char *name, int create)
{
    ngx_rtmp_live_app_conf_t   *lacf;
    ngx_rtmp_live_stream_t    **stream;
    size_t                      len;

    lacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_live_module);
    if (lacf == NULL) {
        return NULL;
    }

    /* Prevent potential buffer overflow in stream name */
    if (name == NULL) {
        return NULL;
    }

    len = ngx_strlen(name);
    if (len == 0 || len >= NGX_RTMP_MAX_NAME) {
        return NULL;
    }
    
    stream = &lacf->streams[ngx_hash_key(name, len) % lacf->nbuckets];

    for (; *stream; stream = &(*stream)->next) {
        if (ngx_strcmp(name, (*stream)->name) == 0) {
            return stream;
        }
    }

    if (!create) {
        return NULL;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "live: create stream '%s'", name);

    if (lacf->free_streams) {
        *stream = lacf->free_streams;
        lacf->free_streams = lacf->free_streams->next;
    } else {
        *stream = ngx_palloc(lacf->pool, sizeof(ngx_rtmp_live_stream_t));
    }
    ngx_memzero(*stream, sizeof(ngx_rtmp_live_stream_t));
    ngx_memcpy((*stream)->name, name,
            ngx_min(sizeof((*stream)->name) - 1, len));
    (*stream)->epoch = ngx_current_msec;

    return stream;
}


static void
ngx_rtmp_live_idle(ngx_event_t *pev)
{
    ngx_connection_t           *c;
    ngx_rtmp_session_t         *s;
    ngx_rtmp_live_ctx_t        *ctx;

    ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, 0,
                  "live: idle timer fired, event=%p", pev);

    c = pev->data;
    if (c == NULL) {
        ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                      "live: idle timer: connection is NULL");
        return;
    }
    
    s = c->data;
    if (s == NULL) {
        ngx_log_error(NGX_LOG_WARN, c->log, 0,
                      "live: idle timer: session is NULL");
        return;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: idle timer check for session=%p, connection=%p", s, s->connection);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);
    if (ctx == NULL || !ctx->publishing) {
        /* Only drop idle publishers, not subscribers */
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: idle timer called for non-publisher (ctx=%p, publishing=%d), ignoring",
                      ctx, ctx ? ctx->publishing : -1);
        return;
    }

    ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                  "live: drop idle publisher, session=%p, stream='%s'", 
                  s, ctx->stream ? (char*)ctx->stream->name : "unknown");

    ngx_rtmp_finalize_session(s);
}


static void
ngx_rtmp_live_set_status(ngx_rtmp_session_t *s, ngx_chain_t *control,
                         ngx_chain_t **status, size_t nstatus,
                         unsigned active)
{
    ngx_rtmp_live_app_conf_t   *lacf;
    ngx_rtmp_live_ctx_t        *ctx, *pctx;
    ngx_chain_t               **cl;
    ngx_event_t                *e;
    size_t                      n;

    lacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_live_module);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: set_status called - session=%p, active=%ui, ctx=%p, publishing=%d",
                  s, active, ctx, ctx ? ctx->publishing : -1);

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "live: set active=%ui", active);

    if (ctx->active == active) {
        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "live: unchanged active=%ui", active);
        return;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: changing active state from %d to %d, session=%p, publishing=%d",
                  ctx->active, active, s, ctx->publishing);

    ctx->active = active;

    if (ctx->publishing) {

        /* publisher */

        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: processing publisher status change, active=%d, idle_timeout=%M",
                      active, lacf->idle_timeout);

        if (lacf->idle_timeout) {
            e = &ctx->idle_evt;

            if (active && !ctx->idle_evt.timer_set) {
                e->data = s->connection;
                e->log = s->connection->log;
                e->handler = ngx_rtmp_live_idle;

                ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                              "live: setting idle timer for publisher, timeout=%M ms, timer=%p",
                              lacf->idle_timeout, e);

                ngx_add_timer(e, lacf->idle_timeout);

            } else if (!active && ctx->idle_evt.timer_set) {
                ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                              "live: deleting idle timer for publisher, timer=%p", e);
                ngx_del_timer(e);
            }
        }

        ctx->stream->active = active;

        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: stream '%s' active set to %d", ctx->stream->name, active);

        /* Handle keep_connections mode when publisher disconnects */
        if (!active && lacf->keep_connections && lacf->reconnect_timeout > 0) {
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: publisher disconnected, enabling keep_connections mode for stream '%s'",
                          ctx->stream->name);

            ctx->stream->publisher_disconnected = 1;
            ctx->stream->keep_subscribers = 1;
            
            ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                           "live: publisher disconnected, keeping subscribers for %M ms",
                           lacf->reconnect_timeout);

            /* Start reconnect timeout timer */
            e = &ctx->stream->reconnect_evt;
            if (!e->timer_set) {
                e->data = s->connection;
                e->log = s->connection->log;
                e->handler = ngx_rtmp_live_reconnect_timeout;
                s->connection->data = ctx->stream;

                ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                              "live: setting reconnect timer for stream '%s', timeout=%M ms, timer=%p",
                              ctx->stream->name, lacf->reconnect_timeout, e);

                ngx_add_timer(e, lacf->reconnect_timeout);
            } else {
                ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                              "live: reconnect timer already set for stream '%s'", ctx->stream->name);
            }
            
            /* Important: Set flags BEFORE processing subscribers */
            ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                           "live: keep_subscribers flag set, skipping EOF for all subscribers");
        }

        ngx_uint_t subscriber_count = 0;
        for (pctx = ctx->stream->ctx; pctx; pctx = pctx->next) {
            subscriber_count++;
            
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: processing subscriber #%d in publisher status change, publishing=%d, session=%p",
                          subscriber_count, pctx->publishing, pctx->session);
            
            if (pctx->publishing == 0 && pctx->session) {
                /* Skip setting status for subscribers if we're keeping connections */
                if (!active && lacf->keep_connections && ctx->stream->keep_subscribers) {
                    if (pctx->session->connection) {
                        ngx_log_error(NGX_LOG_INFO, pctx->session->connection->log, 0,
                                      "live: keeping subscriber #%d connection in keep_connections mode", subscriber_count);
                    }
                    continue;
                }
                ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                              "live: setting status for subscriber #%d, active=%d", subscriber_count, active);
                ngx_rtmp_live_set_status(pctx->session, control, status,
                                         nstatus, active);
            }
        }

        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: processed %d subscribers for publisher status change", subscriber_count);

        return;
    }

    /* subscriber */

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: processing subscriber status change, active=%d, session=%p", active, s);

    if (control && ngx_rtmp_send_message(s, control, 0) != NGX_OK) {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                      "live: failed to send control message to subscriber, finalizing session");
        ngx_rtmp_finalize_session(s);
        return;
    }

    if (!ctx->silent) {
        cl = status;

        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: sending %d status messages to subscriber", (int)nstatus);

        for (n = 0; n < nstatus; ++n, ++cl) {
            if (*cl && ngx_rtmp_send_message(s, *cl, 0) != NGX_OK) {
                ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                              "live: failed to send status message #%d to subscriber, finalizing session", (int)n);
                ngx_rtmp_finalize_session(s);
                return;
            }
        }
    }

    ctx->cs[0].active = 0;
    ctx->cs[0].dropped = 0;

    ctx->cs[1].active = 0;
    ctx->cs[1].dropped = 0;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: subscriber status change completed, cs states reset");
}


static void
ngx_rtmp_live_start(ngx_rtmp_session_t *s)
{
    ngx_rtmp_core_srv_conf_t   *cscf;
    ngx_rtmp_live_app_conf_t   *lacf;
    ngx_chain_t                *control;
    ngx_chain_t                *status[3];
    size_t                      n, nstatus;

    cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);

    lacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_live_module);

    control = ngx_rtmp_create_stream_begin(s, NGX_RTMP_MSID);

    nstatus = 0;

    if (lacf->play_restart) {
        status[nstatus++] = ngx_rtmp_create_status(s, "NetStream.Play.Start",
                                                   "status", "Start live");
        status[nstatus++] = ngx_rtmp_create_sample_access(s);
    }

    if (lacf->publish_notify) {
        status[nstatus++] = ngx_rtmp_create_status(s,
                                                 "NetStream.Play.PublishNotify",
                                                 "status", "Start publishing");
    }

    ngx_rtmp_live_set_status(s, control, status, nstatus, 1);

    if (control) {
        ngx_rtmp_free_shared_chain(cscf, control);
    }

    for (n = 0; n < nstatus; ++n) {
        ngx_rtmp_free_shared_chain(cscf, status[n]);
    }
}


static void
ngx_rtmp_live_stop(ngx_rtmp_session_t *s)
{
    ngx_rtmp_core_srv_conf_t   *cscf;
    ngx_rtmp_live_app_conf_t   *lacf;
    ngx_rtmp_live_ctx_t        *ctx;
    ngx_chain_t                *control;
    ngx_chain_t                *status[3];
    size_t                      n, nstatus;

    cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);

    lacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_live_module);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);

    /* Don't send stream EOF to subscribers if we're keeping connections */
    if (ctx && !ctx->publishing && lacf && lacf->keep_connections && 
        ctx->stream && ctx->stream->keep_subscribers) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "live: skipping stream EOF for subscriber in keep_connections mode");
        return;
    }

    control = ngx_rtmp_create_stream_eof(s, NGX_RTMP_MSID);

    nstatus = 0;

    if (lacf->play_restart) {
        status[nstatus++] = ngx_rtmp_create_status(s, "NetStream.Play.Stop",
                                                   "status", "Stop live");
    }

    if (lacf->publish_notify) {
        status[nstatus++] = ngx_rtmp_create_status(s,
                                               "NetStream.Play.UnpublishNotify",
                                               "status", "Stop publishing");
    }

    ngx_rtmp_live_set_status(s, control, status, nstatus, 0);

    if (control) {
        ngx_rtmp_free_shared_chain(cscf, control);
    }

    for (n = 0; n < nstatus; ++n) {
        ngx_rtmp_free_shared_chain(cscf, status[n]);
    }
}


static ngx_int_t
ngx_rtmp_live_stream_begin(ngx_rtmp_session_t *s, ngx_rtmp_stream_begin_t *v)
{
    ngx_rtmp_live_ctx_t    *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);

    if (ctx == NULL || ctx->stream == NULL || !ctx->publishing) {
        goto next;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "live: stream_begin");

    ngx_rtmp_live_start(s);

next:
    return next_stream_begin(s, v);
}


static ngx_int_t
ngx_rtmp_live_stream_eof(ngx_rtmp_session_t *s, ngx_rtmp_stream_eof_t *v)
{
    ngx_rtmp_live_ctx_t    *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);

    if (ctx == NULL || ctx->stream == NULL || !ctx->publishing) {
        goto next;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "live: stream_eof");

    ngx_rtmp_live_stop(s);

next:
    return next_stream_eof(s, v);
}


static ngx_int_t
ngx_rtmp_live_disconnect(ngx_rtmp_session_t *s)
{
    ngx_rtmp_live_app_conf_t       *lacf;
    ngx_rtmp_live_ctx_t            *ctx;
    ngx_rtmp_live_stream_t         *stream;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: disconnect called for session=%p, connection=%p", s, s->connection);

    lacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_live_module);
    if (lacf == NULL || !lacf->live) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: disconnect - not a live session, lacf=%p", lacf);
        goto next;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);
    if (ctx == NULL || ctx->stream == NULL) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: disconnect - no context or stream, ctx=%p, stream=%p", 
                      ctx, ctx ? ctx->stream : NULL);
        goto next;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: disconnect for stream '%s', publishing=%d, ctx=%p",
                  ctx->stream->name, ctx->publishing, ctx);

    stream = ctx->stream;

    /* Clean up idle timer for any session (publisher or subscriber) */
    if (ctx->idle_evt.timer_set) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: disconnect - deleting idle timer, timer=%p", &ctx->idle_evt);
        ngx_del_timer(&ctx->idle_evt);
    } else {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: disconnect - no idle timer to delete");
    }

    /* Handle publisher disconnect when keep_connections is enabled */
    if (ctx->publishing && lacf->keep_connections && lacf->reconnect_timeout > 0 && s->connection) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: publisher disconnect for stream '%s', enabling keep_connections",
                      stream->name);

        stream->publisher_disconnected = 1;
        stream->keep_subscribers = 1;
        stream->publishing = 0;

        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: set stream flags - publisher_disconnected=1, keep_subscribers=1, publishing=0");

        /* Start reconnect timeout timer */
        if (!stream->reconnect_evt.timer_set && s->connection) {
            stream->reconnect_evt.data = s->connection;
            stream->reconnect_evt.log = s->connection->log;
            stream->reconnect_evt.handler = ngx_rtmp_live_reconnect_timeout;
            s->connection->data = stream;

            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: adding reconnect timer, timeout=%M ms, timer=%p",
                          lacf->reconnect_timeout, &stream->reconnect_evt);

            ngx_add_timer(&stream->reconnect_evt, lacf->reconnect_timeout);
        } else {
            ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                          "live: cannot add reconnect timer - timer_set=%d, connection=%p",
                          stream->reconnect_evt.timer_set, s->connection);
        }
    }

next:
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: disconnect completed for session=%p", s);
    return next_disconnect(s);
}


static ngx_int_t
ngx_rtmp_live_disconnect_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
                              ngx_chain_t *in)
{
    return ngx_rtmp_live_disconnect(s);
}


static void
ngx_rtmp_live_join(ngx_rtmp_session_t *s, u_char *name, unsigned publisher)
{
    ngx_rtmp_live_ctx_t            *ctx;
    ngx_rtmp_live_stream_t        **stream;
    ngx_rtmp_live_app_conf_t       *lacf;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: join called - session=%p, name='%s', publisher=%d", s, name, publisher);

    lacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_live_module);
    if (lacf == NULL) {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                      "live: join failed - no app config");
        return;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);
    if (ctx && ctx->stream) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: already joined stream '%s'", ctx->stream->name);
        return;
    }

    if (ctx == NULL) {
        ctx = ngx_palloc(s->connection->pool, sizeof(ngx_rtmp_live_ctx_t));
        ngx_rtmp_set_ctx(s, ctx, ngx_rtmp_live_module);
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: allocated new context, ctx=%p", ctx);
    }

    ngx_memzero(ctx, sizeof(*ctx));

    ctx->session = s;

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "live: join '%s'", name);

    stream = ngx_rtmp_live_get_stream(s, name, publisher || lacf->idle_streams);

    if (stream == NULL ||
        !(publisher || (*stream)->publishing || lacf->idle_streams))
    {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "live: stream not found - stream=%p, publisher=%d, stream_publishing=%d, idle_streams=%d",
                      stream, publisher, stream ? (*stream)->publishing : -1, lacf->idle_streams);

        ngx_rtmp_send_status(s, "NetStream.Play.StreamNotFound", "error",
                             "No such stream");

        ngx_rtmp_finalize_session(s);

        return;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: found stream '%s', stream=%p, publishing=%d", 
                  (*stream)->name, *stream, (*stream)->publishing);

    if (publisher) {
        if ((*stream)->publishing) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                          "live: already publishing to stream '%s'", (*stream)->name);

            ngx_rtmp_send_status(s, "NetStream.Publish.BadName", "error",
                                 "Already publishing");

            return;
        }

        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: setting publishing=1 for stream '%s'", (*stream)->name);

        (*stream)->publishing = 1;
        
        /* Handle publisher reconnection */
        if ((*stream)->publisher_disconnected && (*stream)->keep_subscribers) {
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: publisher reconnected to stream '%s', canceling reconnect timeout", (*stream)->name);
            
            /* Cancel reconnect timeout timer */
            if ((*stream)->reconnect_evt.timer_set) {
                ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                              "live: deleting reconnect timer, timer=%p", &(*stream)->reconnect_evt);
                ngx_del_timer(&(*stream)->reconnect_evt);
            }
            
            /* Reset publisher disconnected state */
            (*stream)->publisher_disconnected = 0;
            (*stream)->keep_subscribers = 0;
            
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: reset stream flags - publisher_disconnected=0, keep_subscribers=0");
            
            /* Restart subscribers when publisher is back */
            ngx_rtmp_live_restart_subscribers(*stream);
        }
    }

    ctx->stream = *stream;
    ctx->publishing = publisher;
    ctx->next = (*stream)->ctx;

    (*stream)->ctx = ctx;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: session joined stream '%s' - ctx=%p, publishing=%d, stream=%p",
                  (*stream)->name, ctx, publisher, *stream);

    if (lacf->buflen) {
        s->out_buffer = 1;
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: enabled output buffer, buflen=%M", lacf->buflen);
    }

    ctx->cs[0].csid = NGX_RTMP_CSID_VIDEO;
    ctx->cs[1].csid = NGX_RTMP_CSID_AUDIO;

    if (!ctx->publishing && ctx->stream->active) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: starting subscriber on active stream");
        ngx_rtmp_live_start(s);
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: join completed successfully");
}


static ngx_int_t
ngx_rtmp_live_close_stream(ngx_rtmp_session_t *s, ngx_rtmp_close_stream_t *v)
{
    ngx_rtmp_session_t             *ss;
    ngx_rtmp_live_ctx_t            *ctx, **cctx, *pctx;
    ngx_rtmp_live_stream_t        **stream;
    ngx_rtmp_live_app_conf_t       *lacf;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: close_stream called for session=%p", s);

    lacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_live_module);
    if (lacf == NULL) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: close_stream - no app config");
        goto next;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);
    if (ctx == NULL) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: close_stream - no context");
        goto next;
    }

    if (ctx->stream == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "live: not joined");
        goto next;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: leaving stream '%s', publishing=%d, ctx=%p, stream=%p",
                  ctx->stream->name, ctx->publishing, ctx, ctx->stream);

    if (ctx->stream->publishing && ctx->publishing) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: clearing publishing flag for stream '%s'", ctx->stream->name);
        ctx->stream->publishing = 0;
    }

    /* Remove context from stream's context list */
    for (cctx = &ctx->stream->ctx; *cctx; cctx = &(*cctx)->next) {
        if (*cctx == ctx) {
            *cctx = ctx->next;
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: removed context from stream's context list");
            break;
        }
    }

    if (ctx->publishing || ctx->stream->active) {
        /* Only call ngx_rtmp_live_stop if we're not in keep_connections mode for publisher */
        if (!(ctx->publishing && lacf->keep_connections && lacf->reconnect_timeout > 0)) {
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: calling live_stop for session");
            ngx_rtmp_live_stop(s);
        } else {
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: skipping live_stop due to keep_connections mode");
        }
    }

    if (ctx->publishing) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: processing publisher close for stream '%s'", ctx->stream->name);

        ngx_rtmp_send_status(s, "NetStream.Unpublish.Success",
                             "status", "Stop publishing");
        
        /* Cancel idle timer if it's set */
        if (ctx->idle_evt.timer_set) {
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: deleting idle timer in close_stream, timer=%p", &ctx->idle_evt);
            ngx_del_timer(&ctx->idle_evt);
        } else {
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: no idle timer to delete in close_stream");
        }
        
        /* Cancel reconnect timer if it's set */
        if (ctx->stream->reconnect_evt.timer_set) {
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: deleting reconnect timer in close_stream, timer=%p", &ctx->stream->reconnect_evt);
            ngx_del_timer(&ctx->stream->reconnect_evt);
        }
        
        if (lacf->keep_connections && lacf->reconnect_timeout > 0) {
            /* Set up for keeping subscribers during publisher disconnect */
            ctx->stream->publisher_disconnected = 1;
            ctx->stream->keep_subscribers = 1;
            
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: publisher closing, keeping subscribers for %M ms, setting flags",
                          lacf->reconnect_timeout);

            /* Start reconnect timeout timer */
            if (!ctx->stream->reconnect_evt.timer_set) {
                ctx->stream->reconnect_evt.data = s->connection;
                ctx->stream->reconnect_evt.log = s->connection->log;
                ctx->stream->reconnect_evt.handler = ngx_rtmp_live_reconnect_timeout;
                s->connection->data = ctx->stream;

                ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                              "live: adding reconnect timer in close_stream, timeout=%M ms, timer=%p",
                              lacf->reconnect_timeout, &ctx->stream->reconnect_evt);

                ngx_add_timer(&ctx->stream->reconnect_evt, lacf->reconnect_timeout);
            } else {
                ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                              "live: reconnect timer already set in close_stream");
            }
        } else if (!lacf->idle_streams) {
            ngx_uint_t subscriber_count = 0;
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: no keep_connections, closing all subscribers");
            
            for (pctx = ctx->stream->ctx; pctx; pctx = pctx->next) {
                subscriber_count++;
                if (pctx->publishing == 0 && pctx->session) {
                    ss = pctx->session;
                    if (ss->connection) {
                        ngx_log_error(NGX_LOG_INFO, ss->connection->log, 0,
                                      "live: no publisher, closing subscriber #%d", subscriber_count);
                        ngx_rtmp_finalize_session(ss);
                    }
                }
            }
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "live: closed %d subscribers due to no publisher", subscriber_count);
        }
    }

    if (ctx->stream->ctx) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: stream '%s' still has contexts, keeping stream alive", ctx->stream->name);
        ctx->stream = NULL;
        goto next;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: deleting empty stream '%s'", ctx->stream->name);

    /* Clean up any pending reconnect timer */
    if (ctx->stream->reconnect_evt.timer_set) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: deleting reconnect timer for empty stream, timer=%p", &ctx->stream->reconnect_evt);
        ngx_del_timer(&ctx->stream->reconnect_evt);
    }

    stream = ngx_rtmp_live_get_stream(s, ctx->stream->name, 0);
    if (stream == NULL) {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                      "live: could not find stream to delete");
        goto next;
    }
    *stream = (*stream)->next;

    ctx->stream->next = lacf->free_streams;
    lacf->free_streams = ctx->stream;
    ctx->stream = NULL;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: stream deleted and added to free list");

    if (!ctx->silent && !ctx->publishing && !lacf->play_restart) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: sending Play.Stop status to subscriber");
        ngx_rtmp_send_status(s, "NetStream.Play.Stop", "status", "Stop live");
    }

next:
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: close_stream completed for session=%p", s);
    return next_close_stream(s, v);
}


static ngx_int_t
ngx_rtmp_live_pause(ngx_rtmp_session_t *s, ngx_rtmp_pause_t *v)
{
    ngx_rtmp_live_ctx_t            *ctx;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: pause request for session=%p, pause=%d", s, v->pause);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);

    if (ctx == NULL || ctx->stream == NULL) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "live: pause - no context or stream");
        goto next;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: pause=%i timestamp=%f for stream='%V'",
                  (ngx_int_t) v->pause, v->position, &ctx->stream->name);
    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "live: pause=%i timestamp=%f",
                   (ngx_int_t) v->pause, v->position);

    if (v->pause) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: pausing subscriber stream");
        
        if (ngx_rtmp_send_status(s, "NetStream.Pause.Notify", "status",
                                 "Paused live")
            != NGX_OK)
        {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                          "live: pause - failed to send pause status");
            return NGX_ERROR;
        }

        ctx->paused = 1;

        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: stopping paused stream");
        ngx_rtmp_live_stop(s);

    } else {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: unpausing subscriber stream");
        
        if (ngx_rtmp_send_status(s, "NetStream.Unpause.Notify", "status",
                                 "Unpaused live")
            != NGX_OK)
        {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                          "live: pause - failed to send unpause status");
            return NGX_ERROR;
        }

        ctx->paused = 0;

        ngx_rtmp_live_start(s);
    }

next:
    return next_pause(s, v);
}

static ngx_int_t
ngx_rtmp_live_av(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
                 ngx_chain_t *in)
{
    ngx_rtmp_live_ctx_t            *ctx, *pctx;
    ngx_rtmp_codec_ctx_t           *codec_ctx;
    ngx_chain_t                    *header, *coheader, *meta,
                                   *apkt, *aapkt, *acopkt, *rpkt;
    ngx_rtmp_core_srv_conf_t       *cscf;
    ngx_rtmp_live_app_conf_t       *lacf;
    ngx_rtmp_session_t             *ss;
    ngx_rtmp_header_t               ch, lh, clh;
    ngx_int_t                       rc, mandatory, dummy_audio;
    ngx_uint_t                      prio;
    ngx_uint_t                      peers;
    ngx_uint_t                      meta_version;
    ngx_uint_t                      csidx;
    uint32_t                        delta;
    ngx_rtmp_live_chunk_stream_t   *cs;
#ifdef NGX_DEBUG
    const char                     *type_s;

    type_s = (h->type == NGX_RTMP_MSG_VIDEO ? "video" : "audio");
#endif

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: av handler called for session=%p, type=%d", s, h->type);

    lacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_live_module);
    if (lacf == NULL) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "live: av handler - no app config");
        return NGX_ERROR;
    }

    if (!lacf->live || in == NULL  || in->buf == NULL) {
        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "live: av handler - conditions not met (live=%d)", lacf->live);
        return NGX_OK;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);
    if (ctx == NULL || ctx->stream == NULL) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "live: av handler - no context or stream");
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: av handler - ctx=%p, stream=%p, publishing=%d", 
                  ctx, ctx->stream, ctx->publishing);

    if (ctx->publishing == 0) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: av packet from non-publisher, ignoring");
        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "live: %s from non-publisher", type_s);
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: av packet from publisher stream='%V'", &ctx->stream->name);

    if (!ctx->stream->active) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: stream not active, starting stream");
        ngx_rtmp_live_start(s);
    }

    if (ctx->idle_evt.timer_set) {
        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "live: av resetting idle timer for %dms", lacf->idle_timeout);
        ngx_add_timer(&ctx->idle_evt, lacf->idle_timeout);
    }

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "live: %s packet timestamp=%uD",
                   type_s, h->timestamp);

    s->current_time = h->timestamp;

    peers = 0;
    apkt = NULL;
    aapkt = NULL;
    acopkt = NULL;
    header = NULL;
    coheader = NULL;
    meta = NULL;
    meta_version = 0;
    mandatory = 0;

    prio = (h->type == NGX_RTMP_MSG_VIDEO ?
            ngx_rtmp_get_video_frame_type(in) : 0);

    cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);

    csidx = !(lacf->interleave || h->type == NGX_RTMP_MSG_VIDEO);

    cs  = &ctx->cs[csidx];

    ngx_memzero(&ch, sizeof(ch));

    ch.timestamp = h->timestamp;
    ch.msid = NGX_RTMP_MSID;
    ch.csid = cs->csid;
    ch.type = h->type;

    lh = ch;

    if (cs->active) {
        lh.timestamp = cs->timestamp;
    }

    clh = lh;
    clh.type = (h->type == NGX_RTMP_MSG_AUDIO ? NGX_RTMP_MSG_VIDEO :
                                                NGX_RTMP_MSG_AUDIO);

    cs->active = 1;
    cs->timestamp = ch.timestamp;

    delta = ch.timestamp - lh.timestamp;
/*
    if (delta >> 31) {
        ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "live: clipping non-monotonical timestamp %uD->%uD",
                       lh.timestamp, ch.timestamp);

        delta = 0;

        ch.timestamp = lh.timestamp;
    }
*/
    rpkt = ngx_rtmp_append_shared_bufs(cscf, NULL, in);

    ngx_rtmp_prepare_message(s, &ch, &lh, rpkt);

    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    if (codec_ctx) {

        if (h->type == NGX_RTMP_MSG_AUDIO) {
            header = codec_ctx->aac_header;

            if (lacf->interleave) {
                coheader = codec_ctx->avc_header;
            }

            if (codec_ctx->audio_codec_id == NGX_RTMP_AUDIO_AAC &&
                ngx_rtmp_is_codec_header(in))
            {
                prio = 0;
                mandatory = 1;
            }

        } else {
            header = codec_ctx->avc_header;

            if (lacf->interleave) {
                coheader = codec_ctx->aac_header;
            }

            if (codec_ctx->video_codec_id == NGX_RTMP_VIDEO_H264 &&
                ngx_rtmp_is_codec_header(in))
            {
                prio = 0;
                mandatory = 1;
            }
        }

        if (codec_ctx->meta) {
            meta = codec_ctx->meta;
            meta_version = codec_ctx->meta_version;
        }
    }

    /* broadcast to all subscribers */
    
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: av starting broadcast to subscribers, stream_ctx=%p", ctx->stream->ctx);

    for (pctx = ctx->stream->ctx; pctx; pctx = pctx->next) {
        if (pctx == ctx || pctx->paused || !pctx->session) {
            ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                           "live: av skipping subscriber");
            continue;
        }

        ss = pctx->session;
        
        ngx_log_error(NGX_LOG_INFO, ss->connection->log, 0,
                      "live: av processing subscriber session=%p", ss);
        
        /* Skip if session connection is invalid */
        if (!ss->connection) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                          "live: av subscriber has no connection, skipping");
            continue;
        }
        
        cs = &pctx->cs[csidx];

        /* send metadata */

        if (meta && meta_version != pctx->meta_version) {
            ngx_log_debug0(NGX_LOG_DEBUG_RTMP, ss->connection->log, 0,
                           "live: meta");

            if (ngx_rtmp_send_message(ss, meta, 0) == NGX_OK) {
                pctx->meta_version = meta_version;
            }
        }

        /* sync stream */

        if (cs->active && (lacf->sync && cs->dropped > lacf->sync)) {
            ngx_log_debug2(NGX_LOG_DEBUG_RTMP, ss->connection->log, 0,
                           "live: sync %s dropped=%uD", type_s, cs->dropped);

            cs->active = 0;
            cs->dropped = 0;
        }

        /* absolute packet */

        if (!cs->active) {

            if (mandatory) {
                ngx_log_debug0(NGX_LOG_DEBUG_RTMP, ss->connection->log, 0,
                               "live: skipping header");
                continue;
            }

            if (lacf->wait_video && h->type == NGX_RTMP_MSG_AUDIO &&
                !pctx->cs[0].active)
            {
                ngx_log_debug0(NGX_LOG_DEBUG_RTMP, ss->connection->log, 0,
                               "live: waiting for video");
                continue;
            }

            if (lacf->wait_key && prio != NGX_RTMP_VIDEO_KEY_FRAME &&
               (lacf->interleave || h->type == NGX_RTMP_MSG_VIDEO))
            {
                ngx_log_debug0(NGX_LOG_DEBUG_RTMP, ss->connection->log, 0,
                               "live: skip non-key");
                continue;
            }

            dummy_audio = 0;
            if (lacf->wait_video && h->type == NGX_RTMP_MSG_VIDEO &&
                !pctx->cs[1].active)
            {
                dummy_audio = 1;
                if (aapkt == NULL) {
                    aapkt = ngx_rtmp_alloc_shared_buf(cscf);
                    ngx_rtmp_prepare_message(s, &clh, NULL, aapkt);
                }
            }

            if (header || coheader) {

                /* send absolute codec header */

                ngx_log_debug2(NGX_LOG_DEBUG_RTMP, ss->connection->log, 0,
                               "live: abs %s header timestamp=%uD",
                               type_s, lh.timestamp);

                if (header) {
                    if (apkt == NULL) {
                        apkt = ngx_rtmp_append_shared_bufs(cscf, NULL, header);
                        ngx_rtmp_prepare_message(s, &lh, NULL, apkt);
                    }

                    rc = ngx_rtmp_send_message(ss, apkt, 0);
                    if (rc != NGX_OK) {
                        continue;
                    }
                }

                if (coheader) {
                    if (acopkt == NULL) {
                        acopkt = ngx_rtmp_append_shared_bufs(cscf, NULL, coheader);
                        ngx_rtmp_prepare_message(s, &clh, NULL, acopkt);
                    }

                    rc = ngx_rtmp_send_message(ss, acopkt, 0);
                    if (rc != NGX_OK) {
                        continue;
                    }

                } else if (dummy_audio) {
                    ngx_rtmp_send_message(ss, aapkt, 0);
                }

                cs->timestamp = lh.timestamp;
                cs->active = 1;
                ss->current_time = cs->timestamp;

            } else {

                /* send absolute packet */

                ngx_log_debug2(NGX_LOG_DEBUG_RTMP, ss->connection->log, 0,
                               "live: abs %s packet timestamp=%uD",
                               type_s, ch.timestamp);

                if (apkt == NULL) {
                    apkt = ngx_rtmp_append_shared_bufs(cscf, NULL, in);
                    ngx_rtmp_prepare_message(s, &ch, NULL, apkt);
                }

                rc = ngx_rtmp_send_message(ss, apkt, prio);
                if (rc != NGX_OK) {
                    continue;
                }

                cs->timestamp = ch.timestamp;
                cs->active = 1;
                ss->current_time = cs->timestamp;

                ++peers;

                if (dummy_audio) {
                    ngx_rtmp_send_message(ss, aapkt, 0);
                }

                continue;
            }
        }

        /* send relative packet */

        ngx_log_error(NGX_LOG_INFO, ss->connection->log, 0,
                      "live: av sending relative packet to subscriber, delta=%uD", delta);
        ngx_log_debug2(NGX_LOG_DEBUG_RTMP, ss->connection->log, 0,
                       "live: rel %s packet delta=%uD",
                       type_s, delta);

        if (ngx_rtmp_send_message(ss, rpkt, prio) != NGX_OK) {
            ++pctx->ndropped;

            cs->dropped += delta;
            
            ngx_log_error(NGX_LOG_ERR, ss->connection->log, 0,
                          "live: av send failed, dropped=%uD, ndropped=%uD", 
                          cs->dropped, pctx->ndropped);

            if (mandatory) {
                ngx_log_error(NGX_LOG_ERR, ss->connection->log, 0,
                              "live: av mandatory packet failed, finalizing session");
                ngx_log_debug0(NGX_LOG_DEBUG_RTMP, ss->connection->log, 0,
                               "live: mandatory packet failed");
                ngx_rtmp_finalize_session(ss);
            }

            continue;
        }

        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, ss->connection->log, 0,
                       "live: av packet sent successfully to subscriber");

        cs->timestamp += delta;
        ++peers;
        ss->current_time = cs->timestamp;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: av broadcast completed, peers=%uD", peers);

    if (rpkt) {
        ngx_rtmp_free_shared_chain(cscf, rpkt);
    }

    if (apkt) {
        ngx_rtmp_free_shared_chain(cscf, apkt);
    }

    if (aapkt) {
        ngx_rtmp_free_shared_chain(cscf, aapkt);
    }

    if (acopkt) {
        ngx_rtmp_free_shared_chain(cscf, acopkt);
    }

    ngx_rtmp_update_bandwidth(&ctx->stream->bw_in, h->mlen);
    ngx_rtmp_update_bandwidth(&ctx->stream->bw_out, h->mlen * peers);

    ngx_rtmp_update_bandwidth(h->type == NGX_RTMP_MSG_AUDIO ?
                              &ctx->stream->bw_in_audio :
                              &ctx->stream->bw_in_video,
                              h->mlen);

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: av handler completed successfully");
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_live_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v)
{
    ngx_rtmp_live_app_conf_t       *lacf;
    ngx_rtmp_live_ctx_t            *ctx;

    lacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_live_module);

    if (lacf == NULL || !lacf->live) {
        goto next;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "live: publish: name='%s' type='%s'",
                   v->name, v->type);

    /* join stream as publisher */

    ngx_rtmp_live_join(s, v->name, 1);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);
    if (ctx == NULL || !ctx->publishing) {
        goto next;
    }

    ctx->silent = v->silent;

    if (!ctx->silent) {
        ngx_rtmp_send_status(s, "NetStream.Publish.Start",
                             "status", "Start publishing");
    }

next:
    return next_publish(s, v);
}


static ngx_int_t
ngx_rtmp_live_play(ngx_rtmp_session_t *s, ngx_rtmp_play_t *v)
{
    ngx_rtmp_live_app_conf_t       *lacf;
    ngx_rtmp_live_ctx_t            *ctx;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: play request for session=%p, name='%s'", s, v->name);

    lacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_live_module);

    if (lacf == NULL || !lacf->live) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: play - live disabled or no config");
        goto next;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: play: name='%s' start=%uD duration=%uD reset=%d",
                  v->name, (uint32_t) v->start,
                  (uint32_t) v->duration, (uint32_t) v->reset);
    ngx_log_debug4(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "live: play: name='%s' start=%uD duration=%uD reset=%d",
                   v->name, (uint32_t) v->start,
                   (uint32_t) v->duration, (uint32_t) v->reset);

    /* join stream as subscriber */

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: play joining stream as subscriber");
    ngx_rtmp_live_join(s, v->name, 0);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_live_module);
    if (ctx == NULL) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "live: play - failed to get context after join");
        goto next;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: play joined successfully, ctx=%p, stream=%p", 
                  ctx, ctx->stream);

    ctx->silent = v->silent;

    if (!ctx->silent && !lacf->play_restart) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "live: play sending start status to subscriber");
        ngx_rtmp_send_status(s, "NetStream.Play.Start",
                             "status", "Start live");
        ngx_rtmp_send_sample_access(s);
    }

next:
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "live: play completed, forwarding to next handler");
    return next_play(s, v);
}


static ngx_int_t
ngx_rtmp_live_postconfiguration(ngx_conf_t *cf)
{
    ngx_rtmp_core_main_conf_t          *cmcf;
    ngx_rtmp_handler_pt                *h;

    cmcf = ngx_rtmp_conf_get_module_main_conf(cf, ngx_rtmp_core_module);

    /* register raw event handlers */

    h = ngx_array_push(&cmcf->events[NGX_RTMP_MSG_AUDIO]);
    *h = ngx_rtmp_live_av;

    h = ngx_array_push(&cmcf->events[NGX_RTMP_MSG_VIDEO]);
    *h = ngx_rtmp_live_av;

    h = ngx_array_push(&cmcf->events[NGX_RTMP_DISCONNECT]);
    *h = ngx_rtmp_live_disconnect_init;

    /* chain handlers */

    next_publish = ngx_rtmp_publish;
    ngx_rtmp_publish = ngx_rtmp_live_publish;

    next_play = ngx_rtmp_play;
    ngx_rtmp_play = ngx_rtmp_live_play;

    next_close_stream = ngx_rtmp_close_stream;
    ngx_rtmp_close_stream = ngx_rtmp_live_close_stream;

    next_pause = ngx_rtmp_pause;
    ngx_rtmp_pause = ngx_rtmp_live_pause;

    next_stream_begin = ngx_rtmp_stream_begin;
    ngx_rtmp_stream_begin = ngx_rtmp_live_stream_begin;

    next_stream_eof = ngx_rtmp_stream_eof;
    ngx_rtmp_stream_eof = ngx_rtmp_live_stream_eof;

    next_disconnect = ngx_rtmp_disconnect;
    ngx_rtmp_disconnect = ngx_rtmp_live_disconnect;

    return NGX_OK;
}
