#include "ApplicationLayer.h"
#include "DataLink.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

int startApp(struct Application * app, const char * path, int type, const char * fileName, unsigned int nameLength){
	int r=0;

	app->status = type;
	app->bufPointer = 0;

	printf("startApp: %d type\n", type);
	if(type == SEND){
		printf("startApp: SEND\nstartApp: calling openW\n");
		r = openW(app, path, fileName, nameLength);
		printf("startApp: end openW\n");
	}
	else if (type == RECEIVE){
		printf("startApp: RECEIVE\nstartApp: calling openR\n");
		r = openR(app, path);
		printf("startApp: end openR\n");
		//printf("r: %d\n", r);
	}
	printf("startApp: Returning...\n");
	return r;

}

int openW(struct Application * app, const char * path, const char * filename, unsigned int nameLength){
	FILE * file;
	app->nameLength = nameLength;
	app->fileName = (char*) malloc(sizeof(app->nameLength));

	if(app->fileName == NULL)
		return -1;

	strcpy(app->fileName, filename);

	file = fopen(app->fileName, "rb"); // non text file
	printf("openW: opened %s\n",app->fileName);
	if(file == NULL){
		printf("openW: file NULL. Returning...\n");
		return -1;
	}

	fseek(file, 0, SEEK_END); //get file size
	app->fileSize = ftell(file); // returns current file position
	printf("openW: FileSize = %d\n", app->fileSize);
	fseek(file, 0, SEEK_SET); // return to the beginning of the file

	app->buf = (char*) malloc(app->fileSize);
	if(app->buf == NULL)
	{
		return -1;
	}

	fread(app->buf, app->fileSize, sizeof(char), file);
	fclose(file);

	app->filedes = llopen(path, SEND);
	if(app->filedes == -1)
		return -1;

	return 0;
}

int openR(struct Application * app, const char *path){
	printf("openR: calling llopen\n");
	app->filedes = llopen(path, RECEIVE);

	if(app->filedes == -1)
	{
		printf("openR: llopen failed. Returning...\n");
		return -1;
	}

		printf("openR: Returning...\n");
	return 0;
}

int writeApp(struct Application app){
	int pSize = MIN_CONTROL_P_SIZE + app.nameLength;

	char *c_packet = malloc(pSize);
	printf("writeApp: controlPacket allocated\n");
	startPacket(app, c_packet, START);
	printf("writeApp: name length: %d\n", c_packet[8]);

	if(llwrite(app.filedes, c_packet, pSize) == -1){
		printf("writeApp: Unable to send start control packet, connection timed out.... Returning...\n");
		return -1;
	}

	printf("writeApp: Start control Packet sent\n");
	//int packetsToSend = 1;
	/*
	----- NAO APAGAR!!!! -------*/
	int packetsToSend = app.fileSize / PACKET_SIZE;
	int bytesLeft = app.fileSize % PACKET_SIZE;

	printf("writeApp: packetsToSend=%d, bytesLeft=%d\n", packetsToSend, bytesLeft);
	//Sends the Data Packets
	int i;
	int size;
	FILE* logs = fopen("logsfixes.txt", "w");
	for(i = 0; i < packetsToSend; i++)
	{
		//------ NAO APAGAR!! -----
		if((i == packetsToSend-1) && bytesLeft != 0) //Last Byte
		{
			printf("writeApp: Last sent packet size\n");
			size = bytesLeft;
		}
		else
			size = PACKET_SIZE;
		//size = app.fileSize;
		char * d_packet = (char *)malloc(sizeof(int)*(size+MIN_DATA_P_SIZE));

		fprintf(logs, "Packet Serial Number: %d with size= %d\n", i, size);

		printf("writeApp: size of packet: %d\n", size);

		dataPacket(&app, d_packet, i);
		if(llwrite(app.filedes, d_packet, size) == -1)
		{
			printf("writeApp: Unable to send data packet %d, connection timed out.... Returning...\n", i);
			free(d_packet);
			return -1;
		}
		printf("writeApp: before free\n");
		free(d_packet);
		printf("writeApp: after free\n");
	}
	fclose(logs);

	startPacket(app, c_packet, END);
	if(llwrite(app.filedes, c_packet, pSize) == -1){
		printf("writeApp: Unable to send end control packet, connection timed out.... Returning...\n");
		free(c_packet);
		return -1;
	}

	free(c_packet);

	printf("writeApp: Returning...\n");
	return 0;
}

