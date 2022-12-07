-- Copyright 2022 Shenzhen ZhiLiu Technology Co., Ltd.
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
-- http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.

local protoc = require("protoc")
local pb = require("pb")
local base = require("resty.core.base")
local get_request = base.get_request
local ffi = require("ffi")
local C = ffi.C
local ffi_cast = ffi.cast
local NGX_OK = ngx.OK
local subsystem = ngx.config.subsystem
local is_http = subsystem == "http"

local MAX_INIT32 = 2147483647

ffi.cdef[[
typedef struct DialOpt {
    bool                     insecure;
    bool                     tls_verify;
    int                      max_recv_msg_size;
    int                      client_cert_len;
    char                    *client_cert;
    int                      client_key_len;
    char                    *client_key;
    int                      trusted_ca_len;
    char                    *trusted_ca;
} DialOpt;

typedef struct {
	int		key_len;
	int		value_len;
	char   *key;
	char   *value;
} Metadata;

typedef uintptr_t ngx_msec_t;
typedef struct {
    ngx_msec_t timeout;

	int		   metadata_len;
	Metadata  *metadata;
} CallOpt;

int
ngx_grpc_cli_is_engine_inited(void);
void
ngx_grpc_cli_free_buf(const unsigned char *buf);

void *
ngx_grpc_cli_connect(unsigned char *err_buf, size_t *err_len,
                     void *r, bool is_http,
                     const char *target_data, int target_len,
                     void *opt);
void
ngx_grpc_cli_close(void *ctx, void *r, bool is_http);

int
ngx_grpc_cli_call(unsigned char *err_buf, size_t *err_len,
                  void *r, bool is_http,
                  void *ctx,
                  const char *method_data, int method_len,
                  const char *req_data, int req_len,
                  void *opt, ngx_str_t *blocking_call_output);

void *
ngx_grpc_cli_new_stream(unsigned char *err_buf, size_t *err_len,
                        void *r, bool is_http,
                        void *ctx,
                        const char *method_data, int method_len,
                        const char *req_data, int req_len,
                        void *opt, int type);
void
ngx_grpc_cli_close_stream(void *ctx, void *r, bool is_http);
int
ngx_grpc_cli_stream_recv(unsigned char *err_buf, size_t *err_len,
                         void *r, bool is_http,
                         void *ctx, void *opt);
int
ngx_grpc_cli_stream_send(unsigned char *err_buf, size_t *err_len,
                         void *r, bool is_http,
                         void *ctx, void *opt,
                         const char *req_data, int req_len);
int
ngx_grpc_cli_stream_close_send(unsigned char *err_buf, size_t *err_len,
                               void *r, bool is_http,
                               void *ctx, void *opt);
]]

if C.ngx_grpc_cli_is_engine_inited() == 0 then
    error("The gRPC client engine is not initialized. " ..
          "Need to ensure the grpc engine exist in the defined NGX_GRPC_CLI_ENGINE_PATH. " ..
          "And the Nginx configuration contains 'thread_pool grpc-client-nginx-module'. " ..
          "And this library can not be loaded in the init phase."
          )
end


local _M = {
    PROTO_TYPE_FILE = 1,
    PROTO_TYPE_STR = 2,

    INT64_AS_NUMBER = 1,
    INT64_AS_STRING = 2,
    INT64_AS_HEXSTRING = 3,
}
local Conn = {}
Conn.__index = Conn
local ClientStream = {}
ClientStream.__index = ClientStream
local ServerStream = {}
ServerStream.__index = ServerStream
local BidirectionalStream = {}
BidirectionalStream.__index = BidirectionalStream

local protoc_inst
local current_pb_state

local ERR_BUF_SIZE = 512
local err_buf = ffi.new("char[?]", ERR_BUF_SIZE)
local err_len = ffi.new("size_t[1]")
local blocking_call_output = ffi.new("ngx_str_t[1]")
local gRPCClientStreamType = 1
local gRPCServerStreamType = 2
local gRPCBidirectionalStreamType = 3


