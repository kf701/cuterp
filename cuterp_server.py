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
    count = 0
    while (not rets) :
        for s in g_dev_socks :
            if not s['use'] :
                rets = s
                break
        time.sleep(0.1)
        count += 1
        if count > 100 : break

    if not rets : return None
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
        clientsock, clientaddr = self.server.accept()
        print clientaddr, "has connected"
        self.input_list.append(clientsock)
        print 'input list len =', len(self.input_list)

    def on_close(self):
        print 'on close'
        #print self.s.getpeername(), "has disconnected"
        self.input_list.remove(self.s)
        self.s.close()

        if self.s in self.channel :
            out = self.channel[self.s]
            self.input_list.remove(out)
            out.close()
            del self.channel[out]
            del self.channel[self.s]
        else :
            print 'not in channel'

    def on_recv(self):
        if not self.s in self.channel :
            forward = get_idle_sock()
            if forward :
                self.input_list.append(forward)
                self.channel[self.s] = forward
                self.channel[forward] = self.s
        # here we can parse and/or modify the data before send forward
        #print data
        #request = HTTPRequest(data)
        self.channel[self.s].send(self.data)


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
                print 'dev loop socket close'
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
   server.main_loop()
