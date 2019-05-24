#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>

#include "utlist.h" // 3rd party list handling library


/*
///// On/Off logging
*/

#if 1
#define LOGGING
#endif

#define LOG(f_, ...) printf((f_), ##__VA_ARGS__);

#ifndef LOGGING
#undef LOG
#define LOG(f_, ...) do{}while(0)
#endif



/*
///// Basic defines
*/

#define TRUE  1
#define FALSE 0

#define MAX_LINE_SIZE    64
#define MAX_CLIENT_COUNT 32
#define MAX_SEQ_COUNT    32

#define PORT 12345

#ifndef UINTMAX_MAX
#define UINTMAX_MAX 0xFFFFFFFFFFFFFFFF
#endif



typedef struct
{
	uint32_t number;
	uint64_t of_number; 
	uint64_t start_value;
	uint64_t step;
} seq_data_t;

typedef struct
{
	struct sockaddr_in addr;
	int                id;
	int                client_fd;
	pthread_t          thread;
} cl_data_t;



typedef struct seq_element
{
	seq_data_t *data;
	struct seq_element *next, *prev;
} seq_element;

typedef struct cl_element
{
	cl_data_t *data;
	struct cl_element *next, *prev;
} cl_element;



/*
///// Global variables
*/

pthread_mutex_t e_mutex   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t con_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t l_mutex = PTHREAD_MUTEX_INITIALIZER;

int isOverflow  = FALSE;
int isExporting = FALSE;
int isOnExit    = FALSE;
int isSeqReady  = FALSE;

int socket_fd;
struct sockaddr_in address, cl_addr;
//int client_sd[MAX_CLIENT_COUNT] = {0};

seq_element *seq_list = NULL;
cl_element  *cl_list  = NULL;

uint64_t seqN           = 0;
uint64_t sendingValue   = 1;
uint64_t handlersCount  = 0;
uint32_t clientsCount   = 0;
uint64_t newClientId    = 0;

const char *help_message  = "\nAvailable commands:\n"
							"'seq# x y':\n"
							"\t# - Number of sequence\n"
							"\tx - Start sequence value\n"
							"\ty - Step of sequence\n"
							"'export seq' or 'e':\n"
							"\tStart sequence export process\n"
							"'stop' or 's':\n"
							"\tStop sequence export process\n"
							"'exit' or 'q':\n"
							"\tShutdown application\n"
							"'help':\n"
							"\tCall help message\n";



/*
///// Func's declaration /////
*/

int      nodeCmp              (seq_element *a, seq_element *b); // Compares 2 nodes with numCmp function
int      numCmp               (uint32_t a, uint32_t b);         // Compares 2 numbers of node's sequence struct
void     stripMsg             (char *s);                        // Strips message

int      socketInit           ();
void    *socketThread         ();
void    *connectionHandler    (void *s);
void     sendMsg              (char *msg);

void     sequenceGeneratorStr (char     *buffer, int size, seq_element *data);
void     sequenceGeneratorUInt(uint64_t *value, seq_element *data);
void     sequenceAddPreset    (uint64_t num, uint64_t start, uint64_t step);
void    *sequenceThread       ();
void     commandHandler       (char *command);
void     mainLoop             ();



/*
///// Func's defenition /////
*/

int nodeCmp(seq_element *a, seq_element *b)
{
	return numCmp(a->data->number, b->data->number);
}

int numCmp(uint32_t a, uint32_t b)
{
	if (a < b)
		return -1;
	else
		if (a > b)
			return 1;
		else
			return 0;
	//return a < b ? -1 : (a > b ? 1 : 0);
}

void stripMsg(char *s)
{
    while (*s != '\0')
    {
        if (*s == '\r' || *s == '\n')
        {
            *s = '\0';
        }
        s++;
    }
}

int socketInit()
{
	LOG("INFO:  Server socket initialization!\n");
	int opt = 1;

	if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
	{
		LOG("ERROR: Initialization: Failed to create socket!\n");
		return -1;
	}
	
	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
	{
		LOG("ERROR: Initialization: Failed to attach port!\n");
		return -1;
	}
	
	address.sin_family      = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port        = htons(PORT);

	LOG("INFO:  Binding to port!\n");
	if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
	{
		LOG("ERROR: Initialization: Failed to bind port!\n");
		return -1;
	}

	LOG("INFO:  Listener on %d port!\n", PORT);
	if (listen(socket_fd, 5) < 0)
	{
		LOG("ERROR: Initialization: Failed to start listening!\n");
		return -1;
	}

	LOG("INFO:  Initialization successful!\n");
	return 0;
}



/*
///// Cap /////
*/

