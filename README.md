# mdadm-Linear-Device
An implementation that simulates the Linear Device functionality in mdadm for JBOD, which provides an interface for managing a collection of disks as a single logical volume.

# mdadm.c
This is a layer just above JBOD. Given the device driver for JBOD, with a list of commands such as mount/unmount JBOD, disk/block seeking, and read/write (you can find these commands in JBOD.h), mdadm.c implements the device driver functions for JBOD as a Linear Device.

### Functions
uint32_t createOpCode(uint32_t command, uint32_t diskID, uint32_t reserved, uint32_t blockID)
This helper function creates a 32-bit operation code (opCode) by combining four separate bit fields for command, diskID, reserved, and blockID. The resulting opCode is returned as a uint32_t value.

int mdadm_mount(void)
This function mounts the JBOD, making it ready for read operations. If the JBOD is already mounted, subsequent mount attempts will fail. The function returns 1 on success and -1 if the JBOD is already mounted.

int mdadm_unmount(void)
This function unmounts the JBOD, making it unavailable for read operations. If the JBOD is not currently mounted, subsequent unmount attempts will fail. The function returns 1 on success and -1 if the JBOD is not mounted.

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf)
This function reads data from the JBOD starting from the specified address (addr) and copies it into the provided buffer (buf) for the specified length (len) of bytes. The function performs various checks to ensure that the read operation is valid, including checking if the JBOD is mounted, if the address and length are within valid ranges, and if the buffer is properly allocated. The function also supports caching of read data for improved performance, if enabled. The function returns -1 on failure and 0 on success.

# cache.c
cache.c handles the cache creation and functionality. We use the cache in order to improve the performance of mdadm.c, as storing blocks into the cache enables us to less read from the disk. This overall reduces the latency. We utilize write through caching here and implement the least recently used (LRU) cache replacement policy. The cache itself, for testing purposes, is able to range from a size of 2 to 4096 entries, and when testing random orders of read/write, one can observe a signicant reduction in cost. The functions of cache.c can be seen implemented in mdadm.c--specifically in mdadm_read and mdadm_write.

### Data Structure
The cache.c defines the following data structure:

cache_entry_t: Represents a cache entry, which contains information about a disk block stored in the cache. It has the following members:
disk_num: An integer that represents the disk number of the disk block.
block_num: An integer that represents the block number of the disk block.
block: An array of bytes that represents the data of the disk block.
valid: A boolean flag that indicates whether the cache entry is valid or not.
access_time: An integer that represents the access time of the cache entry, used for the LRU replacement policy.

### Functions
cache.c provides the following functions:

int cache_create(int num_entries): Creates a cache with the specified number of cache entries. The num_entries parameter specifies the maximum number of disk blocks that can be stored in the cache. Returns 1 on success, and -1 on failure.

int cache_destroy(void): Destroys the cache and frees the memory allocated for the cache. Returns 1 on success, and -1 on failure.

int cache_lookup(int disk_num, int block_num, uint8_t *buf): Looks up a disk block in the cache by its disk number and block number, and copies the data of the block to the specified buffer. Returns 1 on cache hit, -1 on cache miss, and -1 on failure.

void cache_update(int disk_num, int block_num, const uint8_t *buf): Updates the data of a disk block in the cache with the specified disk number and block number, using the data in the specified buffer. This function is called when a cache hit occurs.

int cache_insert(int disk_num, int block_num, const uint8_t *buf): Inserts a disk block into the cache with the specified disk number and block number, using the data in the specified buffer. If the cache is full, it replaces the least recently used block with the new block. Returns 1 on success, and -1 on failure.

# net.c
net.c allows for execution of JBOD operations over a network. This file contains the functions for connecting/disconnecting to the JBOD server, execution of reads and writes, and for creating, sending, and receiving packets.

### Functions
nread(int fd, int len, uint8_t *buf)
This function attempts to read len bytes from the file descriptor fd and stores the read data into the buffer pointed to by buf. It may need to call the system call read multiple times to read the entire data. The function returns true on success and false on failure.

nwrite(int fd, int len, uint8_t *buf)
This function attempts to write len bytes to the file descriptor fd from the buffer pointed to by buf. It may need to call the system call write multiple times to write the entire data. The function returns true on success and false on failure.

recv_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block)
This function receives a packet from the server using the file descriptor sd and stores the received data into the memory locations pointed to by op, ret, and block. The function returns true on success and false on failure. The op parameter is used to store the jbod "opcode", the ret parameter is used to store the return value of the server side calling the corresponding jbod operation function, and the block parameter holds the received block content if existing (e.g., when the opcode is JBOD_READ_BLOCK). The function reads the packet header first (i.e., reads HEADER_LEN bytes) and then uses the length field in the header to determine whether it is needed to read a block of data from the server. The nread function is used to perform the actual reading.

create_packet(uint16_t length, uint32_t opCode, uint16_t returnCode, uint8_t *block)
This function creates a jbod request packet with the specified length, opCode, returnCode, and block parameters. The length parameter specifies the length of the packet, the opCode parameter specifies the jbod operation code, the returnCode parameter specifies the return value of the server side calling the corresponding jbod operation function, and the block parameter contains the data to write to the server jbod system for the JBOD_WRITE_BLOCK operation. The function returns a pointer to the constructed jbod request packet: block.
