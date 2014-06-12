import socket
import sys
import os
import binascii
import threading
import select
import shlex
import signal
import subprocess
import sys
import threading
import time

server_address = '/tmp/unixif'

http_ok = "485454502f312e3120323030204f4b0d0a446174653a204672692c203331204a616e20323031342031303a31353a323820474d540d0a5365727665723a205a6f70652f285a6f706520322e31302e392d66696e616c2c20707974686f6e20322e342e332c206c696e75783229205a5365727665722f312e310d0a436f6e74656e742d4c656e6774683a2031373334350d0a436f6e74656e742d456e636f64696e673a20677a69700d0a566172793a204163636570742d456e636f64696e670d0a582d55612d436f6d70617469626c653a2049453d656467650d0a436f6e74656e742d547970653a20746578742f68746d6c3b20636861727365743d69736f2d383835392d310d0a4b6565702d416c6976653a2074696d656f75743d31352c206d61783d3130300d0a436f6e6e656374696f6e3a204b6565702d416c6976650d0a0d0a"


def openSocket():
	socketPath = "/tmp/unixif"

	print("Connecting...")
	if os.path.exists(socketPath):
		client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		client.connect(socketPath)

		return client
	else:
		# TODO raise exception
		return None

def sigHandler(signum, frame):
    # set shutdown event
    shutdownEvent.set()


class TcpIpStackThread(threading.Thread): 
    def __init__(self): 
        threading.Thread.__init__(self) 
        self.cmd    = "/home/fhau/university/bsc_hdm_medieninformatik/thesis/lwip_upper/lwip-contrib/ports/unix/proj/minimal/echop"
			
    def run(self):
        print("TCP/IP Thread starting...")

        args = shlex.split(self.cmd)
		
        self.p = subprocess.Popen(args)

        while shutdownEvent.is_set() == False:
            time.sleep(0.1)

        print("TCP/IP Thread terminating...")
        self.p.terminate()
        self.p.wait()

        print("TCP/IP Thread terminated.")


class ReaderThread(threading.Thread): 
    def __init__(self, sock): 
        threading.Thread.__init__(self)
        self.sock = sock
	
    def run(self): 
        while shutdownEvent.is_set() == False:
            r, w, e = select.select([self.sock], [], [], 0.1)

            if self.sock in r:
                data = self.sock.recv(4096)
                print("recv: %s" % binascii.hexlify(data))
                #self.socket.send(data)


def main():
    signal.signal(signal.SIGINT, sigHandler)

    tcpIpThread = TcpIpStackThread()
    tcpIpThread.daemon = True
    tcpIpThread.start()

    # Wait until TCP/IP-Stack has been started.
    # TODO Better synchronization mechanism required!!
    time.sleep(1.0)
    
    client = openSocket()
    print("Starting Reader-Thread...")
    readerThread = ReaderThread(client)
    readerThread.daemon = True
    readerThread.start()

    while readerThread.is_alive() or tcpIpThread.is_alive():
        time.sleep(0.1)

    readerThread.join()


    print("Done")

if __name__ == "__main__":
	sys.exit(main())