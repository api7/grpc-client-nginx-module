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
                tls_verify = true,
                insecure = false,
                client_cert = "t/certs/client.crt",
                client_key = "t/certs/client.key",
                trusted_ca = "t/certs/ca.crt"
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
                tls_verify = true,
                insecure = false,
                client_cert = "t/certs/client.crt"
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
client_cert and client_key must both be present or both absent: cert: t/certs/client.crt key: 



=== TEST 3: mtls cert not exits
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn, err = gcli.connect("127.0.0.1:22379",
            {
                tls_verify = true,
                insecure = false,
                client_cert = "tt/certs/client.crt",
                client_key = "t/certs/client.key"
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
open tt/certs/client.crt: no such file or directory



=== TEST 4: mtls cert incorrect cert format
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn, err = gcli.connect("127.0.0.1:22379",
            {
                tls_verify = true,
                insecure = false,
                client_cert = "t/certs/incorrect.crt",
                client_key = "t/certs/incorrect.key"
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
tls: failed to find any PEM data in certificate input



=== TEST 5: mtls wrong client certificate
--- config
location /t {
    content_by_lua_block {
        local gcli = require("resty.grpc")
        assert(gcli.load("t/testdata/rpc.proto"))

        local conn = assert(gcli.connect("127.0.0.1:22379",
            {
                tls_verify = true,
                insecure = false,
                client_cert = "t/certs/client.crt",
                client_key = "t/certs/client.key",
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
failed to call: rpc error: code = Unavailable desc = connection error: desc = "transport: authentication handshake failed: x509: certificate signed by unknown authority"
