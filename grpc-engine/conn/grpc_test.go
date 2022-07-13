package conn

import "testing"

func TestConnLifeCycle(t *testing.T) {
	conn := Connect()
	Close(conn)
}
