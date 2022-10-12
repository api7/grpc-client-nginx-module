use t::GRPC_CLI 'no_plan';

run_tests();

__DATA__

=== TEST 1: mtls ok
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:22379",
            {
                tls_verify = false,
                insecure = false,
                client_cert = "t/certs/mtls_client.crt",
                client_key = "t/certs/mtls_client.key"
            })
        )
        local res, err = conn:call("etcdserverpb.KV", "Put", {key = 'k', value = 'v'})
        if err then
            return ngx.say(err)
        end
        conn:close()
        ngx.say("ok")
    }
}
--- response_body
ok



=== TEST 2: mtls missing key
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn, err = gcli.connect("127.0.0.1:22379",
            {
                tls_verify = false,
                insecure = false,
                client_cert = "t/certs/mtls_client.crt"
            }
        )
        if err then
            return ngx.say(err)
        end
        conn:close()
        ngx.say("ok")
    }
}
--- response_body
ClientKeyFile and ClientCertFile must both be present or both absent: key: , cert: t/certs/mtls_client.crt



=== TEST 3: mtls cert not exits
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn, err = gcli.connect("127.0.0.1:22379",
            {
                tls_verify = false,
                insecure = false,
                client_cert = "tt/certs/mtls_client.crt",
                client_key = "t/certs/mtls_client.key"
            }
        )
        if err then
            return ngx.say(err)
        end
        conn:close()
        ngx.say("ok")
    }
}
--- response_body
open tt/certs/mtls_client.crt: no such file or directory