function _M.load(def, proto_type)
    local old_state
    if not protoc_inst then
        -- initialize protoc compiler
        old_state = pb.state(nil)
        protoc.reload()
        protoc_inst = protoc.new()
        protoc_inst.index = {}
    else
        old_state = pb.state(current_pb_state)
    end

    if not proto_type then
        proto_type = _M.PROTO_TYPE_FILE
    end

    local ok, err
    local loaded_key

    if proto_type == _M.PROTO_TYPE_FILE then
        loaded_key = def
        ok, err = pcall(protoc_inst.loadfile, protoc_inst, loaded_key)
    else
        -- proto_type == _M.PROTO_TYPE_STR
        loaded_key = "dummy"
        ok, err = pcall(protoc_inst.load, protoc_inst, def, loaded_key)
    end

    if not ok then
        pb.state(old_state)
        return nil, "failed to load protobuf: " .. err
    end

    local index = protoc_inst.index
    for _, s in ipairs(protoc_inst.loaded[loaded_key].service or {}) do
        local method_index = {}
        for _, m in ipairs(s.method) do
            method_index[m.name] = m
        end
        index[protoc_inst.loaded[loaded_key].package .. '.' .. s.name] = method_index
    end

    current_pb_state = pb.state(nil)
    pb.state(old_state)
    return true
end


local function ctx_gc_handler(ctx)
    C.ngx_grpc_cli_close(ctx, nil, is_http)
end


local function load_state(opt, ...)
    local old_state = pb.state(current_pb_state)
    local enc = opt.int64_encoding
    if not enc or enc == _M.INT64_AS_NUMBER then
        pb.option("int64_as_number")
    else
        if enc == _M.INT64_AS_STRING then
            pb.option("int64_as_string")
        else
            pb.option("int64_as_hexstring")
        end
    end

    local ok, res = pcall(...)
    pb.state(old_state)
    return ok, res
end


