#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;


/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
//Read returns the number of sucessful bytes read, so we simply take note of how many are wrttien
//End when len bytes have been written
	int totalBytesRead = 0;
	int currentBytesRead = 0;
	while (totalBytesRead < len)
	{
		currentBytesRead = read(fd, buf + totalBytesRead, len - totalBytesRead);
		
		if (currentBytesRead == -1)
		{
			return false;
		}
		
		totalBytesRead += currentBytesRead;
	}
	
	return true;
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
//Same logic as the previous function, as write also returns the number of successful bytes written
	int totalBytesWritten = 0;
	int currentBytesWritten = 0;
	while (totalBytesWritten < len)
	{
		currentBytesWritten = write(fd, buf + totalBytesWritten, len - totalBytesWritten);
		
		if (currentBytesWritten == -1)
		{
			return false;
		}
		
		totalBytesWritten += currentBytesWritten;
	}
	
	return true;
}



/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 
op - the address to store the jbod "opcode"  
ret - the address to store the return value of the server side calling the corresponding jbod_operation function.
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)
In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/


static bool recv_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block) {
// Packet will posess the data elements we are interested in
	uint8_t packet[HEADER_LEN];
	uint16_t length;

	// We extra the the 8 bytes and place them into packet
	if (nread(sd, HEADER_LEN, packet) == false)
	{
		return false;
	}
	// We update the values desired.
	memcpy(&length, packet, 2);
	memcpy(op, packet + 2, 4);
	memcpy(ret, packet + 6, 2);
	
	//uint8_t temporaryBuffer[HEADER_LEN];
	
	// We adjust the values for the network
	length = htons(length);
	*op = htonl(*op);
	*ret = htons(*ret);
	
	// Determines whether we need to the block parameter
	if (length == (HEADER_LEN + JBOD_BLOCK_SIZE))
	{
		if (nread(sd, JBOD_BLOCK_SIZE, block) == true) 
		{
			return true;
		}
		
		else
		{
			return false;
		}
		
	}
	
	return true;
}



/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 
op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.
The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/

uint8_t * create_packet(uint16_t length, uint32_t opCode, uint16_t returnCode, uint8_t *block){
// Helper function to construct the packet, works similiarly to the previous function but reversed. We store into the array with our adjust values.
	int packet_size = length;
	uint8_t packet[packet_size];
	uint8_t *packetPtr;
	packetPtr = packet;
	
	length = ntohs(length);
	opCode = ntohl(opCode);
	returnCode = ntohs(returnCode);
	
	//We store into the array with our adjust values.
	memcpy(packet, &length, 2);
	memcpy(packet + 2, &opCode, 4);
	memcpy(packet + 6, &returnCode, 2);
	
	// Determines if we need the block parameter
	if (packet_size == 264)
	{
		memcpy(packet + 8, block, JBOD_BLOCK_SIZE);
	}
	
	return packetPtr;
	
}

// We implement our helper function in here, simplifying the logic considerably
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
	uint32_t length;
	uint16_t returnCode = 0;
	if ((op >> 26) == JBOD_WRITE_BLOCK)
	{
		length = HEADER_LEN + 256;
	}
	
	else
	{
		length = HEADER_LEN;
	}
	
	uint8_t *packet;
	packet = create_packet(length, op, returnCode, block);
	
	if (nwrite(sd, length, packet) == false)
	{
		return false;
	}
	
	return true;
	
}



/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/

//Connects to the server.
bool jbod_connect(const char *ip, uint16_t port) {
	struct sockaddr_in caddr;
	
	cli_sd = socket(AF_INET, SOCK_STREAM, 0);
	if (cli_sd == -1)
	{
		return false;
	}
	
	caddr.sin_family = AF_INET;
	caddr.sin_port = htons(port);
	
	if (inet_aton(ip, &caddr.sin_addr) == 0) 
	{
		return false;
	}
	
	if (connect(cli_sd, (const struct sockaddr *) &caddr, sizeof(caddr)) == -1) 
	{
		return false;
	} 
	
	return true;
}




/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
	close(cli_sd);
	cli_sd = -1;
}



/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 
The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
//Below function implements send and receive.
int jbod_client_operation(uint32_t op, uint8_t *block) {
	
	uint16_t returnValue;
	
	send_packet(cli_sd, op, block);
	
	recv_packet(cli_sd, &op, &returnValue, block);
	
	return returnValue;
}
