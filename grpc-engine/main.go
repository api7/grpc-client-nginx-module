package main

/*
#cgo LDFLAGS: -shared -ldl -lpthread
#include <stdbool.h>
#include <stdlib.h>

typedef struct DialOpt {
    bool insecure;
} DialOpt;
*/
import "C"
import (
	"errors"
	"log"
	"os"
	"unsafe"

	"google.golang.org/grpc"

	"github.com/api7/grpc-client-nginx-module/conn"
	"github.com/api7/grpc-client-nginx-module/task"
)

func main() {
}

func init() {
	// only keep the latest debug log
	f, err := os.OpenFile("/tmp/grpc-engine-debug.log", os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0644)
	if err != nil {
		panic(err)
	}
	log.Default().SetOutput(f)
}

const (
	// buffer size allocated in the grpc-client-nginx-module
	ERR_BUF_SIZE = 512
)

type EngineCtx struct {
	c *grpc.ClientConn
}

var EngineCtxRef = map[unsafe.Pointer]*EngineCtx{}

func reportErr(err error, errBuf unsafe.Pointer, errLen *C.size_t) {
	s := err.Error()
	if len(s) > ERR_BUF_SIZE-1 {
		s = s[:ERR_BUF_SIZE-1]
	}

	pp := (*[1 << 30]byte)(errBuf)
	copy(pp[:], s)
	*errLen = C.size_t(len(s))
}

//export grpc_engine_connect
func grpc_engine_connect(errBuf unsafe.Pointer, errLen *C.size_t,
	targetData unsafe.Pointer, targetLen C.int, opt *C.struct_DialOpt) unsafe.Pointer {

	target := string(C.GoBytes(targetData, targetLen))

	co := &conn.ConnectOption{
		Insecure: bool(opt.insecure),
	}
	c, err := conn.Connect(target, co)
	if err != nil {
		reportErr(err, errBuf, errLen)
		return nil
	}

	ctx := EngineCtx{}
	ctx.c = c

	// A Go function called by C code may not return a Go pointer
	var ref unsafe.Pointer = C.malloc(C.size_t(1))
	if ref == nil {
		reportErr(errors.New("no memory"), errBuf, errLen)
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

//export grpc_engine_call
func grpc_engine_call(errBuf unsafe.Pointer, errLen *C.size_t,
	taskId C.long, ref unsafe.Pointer,
	methodData unsafe.Pointer, methodLen C.int,
	reqData unsafe.Pointer, reqLen C.int,
) {
	method := string(C.GoBytes(methodData, methodLen))
	req := C.GoBytes(reqData, reqLen)
	ctx := EngineCtxRef[ref]
	c := ctx.c

	go func() {
		out, err := conn.Call(c, method, req)
		if err != nil {
			reportErr(err, errBuf, errLen)
		}

		task.ReportFinishedTask(uint64(taskId), out)
	}()
}

//export grpc_engine_free
func grpc_engine_free(ptr unsafe.Pointer) {
	C.free(ptr)
}

//export grpc_engine_wait
func grpc_engine_wait(taskNum *C.int) unsafe.Pointer {
	out, n := task.WaitFinishedTasks()
	*taskNum = C.int(n)
	return C.CBytes(out)
}
