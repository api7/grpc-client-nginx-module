package task

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
	taskIds   []byte
}

func NewTaskQueue() *taskQueue {
	lock := &sync.Mutex{}
	cond := sync.NewCond(lock)
	return &taskQueue{
		cond:      cond,
		taskIdBuf: make([]byte, 8),
		taskIds:   []byte{},
	}
}

func (self *taskQueue) Wait() []byte {
	self.cond.L.Lock()

	for len(self.taskIds) == 0 {
		self.cond.Wait()
	}

	out := self.taskIds
	self.taskIds = []byte{}
	self.cond.L.Unlock()

	return out
}

func (self *taskQueue) Done(id uint64) {
	self.cond.L.Lock()

	hostEndian.PutUint64(self.taskIdBuf, id)
	self.taskIds = append(self.taskIds, self.taskIdBuf...)
	for i := range self.taskIdBuf {
		self.taskIdBuf[i] = 0
	}
	self.cond.Signal()
	self.cond.L.Unlock()
}

var (
	finishedTaskQueue = NewTaskQueue()
)

func WaitFinishedTasks() []byte {
	taskIds := finishedTaskQueue.Wait()
	return taskIds
}

func ReportFinishedTask(id uint64) {
	finishedTaskQueue.Done(id)
}
