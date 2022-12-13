// Copyright 2022 Shenzhen ZhiLiu Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

type taskResult struct {
	id     uint64
	result []byte
	err    error
}

type taskQueue struct {
	cond *sync.Cond

	done bool

	taskIdBuf []byte
	tasks     []*taskResult
}

func NewTaskQueue() *taskQueue {
	lock := &sync.Mutex{}
	cond := sync.NewCond(lock)
	return &taskQueue{
		cond:      cond,
		taskIdBuf: make([]byte, 24),
		tasks:     []*taskResult{},
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

	out := []byte{}

	for _, res := range self.tasks {
		var size uint64
		var ptrRes uintptr

		if res.err != nil {
			errStr := res.err.Error()
			size = uint64(len(errStr))
			ptrRes = uintptr(C.CBytes([]byte(errStr))) | GrpcResTypeErr
		} else if res.result == nil {
			size = 0
			ptrRes = GrpcResTypeOkNotRes
		} else {
			size = uint64(len(res.result))
			ptrRes = uintptr(C.CBytes(res.result))
		}

		hostEndian.PutUint64(self.taskIdBuf, res.id)
		hostEndian.PutUint64(self.taskIdBuf[8:16], size)
		hostEndian.PutUint64(self.taskIdBuf[16:], uint64(ptrRes))
		out = append(out, self.taskIdBuf...)
	}

	self.tasks = []*taskResult{}
	self.cond.L.Unlock()

	return out, len(out) / 24
}

func (self *taskQueue) Done(id uint64, result []byte, err error) {
	self.cond.L.Lock()

	self.tasks = append(self.tasks, &taskResult{
		id:     id,
		result: result,
		err:    err,
	})

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
