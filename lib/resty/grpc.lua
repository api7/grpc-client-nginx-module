local protoc = require("protoc")
local pb = require("pb")


local _M = {}
local protoc_inst
local current_pb_state


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


local function _stub(encoded, m)
    local ok, encoded = pcall(pb.encode, m.output_type, {header = {revision = 1}})
    assert(ok)
    return encoded
end


local function call_with_pb_state(m, req)
    local ok, encoded = pcall(pb.encode, m.input_type, req)
    if not ok then
        return nil, "failed to encode: " .. encoded
    end

    local mock = _stub(encoded, m)
    local ok, decoded = pcall(pb.decode, m.output_type, mock)
    if not ok then
        return nil, "failed to decode: " .. decoded
    end

    return decoded
end


function _M.call(service, method, req)
    local serv = protoc_inst.index[service]
    if not serv then
        return nil, string.format("service %s not found", service)
    end

    local m = serv[method]
    if not m then
        return nil, string.format("method %s not found", method)
    end

    pb.state(current_pb_state)
    local res, err = call_with_pb_state(m, req)
    pb.state(nil)

    if not res then
        return nil, err
    end

    return res
end


return _M
