#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"
#include "cache.h"
#include "net.h"

static bool isMounted = false;
/*Helper function that isolates bits. 
 *Field 1 = Command
 *Field 2 = DiskID
 *Field 3 = Reserved
 *Field 4 = BlockID
 */
uint32_t createOpCode(uint32_t command, uint32_t diskID, uint32_t reserved, uint32_t blockID){
	uint32_t opCode = 0; /* Our unsigned, 32 bit op code, which will be constructed from the four different-sized bit widths*/

	command = command << 26;  /* Bit width 6, corresponds to bits 26-31 */
	diskID = diskID <<  22;   /* Bit width of 4, corresponds to bits 22-25 */
	reserved = reserved << 8; /* Bit width of 14, corresponds to bits 8-21 */
	                          /* BlockID is fine as is, since its already the 8 least significant bits */
	opCode = opCode | command | diskID | reserved | blockID;
	return opCode;
}


//If mounted, subsequent mounts must fail. If unmounted, mount must succeed.
int mdadm_mount(void) {
	jbod_cmd_t op = createOpCode(JBOD_MOUNT, 0, 0, 0); //Creates  32 bit command

	if (!isMounted){
		jbod_client_operation(op, NULL); //Mounts disc
		isMounted = true;
		return 1;
	}
	
	return -1;
}

