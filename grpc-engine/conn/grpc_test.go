package conn

import "testing"

func TestConnLifeCycle(t *testing.T) {
	conn, err := Connect("localhost:2379", &ConnectOption{})
	if err != nil {
		t.FailNow()
	}
	Close(conn)
}
