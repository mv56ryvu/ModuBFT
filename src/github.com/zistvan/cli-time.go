package main

import (
	"crypto/rand"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
	"time"

	n "github.com/zistvan/node"
	pb "github.com/zistvan/proto"
)

func main() {

	/* the Command Line Interface  can be used to start all nodes of a network, both peers and clients.

	It takes the following arguments:
	1 - nodeID, start nodes from zero
	2 - list of nodes defined by IP addresses, seprarated by comma. The list is the same for all. The location in the list == nodeID
	3 - list of base ports of nodes, seprarated by comma. The list is the same for all. The location in the list == nodeID
	4 - role of each node, separeted by comma. ROLE constants defined in Node.
	5 - wether the node should offload functionality to a connected dpu
	6 - length of client requests in Bytes. This is only needed when starting a client
	7 - optional address of DPU connected to the leader node, if present
	8 - optional batch factor (64)
	9 - optional runtime (default 10s)
	10 - optional print tput reading live (default 0)
	*/

	myId, _ := strconv.Atoi(os.Args[1])
	nodeList := strings.Split(os.Args[2], ",")
	portList := strings.Split(os.Args[3], ",")
	nodeRoleS := strings.Split(os.Args[4], ",")
	offload, _ := strconv.ParseBool(os.Args[5])
	nodeRole := make([]int, len(nodeRoleS))
	for ind, elem := range nodeRoleS {
		nodeRole[ind], _ = strconv.Atoi(elem)
	}

	dpuAddress := ""
	batchF := 64
	runTime := 10
	showLiveTput := 0

	if len(os.Args) > 7 {
		dpuAddress = os.Args[7]
	}

	if len(os.Args) > 8 {
		batchF, _ = strconv.Atoi(os.Args[8])
	}

	if len(os.Args) > 9 {
		runTime, _ = strconv.Atoi(os.Args[9])
	}

	if len(os.Args) > 10 {
		showLiveTput, _ = strconv.Atoi(os.Args[10])
	}

	portListNumbers := make([]int, len(portList))
	for i, portString := range portList {
		port, err := strconv.Atoi(portString)
		if err != nil {
			fmt.Println(err)
			return
		}
		portListNumbers[i] = port
	}

	me := &n.Node{}

	me.Initialize(myId, nodeList, portListNumbers, nodeRole, offload, dpuAddress)
	me.Run()

	// if the node is to be a client...
	if nodeRole[myId] == n.ROLE_CLIENT {

		time.Sleep(time.Millisecond * 2000)

		//create a random value of specified length
		vallen, _ := strconv.Atoi(os.Args[6])
		rtxt := make([]byte, vallen)
		if _, err := io.ReadFull(rand.Reader, rtxt); err != nil {
			fmt.Println(err)
		}

		//define the length of the experiment

		//the benchmarking client can issue at most this many outstanding requests.

		chResult := make(chan int, 1000)

		//chTimeout := make(chan int, 1000)

		//start a parallel goroutine for async message receival
		go me.ClientReceiveFromPeers(chResult)

		//send one message synchronously to generate crypto signatures -- these will be reused for benchmarking to avoid recomputation...
		_, _, rawB := me.ClientSendToLeader(rtxt, pb.SigType_PKSig)
		<-chResult

		issued := 1
		received := 1

		start := time.Now()
		elapsed := time.Since(start)
		mustStop := false

		realReceived := 0
		//msgId := 0

		lastCheckReceived := 0
		lastStepReceived := 0
		lastStepBegin := time.Now()

		for {

			for (issued - received) < batchF {
				//st := time.Now()
				//me.ClientSendToLeaderCached(rtxt, pb.SigType_PKSig, sign)

				me.ClientSendToLeaderCachedRaw(rawB)
				//nextTimeOut := issued

				/*go func(nto int) {
					time.Sleep(time.Millisecond * 500)
					chTimeout <- -1 * nto
				}(nextTimeOut)
				*/
				issued++
			}
			//fmt.Println(time.Since(st))
			//st = time.Now()
			for received < issued-batchF/2 || (received < issued && mustStop) {

				<-chResult
				received++
				realReceived++
				/*
					select {
					case msgId = <-chResult:
					case msgId = <-chTimeout:
					}

					if msgId >= 0 {
						received++
						realReceived++
					} else if msgId < 0 && (-1*msgId) >= received {
						received = (-1 * msgId) + 1
						//fmt.Printf("timeout on %d \n", msgId)
					}
				*/

			}
			//fmt.Prntln(time.Since(st))

			if issued == received && mustStop == true {
				break
			} else if received-lastCheckReceived > (batchF * 100) {

				lastCheckReceived = received

				elapsed = time.Since(start)
				if int(elapsed) > (int(time.Second) * runTime) {
					mustStop = true
				}

				if showLiveTput == 1 {
					stepElapsed := time.Since(lastStepBegin)
					if stepElapsed > time.Duration(time.Second) {
						fmt.Println((1000000000 * float32(realReceived-lastStepReceived) / float32(stepElapsed)), " (", (stepElapsed), ")")
						lastStepBegin = time.Now()
						lastStepReceived = realReceived
					}
				}
			}
		}

		elapsed = time.Since(start)
		fmt.Println(realReceived)
		fmt.Println(float32(elapsed) / 1000000000.0)
		tput := float32(realReceived) / (float32(elapsed) / 1000000000.0)
		fmt.Printf("\nThroughput [ops/s] %d\n", int(tput))
		os.Exit(0)

	}

}