//If mounted, unmount must succeed. If unmounted, subsequent unmount must fail.
int mdadm_unmount(void) {
	jbod_cmd_t op = createOpCode(JBOD_UNMOUNT, 0, 0, 0); //Creates our 32 bit command

	if (isMounted){
		jbod_client_operation(op, NULL); //Unmounts disc
		isMounted = false;
		return 1;
	}
	
	return -1;
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
	// We are reading from starting address (addr) to the end address (addr + len). 
	int endRead = addr + len;     
	
	// Invalid parameters for read below
	if ((!isMounted) || (endRead > 0x100000) || (len > 0x400) || (buf == 0 && len != 0)){
		return -1;
	}
	
	int diskValue  = addr / JBOD_DISK_SIZE; 			/* Disk size is 65,536, to be used to seek to disk (tell's us which disk we are on) */
	int blockValue = (addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;   /* To be used to seek to block, as we determine what disk then what 	*/
	int success;						/* Boolean for whether the disk was just changed */

	 
	uint8_t readValues[JBOD_BLOCK_SIZE]; // readValues will, at most, only need to store 256 values: the block size. 
	uint8_t *readPtr; 		      // Pointer to hold readValues address
	readPtr = &readValues[(addr % JBOD_DISK_SIZE) % JBOD_BLOCK_SIZE];
	
	/*
	 * We need to convert our parameter for the operation into a 32 bit value with corresponding bit positions
	 * In order (Command, DiskID, Reserve, BlockID)
	 */ 
	 
	 jbod_cmd_t op_seek_disk = createOpCode(JBOD_SEEK_TO_DISK, diskValue, 0, 0);
	 jbod_cmd_t op_seek_block = createOpCode(JBOD_SEEK_TO_BLOCK, 0, 0, blockValue);
	 jbod_cmd_t op_read_block = createOpCode(JBOD_READ_BLOCK, 0, 0, 0); 
	 
	 jbod_client_operation(op_seek_disk, NULL); 	       // Sets up disk
	 jbod_client_operation(op_seek_block, NULL); 	      // Sets up block to read from
	 
	 if (cache_enabled())
	 {
	 	success = cache_lookup(diskValue, blockValue, readValues);
	 	if (success == -1) // Cache functions return 1 or -1.
	 	{
	 		jbod_client_operation(op_read_block, readValues);  // Performs read, read values stored in readValues array
	 		cache_insert(diskValue, blockValue, readValues);
	 	}
	
	 }
	 	
	 else
	 {
		jbod_client_operation(op_read_block, readValues);  // Performs read, read values stored in readValues array
	 }
	 
	/*
	 * We copy len bytes from readValues into buf
	 * will also increment addr everytime we copy from read values to buf
	 */
	while (addr < endRead)
	{
	 
		memcpy(buf, readPtr, 1);
		buf++;
		readPtr++;
	/*
	 * If we reach the end of the block, i.e. when the next address is divisible by the disk size, need to switch disks
	 * We seek to the next disk, diskValue's calculation is based on the most recently copied byte, so we add 1 
	 * We also note that JBOD_DISK_SIZE is a multiple of JBOD_BLOCK_SIZE
	 * Additionally, whenever we have a new disk, we start from block zero since we read linearly.
	 * We also start begin overwriting readValues from its first index again, as its previous contents have already been copied
	 * and we always have to read a whole block at a time. This is done also at the end of blocks.
	 */
	
		if ((addr % JBOD_DISK_SIZE) == JBOD_DISK_SIZE - 1)
		{
			diskValue++;
			blockValue = 0;
			
			op_seek_disk = createOpCode(JBOD_SEEK_TO_DISK, diskValue, 0, 0);
			op_seek_block = createOpCode(JBOD_SEEK_TO_BLOCK, 0, 0, blockValue);
			
			jbod_client_operation(op_seek_disk, NULL);
			jbod_client_operation(op_seek_block, NULL);
			
			if (cache_enabled())
			{
				success = cache_lookup(diskValue, blockValue, readValues);
				if (success == -1)
				{
					jbod_client_operation(op_read_block, readValues);
					cache_insert(diskValue, blockValue, readValues);
				}
		
			}
			
			else
			{
				jbod_client_operation(op_read_block, readValues);
			}
			
			
			readPtr = readValues;
		}
	/*
	 * If we reach the end of the block and are not done reading bytes, we need to move to the next block
	 * Example: We reach address 255. 255 mod 256 = 255, signifying end of block. Need to seek to correct block.
	 * We also note that everytime read is called, it increments the block for us. So we just need to call read again to continue reading on subsequent blocks
	 * Which once again, is indicated by reaching index (byte) 255 of the block.
	 */
		else if ((addr % JBOD_BLOCK_SIZE == JBOD_BLOCK_SIZE - 1))
		{
			blockValue += 1;
			op_seek_block = createOpCode(JBOD_SEEK_TO_BLOCK, 0, 0, blockValue);
			jbod_client_operation(op_seek_block, readValues);
			
			if (cache_enabled())
			{
				success = cache_lookup(diskValue, blockValue, readValues);
				if (success == -1)
				{
					jbod_client_operation(op_read_block, readValues);
					cache_insert(diskValue, blockValue, readValues);
				}
			}
			else
			{
				jbod_client_operation(op_read_block, readValues);
			}
			
			
			readPtr = readValues;
		}
	// We increment the address for the next iteration.
	
		addr++;
	}

  	return len;
}


int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {

	int endRead = addr + len; /* We are reading from starting address (addr) to the end address (addr + len).  */
	
	if ( (!isMounted) || (endRead > 0x100000) || (len > 0x400) || (buf == 0 && len != 0) ){
		return -1;
	}
	
	int diskValue  = addr / JBOD_DISK_SIZE; 			/* Disk size is 65,536, to be used to seek to disk (tell's us which disk we are on) */
	int blockValue = (addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;   /* To be used to seek to block, as we determine what disk then what 	*/
	int success;

	uint8_t readValues[JBOD_BLOCK_SIZE]; // readValues will, at most, only need to store 256 values: the block size. 
	uint8_t *readPtr; 		      // Pointer to hold readValues address
	readPtr = &readValues[(addr % JBOD_DISK_SIZE) % JBOD_BLOCK_SIZE];
	
	/*
	 * We need to convert our parameter for the operation into a 32 bit value with corresponding bit positions
	 * In order (Command, DiskID, Reserve, BlockID)
	 */ 
	
	jbod_cmd_t op_seek_disk = createOpCode(JBOD_SEEK_TO_DISK, diskValue, 0, 0);     // We assign it command and the diskValue
	jbod_cmd_t op_seek_block = createOpCode(JBOD_SEEK_TO_BLOCK, 0, 0, blockValue); // We assign it command and the blockValue
	jbod_cmd_t op_read_block = createOpCode(JBOD_READ_BLOCK, 0, 0, 0);
	jbod_cmd_t op_write_block = createOpCode(JBOD_WRITE_BLOCK, 0, 0, 0); // We assign it command and the blockValue
	
	jbod_client_operation(op_seek_disk, NULL); 	       // Sets up disk
	jbod_client_operation(op_seek_block, NULL); 	      // Sets up block to read
	
	if (cache_enabled())
	{
		success = cache_lookup(diskValue, blockValue, readValues);
		if (success == -1)
		{
			jbod_client_operation(op_read_block, readValues);  // Because writes must write to the whole block, we need this preserve values not meant to be overwritten
			jbod_client_operation(op_seek_block, NULL);        // Reading increments the current block, so we must re-seek to the return the block we plan to write to.
			cache_insert(diskValue, blockValue, readValues);	
		}
		
	}
	
	else
	{
	
		jbod_client_operation(op_read_block, readValues);  // Because writes must write to the whole block, we need this preserve values not meant to be overwritten
		jbod_client_operation(op_seek_block, NULL);        // Reading increments the current block, so we must re-seek to the return the block we plan to write to.	
	}


	/*
	 * We copy len bytes from readValues into buf
	 * will also increment addr everytime we copy from read values to buf
	*/
	while (addr < endRead)
	{
		
		memcpy(readPtr, buf, 1);
		buf++;
		readPtr++;
		
		if ((addr % JBOD_DISK_SIZE) == JBOD_DISK_SIZE - 1) // if End of disk
		{
			jbod_client_operation(op_write_block, readValues);
			
			if (cache_enabled()) 
			{
				cache_update(diskValue, blockValue, readValues);
			}
			
			diskValue++; 
			blockValue = 0;
			
			op_seek_disk = createOpCode(JBOD_SEEK_TO_DISK, diskValue, 0, 0);
			op_seek_block = createOpCode(JBOD_SEEK_TO_BLOCK, 0, 0, 0);
			
			jbod_client_operation(op_seek_disk, NULL);
			jbod_client_operation(op_seek_block, NULL);
			
			if (cache_enabled())
			{
				success = cache_lookup(diskValue, blockValue, readValues);
				if (success == -1)
				{
					op_read_block = createOpCode(JBOD_READ_BLOCK, 0, 0, 0);
					op_write_block = createOpCode(JBOD_WRITE_BLOCK, 0, 0, 0); // We assign it command and the blockValue
				
					jbod_client_operation(op_read_block, readValues);
					cache_insert(diskValue, blockValue, readValues);
					jbod_client_operation(op_seek_block, NULL);
				}
				
			}
			
			else
			{
				op_read_block = createOpCode(JBOD_READ_BLOCK, 0, 0, 0);
				op_write_block = createOpCode(JBOD_WRITE_BLOCK, 0, 0, 0); // We assign it command and the blockValue
			
				jbod_client_operation(op_read_block, readValues);
				jbod_client_operation(op_seek_block, NULL);
			
			}
		
			readPtr = readValues;
		} 
	
		
		else if ((addr % JBOD_BLOCK_SIZE == JBOD_BLOCK_SIZE - 1))
		{
			jbod_client_operation(op_write_block, readValues);
			if (cache_enabled()) 
			{
				cache_update(diskValue, blockValue, readValues);
			}
			
			blockValue += 1;
			
			op_seek_disk = createOpCode(JBOD_SEEK_TO_DISK, diskValue, 0, 0);
			op_seek_block = createOpCode(JBOD_SEEK_TO_BLOCK, 0, 0, blockValue);
			
			jbod_client_operation(op_seek_block, NULL);
			
			if (cache_enabled())
			{
				success = cache_lookup(diskValue, blockValue, readValues);
				if (success == -1)
				{
					jbod_client_operation(op_read_block, readValues);
					jbod_client_operation(op_seek_block, readValues);
					
					cache_insert(diskValue, blockValue, readValues);
					jbod_client_operation(op_seek_block, readValues);
				}
			
			}
			
			else
			{
			
				jbod_client_operation(op_read_block, readValues);
				jbod_client_operation(op_seek_block, readValues);
			}
			
			readPtr = readValues;
		}
		 
		addr++;
	}
	
	if (cache_enabled()) 
	{
		cache_update(diskValue, blockValue, readValues);
	}
	
	jbod_client_operation(op_write_block, readValues);
  	return len;
}

/*
The following constants can be found in jbod.h
#define JBOD_NUM_DISKS            16
#define JBOD_DISK_SIZE            65536
#define JBOD_BLOCK_SIZE           256
#define JBOD_NUM_BLOCKS_PER_DISK  (JBOD_DISK_SIZE / JBOD_BLOCK_SIZE)
*/
