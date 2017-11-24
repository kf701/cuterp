#!/usr/bin/python

import socket
import select
import time
import sys
import threading

from BaseHTTPServer import BaseHTTPRequestHandler
from StringIO import StringIO

buffer_size = 4096
delay = 0.0001
g_dev_socks = []

def get_idle_sock() :
    rets = None
    for s in g_dev_socks :
        if not s['use'] :
            rets = s
            break
    if not rets :
        return None
    g_dev_socks.remove(rets)
    return rets['sock']

class HTTPRequest(BaseHTTPRequestHandler):
    def __init__(self, request_text):
        self.rfile = StringIO(request_text)
        self.raw_requestline = self.rfile.readline()
        self.error_code = self.error_message = None
        self.parse_request()

    def send_error(self, code, message):
        self.error_code = code
        self.error_message = message


class TheServer:
    input_list = []
    channel = {}

    def __init__(self, host, port):
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((host, port))
        self.server.listen(200)
        print 'Listen on', port

    def main_loop(self):
        self.input_list.append(self.server)
        while 1:
            time.sleep(delay)
            inputready, outputready, exceptready = select.select(self.input_list, [], [])
            for self.s in inputready:
                if self.s == self.server:
                    self.on_accept()
                    break

                self.data = self.s.recv(buffer_size)
                if len(self.data) == 0:
                    self.on_close()
                    break
                else:
                    self.on_recv()

    def on_accept(self):
        forward = get_idle_sock()
        clientsock, clientaddr = self.server.accept()
        print clientaddr, "has connected"
        if forward :
            self.input_list.append(clientsock)
            self.input_list.append(forward)
            self.channel[clientsock] = forward
            self.channel[forward] = clientsock
        else:
            print "Can't establish connection with remote server.",
            print "Closing connection with client side", clientaddr
            clientsock.close()

    def on_close(self):
        print self.s.getpeername(), "has disconnected"
        #remove objects from input_list
        self.input_list.remove(self.s)
        self.input_list.remove(self.channel[self.s])
        out = self.channel[self.s]
        # close the connection with client
        self.channel[out].close()  # equivalent to do self.s.close()
        # close the connection with remote server
        self.channel[self.s].close()
        # delete both objects from channel dict
        del self.channel[out]
        del self.channel[self.s]

    def on_recv(self):
        data = self.data
        # here we can parse and/or modify the data before send forward
        #print data
        #request = HTTPRequest(data)
        self.channel[self.s].send(data)


def dev_loop() :
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('', 5800))
    server.listen(200)
    print 'listen dev on 5800'

    input_list = []
    input_list.append(server)

    while True :
        inputready, outputready, exceptready = select.select(input_list, [], [])
        for s in inputready:
            if s == server:
                clientsock, clientaddr = server.accept()
                input_list.append(clientsock)
                print 'dev loop', clientaddr, "has connected"
                break

            data = s.recv(128)
            if len(data) == 0:
                input_list.remove(s)
                s.close()
                print 'dev loop socket close:', s.getpeername()
                break

            input_list.remove(s)
            g_dev_socks.append({'sock':s, 'name':data, 'use':False})
            print 'dev loop socket got name: ' + data
            break

            print 'dev loop unknow error'



if __name__ == '__main__':

   devt = threading.Thread(target=dev_loop)
   devt.start()

   server = TheServer('', 5801)
   try:
       server.main_loop()
   except KeyboardInterrupt:
       print "Ctrl C - Stopping server"
       sys.exit(1)
