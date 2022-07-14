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
ngx_http_grpc_cli_connect(char *err_buf, ngx_http_request_t *r,
                          const char *target_data, int target_len);
void
ngx_http_grpc_cli_close(ngx_http_request_t *r, void *ctx);
int
ngx_http_grpc_cli_call(char *err_buf, void *ctx,
                       const char *method_data, int method_len,
                       const char *req_data, int req_len,
                       ngx_str_t *resp);
void
ngx_http_grpc_cli_free(void *data);
]]

if C.ngx_http_grpc_cli_is_engine_inited() == 0 then
    error("The gRPC client engine is not initialized. " ..
          "Need to configure 'grpc_client_engine_path' in the nginx.conf.")
end


local _M = {}
local Conn = {}
local mt = {__index = Conn}

local protoc_inst
local current_pb_state

local err_buf = ffi.new("char[512]")
local str_buf = ffi.new("ngx_str_t[1]")


function _M.load(path, filename)
    if not protoc_inst then
        -- initialize protoc compiler
        pb.state(nil)
        protoc.reload()
        protoc_inst = protoc.new()
        protoc_inst.index = {}
        current_pb_state = pb.state(nil)
    end

    pb.state(current_pb_state)
    protoc_inst:addpath(path)
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
    C.ngx_http_grpc_cli_close(nil, ctx)
end


function _M.connect(target)
    local conn = {}
    local r = get_request()
    conn.r = r

    -- grpc-go dials the target in non-blocking way
    local ctx = C.ngx_http_grpc_cli_connect(err_buf, r, target, #target)
    if ctx == nil then
        return nil, ffi.string(err_buf)
    end
    ffi.gc(ctx, ctx_gc_handler)
    conn.ctx = ctx

    return setmetatable(conn, mt)
end


function Conn:close()
    local r = self.r

    local ctx = self.ctx
    if not self.ctx then
        return
    end

    self.ctx = nil
    ffi.gc(ctx, nil)
    C.ngx_http_grpc_cli_close(r, ctx)
end


local function call_with_pb_state(ctx, m, path, req)
    local ok, encoded = pcall(pb.encode, m.input_type, req)
    if not ok then
        return nil, "failed to encode: " .. encoded
    end

    local rc = C.ngx_http_grpc_cli_call(err_buf, ctx, path, #path, encoded, #encoded, str_buf)
    if rc ~= NGX_OK then
        local err = ffi.string(err_buf)
        return nil, "failed to call: " .. err
    end

    local str = str_buf[0]
    local resp = ffi.string(str.data, str.len)
    local ok, decoded = pcall(pb.decode, m.output_type, resp)
    C.ngx_http_grpc_cli_free(ffi.cast("void*", str.data))
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

    pb.state(current_pb_state)
    local res, err = call_with_pb_state(self.ctx, m, path, req)
    pb.state(nil)

    if not res then
        return nil, err
    end

    return res
end


return _M
