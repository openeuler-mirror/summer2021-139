import os
import glob
from math import log2

from hwcompatible.command import Command

def fmt_memsize(kb):
    '''format memsize. this is a code snippit from dpdk repo'''
    BINARY_PREFIX = "KMG"
    logk = int(log2(kb) / 10)
    suffix = BINARY_PREFIX[logk]
    unit = 2 ** (logk * 10)
    return '{}{}b'.format(int(kb / unit), suffix)

def get_mountpoints():
    '''Get list of where hugepage filesystem is mounted'''
    mounted = []
    with open('/proc/mounts') as mounts:
        for line in mounts:
            fields = line.split()
            if fields[2] != 'hugetlbfs':
                continue
            mounted.append(fields[1])
    return mounted

def is_numa():
    '''Test if numa is used on this system'''
    return os.path.exists('/sys/devices/system/node')

def set_hugepage():
    pass

def show_numa_pages():
    '''Show huge page reservations on numa system'''
    print('Node Pages Size Total')
    for numa_path in glob.glob('/sys/devices/system/node/node*'):
        node = numa_path[29:]  # slice after /sys/devices/system/node/node
        path = numa_path + '/hugepages/'
        for hdir in os.listdir(path):
            comm = Command("cat %s" % path + hdir + '/nr_hugepages')
            pages = int(comm.read())
            if pages > 0:
                kb = int(hdir[10:-2])  # slice out of hugepages-NNNkB
                print('{:<4} {:<5} {:<6} {}'.format(node, pages,
                        fmt_memsize(kb),
                        fmt_memsize(pages * kb)))

def show_non_numa_pages():
    '''Show huge page reservations on non numa system'''
    print('Pages Size Total')
    hugepagedir = '/sys/kernel/mm/hugepages/'
    for hdir in os.listdir(hugepagedir):
        comm = Command("cat %s" % hugepagedir + hdir + '/nr_hugepages')
        pages = int(comm.read())
        if pages > 0:
            kb = int(hdir[10:-2])
            print('{:<5} {:<6} {}'.format(pages, fmt_memsize(kb),
                    fmt_memsize(pages * kb)))

def check_hugepage_allocate(isnuma):
    if not isnuma:
        hugepagedir = '/sys/kernel/mm/hugepages/'
    else:
        numaid = 0 # TODO: detect numaid
        hugepagedir = '/sys/devices/system/node/node%d/hugepages/' % numaid

    for (_, dirs, _) in os.walk(hugepagedir):
        for directory in dirs:
            comm = Command("cat %s" % hugepagedir + directory + '/nr_hugepages')
            nb = comm.read()
            if int(nb) != 0:
                return True
        break
    return False
    # return false when
    # 1. no files in hugepagedir, 2. no non-zero entry was found
