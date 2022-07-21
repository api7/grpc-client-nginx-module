package task

import "C"
import (
	"encoding/binary"
	"sync"
	"unsafe"
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

func (self *taskQueue) Wait() ([]byte, int) {
	self.cond.L.Lock()

	for len(self.tasks) == 0 {
		self.cond.Wait()
	}

	out := self.tasks
	self.tasks = []byte{}
	self.cond.L.Unlock()

	return out, len(out) / 24
}

func (self *taskQueue) Done(id uint64, result []byte) {
	var size uint64
	var ptrRes uintptr

	if result != nil {
		size = uint64(len(result))
		ptrRes = uintptr(C.CBytes(result))
	}

	self.cond.L.Lock()

	hostEndian.PutUint64(self.taskIdBuf, id)
	hostEndian.PutUint64(self.taskIdBuf[8:16], size)
	hostEndian.PutUint64(self.taskIdBuf[16:], uint64(ptrRes))
	self.tasks = append(self.tasks, self.taskIdBuf...)
	self.cond.Signal()
	self.cond.L.Unlock()
}

var (
	finishedTaskQueue = NewTaskQueue()
)

func WaitFinishedTasks() ([]byte, int) {
	return finishedTaskQueue.Wait()
}

func ReportFinishedTask(id uint64, result []byte) {
	finishedTaskQueue.Done(id, result)
}
