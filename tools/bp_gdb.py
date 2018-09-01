#
# gdb commands for debugging/inspecting beng-proxy.
#
# Author: Max Kellermann <mk@cm4all.com>
#

from __future__ import print_function

import gdb

try:
    from gdb.types import get_basic_type
except ImportError:
    # from libstdcxx printers
    def get_basic_type(type):
        # If it points to a reference, get the reference.
        if type.code == gdb.TYPE_CODE_REF:
            type = type.target()
        # Get the unqualified type, stripped of typedefs.
        type = type.unqualified().strip_typedefs()
        return type

def is_null(p):
    return str(p) == '0x0'

def for_each_hashmap(h):
    if h.type != gdb.lookup_type('struct hashmap'):
        print("not a hashmap")
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
            print("usage: bp_dump_hashmap ptr i")
            return

        h = gdb.parse_and_eval(arg_list[0])
        if h.type != gdb.lookup_type('struct hashmap').pointer():
            print("%s is not a hashmap*") % arg_list[0]
            return

        i = int(arg_list[1])
        slot = h['slots'][i]

        if not slot['pair']['key']:
            print("empty")
            return

        while slot:
            print(slot['pair']['key'].string())
            slot = slot['next']

class DumpHashmap(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_hashmap", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        h = gdb.parse_and_eval(arg)
        if h.type != gdb.lookup_type('struct hashmap').pointer():
            print("%s is not a hashmap*" % arg)
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
                    print("big", i, n)
                n_total += n
                n_slots += 1
                #print(n, slot['pair']['key'])
                if n_slots % 256 == 0:
                    print(n_slots, n_total, biggest_slot, i_biggest_slot)
        print(n_slots, n_total, biggest_slot, i_biggest_slot)

class DumpHashmap2(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_hashmap2", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        h = gdb.parse_and_eval(arg)
        for i, key, value in for_each_hashmap(h.dereference()):
            print(i, key, value)

class DumpStrmap(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_strmap", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        h = gdb.parse_and_eval(arg)
        if h.type != gdb.lookup_type('StringMap').pointer():
            print("%s is not a strmap*" % arg)
            return

        string_type = gdb.lookup_type('char').pointer()
        for i, key, value in for_each_hashmap(h['hashmap'].dereference()):
            print(key, '=', value.cast(string_type).string())

def pool_size(pool):
    return int(pool['netto_size'])

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

def for_each_list_item(head, cast):
    item = head['next']
    head_address = head.address
    while item != head_address:
        yield item.cast(cast)
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

class IntrusiveContainerType:
    def __init__(self, list_type, member_hook=None):
        self.list_type = get_basic_type(list_type)
        self.value_type = list_type.template_argument(0)
        self.value_pointer_type = self.value_type.pointer()

        self.member_hook = None
        if member_hook is not None:
            self.member_hook = self.value_type[member_hook]
            print("member_hook", member_hook, "offset", self.member_hook.bitpos/8)

    def node_to_value(self, node):
        if self.member_hook is None:
            return node.cast(self.value_pointer_type)
        else:
            return (node.dereference().address - self.member_hook.bitpos // 8).cast(self.value_pointer_type)

def for_each_intrusive_list(l):
    root = l['data_']['root_plus_size_']['root_']
    root_address = root.address
    node = root['next_']
    while node != root_address:
        yield node
        node = node['next_']

def for_each_intrusive_list_item(l, member_hook=None):
    t = IntrusiveContainerType(l.type, member_hook=member_hook)
    for node in for_each_intrusive_list(l):
        yield t.node_to_value(node)

def for_each_intrusive_list_reverse(l):
    root = l['data_']['root_plus_size_']['root_']
    root_address = root.address
    node = root['prev_']
    while node != root_address:
        yield node
        node = node['prev_']

def for_each_intrusive_list_item_reverse(l, member_hook=None):
    t = IntrusiveContainerType(l.type, member_hook=member_hook)
    for node in for_each_intrusive_list_reverse(l):
        yield t.node_to_value(node)

def for_each_recursive_pool(pool):
    yield pool

    pool_pointer = gdb.lookup_type('struct pool').pointer()

    for child in for_each_list_item(pool['children'], pool_pointer):
        for x in for_each_recursive_pool(child):
            yield x

def pool_recursive_sizes(pool):
    pool_pointer = gdb.lookup_type('struct pool').pointer()

    brutto_size, netto_size = pool_sizes(pool)
    for child in for_each_list_item(pool['children'], pool_pointer):
        child_brutto_size, child_netto_size = pool_recursive_sizes(child)
        brutto_size += child_brutto_size
        netto_size += child_netto_size

    return brutto_size, netto_size

#
# beng-lb specific code
#

def lb_goto_get_any_cluster(g):
    if g['cluster']:
        return g['cluster']
    elif g['branch']:
        return lb_branch_get_any_cluster(g['branch'])
    else:
        return None

def lb_branch_get_any_cluster(b):
    return lb_goto_get_any_cluster(b['fallback'])

def lb_listener_get_any_cluster(l):
    return lb_goto_get_any_cluster(l['destination'])

#
# gdb Commands
#

class PoolTree(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_pool_tree", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        arg_list = gdb.string_to_argv(arg)
        if len(arg_list) != 1:
            print("usage: bp_pool_tree pool")
            return

        pool = gdb.parse_and_eval(arg_list[0])

        for x in for_each_recursive_pool(pool):
            print(x, x.dereference())

class DumpPoolStats(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_pool_stats", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        pool = gdb.parse_and_eval(arg)
        if pool.type == gdb.lookup_type('struct pool').pointer():
            print("pool '%s' type=%d" % (pool['name'].string(), pool['type']))
            print("size", pool_sizes(pool))
            print("recursive_size", pool_recursive_sizes(pool))
        elif pool.type == gdb.lookup_type('struct SlicePool').pointer():
            print("slice_pool", pool.address)
            for x in ('slice_size', 'area_size', 'slices_per_area'):
                print(x, pool[x])
            area_pointer = gdb.lookup_type('struct SliceArea').pointer()
            brutto_size = netto_size = 0
            n_allocated = 0
            for area in for_each_list_item(pool['areas'], area_pointer):
                print("area", area.address, "allocated=", area['allocated_count'])
                n_allocated += area['allocated_count']
                brutto_size += pool['area_size']
                netto_size += area['allocated_count'] * pool['slice_size']
            print("size", brutto_size, netto_size)
            print("n_allocated", n_allocated)
        else:
            print("unrecognized pool:", arg)

class DumpPoolRefs(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_pool_refs", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def _dump_refs(self, pool, label, head):
        print("pool '%s' %s:" % (pool['name'].string(), label))

        ref_pointer = gdb.lookup_type('PoolRef').pointer()
        for r in for_each_list_item_reverse(head, ref_pointer):
            print('%4u %s:%u' % (r['count'], r['file'].string().replace('../', ''), r['line']))

    def invoke(self, arg, from_tty):
        pool = gdb.parse_and_eval(arg)
        if pool.type != gdb.lookup_type('struct pool').pointer():
            print("%s is not a pool*" % arg)
            return

        for i in ('refs', 'unrefs'):
            self._dump_refs(pool, i, pool[i])

class DumpPoolAllocations(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_pool_allocations", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        pool = gdb.parse_and_eval(arg)
        if pool.type != gdb.lookup_type('struct pool').pointer():
            print("%s is not a pool*" % arg)
            return

        allocation_pointer = gdb.lookup_type('struct allocation_info').pointer()
        for a in for_each_list_item_reverse(pool['allocations'], allocation_pointer):
            print('%8u %s:%u' % (a['size'], a['file'].string().replace('../', ''), a['line']))

class FindPool(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_find_pool", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        arg_list = gdb.string_to_argv(arg)
        if len(arg_list) != 2:
            print("usage: bp_find_pool pool name")
            return

        pool = gdb.parse_and_eval(arg_list[0])
        name = arg_list[1]

        for x in for_each_recursive_pool(pool):
            if x['name'].string() == name:
                print(x, x.dereference())

class DumpPoolRecycler(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_pool_recycler", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        recycler = gdb.parse_and_eval('recycler')

        n_pools = 0
        pool = recycler['pools']
        while not is_null(pool):
            n_pools += 1
            pool = pool['current_area']['recycler']
        print("n_pools", recycler['num_pools'], n_pools)

        n_areas = 0
        total_size = 0
        area = recycler['linear_areas']
        while not is_null(area):
            n_areas += 1
            total_size += area['size']
            area = area['prev']
        print("n_areas", recycler['num_linear_areas'], n_areas)
        print("area_total_size", total_size)

class SliceArea:
    def __init__(self, pool, area):
        if area.type != gdb.lookup_type('SliceArea').pointer():
            raise ValueError("SliceArea* expected")

        self.pool = pool
        self.area = area

    def get_page(self, page_index, offset=0):
        offset += (self.pool.header_pages + page_index) * self.pool.page_size
        return self.area.cast(gdb.lookup_type('uint8_t').pointer()) + offset

    def get_slice(self, slice_index):
        page_index = (slice_index / self.pool.slices_per_page) * self.pool.pages_per_slice;
        slice_index %= self.pool.slices_per_page;

        return self.get_page(page_index, slice_index * self.pool.slice_size)

class SlicePool:
    def __init__(self, pool):
        if pool.type != gdb.lookup_type('SlicePool').pointer():
            raise ValueError("SlicePool* expected")

        self.pool = pool
        self.page_size = 4096
        self.slice_size = long(pool['slice_size'])
        self.header_pages = long(pool['header_pages'])
        self.slices_per_area = long(pool['slices_per_area'])
        self.slices_per_page = long(pool['slices_per_page'])
        self.pages_per_slice = long(pool['pages_per_slice'])

    def areas(self):
        for area in for_each_intrusive_list_item(self.pool['areas']):
            yield SliceArea(self, area)

def iter_fifo_buffers(instance):
    for connection in for_each_intrusive_list_item(instance['connections']):
        protocol = int(connection['listener']['destination']['cluster']['protocol'])
        if protocol == 0:
            http = connection['http']
        elif protocol == 1:
            tcp = connection['tcp']

            inbound = tcp['inbound']
            yield ('tcp inbound input', inbound['base']['input']['data'], connection, tcp)

            if inbound['filter']:
                tsf = inbound['filter_ctx'].cast(gdb.lookup_type('ThreadSocketFilter').pointer())
                for name in ('encrypted_input', 'decrypted_input', 'plain_output', 'encrypted_output'):
                    yield ('tcp inbound ' + name, tsf[name]['data'], connection, tcp, tsf)

                ssl = tsf['handler_ctx'].cast(gdb.lookup_type('SslFilter').pointer())
                for name in ('decrypted_input', 'plain_output'):
                    yield ('tcp inbound ssl ' + name, ssl[name]['data'], connection, tcp, ssl)
        else:
            print("unknown protocol", connection)

class DumpSlicePoolAreas(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_slice_pool_areas", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        pool = SlicePool(gdb.parse_and_eval(arg))

        ALLOCATED = -1 + 2**32
        END_OF_LIST = -2 + 2**32

        for area in pool.areas():
            print("0x%x allocated_count=%u free_head=%u" % (area.area, int(area.area['allocated_count']), int(area.area['free_head'])))
            if int(area.area['allocated_count']) > 0:
                free_list = [False] * pool.slices_per_area
                i = int(area.area['free_head'])
                previous = -1
                while i != END_OF_LIST:
                    if i < 0 or i >= pool.slices_per_area:
                        print("XXX", i, previous)
                        break
                    free_list[i] = True
                    previous = i
                    i = int(area.area['slices'][i]['next'])
                for i in range(pool.slices_per_area):
                    if not free_list[i]:
                        print("  slice[%u] data=0x%x" % (i, area.get_slice(i)))

class FindSliceFifoBuffer(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_find_slice_fifo_buffer", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        instance = gdb.parse_and_eval(arg)

        ptr = 0x7f332c403000

        for x in iter_fifo_buffers(instance):
            if long(x[1]) == ptr:
                print(x)
                break

class FindChild(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_find_child", gdb.COMMAND_DATA, gdb.COMPLETE_NONE, True)

    def _find_child_by_pid(self, pid):
        lh = gdb.parse_and_eval('children')
        if lh.type != gdb.lookup_type('struct list_head'):
            print("not a list_head")
            return None

        child_pointer = gdb.lookup_type('struct child').pointer()
        for child in for_each_list_item(lh, child_pointer):
            if child['pid'] == pid:
                return child

        return None

    def invoke(self, arg, from_tty):
        pid = gdb.parse_and_eval(arg)
        child = self._find_child_by_pid(pid)
        if child is None:
            print("Not found")
        else:
            print(child, child.dereference())

class FindChildStockClient(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_find_child_stock_client", gdb.COMMAND_DATA, gdb.COMPLETE_NONE, True)

    def _find_child_by_pid(self, pid):
        lh = gdb.parse_and_eval('children')
        if lh.type != gdb.lookup_type('struct list_head'):
            print("not a list_head")
            return None

        child_pointer = gdb.lookup_type('struct child').pointer()
        for child in for_each_list_item(lh, child_pointer):
            if child['pid'] == pid:
                return child

        return None

    def invoke(self, arg, from_tty):
        pid = gdb.parse_and_eval(arg)
        child = self._find_child_by_pid(pid)
        if child is None:
            print("Not found")
            return

        stock_type = gdb.lookup_type('struct Stock').pointer()
        child_stock_item_type = gdb.lookup_type('struct child_stock_item').pointer()
        child_stock_item = child['callback_ctx'].cast(child_stock_item_type)

        string_type = gdb.lookup_type('char').pointer()
        fcgi_connection_type = gdb.lookup_type('struct fcgi_connection').pointer()

        fcgi_stock = gdb.parse_and_eval('global_fcgi_stock')
        h = fcgi_stock['hstock']['stocks']

        for i, key, value in for_each_hashmap(h.dereference()):
            stock = value.cast(stock_type)

            for x in ('idle', 'busy'):
                for c in for_each_list_item(stock[x], fcgi_connection_type):
                    if c['child'] == child_stock_item:
                        print(key, c, c.dereference())

class LbStats(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "lb_stats", gdb.COMMAND_DATA, gdb.COMPLETE_NONE, True)

    def invoke(self, arg, from_tty):
        instance = gdb.parse_and_eval(arg)
        if instance.type != gdb.lookup_type('LbInstance').pointer():
            print("not a lb_instance")
            return None

        print("n_connections", instance['connections']['data_']['root_plus_size_']['size_'])

        n = 0
        n_ssl = 0
        n_http = 0
        n_tcp = 0
        n_buffers = 0

        void_ptr_type = gdb.lookup_type('void').pointer()
        long_type = gdb.lookup_type('long')
        for c in for_each_intrusive_list_item(instance['connections']):
            n += 1

            if not is_null(c['ssl_filter']):
                n_ssl += 1
                ssl_filter = c['ssl_filter']
                if not is_null(ssl_filter['encrypted_input']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(ssl_filter['decrypted_input']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(ssl_filter['plain_output']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(ssl_filter['encrypted_output']['data'].cast(void_ptr_type)):
                    n_buffers += 1

            if not is_null(c['thread_socket_filter']):
                n_ssl += 1
                f = c['thread_socket_filter']
                if not is_null(f['encrypted_input']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(f['decrypted_input']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(f['plain_output']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(f['encrypted_output']['data'].cast(void_ptr_type)):
                    n_buffers += 1

            protocol = str(lb_listener_get_any_cluster(c['listener'])['protocol'])
            if protocol == 'HTTP':
                n_http += 1
                n_buffers += 1
            elif protocol == 'TCP':
                n_tcp += 1
                tcp = c['tcp']
                if not is_null(tcp['outbound']['input']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(tcp['inbound']['base']['input']['data'].cast(void_ptr_type)):
                    n_buffers += 1

        print("n_connections", n)
        print("n_ssl", n_ssl)
        print("n_http", n_http)
        print("n_tcp", n_tcp)
        print("n_buffers", n_buffers)

DumpHashmapSlot()
DumpHashmap()
DumpHashmap2()
DumpStrmap()
PoolTree()
DumpPoolStats()
DumpPoolRefs()
DumpPoolAllocations()
FindPool()
DumpPoolRecycler()
DumpSlicePoolAreas()
FindSliceFifoBuffer()
FindChild()
FindChildStockClient()
LbStats()
