package task

import "C"
import (
	"encoding/binary"
	"sync"
	"time"
	"unsafe"
)

const (
	GrpcResTypeOk       = 0
	GrpcResTypeErr      = 1
	GrpcResTypeOkNotRes = 2
)

var hostEndian binary.ByteOrder

func init() {
	buf := [2]byte{}
	*(*uint16)(unsafe.Pointer(&buf[0])) = uint16(0xABCD)

	switch buf {
	case [2]byte{0xCD, 0xAB}:
		hostEndian = binary.LittleEndian
	case [2]byte{0xAB, 0xCD}:
		hostEndian = binary.BigEndian
	default:
		panic("Could not determine host endianness.")
	}
}

type taskQueue struct {
	cond *sync.Cond

	done bool

	taskIdBuf []byte
	tasks     []byte
}

func NewTaskQueue() *taskQueue {
	lock := &sync.Mutex{}
	cond := sync.NewCond(lock)
	return &taskQueue{
		cond:      cond,
		taskIdBuf: make([]byte, 24),
		tasks:     []byte{},
	}
}

func (self *taskQueue) signal() {
	self.done = true
	self.cond.Signal()
}

func (self *taskQueue) Wait(timeout time.Duration) ([]byte, int) {
	timer := time.AfterFunc(timeout, func() {
		self.cond.L.Lock()
		self.signal()
		self.cond.L.Unlock()
	})

	self.cond.L.Lock()

	for !self.done {
		self.cond.Wait()
	}

	self.done = false
	timer.Stop()

	out := self.tasks
	self.tasks = []byte{}
	self.cond.L.Unlock()

	return out, len(out) / 24
}

func (self *taskQueue) Done(id uint64, result []byte, err error) {
	var size uint64
	var ptrRes uintptr

	if err != nil {
		errStr := err.Error()
		size = uint64(len(errStr))
		ptrRes = uintptr(C.CBytes([]byte(errStr))) | GrpcResTypeErr
	} else if result == nil {
		size = 0
		ptrRes = GrpcResTypeOkNotRes
	} else {
		size = uint64(len(result))
		ptrRes = uintptr(C.CBytes(result))
	}

	self.cond.L.Lock()

	hostEndian.PutUint64(self.taskIdBuf, id)
	hostEndian.PutUint64(self.taskIdBuf[8:16], size)
	hostEndian.PutUint64(self.taskIdBuf[16:], uint64(ptrRes))

	self.tasks = append(self.tasks, self.taskIdBuf...)

	self.signal()
	self.cond.L.Unlock()
}

var (
	finishedTaskQueue = NewTaskQueue()
)

func WaitFinishedTasks(timeout time.Duration) ([]byte, int) {
	return finishedTaskQueue.Wait(timeout)
}

func ReportFinishedTask(id uint64, result []byte, err error) {
	finishedTaskQueue.Done(id, result, err)
}

func EncodePointerToBuf(ptr unsafe.Pointer) []byte {
	ptrRes := uintptr(ptr)
	buf := make([]byte, 8)
	hostEndian.PutUint64(buf, uint64(ptrRes))
	return buf
}
