# -*- coding:utf-8 -*-

import os
import socket
import select
import time
import sys
import threading
from struct import *

from http.server import BaseHTTPRequestHandler
from io import StringIO

g_sock_name = {}
g_sock_sock = {}
g_dev_socks = {}


class HTTPRequest(BaseHTTPRequestHandler):
    def __init__(self, request_text):
        self.rfile = StringIO(request_text)
        self.raw_requestline = self.rfile.readline()
        self.error_code = self.error_message = None
        self.parse_request()

    def send_error(self, code, message):
        self.error_code = code
        self.error_message = message


def web_loop():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('', 5801))
    server.listen(200)
    print ('listen web on 5801')

    input_list = []
    input_list.append(server)

    while True :
        inputready, outputready, exceptready = select.select(input_list, [], [])
        for s in inputready:
            if s == server:
                clientsock, clientaddr = server.accept()
                input_list.append(clientsock)
                g_sock_name[clientsock.fileno()] = 'test1'
                g_sock_sock[clientsock.fileno()] = clientsock
                print ('web loop', clientaddr, "has connected, fileno = ", clientsock.fileno())
                break

            print ('web got data fileno = ', s.fileno())
            try:
                data = s.recv(4096)
                print (data)
            except:
                pass
            dlen = len(data)

            if dlen == 0:
                del g_sock_name[s.fileno()]
                del g_sock_sock[s.fileno()]
                input_list.remove(s)
                s.close()
                print ('web socket close')
                break

            name = g_sock_name[s.fileno()]
            dev_sock = g_dev_socks[name]

            #print ('-- send to dev, index = ', s.fileno(), 'len = ', dlen)
            head = pack('<ccccII', b'd', b'0', b'0', b'0', s.fileno(), dlen)
            dev_sock.sendall(head)
            dev_sock.sendall(data)

def parse_head(data) :
    (t,n,n,n,i,l) = unpack('<cBBBII', data)
    t = t.decode("utf-8")
    return {'type':t, 'index':i, 'length':l}

def dev_loop() :
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('', 5800))
    server.listen(200)
    print ('listen dev on 5800')

    input_list = []
    input_list.append(server)

    while True :
        inputready, outputready, exceptready = select.select(input_list, [], [])
        for s in inputready:
            if s == server:
                clientsock, clientaddr = server.accept()
                input_list.append(clientsock)
                print ('dev loop', clientaddr, "has connected, fileno = ", clientsock.fileno())
                break

            #print ('dev got data fileno = ', s.fileno())

            try:
                data = s.recv(12)
            except:
                pass

            if not data or len(data) != 12:
                del g_dev_socks[g_sock_name[s.fileno()]]
                del g_sock_name[s.fileno()]
                input_list.remove(s)
                s.close()
                print ('dev socket close ---------------')
                break
            
            #print (data)
            head = parse_head(data)
            print (head)
            
            if head['type'] == 'i' :
                name = s.recv(head['length'])
                name = name.decode("utf-8")
                g_sock_name[s.fileno()] = name
                g_dev_socks[name] = s
                print ('dev socket got name: ', name)
                break
            
            if head['type'] == 'd':
                #print ('dev socket forward length', head['length'])
                web_sock = g_sock_sock[head['index']]
                rest = head['length']
                while rest != 0 :
                    fdata = s.recv(rest)
                    if not fdata :
                        print ("recv bug");
                    rest = rest - len(fdata)
                    try:
                        web_sock.sendall(fdata)
                    except:
                        pass
                    #print ('forward loop, got ', len(fdata), "rest ", rest)
                break
            
            print ('dev loop unknow error --------------')
            del g_dev_socks[g_sock_name[s.fileno()]]
            del g_sock_name[s.fileno()]
            input_list.remove(s)
            s.close()


if __name__ == '__main__':

    devt = threading.Thread(target=dev_loop)
    devt.start()

    web_loop()
