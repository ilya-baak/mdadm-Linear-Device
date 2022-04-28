# mdadm-Linear-Device
An implementation of the Linear Device functionality in mdadm for JBOD with caching and network support.
PLEASE NOTE: I do not own the tester, trace files, JBOD object files, JBOD Server or utility files. These were provided to me, as the focus of this project was actually implementation of mdadm, the cache, and network. 

The following were implemented by myself: mdadm.c, cache.c, and net.c

mdadm.c is a layer above JBOD. Given the device driver for JBOD, with a list of commands such as mount/unmount JBOD, disk/block seeking, and read/write (you can find these commands in JBOD.h), mdadm.c implements the device driver functions for JBOD as a Linear Device.

cache.c handles the cache creation and functionality. We use the cache in order to improve the performance of mdadm.c, as storing blocks into the cache allows to read from the disk less. This overall reduces the latency. We utilize write through caching here and implement the least recently used (LRU) cache replacement policy. The cache itself, for testing purposes, is able to range from a size of 2 to 4096 entries, and when testing random orders of read/write, one can observe a signicant reduction in cost--about 30%. The functions of cache.c can be seen implemented in mdadm.c--specifically in mdadm_read and mdadm_write.

net.c allows for execution of JBOD operations over a network. This file contains the functions for connecting/disconnecting to the JBOD server, execution of networks reads and writes, and for creating, sending, and receiving packets.
