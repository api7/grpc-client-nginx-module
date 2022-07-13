package main

/*
#cgo LDFLAGS: -shared -ldl -lpthread
#include <stdlib.h>
*/
import "C"
import (
	"unsafe"

	"google.golang.org/grpc"

	"github.com/api7/grpc-client-nginx-module/conn"
)

func main() {
}

type EngineCtx struct {
	c *grpc.ClientConn
}

var EngineCtxRef = map[unsafe.Pointer]*EngineCtx{}

//export grpc_engine_connect
func grpc_engine_connect() unsafe.Pointer {
	// A Go function called by C code may not return a Go pointer
	var ref unsafe.Pointer = C.malloc(C.size_t(1))
	if ref == nil {
		panic("no memory")
	}

	ctx := EngineCtx{}
	EngineCtxRef[ref] = &ctx

	ctx.c = conn.Connect()

	return ref
}

//export grpc_engine_close
func grpc_engine_close(ref unsafe.Pointer) {
	ctx := EngineCtxRef[ref]
	conn.Close(ctx.c)

	delete(EngineCtxRef, ref)
	C.free(ref)
}
