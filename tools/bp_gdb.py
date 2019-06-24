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

def assert_gdb_type(value, expected_type):
    actual_type = value.type.unqualified()
    expected_type = expected_type.unqualified()
    if str(actual_type) != str(expected_type):
        raise gdb.GdbError("Expected '%s', got '%s'" % (expected_type, actual_type))

def parse_and_eval_assert_type(s, expected_type):
    value = gdb.parse_and_eval(s)
    assert_gdb_type(value, expected_type)
    return value

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

class IntrusiveContainerType:
    def __init__(self, list_type, member_hook=None):
        self.list_type = get_basic_type(list_type)
        self.value_type = list_type.template_argument(0)
        self.value_pointer_type = self.value_type.pointer()

        self.member_hook = None
        if member_hook is not None:
            self.member_hook = self.value_type[member_hook]
            print("member_hook", member_hook, "offset", self.member_hook.bitpos/8)

    def get_header(self, l):
        rps = l['data_']['root_plus_size_']
        if 'm_header' in rps.type:
            # seen in Boost 1.62
            return rps['m_header']
        elif 'root_' in rps.type:
            # seen in Boost 1.55
            return rps['root_']
        else:
            # seen in Boost 1.62 slist
            return rps['header_holder_']

    def node_to_value(self, node):
        if self.member_hook is None:
            return node.cast(self.value_pointer_type)
        else:
            return (node.dereference().address - self.member_hook.bitpos // 8).cast(self.value_pointer_type)

    def iter_nodes(self, l):
        root = self.get_header(l)
        root_address = root.address
        node = root['next_']
        while node != root_address:
            yield node
            node = node['next_']

    def iter_nodes_reverse(self, l):
        root = self.get_header(l)
        root_address = root.address
        node = root['prev_']
        while node != root_address:
            yield node
            node = node['prev_']

def for_each_intrusive_list_item(l, member_hook=None):
    t = IntrusiveContainerType(l.type, member_hook=member_hook)
    for node in t.iter_nodes(l):
        yield t.node_to_value(node)

def for_each_intrusive_list_item_reverse(l, member_hook=None):
    t = IntrusiveContainerType(l.type, member_hook=member_hook)
    for node in t.iter_nodes_reverse(l):
        yield t.node_to_value(node)

class IntrusiveListPrinter:
    class Iterator:
        def __init__(self, t, head):
            self.t = t
            self.head_address = head.address
            self.i = head['next_']

        def __iter__(self):
            return self

        def __next__(self):
            if self.i == self.head_address:
                raise StopIteration
            result = self.t.node_to_value(self.i)
            self.i = self.i['next_']
            return '', result.dereference()

    def __init__(self, val):
        self.t = IntrusiveContainerType(val.type)
        self.val = val

    def display_hint(self):
        return 'array'

    def children(self):
        return self.Iterator(self.t, self.t.get_header(self.val))

    def to_string(self):
        return str(self.val.type.strip_typedefs())

class IntrusiveSetType:
    def __init__(self, list_type, member_hook=None):
        self.list_type = get_basic_type(list_type)
        self.value_type = list_type.template_argument(0)
        self.value_pointer_type = self.value_type.pointer()

        self.member_hook = None
        if member_hook is not None:
            self.member_hook = self.value_type[member_hook]

    def get_header(self, s):
        return s['holder']['root']

    def node_to_value(self, node):
        if self.member_hook is None:
            return node.cast(self.value_pointer_type)
        else:
            return (node.dereference().address - self.member_hook.bitpos // 8).cast(self.value_pointer_type)

    def get_left_node(self, node):
        result = node['left_'].dereference()
        if result.address == node.address: return None
        return result

    def get_right_node(self, node):
        result = node['right_'].dereference()
        if result.address == node.address: return None
        return result

    def _iter_nodes(self, node):
        left = self.get_left_node(node)
        if left is not None:
            yield from self._iter_nodes(left)
        yield node
        right = self.get_right_node(node)
        if right is not None:
            yield from self._iter_nodes(right)

    def iter_nodes(self, s):
        left = self.get_left_node(s)
        if left is not None:
            yield from self._iter_nodes(left)
        right = self.get_right_node(s)
        if right is not None:
            yield from self._iter_nodes(right)

class IntrusiveUnorderedSetType:
    def __init__(self, list_type, member_hook=None):
        self.list_type = get_basic_type(list_type)
        self.value_type = list_type.template_argument(0)
        self.value_pointer_type = self.value_type.pointer()

        self.member_hook = None
        if member_hook is not None:
            self.member_hook = self.value_type[member_hook]

    def node_to_value(self, node):
        if self.member_hook is None:
            return node.cast(self.value_pointer_type)
        else:
            return (node.dereference().address - self.member_hook.bitpos // 8).cast(self.value_pointer_type)

    def iter_nodes(self, s):
        bucket_traits = s['data']['bucket_traits_']
        buckets = bucket_traits['buckets_']
        n_buckets = int(bucket_traits['buckets_len_'])
        for i in range(n_buckets):
            l = buckets[i]
            root = l['data_']['root_plus_size_']['header_holder_']
            root_address = root.address
            while True:
                node = root['next_']
                if node.address == root_address:
                    break
                yield node

    def iter_values(self, s):
        for node in self.iter_nodes(s):
            yield self.node_to_value(node)

def for_each_recursive_pool(pool):
    yield pool

    for child in for_each_intrusive_list_item(pool['children']):
        for x in for_each_recursive_pool(child):
            yield x

def pool_recursive_sizes(pool):
    brutto_size, netto_size = pool_sizes(pool)
    for child in for_each_intrusive_list_item(pool['children']):
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
            print(x, x['name'])

class DumpPoolStats(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_pool_stats", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        pool = gdb.parse_and_eval(arg)
        if pool.type == gdb.lookup_type('struct pool').pointer():
            print("pool '%s' type=%d" % (pool['name'].string(), pool['type']))
            print("size", pool_sizes(pool))
            print("recursive_size", pool_recursive_sizes(pool))
        elif pool.type == gdb.lookup_type('class SlicePool').pointer():
            print("slice_pool", pool.address)
            for x in ('slice_size', 'area_size', 'slices_per_area'):
                print(x, pool[x])
            brutto_size = netto_size = 0
            n_allocated = 0
            for area in for_each_intrusive_list_item(pool['areas']):
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
        pool = parse_and_eval_assert_type(arg,
                                          gdb.lookup_type('struct pool').pointer())

        for i in ('refs', 'unrefs'):
            self._dump_refs(pool, i, pool[i])

class DumpPoolAllocations(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_pool_allocations", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        pool = parse_and_eval_assert_type(arg,
                                          gdb.lookup_type('struct pool').pointer())

        for a in for_each_intrusive_list_item_reverse(pool['allocations'], member_hook='siblings'):
            if 'file' in a.type:
                print('%s %8u %s:%u' % (a + 1, a['size'], a['file'].string().replace('../', ''), a['line']))
            else:
                print('%s %8u' % (a + 1, a['size'],))

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
        for item in for_each_intrusive_list_item(recycler['pools']['list']):
            n_pools += 1
        print("n_pools", n_pools)

        n_areas = 0
        total_size = 0
        area = recycler['linear_areas']
        while not is_null(area):
            n_areas += 1
            total_size += area['size']
            area = area['prev']
        print("n_areas", recycler['num_linear_areas'], n_areas)
        print("area_total_size", total_size)

class DumpLeaks(gdb.Command):
    """Dump all objects registered in LeakDetector."""

    def __init__(self):
        gdb.Command.__init__(self, "bp_dump_leaks", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        leaks = gdb.parse_and_eval('leak_detector_container.list')
        for i in for_each_intrusive_list_item(leaks):
            print(i)

class SliceArea:
    def __init__(self, pool, area):
        assert_gdb_type(area, gdb.lookup_type('SliceArea').pointer())

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
        assert_gdb_type(pool, gdb.lookup_type('SlicePool').pointer())

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

class LbStats(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "lb_stats", gdb.COMMAND_DATA, gdb.COMPLETE_NONE, True)

    def invoke(self, arg, from_tty):
        instance = parse_and_eval_assert_type(arg,
                                              gdb.lookup_type('LbInstance').pointer())

        print("n_http_connections", instance['http_connections']['data_']['root_plus_size_']['size_'])
        print("n_tcp_connections", instance['tcp_connections']['data_']['root_plus_size_']['size_'])

        n = 0
        n_ssl = 0
        n_http = 0
        n_tcp = 0
        n_buffers = 0

        void_ptr_type = gdb.lookup_type('void').pointer()
        long_type = gdb.lookup_type('long')

        for c in for_each_intrusive_list_item(instance['http_connections']):
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

            f = c['http']['socket']['filter']['_M_t']['_M_head_impl']
            if not is_null(f):
                if not is_null(f['encrypted_input']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(f['decrypted_input']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(f['plain_output']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(f['encrypted_output']['data'].cast(void_ptr_type)):
                    n_buffers += 1

            n_http += 1
            n_buffers += 1

        for c in for_each_intrusive_list_item(instance['tcp_connections']):
            n += 1

            f = c['inbound']['socket']['filter']['_M_t']['_M_head_impl']
            if not is_null(f):
                if not is_null(f['encrypted_input']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(f['decrypted_input']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(f['plain_output']['data'].cast(void_ptr_type)):
                    n_buffers += 1
                if not is_null(f['encrypted_output']['data'].cast(void_ptr_type)):
                    n_buffers += 1

                ssl_filter = f['handler']
                if not is_null(ssl_filter):
                    n_ssl += 1
                    if not is_null(ssl_filter['encrypted_input']['data'].cast(void_ptr_type)):
                        n_buffers += 1
                    if not is_null(ssl_filter['decrypted_input']['data'].cast(void_ptr_type)):
                        n_buffers += 1
                    if not is_null(ssl_filter['plain_output']['data'].cast(void_ptr_type)):
                        n_buffers += 1
                    if not is_null(ssl_filter['encrypted_output']['data'].cast(void_ptr_type)):
                        n_buffers += 1

            n_tcp += 1
            if not is_null(c['outbound']['input']['data'].cast(void_ptr_type)):
                n_buffers += 1
            if not is_null(c['inbound']['base']['input']['data'].cast(void_ptr_type)):
                n_buffers += 1

        print("n_connections", n)
        print("n_ssl", n_ssl)
        print("n_http", n_http)
        print("n_tcp", n_tcp)
        print("n_buffers", n_buffers)

PoolTree()
DumpPoolStats()
DumpPoolRefs()
DumpPoolAllocations()
FindPool()
DumpPoolRecycler()
DumpLeaks()
DumpSlicePoolAreas()
FindSliceFifoBuffer()
LbStats()

class IntrusiveSetPrinter:
    def __init__(self, val):
        self.t = IntrusiveSetType(val.type)
        self.val = val

    def display_hint(self):
        return 'array'

    def children(self):
        return list(self.t.iter_nodes(self.t.get_header(self.val)))

    def to_string(self):
        return str(self.val.type.strip_typedefs())

class IntrusiveUnorderedSetPrinter:
    def __init__(self, val):
        self.t = IntrusiveUnorderedSetType(val.type)
        self.val = val

    def display_hint(self):
        return 'array'

    def children(self):
        return list(self.t.iter_values(self.val))

    def to_string(self):
        return str(self.val.type.strip_typedefs())

class StdArrayPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return str(self.val['_M_elems'])

class StringViewPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        data = self.val['data']
        if data.address == 0:
            return "nullptr"

        return '"%s"' % data.string(length=self.val['size'])

class PoolPtrPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        pool = self.val['value']

        if pool.address != 0:
            return 'PoolPtr{"%s"}' % pool['name'].string()
        else:
            return 'PoolPtr{nullptr}'

class PoolHolderPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        pool = self.val['pool']
        if 'value' in pool.type:
            pool = pool['value']

        if pool.address != 0:
            return 'PoolHolder{"%s"}' % pool['name'].string()
        else:
            return 'PoolHolder{nullptr}'

class PoolAllocationInfoPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return '{%s, %u}' % (self.val.address + 1, self.val['size'])

class LeakDetectorPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return 'LeakDetector'

class FileDescriptorPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return 'fd{%d}' % self.val['fd']

class SocketDescriptorPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return 'sd{%d}' % self.val['fd']

class SocketEventPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return 'SocketEvent{%d, scheduled=0x%x}' % (self.val['fd']['fd'], self.val['scheduled_flags'])

class SliceAllocationPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        val = self.val
        if is_null(val['data']):
            return "nullptr"
        return "SliceAllocation{%s, %s}" % (val['data'], val['size'])

class SliceFifoBufferPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        val = self.val
        if is_null(val['allocation']['data']):
            return "nullptr"
        return "SliceFifoBuffer{%s, %s}" % (val['data'] + val['head'], val['tail'] - val['head'])

class StockMapPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return 'StockMap{%s, %s}' % (self.val['cls'].referenced_value().dynamic_type, self.val['map'])

import gdb.printing
def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("cm4all-beng-proxy")
    pp.add_printer('std::array', '^std::array<', StdArrayPrinter)
    pp.add_printer('boost::intrusive::list', 'boost::intrusive::s?list<', IntrusiveListPrinter)
    pp.add_printer('boost::intrusive::set', 'boost::intrusive::(multi)?set<', IntrusiveSetPrinter)
    pp.add_printer('boost::intrusive::unordered_set', 'boost::intrusive::unordered_(multi)?set<', IntrusiveUnorderedSetPrinter)
    pp.add_printer('StringView', '^BasicStringView<char>$', StringViewPrinter)
    pp.add_printer('StringView', '^StringView$', StringViewPrinter)
    pp.add_printer('PoolPtr', '^PoolPtr$', PoolPtrPrinter)
    pp.add_printer('PoolHolder', '^PoolHolder$', PoolHolderPrinter)
    pp.add_printer('allocation_info', '^allocation_info$', PoolAllocationInfoPrinter)
    pp.add_printer('LeakDetector', '^LeakDetector$', LeakDetectorPrinter)
    pp.add_printer('FileDescriptor', '^(Unique)?FileDescriptor$', FileDescriptorPrinter)
    pp.add_printer('SocketDescriptor', '^(Unique)?SocketDescriptor$', SocketDescriptorPrinter)
    pp.add_printer('SocketEvent', '^SocketEvent$', SocketEventPrinter)
    pp.add_printer('SliceAllocation', '^SliceAllocation$', SliceAllocationPrinter)
    pp.add_printer('SliceFifoBuffer', '^SliceFifoBuffer$', SliceFifoBufferPrinter)
    pp.add_printer('StockMap', '^StockMap$', StockMapPrinter)
    return pp

gdb.printing.register_pretty_printer(gdb.current_objfile(), build_pretty_printer(), replace=True)