int readApp(struct Application app)
{
	printf("readApp: Initializing...\n");
	/*
	FILE * file = fopen(app.fileName, "w");
	printf("readApp: opened File %s\n", app.fileName);
	*/

	char * startPacket = malloc(MAX_CONTROL_P_SIZE);
	char * endPacket = malloc(MAX_CONTROL_P_SIZE);
	printf("readApp: allocated start and end packets\n");

	int resultStartPacket =	llread(app.filedes, startPacket); //Reads the start packet
	if(resultStartPacket == -1)
	{
		printf("readApp: llread(startPacket) failed\n");
		return -1;
	}
	else if(resultStartPacket == 1)
	{
		printf("readApp: llread(startPacket) repeated\n");
	}

	printf("readApp: read the StartPacket\n");
	if(verifyControlPacket(startPacket, START) != 1)
	{
		printf("readApp: StartPacket verification FAILED \n");
		return -1;
	}

	printf("readApp: StartPacket verification successful\n");

	int fileSize;
	int nameSize;
	memcpy(&fileSize, &startPacket[3], startPacket[2]); //Reads the file size from the packet
	memcpy(&nameSize, &startPacket[8], 1); //Reads the file's name size from the packet
	char * fileName = malloc(nameSize);
	memcpy(fileName, &startPacket[9], startPacket[8]); //Reads the file's name from the packet
	app.fileName = fileName; //TODO e necessario alterar a app? Nao seria melhor passa-la por referencia?
	printf("readApp: retrieved file information from StartPacket\n");

	FILE * file = fopen(fileName, "w");
	printf("readApp: opened File %s\n", fileName);

	//int numPackets = 1;
	//NAO APAGAR!!!!!!
	int numPackets = ceil((float) (fileSize/PACKET_SIZE));
	int i;
	char * dataPacket = malloc(4+MAX_DATA_P_SIZE);
	for(i = 0; i < numPackets; i++) //Reads each packet, and copies the data to the new file
	{
		int packetSize = llread(app.filedes, dataPacket); //Assumindo que llread retorna o numero de bytes do packet
		if(packetSize == -1)
		{
			printf("readApp: datapacket with error. llread failed\n");
			return -1;
		}
		fwrite(&dataPacket[4], sizeof(char), packetSize-MIN_DATA_P_SIZE, file); //Writes the data to the new file
	}

	if(llread(app.filedes, endPacket) == -1) //Reads the end packet
	{
		printf("readApp: llread(endPacket) failed\n");
		return -1;
	}
	if(verifyControlPacket(endPacket, END) != 1)
	{
		printf("readApp: EndPacket verification failed \n");
		return -1;
	}

	return 0;

}

void startPacket(struct Application app, char * c_packet, char CONTROL_FLAG){
	printf("startPacket: Initializing...\n");
	c_packet[0] = CONTROL_FLAG;
	c_packet[1] = 0;
	c_packet[2] = sizeof(int);
	printf("startPacket: CONTROL(%02x), T1(%02x), L1(%02x) defined\n", c_packet[0], c_packet[1], c_packet[2]);
	memcpy(&c_packet[3], &app.fileSize, sizeof(int));//Atencao!!! Alguns Linux's fazem memcpy ao contrario. Para obter o valor de novo, temos de fazer memcpy outra vez
	printf("startPacket: V1 defined\n");
	c_packet[7] = 1;
	c_packet[8] = app.nameLength;
	printf("startPacket: T2, L2 defined\n");
	memcpy(&c_packet[9], app.fileName, app.nameLength);
	printf("startPacket: V2(%s) defined\nstartPacket: Done. Returning...\n", &c_packet[9]);
	//printf("fileName == pinguim.gif ? %d\n", strcmp("pinguim.gif", &c_packet[9]));
	return;
}

void dataPacket(struct Application * app, char * d_packet, int serialNumber){
	printf("dataPacket: Initializing...\n");//aqui
	d_packet[0] = DATA_CONTROL;
	d_packet[1] = serialNumber % 255;
	d_packet[2] = app->fileSize % 256;
	d_packet[3] = (int)(app->fileSize / 256);
	printf("dataPacket: CONTROL, N, L2, L1 defined\n");
	memcpy(&d_packet[4], app->buf+app->bufPointer, PACKET_SIZE); //TODO TEMPORARIO. TEM DE SER TROCADO POR PACKETS MAIS RECENTES
	app->bufPointer += PACKET_SIZE;
	printf("dataPacket: bufPointer=%d\n", app->bufPointer);
	printf("dataPacket: DATA defined\ndataPacket: Done. Returning...\n");
	return;

}

/**
* Returns:
* 1 = Sucess | 0 = Failure | -1 = Error
*/
int verifyControlPacket(char * packet, int type)
{
	if(packet == NULL)
	{
		printf("verifyControlPacket: EMPTY PACKET \n");
		return -1; //Error
	}
	else if(type == packet[0])
	{
		printf("verifyControlPacket: CORRECT\n");
		return 1; //Its the correct control packet
	}
	else
	{
		printf("verifyControlPacket: INCORRECT\n");
		return 0; //Its an incorrect control packet
	}
}
