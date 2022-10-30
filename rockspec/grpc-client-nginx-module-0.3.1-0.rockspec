package = "grpc-client-nginx-module"
version = "0.3.1-0"
source = {
    url = "git://github.com/api7/grpc-client-nginx-module",
    branch = "v0.3.1",
}

description = {
    summary = "Call gRPC service in Nginx",
    homepage = "https://github.com/api7/grpc-client-nginx-module",
    license = "Apache License 2.0",
    maintainer = "Yuansheng Wang <membphis@gmail.com>"
}

dependencies = {
    "lua-protobuf = 0.3.4",
}


build = {
    type = "builtin",
    modules = {
        ["resty.grpc"] = "lib/resty/grpc.lua",
    }
}
