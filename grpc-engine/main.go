package main

// create the engine with:
// go build -o libgrpc_engine.a -buildmode=c-archive main.go

/*
#cgo LDFLAGS: -shared -ldl
*/
import "C"
import "fmt"

func main() {
}

//export nothing
func nothing() {
	fmt.Printf("%+v\n", "AA")
}
