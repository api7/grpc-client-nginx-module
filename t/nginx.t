use t::GRPC_CLI 'no_plan';

run_tests();

__DATA__

=== TEST 1: lua-resty-shell
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))
        local shell = require("resty.shell")
        local stdin = "hello"
        local timeout = 1000  -- ms
        local max_size = 4096  -- byte

        local ok, stdout, stderr, reason, status =
            shell.run([[perl -e 'warn "he\n"; print <>']], stdin, timeout, max_size)
        if not ok then
            ngx.say(stderr)
            return
        end

        local conn = assert(gcli.connect("127.0.0.1:2379"))
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        local old = res.header.revision
        local res = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'c'})
        ngx.say(res.header.revision - old)
        conn:close()
    }
}
--- response_body
1