void *socketThread()
{
	LOG("INFO:  Socket thread is created\n");
	char buffer[MAX_LINE_SIZE];
	int new_socket, *new_sock;
	int addrlen = sizeof(address);

	struct sockaddr client;

	while (TRUE)
	{
		socklen_t cl_len = sizeof(cl_addr);
		new_socket = accept(socket_fd, (struct sockaddr *)&cl_addr, &cl_len);
		if (new_socket < 0)
		{
			LOG("ERROR: Socket accept!\n");
		}

		if (isOnExit)
		{
			LOG("INFO:  isOnExit event is true\n");
			break;
		}

		if ((clientsCount + 1) == MAX_CLIENT_COUNT)
		{
			LOG("WARN:  Maximum number of clients reached!\n");
			LOG("WARN:  Adding new client rejected!\n");
			close(new_socket);
			continue;
		}

		cl_data_t *new_data   = (cl_data_t *)malloc(sizeof(cl_data_t));
		new_data->id          = newClientId;
		new_data->client_fd   = new_socket;
		new_data->addr        = cl_addr;

		cl_element *new_node = (cl_element *)malloc(sizeof(cl_element));
		new_node->data = new_data;
		DL_APPEND(cl_list, new_node);

		pthread_t handler_thread;
		
		LOG("INFO:  Creating new connection handler\n");
		if ( pthread_create(&handler_thread, NULL, connectionHandler, (void *)new_node) < 0)
		{
			LOG("ERROR: Could not create new socket thread!\n");
		}

		usleep(100); // Wait for 0.1ms
	}
	LOG("INFO:  Exit from socket thread\n");
}

// Sends message to all clients
void sendMsg(char *msg)
{
	cl_element *temp;
	DL_FOREACH(cl_list, temp)
	{
		if (write(temp->data->client_fd, msg, MAX_LINE_SIZE) < 0)
		{
			LOG("ERROR: Write message to client is failed!");
			break;
		}
		usleep(100);
	}
}


// Handles new clients
void *connectionHandler(void *s)
{
	LOG("INFO:  New connection handler is created\n");

	pthread_mutex_lock(&con_mutex);
	newClientId++;
	pthread_mutex_unlock(&con_mutex);

	int readl;
	cl_element *client = (cl_element *)s;
	char buffer[MAX_LINE_SIZE];

	while ((readl = read(client->data->client_fd, buffer, MAX_LINE_SIZE - 1)) > 0)
	{
		if (isOnExit)
		{
			LOG("INFO:  Exit event in connection handler\n");
			break;
		}

		buffer[MAX_LINE_SIZE] = '\0';
		stripMsg(buffer);

		if (!strlen(buffer))
			continue;

		commandHandler(buffer);
		memset(buffer, 0, MAX_LINE_SIZE); // Clear buffer
	}

	LOG("INFO:  reducing connection handlers\n");
	pthread_mutex_lock(&l_mutex);
	handlersCount--;

	LOG("INFO:  deleteingfrom list\n");
	close(client->data->client_fd);
	DL_DELETE(cl_list, client);
	pthread_mutex_unlock(&l_mutex);

	LOG("INFO:  Exit from connection handler\n");

	pthread_detach(pthread_self());
	return NULL;
}



void *sequenceThread()
{
	LOG("INFO:  Sequence thread is created\n");
	char buffer[MAX_LINE_SIZE];
	seq_element *temp_node, *el;
	uint64_t wastedTime = 0;

	while (TRUE)
	{
		// Handling exporting sequences
		if (isExporting)
		{
			DL_FOREACH(seq_list, temp_node)
			{
				pthread_mutex_lock(&con_mutex);

				sequenceGeneratorUInt(&sendingValue, temp_node);
				printf("%lu\n", sendingValue);
				snprintf(buffer, MAX_LINE_SIZE, "%lu,", sendingValue);
				sendMsg(buffer);
				
				usleep(1000); // Wait for 1ms

				pthread_mutex_unlock(&con_mutex);
			}
			seqN++;
			memset(buffer, 0, MAX_LINE_SIZE);
		}

		// Handling exit
		if (isOnExit)
		{
			pthread_mutex_lock(&e_mutex);
			isExporting = FALSE;
			// Delete all sequence presets
			DL_FOREACH_SAFE(seq_list,el,temp_node)
			{
				DL_DELETE(seq_list,el);
				free(el->data);
				free(el);
			}
			break;
			pthread_mutex_unlock(&e_mutex);
		}
		usleep(100000); // Wait for 100ms
	}
	LOG("INFO:  Exit from sequence thread\n");
}



