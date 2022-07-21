local protoc = require("protoc")
local pb = require("pb")
local base = require("resty.core.base")
local get_request = base.get_request
local ffi = require("ffi")
local C = ffi.C
local NGX_OK = ngx.OK


ffi.cdef[[
int
ngx_http_grpc_cli_is_engine_inited(void);
void *
ngx_http_grpc_cli_connect(unsigned char *err_buf, size_t *err_len,
                          ngx_http_request_t *r,
                          const char *target_data, int target_len);
void
ngx_http_grpc_cli_close(void *ctx, int gc);
int
ngx_http_grpc_cli_call(unsigned char *err_buf, size_t *err_len,
                       void *ctx,
                       const char *method_data, int method_len,
                       const char *req_data, int req_len);
void
ngx_http_grpc_cli_free(void *data);
]]

if C.ngx_http_grpc_cli_is_engine_inited() == 0 then
    error("The gRPC client engine is not initialized. " ..
          "Need to configure 'grpc_client_engine_path' in the nginx.conf. " ..
          "And this library can not be loaded in the init phase."
          )
end


local _M = {}
local Conn = {}
local mt = {__index = Conn}

local protoc_inst
local current_pb_state

local ERR_BUF_SIZE = 512
local err_buf = ffi.new("char[?]", ERR_BUF_SIZE)
local err_len = ffi.new("size_t[1]")


function _M.load(filename)
    if not protoc_inst then
        -- initialize protoc compiler
        pb.state(nil)
        protoc.reload()
        protoc_inst = protoc.new()
        protoc_inst.index = {}
        current_pb_state = pb.state(nil)
    end

    pb.state(current_pb_state)
    local ok, err = pcall(protoc_inst.loadfile, protoc_inst, filename)
    if not ok then
        return nil, "failed to load protobuf: " .. err
    end

    local index = protoc_inst.index
    for _, s in ipairs(protoc_inst.loaded[filename].service or {}) do
        local method_index = {}
        for _, m in ipairs(s.method) do
            method_index[m.name] = m
        end
        index[protoc_inst.loaded[filename].package .. '.' .. s.name] = method_index
    end

    current_pb_state = pb.state(nil)
    return true
end


local function ctx_gc_handler(ctx)
    C.ngx_http_grpc_cli_close(ctx, 1)
end


function _M.connect(target)
    local conn = {}
    local r = get_request()

    err_len[0] = ERR_BUF_SIZE
    -- grpc-go dials the target in non-blocking way
    local ctx = C.ngx_http_grpc_cli_connect(err_buf, err_len, r, target, #target)
    if ctx == nil then
        local err = ffi.string(err_buf, err_len[0])
        return nil, err
    end
    ffi.gc(ctx, ctx_gc_handler)
    conn.ctx = ctx

    return setmetatable(conn, mt)
end


function Conn:close()
    if not self.ctx then
        return
    end

    local ctx = self.ctx
    self.ctx = nil
    ffi.gc(ctx, nil)
    C.ngx_http_grpc_cli_close(ctx, 0)
end


local function call_with_pb_state(ctx, m, path, req)
    pb.state(current_pb_state)
    local ok, encoded = pcall(pb.encode, m.input_type, req)
    pb.state(nil)
    if not ok then
        return nil, "failed to encode: " .. encoded
    end

    err_len[0] = ERR_BUF_SIZE
    local rc = C.ngx_http_grpc_cli_call(err_buf, err_len, ctx, path, #path, encoded, #encoded)
    if rc ~= NGX_OK then
        local err = ffi.string(err_buf, err_len[0])
        return nil, "failed to call: " .. err
    end

    local ok, resp = coroutine._yield()
    if not ok then
        local err = ffi.string(err_buf, err_len[0])
        return nil, "failed to call: " .. err
    end

    pb.state(current_pb_state)
    local ok, decoded = pcall(pb.decode, m.output_type, resp)
    pb.state(nil)
    if not ok then
        return nil, "failed to decode: " .. decoded
    end

    return decoded
end


function Conn:call(service, method, req)
    if protoc_inst == nil then
        return nil, "proto files not loaded"
    end

    if self.ctx == nil then
        return nil, "closed"
    end

    local serv = protoc_inst.index[service]
    if not serv then
        return nil, string.format("service %s not found", service)
    end

    local m = serv[method]
    if not m then
        return nil, string.format("method %s not found", method)
    end

    local path = string.format("/%s/%s", service, method)

    local res, err = call_with_pb_state(self.ctx, m, path, req)

    if not res then
        return nil, err
    end

    return res
end


return _M
