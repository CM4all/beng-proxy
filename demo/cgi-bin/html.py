#!/usr/bin/python

from sys import stdin, stdout
from pygments.lexers import get_lexer_by_name
from pygments.formatters.html import HtmlFormatter
from pygments import highlight

code = stdin.read()
lexer = get_lexer_by_name('html')
formatter = HtmlFormatter()

print "Content-Type: text/html"
print
highlight(code, lexer, formatter, outfile=stdout)
