package main

import "C"
import "time"

//export gosoMainLoop
func gosoMainLoop() {
	for {
		time.Sleep(time.Second)
	}
}

func main() {}
