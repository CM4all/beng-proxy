#include "http-util.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc gcc_unused, char **argv gcc_unused) {
    assert(http_list_contains("foo", "foo"));
    assert(!http_list_contains("foo", "bar"));
    assert(http_list_contains("foo,bar", "bar"));
    assert(http_list_contains("bar,foo", "bar"));
    assert(!http_list_contains("bar,foo", "bart"));
    assert(!http_list_contains("foo,bar", "bart"));
    assert(http_list_contains("bar,foo", "\"bar\""));
    assert(http_list_contains("\"bar\",\"foo\"", "\"bar\""));
    assert(http_list_contains("\"bar\",\"foo\"", "bar"));
}
