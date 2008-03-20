#!/usr/bin/python

import os, signal, select

def command_expect(command, expected):
    f = os.popen(command)
    output = f.read()
    if not output == expected:
        print "output", repr(output)
        print "expected", repr(expected)
        assert False

prefix = 'Cookie2: $Version="1"\r\n'
suffix = ' \r\n'

command_expect('./test/run-cookie-client foo=bar',
               prefix + 'Cookie: foo=bar;' + suffix)
command_expect('./test/run-cookie-client foo=bar;a=b',
               prefix + 'Cookie: foo=bar;' + suffix)
command_expect('./test/run-cookie-client foo=bar,a=b',
               prefix + 'Cookie: a=b; foo=bar;' + suffix)
