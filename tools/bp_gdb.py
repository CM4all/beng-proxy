#
# gdb commands for debugging/inspecting beng-proxy.
#
# Author: Max Kellermann <mk@cm4all.com>
#

import re
import gdb
from gdb.types import get_basic_type

def is_null(p):
    return str(p) == '0x0'

def assert_gdb_type(value, expected_type):
    actual_type = value.type.unqualified()
    expected_type = expected_type.unqualified()
    if str(actual_type) != str(expected_type):
        raise gdb.GdbError(f"Expected '{expected_type}', got '{actual_type}'")

def parse_and_eval_assert_type(s, expected_type):
    value = gdb.parse_and_eval(s)
    assert_gdb_type(value, expected_type)
    return value

def pool_size(pool):
    return int(pool['netto_size'])

def pool_sizes(pool):
    netto_size = pool_size(pool)
    if pool['type'] == 2:
        # linear pool
        brutto_size = 0
        area = pool['current_area']['linear']
        while area:
            brutto_size += int(area['size'])
            area = area['prev']
    else:
        brutto_size = netto_size
    return brutto_size, netto_size

def find_field(t, name):
    for i in t.fields():
        if i.name == name and not i.artificial:
            return i
    return None

def find_intrusive_base_hook(container_type_name, t, tag):
    prefix = f'{container_type_name}Hook<'
    for i in t.fields():
        if i.is_base_class and not i.artificial:
            b = i.type.strip_typedefs()
            if b.tag.startswith(prefix) and b.template_argument(1) == tag:
                return b
    return None

class IntrusiveOffsetHookTraits:
    def __init__(self, value_type, offset):
        self.__void_pointer_type = gdb.lookup_type('void').pointer()
        self.__value_pointer_type = value_type.pointer()
        self.__offset = offset

    def node_to_value(self, node):
        return (node.reinterpret_cast(self.__void_pointer_type) - self.__offset).reinterpret_cast(self.__value_pointer_type)