void sequenceGeneratorUInt(uint64_t *value, seq_element *data)
{
	// Overflow handling
	if ((data->data->step * seqN) > (0xFFFFFFFFFFFFFFFF - data->data->start_value))
	{
		data->data->of_number = seqN;
	}

	uint64_t res = data->data->start_value + data->data->step * (seqN - data->data->of_number);
	*value = res; 
}

void sequenceAddPreset(uint64_t num, uint64_t start, uint64_t step)
{
	seq_element *new_node;
	LOG("INFO:  ============\n");
	LOG("INFO:  New sequence\n");
	LOG("INFO:  %lu %lu %lu\n", num, start, step);
	LOG("INFO:  ============\n");

	// Generator preset
	seq_data_t *new_data  = (seq_data_t *)malloc(sizeof(seq_data_t));
	new_data->number      = num;
	new_data->of_number   = 0;
	new_data->start_value = start;
	new_data->step        = step;

	// Add preset to list
	new_node = (seq_element *)malloc(sizeof(seq_element));
	new_node->data = new_data;
	DL_APPEND(seq_list, new_node);
	DL_SORT  (seq_list, nodeCmp);
}



void commandHandler(char *command)
{
	// Sequence initialization command
	if (strncmp("seq", command, 3) == 0)
	{
		char a = command[3];
		if (a < '0' || a > '9')
		{
			LOG("WARN:  Wrong command\n");
			LOG("%s\n", help_message);
		}
		else
		{
			const int seqOffset = 3;
			uint64_t commandData[3];

			sscanf(command + seqOffset, "%lu %lu %lu", &commandData[0], &commandData[1], &commandData[2]);
			if (!commandData[0] || !commandData[1] || !commandData[2]) // !data[] for data[] == 0
			{
				LOG("WARN:  Wrong command\n");
				LOG("%s\n", help_message);
				return;
			}

			sequenceAddPreset(commandData[0], commandData[1], commandData[2]);
		}
		return;
	}

	// Sequence export command
	if (command[0] == 'e' || strncmp("export seq",command, 10) == 0)
	{
		LOG("INFO:  ============\n");
		LOG("INFO:  Exporting...\n");
		LOG("INFO:  ============\n");

		pthread_mutex_lock(&e_mutex);
		isExporting = TRUE;
		pthread_mutex_unlock(&e_mutex);

		return;
	}

	/*
	///// Additional (debug) commands
	*/

	// Prints help
	if ((strncmp("help",command, 4) == 0))
	{
		printf("%s\n", help_message);
		return;
	}

	// Stop exporting
	if (command[0] == 's' || (strncmp("stop",command, 4) == 0))
	{
		LOG("INFO:  Call to stop export\n");

		pthread_mutex_lock(&e_mutex);
		isExporting = FALSE;
		pthread_mutex_unlock(&e_mutex);
		return;
	}

	// Stop exporting and exit from application
	if (command[0] == 'q' || (strncmp("exit",command, 4) == 0) || (strncmp("quit",command, 4) == 0))
	{
		LOG("INFO:  Call to exit From App\n");
		
		pthread_mutex_lock(&e_mutex);
		isExporting = FALSE;
		isOnExit = TRUE;
		pthread_mutex_unlock(&e_mutex);
		return;
	}
}



/*
///// Yeah, Cap again /////
*/

void mainLoop()
{
	char command[MAX_LINE_SIZE];

	pthread_t seqThread;
	pthread_t sockThread;

	socketInit();

	LOG("INFO:  Creating new threads!\n");
	pthread_create(&seqThread,  NULL, sequenceThread, NULL);
	pthread_create(&sockThread, NULL,   socketThread, NULL);

	LOG("INFO:  Starting main loop!\n");
	while (TRUE)
	{
		if (fgets(command, MAX_LINE_SIZE, stdin) == NULL)
			continue;

		commandHandler(command);
		if (isOnExit)
			break;
	}

	LOG("INFO:  Joining threads!\n");
	LOG("INFO:  Joining sequenceThread!\n");
	pthread_join  (seqThread,  NULL);
	LOG("INFO:  Closing socketThread!\n");
	pthread_cancel(sockThread);
	LOG("INFO:  Closed socketThread!\n");

	pthread_mutex_destroy(&e_mutex);
	pthread_mutex_destroy(&con_mutex);

	close(socket_fd);

	LOG("INFO:  Exit main loop!\n");
}



/*
///// oh, come on, Cap /////
*/

int main(int argc, char const *argv[])
{
	LOG("INFO:  Start app!\n");

	mainLoop();
	
	LOG("INFO:  Exit app!\n");
	return 0;
}