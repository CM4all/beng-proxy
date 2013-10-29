#
# gdb commands for debugging/inspecting beng-proxy.
#
# Author: Max Kellermann <mk@cm4all.com>
#

import gdb

def for_each_hashmap(h):
    if h.type.code != gdb.lookup_type('struct hashmap').code:
        print "not a hashmap"
        return

    capacity = h['capacity']
    slots = h['slots']
    for i in range(capacity):
        slot = slots[i]
        if not slot['pair']['key']:
            continue

        while slot:
            pair = slot['pair']
            yield i, pair['key'].string(), pair['value']
            slot = slot['next']

class DumpHashmapSlot(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_hashmap_slot", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        arg_list = gdb.string_to_argv(arg)
        if len(arg_list) != 2:
            print "usage: bp_dump_hashmap ptr i"
            return

        h = gdb.parse_and_eval(arg_list[0])
        if h.type.code != gdb.lookup_type('struct hashmap').pointer().code:
            print "%s is not a hashmap*" % arg_list[0]
            return

        i = int(arg_list[1])
        slot = h['slots'][i]

        if not slot['pair']['key']:
            print "empty"
            return

        while slot:
            print slot['pair']['key'].string()
            slot = slot['next']

class DumpHashmap(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_hashmap", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        h = gdb.parse_and_eval(arg)
        if h.type.code != gdb.lookup_type('struct hashmap').pointer().code:
            print "%s is not a hashmap*" % arg_list[0]
            return

        n_slots = 0
        n_total = 0
        biggest_slot = 0
        i_biggest_slot = -1

        capacity = h['capacity']
        for i in range(capacity):
            slot = h['slots'][i]
            if slot['pair']['key']:
                s = slot['next']
                n = 1
                while s:
                    n += 1
                    s = s['next']
                if n > biggest_slot:
                    biggest_slot = n
                    i_biggest_slot = i
                if n > 1000:
                    print "big", i, n
                n_total += n
                n_slots += 1
                #print n, slot['pair']['key']
                if n_slots % 256 == 0:
                    print n_slots, n_total, biggest_slot, i_biggest_slot
        print n_slots, n_total, biggest_slot, i_biggest_slot

class DumpHashmap2(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_hashmap2", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        h = gdb.parse_and_eval(arg)
        for i, key, value in for_each_hashmap(h.dereference()):
            print i, key, value

class DumpStrmap(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_strmap", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        h = gdb.parse_and_eval(arg)
        if h.type.code != gdb.lookup_type('struct strmap').pointer().code:
            print "%s is not a strmap*" % arg_list[0]
            return

        string_type = gdb.lookup_type('char').pointer()
        for i, key, value in for_each_hashmap(h['hashmap'].dereference()):
            print key, '=', value.cast(string_type).string()

def pool_size(pool):
    return int(pool['size'])

def pool_sizes(pool):
    netto_size = pool_size(pool)
    if pool['type'] == 1:
        # linear pool
        brutto_size = 0
        area = pool['current_area']['linear']
        while area:
            brutto_size += int(area['size'])
            area = area['prev']
    else:
        brutto_size = netto_size
    return brutto_size, netto_size

def for_each_list_head(head):
    item = head['next']
    head_address = head.address
    while item != head_address:
        yield item
        item = item['next']

def for_each_list_head_reverse(head):
    item = head['prev']
    head_address = head.address
    while item != head_address:
        yield item
        item = item['prev']

def for_each_list_item_reverse(head, cast):
    item = head['prev']
    head_address = head.address
    while item != head_address:
        yield item.cast(cast)
        item = item['prev']

def for_each_recursive_pool(pool):
    yield pool

    pool_pointer = gdb.lookup_type('struct pool').pointer()

    for child in for_each_list_head(pool['children']):
        child = child.cast(pool_pointer)
        for x in for_each_recursive_pool(child):
            yield x

def pool_recursive_sizes(pool):
    pool_pointer = gdb.lookup_type('struct pool').pointer()

    brutto_size, netto_size = pool_sizes(pool)
    for child in for_each_list_head(pool['children']):
        child_brutto_size, child_netto_size = pool_recursive_sizes(child.cast(pool_pointer))
        brutto_size += child_brutto_size
        netto_size += child_netto_size

    return brutto_size, netto_size

class DumpPoolStats(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_pool_stats", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        pool = gdb.parse_and_eval(arg)
        if pool.type.code != gdb.lookup_type('struct pool').pointer().code:
            print "%s is not a strmap*" % arg_list[0]
            return

        print "pool '%s' type=%d" % (pool['name'].string(), pool['type'])
        print "size", pool_sizes(pool)
        print "recursive_size", pool_recursive_sizes(pool)

class DumpPoolRefs(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_pool_refs", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def _dump_refs(self, pool, label, head):
        print "pool '%s' %s:" % (pool['name'].string(), label)

        ref_pointer = gdb.lookup_type('struct pool_ref').pointer()
        for r in for_each_list_item_reverse(head, ref_pointer):
            print '%4u %s:%u' % (r['count'], r['file'].string().replace('../', ''), r['line'])

    def invoke(self, arg, from_tty):
        pool = gdb.parse_and_eval(arg)
        if pool.type.code != gdb.lookup_type('struct pool').pointer().code:
            print "%s is not a pool*" % arg_list[0]
            return

        for i in ('refs', 'unrefs'):
            self._dump_refs(pool, i, pool[i])

class DumpPoolAllocations(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_pool_allocations", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        pool = gdb.parse_and_eval(arg)
        if pool.type.code != gdb.lookup_type('struct pool').pointer().code:
            print "%s is not a pool*" % arg_list[0]
            return

        allocation_pointer = gdb.lookup_type('struct allocation_info').pointer()
        for a in for_each_list_item_reverse(pool['allocations'], allocation_pointer):
            print '%8u %s:%u' % (a['size'], a['file'].string().replace('../', ''), a['line'])

class FindPool(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_find_pool", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        arg_list = gdb.string_to_argv(arg)
        if len(arg_list) != 2:
            print "usage: bp_find_pool pool name"
            return

        pool = gdb.parse_and_eval(arg_list[0])
        name = arg_list[1]

        for x in for_each_recursive_pool(pool):
            if x['name'].string() == name:
                print x, x.dereference()

class FindChild(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_find_child", gdb.COMMAND_DATA, gdb.COMPLETE_NONE, True)

    def _find_child_by_pid(self, pid):
        lh = gdb.parse_and_eval('children')
        if lh.type.code != gdb.lookup_type('struct list_head').code:
            print "not a list_head"
            return None

        child_pointer = gdb.lookup_type('struct child').pointer()
        for li in for_each_list_head(lh):
            child = li.cast(child_pointer)
            if child['pid'] == pid:
                return child

        return None

    def invoke(self, arg, from_tty):
        pid = gdb.parse_and_eval(arg)
        child = self._find_child_by_pid(pid)
        if child is None:
            print "Not found"
        else:
            print child, child.dereference()

class FindChildStockClient(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_find_child_stock_client", gdb.COMMAND_DATA, gdb.COMPLETE_NONE, True)

    def _find_child_by_pid(self, pid):
        lh = gdb.parse_and_eval('children')
        if lh.type.code != gdb.lookup_type('struct list_head').code:
            print "not a list_head"
            return None

        child_pointer = gdb.lookup_type('struct child').pointer()
        for li in for_each_list_head(lh):
            child = li.cast(child_pointer)
            if child['pid'] == pid:
                return child

        return None

    def invoke(self, arg, from_tty):
        pid = gdb.parse_and_eval(arg)
        child = self._find_child_by_pid(pid)
        if child is None:
            print "Not found"
            return

        stock_type = gdb.lookup_type('struct stock').pointer()
        child_stock_item_type = gdb.lookup_type('struct child_stock_item').pointer()
        child_stock_item = child['callback_ctx'].cast(child_stock_item_type)

        string_type = gdb.lookup_type('char').pointer()
        fcgi_connection_type = gdb.lookup_type('struct fcgi_connection').pointer()

        fcgi_stock = gdb.parse_and_eval('global_fcgi_stock')
        h = fcgi_stock['hstock']['stocks']

        for i, key, value in for_each_hashmap(h.dereference()):
            stock = value.cast(stock_type)

            for x in ('idle', 'busy'):
                for lh in for_each_list_head(stock[x]):
                    c = lh.cast(fcgi_connection_type)

                    if c['child'] == child_stock_item:
                        print key, c, c.dereference()

DumpHashmapSlot()
DumpHashmap()
DumpHashmap2()
DumpStrmap()
DumpPoolStats()
DumpPoolRefs()
DumpPoolAllocations()
FindPool()
FindChild()
FindChildStockClient()