def MakeIntrusiveMemberHookTraits(value_type, member_name):
    field = find_field(value_type, member_name)
    if field is None:
        raise RuntimeError('Field not found')

    return IntrusiveOffsetHookTraits(value_type, field.bitpos // 8)

class IntrusiveCastHookTraits:
    def __init__(self, value_type, base_hook_type):
        self.__value_pointer_type = value_type.pointer()
        self.__base_hook_pointer_type = base_hook_type.pointer()

    def node_to_value(self, node):
        return node.cast(self.__base_hook_pointer_type).cast(self.__value_pointer_type)

def GuessIntrusiveHookTraits(container_type, hook_traits_type):
    value_type = container_type.template_argument(0)
    container_type_name = container_type.strip_typedefs().tag.split('<', 1)[0]
    hook_traits_name = hook_traits_type.tag.split('<', 1)[0]
    if hook_traits_name == f'{container_type_name}MemberHookTraits':
        member_ptr = hook_traits_type.template_argument(0)
        assert(member_ptr.type.code == gdb.TYPE_CODE_MEMBERPTR)

        field_name = str(member_ptr).rsplit('::', 1)[1]
        return MakeIntrusiveMemberHookTraits(value_type, field_name)
    elif hook_traits_name == f'{container_type_name}BaseHookTraits':
        tag = hook_traits_type.template_argument(1)
        base_hook = find_intrusive_base_hook(container_type_name, value_type, tag)
        if base_hook is None:
            raise RuntimeError('No base hook found')

        return IntrusiveCastHookTraits(value_type, base_hook)
    else:
        raise RuntimeError('Could not determine hook traits')

class IntrusiveContainerType:
    def __init__(self, list_type, hook_traits=None):
        self.value_type = list_type.template_argument(0)
        self.__hook_traits = hook_traits if hook_traits else GuessIntrusiveHookTraits(list_type, list_type.template_argument(1).strip_typedefs())

    def node_to_value(self, node):
        return self.__hook_traits.node_to_value(node)

class IntrusiveListType(IntrusiveContainerType):
    def get_header(self, l):
        return l['head']

    def iter_nodes(self, l):
        root = self.get_header(l)
        root_address = root.address
        node = root['next']
        while node != root_address:
            yield node
            node = node['next']

    def iter_nodes_reverse(self, l):
        root = self.get_header(l)
        root_address = root.address
        node = root['prev']
        while node != root_address:
            yield node
            node = node['prev']

class IntrusiveListPrinter:
    def __init__(self, val):
        self.t = IntrusiveListType(val.type)
        self.val = val

    def display_hint(self):
        return 'array'

    def children(self):
        for i in self.t.iter_nodes(self.val):
            yield '', self.t.node_to_value(i).dereference()

    def to_string(self):
        return f"ilist<{self.t.value_type}>"

def for_each_intrusive_list_item(l, member_hook=None):
    t = IntrusiveContainerType(l.type, member_hook=member_hook)
    for node in t.iter_nodes(l):
        yield t.node_to_value(node).dereference()

def for_each_intrusive_list_item_reverse(l, member_hook=None):
    t = IntrusiveListType(l.type, member_hook=member_hook)
    for node in t.iter_nodes_reverse(l):
        yield t.node_to_value(node).dereference()

class IntrusiveHashSetHookTraits:
    def __init__(self, container_type):
        self.__next = GuessIntrusiveHookTraits(container_type, container_type.template_argument(3).strip_typedefs())

    def node_to_value(self, node):
        return self.__next.node_to_value(node)

class IntrusiveHashSetPrinter:
    def __init__(self, val):
        self.__val = val
        self.__hook_traits = IntrusiveHashSetHookTraits(get_basic_type(val.type))

    def display_hint(self):
        return 'array'

    def children(self):
        table = self.__val['table']

        table_size = int(table.type.template_argument(1))
        for i in range(table_size):
            bucket = table['_M_elems'][i]
            list_type = get_basic_type(bucket.type)
            t = IntrusiveListType(list_type, self.__hook_traits)
            for i in t.iter_nodes(bucket):
                yield '', self.__hook_traits.node_to_value(i).dereference()

    def to_string(self):
        return f"ihset<{self.__val.type.strip_typedefs().template_argument(0)}>"

class IntrusiveHashArrayTriePrinter:
    def __init__(self, val):
        self.__val = val
        self.__type = IntrusiveContainerType(val.type)

    def display_hint(self):
        return 'array'

    def children(self):
        def for_each_child(node):
            children = node['children']
            for i in range(4):
                child = children['_M_elems'][i]
                if child:
                    child = child
                    yield '', self.__type.node_to_value(child).dereference()
                    yield from for_each_child(child)

        yield from for_each_child(self.__val['root'])

    def to_string(self):
        return f"ihamt<{self.__type.value_type}>"

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
            print(x.address, x['name'])

class PoolChildren(gdb.Command):
    def __init__(self):
        gdb.Command.__init__(self, "bp_pool_children", gdb.COMMAND_DATA, gdb.COMPLETE_SYMBOL, True)

    def invoke(self, arg, from_tty):
        arg_list = gdb.string_to_argv(arg)
        if len(arg_list) != 1:
            print("usage: bp_pool_children pool")
            return

        pool = gdb.parse_and_eval(arg_list[0])

        for child in for_each_intrusive_list_item(pool['children']):
            print(child.address, child['name'])

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
        for r in for_each_intrusive_list_item_reverse(head, ref_pointer):
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
            columns = [str(a.address + 1), '%8u' % a['size']]

            if 'type' in a.type:
                t = a['type']
                if not is_null(t):
                    columns.append(t.string())

            if 'file' in a.type:
                columns.append('%s:%u' % (a['file'].string().replace('../', ''), a['line']))

            print(*columns)

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
        self.slice_size = int(pool['slice_size'])
        self.header_pages = int(pool['header_pages'])
        self.slices_per_area = int(pool['slices_per_area'])
        self.slices_per_page = int(pool['slices_per_page'])
        self.pages_per_slice = int(pool['pages_per_slice'])

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
            if int(x[1]) == ptr:
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
PoolChildren()
DumpPoolStats()
DumpPoolRefs()
DumpPoolAllocations()
FindPool()
DumpPoolRecycler()
DumpLeaks()
DumpSlicePoolAreas()
FindSliceFifoBuffer()
LbStats()

class StdArrayPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return str(self.val['_M_elems'])

class StaticArrayPrinter:
    def __init__(self, val):
        self.val = val

    def display_hint(self):
        return 'array'

    def children(self):
        return [('', self.val['data']['_M_elems'][i]) for i in range(self.val['the_size'])]

    def to_string(self):
        t = get_basic_type(self.val.type)
        return f"StaticArray<{t.template_argument(0)}>"

class StaticVectorPrinter:
    def __init__(self, val):
        self.val = val
        self.__t = get_basic_type(self.val.type).template_argument(0)

    def display_hint(self):
        return 'array'

    def children(self):
        return [('', self.val['array']['_M_elems'][i].cast(self.__t)) for i in range(self.val['the_size'])]

    def to_string(self):
        return f"StaticVector<{self.__t}>"

class StringMapItemPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        k = self.val['key']
        v = self.val['value']
        return f'{{"{k.string()}"="{v.string()}"}}'

class StringMapPrinter:
    def __init__(self, val):
        self.__next = IntrusiveHashArrayTriePrinter(val['map'])

    def display_hint(self):
        return 'array'

    def children(self):
        return self.__next.children()

    def to_string(self):
        return 'StringMap'

class BoundMethodPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        instance = self.val['instance_']
        if is_null(instance): return 'nullptr'
        function = str(self.val['function'].dereference())
        import re
        function = re.sub(r'^.*BindMethodDetail::BindMethodWrapperGenerator.*, &(.+?),.*$', r'\1', function)
        return f"BoundMethod{{{function}, {instance}}}"

class CancellablePointerPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        c = self.val['cancellable']
        if is_null(c):
            return 'nullptr'
        t = c.dynamic_type
        c = c.cast(t)
        return '%s{0x%x}' % (t.target(), c)

class LeasePtrPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        c = self.val['lease']
        if is_null(c):
            return 'nullptr'
        t = c.dynamic_type
        c = c.cast(t)
        return '%s{0x%x}' % (t.target(), c)

class PoolPtrPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        pool = self.val['value'].dereference()

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
            pool = pool['value'].dereference()

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
        return 'SocketEvent{%d, scheduled=0x%x, callback=%s}' % (self.val['fd']['fd'], self.val['scheduled_flags'], self.val['callback'])

def format_steady_tp(tp, event_loop):
    steady_cache = event_loop['steady_clock_cache']
    now = steady_cache['value']
    delta = int(tp['__d']['__r'] - now['__d']['__r'])
    return '%fs' % (delta / 1000000000)

class TimerEventPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        if is_null(self.val['parent_']):
            due = 'none'
        else:
            due = format_steady_tp(self.val['due'], self.val['loop'])
        return 'TimerEvent{%s, callback=%s}' % (due, self.val['callback'])

class DeferEventPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return f"DeferEvent{{scheduled={not is_null(self.val['siblings']['next'])}, callback={self.val['callback']}}}"

class SliceAllocationPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        val = self.val
        if is_null(val['data']):
            return "nullptr"
        return f"SliceAllocation{{{val['data']}, {val['size']}}}"

class SliceFifoBufferPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        val = self.val
        if is_null(val['allocation']['data']):
            return "nullptr"
        return f"SliceFifoBuffer{{{val['data'] + val['head']}, {val['tail'] - val['head']}}}"

class StockPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return f"Stock{{{self.val['cls'].referenced_value().dynamic_type}, {self.val['name']}, idle={self.val['idle']}, busy={self.val['busy']}}}"

class StockItemPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return f"StockItem{{{self.val.dynamic_type} {self.val.address}}}"

class StockMapPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return 'StockMap{%s, %s}' % (self.val['cls'].referenced_value().dynamic_type, self.val['map'])

class StockMapItemPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return f"StockMap::Item{{{self.val['cls'].referenced_value().dynamic_type}, {self.val['name']}, idle={self.val['idle']}, busy={self.val['busy']}, sticky={self.val['sticky']}}}"

class IstreamPointerPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        c = self.val['stream']
        if is_null(c):
            return 'nullptr'
        t = c.dynamic_type
        c = c.cast(t)
        return '%s{0x%x}' % (t.target(), c)

class CatIstreamInputPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return self.val['input']

class SpawnServerChildPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return f"{{id={self.val['id']}, pid={self.val['pid']}, name={self.val['name']}}}"

import gdb.printing
def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("cm4all-beng-proxy")
    pp.add_printer('std::array', '^std::array<', StdArrayPrinter)
    pp.add_printer('TrivialArray', '^TrivialArray<', StaticArrayPrinter)
    pp.add_printer('StaticArray', '^StaticArray<', StaticArrayPrinter)
    pp.add_printer('StaticVector', '^StaticVector<', StaticVectorPrinter)
    pp.add_printer('IntrusiveList', '^Intrusive(Forward)?List<', IntrusiveListPrinter)
    pp.add_printer('IntrusiveHashSet', '^IntrusiveHashSet<', IntrusiveHashSetPrinter)
    pp.add_printer('IntrusiveHashArrayTrie', '^IntrusiveHashArrayTrie<', IntrusiveHashArrayTriePrinter)
    pp.add_printer('StringMap::Item', '^StringMap::Item$', StringMapItemPrinter)
    pp.add_printer('StringMap', '^StringMap$', StringMapPrinter)
    pp.add_printer('BoundMethod', '^BoundMethod<', BoundMethodPrinter)
    pp.add_printer('CancellablePointer', '^CancellablePointer$', CancellablePointerPrinter)
    pp.add_printer('LeasePtr', '^LeasePtr$', LeasePtrPrinter)
    pp.add_printer('PoolPtr', '^PoolPtr$', PoolPtrPrinter)
    pp.add_printer('PoolHolder', '^PoolHolder$', PoolHolderPrinter)
    pp.add_printer('allocation_info', '^allocation_info$', PoolAllocationInfoPrinter)
    pp.add_printer('LeakDetector', '^LeakDetector$', LeakDetectorPrinter)
    pp.add_printer('FileDescriptor', '^(Unique)?FileDescriptor$', FileDescriptorPrinter)
    pp.add_printer('SocketDescriptor', '^(Unique)?SocketDescriptor$', SocketDescriptorPrinter)
    pp.add_printer('SocketEvent', '^SocketEvent$', SocketEventPrinter)
    pp.add_printer('TimerEvent', '^TimerEvent$', TimerEventPrinter)
    pp.add_printer('DeferEvent', '^DeferEvent$', DeferEventPrinter)
    pp.add_printer('SliceAllocation', '^SliceAllocation$', SliceAllocationPrinter)
    pp.add_printer('SliceFifoBuffer', '^SliceFifoBuffer$', SliceFifoBufferPrinter)
    pp.add_printer('Stock', '^Stock$', StockPrinter)
    pp.add_printer('StockItem', '^StockItem$', StockItemPrinter)
    pp.add_printer('StockMap', '^StockMap$', StockMapPrinter)
    pp.add_printer('StockMap::Item', '^StockMap::Item$', StockMapItemPrinter)
    pp.add_printer('IstreamPointer', '^IstreamPointer$', IstreamPointerPrinter)
    pp.add_printer('CatIstream::Input', '^CatIstream::Input$', CatIstreamInputPrinter)
    pp.add_printer('SpawnServerChild', '^SpawnServerChild$', SpawnServerChildPrinter)
    return pp

gdb.printing.register_pretty_printer(gdb.current_objfile(), build_pretty_printer(), replace=True)