function _M.connect(target, opt)
    if not opt then
        opt = {}
    end

    local opt_buf = ffi.new("DialOpt[1]")
    local opt_ptr = opt_buf[0]

    if opt.insecure == false then
        opt_ptr.insecure = false
    else
        opt_ptr.insecure = true
    end

    if opt.tls_verify == false then
        opt_ptr.tls_verify = false
    else
        opt_ptr.tls_verify = true
    end

    if opt.max_recv_msg_size then
        if opt.max_recv_msg_size > MAX_INIT32 then
            opt_ptr.max_recv_msg_size = MAX_INIT32
        else
            opt_ptr.max_recv_msg_size = opt.max_recv_msg_size
        end
    else
        opt_ptr.max_recv_msg_size = 0
    end

    if (opt.client_cert == nil ) ~= (opt.client_key == nil) then
        return nil, string.format("client_cert and client_key must both be " ..
                                    "present or both absent: cert: %s key: %s",
                                     opt.client_cert or "", opt.client_key or "")
    end

    if opt.client_cert then
        opt_ptr.client_cert = ffi_cast("char *", opt.client_cert)
        opt_ptr.client_cert_len = #opt.client_cert
    else
        opt_ptr.client_cert_len = 0
    end

    if opt.client_key then
        opt_ptr.client_key = ffi_cast("char *", opt.client_key)
        opt_ptr.client_key_len = #opt.client_key
    else
        opt_ptr.client_key_len = 0
    end

    if opt.trusted_ca then
        opt_ptr.trusted_ca = ffi_cast("char *", opt.trusted_ca)
        opt_ptr.trusted_ca_len = #opt.trusted_ca
    else
        opt_ptr.trusted_ca_len = 0
    end

    local conn = {}
    local r = get_request()

    err_len[0] = ERR_BUF_SIZE
    -- grpc-go dials the target in non-blocking way
    local ctx = C.ngx_grpc_cli_connect(err_buf, err_len,
                                       r, is_http,
                                       target, #target, opt_buf)
    if ctx == nil then
        local err = ffi.string(err_buf, err_len[0])
        return nil, err
    end
    ffi.gc(ctx, ctx_gc_handler)
    conn.ctx = ctx

    return setmetatable(conn, Conn)
end


function Conn:close()
    if not self.ctx then
        return
    end

    local r = get_request()
    local ctx = self.ctx
    self.ctx = nil
    C.ngx_grpc_cli_close(ctx, r, is_http)
end


local function attach_metadata(opt, opt_ptr)
    if type(opt.metadata) ~= "table" then
        return
    end

    opt_ptr.metadata_len = #opt.metadata
    opt_ptr.metadata = ffi.new("Metadata[?]", #opt.metadata)
    for i, pair in ipairs(opt.metadata) do
        local md = opt_ptr.metadata[i-1]
        md.key = ffi.cast("char*", pair[1])
        md.key_len = #pair[1]
        md.value = ffi.cast("char*", pair[2])
        md.value_len = #pair[2]
    end
end


local function call_with_pb_state(r, ctx, m, path, req, opt)
    local opt_buf = ffi.new("CallOpt[1]")
    local opt_ptr = opt_buf[0]

    if opt.timeout and opt.timeout > 0 then
        opt_ptr.timeout = opt.timeout
    else
        opt_ptr.timeout = 60 * 1000
    end

    attach_metadata(opt, opt_ptr)

    local ok, encoded = load_state(opt, pb.encode, m.input_type, req)
    if not ok then
        return nil, "failed to encode: " .. encoded
    end

    err_len[0] = ERR_BUF_SIZE
    local resp_or_err

    local blocking = ngx.get_phase() == "init_worker"
    local rc = C.ngx_grpc_cli_call(err_buf, err_len, r, is_http,
                                   ctx, path, #path, encoded, #encoded, opt_buf,
                                   blocking and blocking_call_output or nil)
    if rc ~= NGX_OK then
        local err = ffi.string(err_buf, err_len[0])
        return nil, "failed to call: " .. err
    end

    if blocking then
        resp_or_err = ffi.string(blocking_call_output[0].data, blocking_call_output[0].len)
        blocking_call_output[0].len = 0
        C.ngx_grpc_cli_free_buf(blocking_call_output[0].data)

    else
        ok, resp_or_err = coroutine._yield()
        if not ok then
            return nil, "failed to call: " .. resp_or_err
        end
    end

    local ok, decoded = load_state(opt, pb.decode, m.output_type, resp_or_err)
    if not ok then
        return nil, "failed to decode: " .. decoded
    end

    return decoded
end


function Conn:call(service, method, req, opt)
    if protoc_inst == nil then
        return nil, "proto files not loaded"
    end

    if self.ctx == nil then
        return nil, "closed"
    end

    local r = get_request()

    local serv = protoc_inst.index[service]
    if not serv then
        return nil, string.format("service %s not found", service)
    end

    local m = serv[method]
    if not m then
        return nil, string.format("method %s not found", method)
    end

    if not opt then
        opt = {}
    end

    local path = string.format("/%s/%s", service, method)

    local res, err = call_with_pb_state(r, self.ctx, m, path, req, opt)

    if not res then
        return nil, err
    end

    return res
end


local function stream_gc_handler(ctx)
    C.ngx_grpc_cli_close_stream(ctx, nil, is_http)
end


local function new_stream(self, service, method, req, opt, stream_type)
    if protoc_inst == nil then
        return nil, "proto files not loaded"
    end

    if self.ctx == nil then
        return nil, "closed"
    end

    local ctx = self.ctx

    local r = get_request()

    local serv = protoc_inst.index[service]
    if not serv then
        return nil, string.format("service %s not found", service)
    end

    local m = serv[method]
    if not m then
        return nil, string.format("method %s not found", method)
    end

    local path = string.format("/%s/%s", service, method)

    if not opt then
        opt = {}
    end

    local opt_buf = ffi.new("CallOpt[1]")
    local opt_ptr = opt_buf[0]

    if opt.timeout and opt.timeout > 0 then
        opt_ptr.timeout = opt.timeout
    else
        -- This timeout applies to the whole lifetime of the stream
        -- To make the implementation simple, we set the timeout at operation level in Nginx
        -- but set it for the whole lifetime in the grpc engine
        opt_ptr.timeout = 60 * 1000
    end

    attach_metadata(opt, opt_ptr)

    local ok, encoded = load_state(opt, pb.encode, m.input_type, req)
    if not ok then
        return nil, "failed to encode: " .. encoded
    end

    err_len[0] = ERR_BUF_SIZE
    local stream_ctx = C.ngx_grpc_cli_new_stream(err_buf, err_len, r, is_http,
                                                 ctx, path, #path,
                                                 encoded, #encoded,
                                                 opt_buf, stream_type)
    if stream_ctx == nil then
        local err = ffi.string(err_buf, err_len[0])
        return nil, "failed to new stream: " .. err
    end

    local ok, err = coroutine._yield()
    if not ok then
        return nil, "failed to new stream: " .. err
    end

    local stream = {
        ctx = stream_ctx,
        input_type = m.input_type,
        output_type = m.output_type,
        opt = opt,
        opt_buf = opt_buf,
    }
    ffi.gc(stream_ctx, stream_gc_handler)

    if stream_type == gRPCServerStreamType then
        return setmetatable(stream, ServerStream)
    elseif stream_type == gRPCClientStreamType then
        return setmetatable(stream, ClientStream)
    else
        return setmetatable(stream, BidirectionalStream)
    end
end


function Conn:new_client_stream(service, method, req, opt)
    return new_stream(self, service, method, req, opt, gRPCClientStreamType)
end


function Conn:new_server_stream(service, method, req, opt)
    return new_stream(self, service, method, req, opt, gRPCServerStreamType)
end


function Conn:new_bidirectional_stream(service, method, req, opt)
    return new_stream(self, service, method, req, opt, gRPCBidirectionalStreamType)
end


local function stream_close(self)
    if not self.ctx then
        return
    end

    local r = get_request()
    local ctx = self.ctx
    self.ctx = nil
    C.ngx_grpc_cli_close_stream(ctx, r, is_http)
end


local function stream_recv(self)
    if self.ctx == nil then
        return nil, "closed"
    end

    local ctx = self.ctx
    local r = get_request()

    err_len[0] = ERR_BUF_SIZE
    local rc = C.ngx_grpc_cli_stream_recv(err_buf, err_len, r, is_http, ctx, self.opt_buf)
    if rc ~= NGX_OK then
        local err = ffi.string(err_buf, err_len[0])
        return nil, "failed to recv: " .. err
    end

    local ok, resp_or_err = coroutine._yield()
    if not ok then
        return nil, "failed to recv: " .. resp_or_err
    end

    local ok, decoded = load_state(self.opt, pb.decode, self.output_type, resp_or_err)
    if not ok then
        return nil, "failed to decode: " .. decoded
    end

    return decoded
end


local function stream_send(self, req)
    if self.ctx == nil then
        return nil, "closed"
    end

    local ctx = self.ctx
    local r = get_request()

    local ok, encoded = load_state(self.opt, pb.encode, self.input_type, req)
    if not ok then
        return nil, "failed to encode: " .. encoded
    end

    err_len[0] = ERR_BUF_SIZE
    local rc = C.ngx_grpc_cli_stream_send(err_buf, err_len, r, is_http, ctx, self.opt_buf,
                                          encoded, #encoded)
    if rc ~= NGX_OK then
        local err = ffi.string(err_buf, err_len[0])
        return nil, "failed to send: " .. err
    end

    local ok, err = coroutine._yield()
    if not ok then
        return nil, "failed to send: " .. err
    end

    return ok
end


local function stream_close_send(self, req)
    if self.ctx == nil then
        return nil, "closed"
    end

    local ctx = self.ctx
    local r = get_request()

    err_len[0] = ERR_BUF_SIZE
    local rc = C.ngx_grpc_cli_stream_close_send(err_buf, err_len, r, is_http,
                                                ctx, self.opt_buf)
    if rc ~= NGX_OK then
        local err = ffi.string(err_buf, err_len[0])
        return nil, "failed to close send: " .. err
    end

    local ok, err = coroutine._yield()
    if not ok then
        return nil, "failed to close send: " .. err
    end

    return ok
end


local function stream_recv_close(self)
    local res, err = stream_recv(self)
    if not res then
        return nil, err
    end

    stream_close(self)

    return res
end


ServerStream.close = stream_close
ServerStream.recv = stream_recv

ClientStream.close = stream_close
ClientStream.send = stream_send
ClientStream.recv_close = stream_recv_close

BidirectionalStream.close = stream_close
BidirectionalStream.send = stream_send
BidirectionalStream.close_send = stream_close_send
BidirectionalStream.recv = stream_recv


return _M
