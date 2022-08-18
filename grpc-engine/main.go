package main

/*
#cgo LDFLAGS: -shared -ldl -lpthread
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct DialOpt {
    bool insecure;
    bool tls_verify;
} DialOpt;

typedef uintptr_t ngx_msec_t;
typedef struct CallOpt {
    ngx_msec_t timeout;
} CallOpt;
*/
import "C"
import (
	"errors"
	"log"
	"os"
	"time"
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
	log.Default().SetFlags(log.Ldate | log.Ltime | log.Lmicroseconds | log.Lshortfile)
}

const (
	// buffer size allocated in the grpc-client-nginx-module
	ERR_BUF_SIZE = 512
)

type EngineCtx struct {
	c *grpc.ClientConn
}

var EngineCtxRef = map[unsafe.Pointer]*EngineCtx{}
var StreamRef = map[unsafe.Pointer]*conn.Stream{}

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
		Insecure:  bool(opt.insecure),
		TLSVerify: bool(opt.tls_verify),
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
	opt *C.struct_CallOpt,
) {
	method := string(C.GoBytes(methodData, methodLen))
	req := C.GoBytes(reqData, reqLen)
	ctx := EngineCtxRef[ref]
	c := ctx.c
	co := &conn.CallOption{
		Timeout: time.Duration(opt.timeout) * time.Millisecond,
	}

	go func() {
		out, err := conn.Call(c, method, req, co)
		task.ReportFinishedTask(uint64(taskId), out, err)
	}()
}

//export grpc_engine_new_stream
func grpc_engine_new_stream(errBuf unsafe.Pointer, errLen *C.size_t,
	sctx unsafe.Pointer, ref unsafe.Pointer,
	methodData unsafe.Pointer, methodLen C.int,
	reqData unsafe.Pointer, reqLen C.int,
	opt *C.struct_CallOpt, streamType C.int,
) {
	method := string(C.GoBytes(methodData, methodLen))
	req := C.GoBytes(reqData, reqLen)
	ctx := EngineCtxRef[ref]
	c := ctx.c
	co := &conn.CallOption{
		Timeout: time.Duration(opt.timeout) * time.Millisecond,
	}

	go func() {
		s, err := conn.NewStream(c, method, req, co, int(streamType))
		if err != nil {
			task.ReportFinishedTask(uint64(uintptr(sctx)), nil, err)
			return
		}

		StreamRef[sctx] = s
		task.ReportFinishedTask(uint64(uintptr(sctx)), nil, nil)
	}()
}

//export grpc_engine_close_stream
func grpc_engine_close_stream(sctx unsafe.Pointer) {
	s, found := StreamRef[sctx]
	if !found {
		// stream is already closed
		return
	}
	delete(StreamRef, sctx)
	s.Close()
}

//export grpc_engine_stream_recv
func grpc_engine_stream_recv(sctx unsafe.Pointer) {
	s := StreamRef[sctx]

	go func() {
		out, err := s.Recv()
		task.ReportFinishedTask(uint64(uintptr(sctx)), out, err)
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
