package main

/*
#cgo LDFLAGS: -shared -ldl -lpthread
#include <stdlib.h>
*/
import "C"
import (
	"errors"
	"unsafe"

	"google.golang.org/grpc"

	"github.com/api7/grpc-client-nginx-module/conn"
)

func main() {
}

const (
	// buffer size allocated in the grpc-client-nginx-module
	ERR_BUF_SIZE = 512
)

type EngineCtx struct {
	c *grpc.ClientConn
}

var EngineCtxRef = map[unsafe.Pointer]*EngineCtx{}

func reportErr(err error, errBuf *C.char) {
	s := err.Error()
	if len(s) > ERR_BUF_SIZE-1 {
		s = s[:ERR_BUF_SIZE-1]
	}

	pp := (*[1 << 30]byte)(unsafe.Pointer(errBuf))
	copy(pp[:], s)
	pp[len(s)] = 0
}

//export grpc_engine_connect
func grpc_engine_connect(errBuf *C.char) unsafe.Pointer {
	c, err := conn.Connect()
	if err != nil {
		reportErr(err, errBuf)
		return nil
	}

	ctx := EngineCtx{}
	ctx.c = c

	// A Go function called by C code may not return a Go pointer
	var ref unsafe.Pointer = C.malloc(C.size_t(1))
	if ref == nil {
		reportErr(errors.New("no memory"), errBuf)
		return nil
	}

	EngineCtxRef[ref] = &ctx
	return ref
}

//export grpc_engine_close
func grpc_engine_close(ref unsafe.Pointer) {
	ctx := EngineCtxRef[ref]
	conn.Close(ctx.c)

	delete(EngineCtxRef, ref)
	C.free(ref)
}
