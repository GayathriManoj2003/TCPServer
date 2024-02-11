#include <vector>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <iostream>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unordered_map>
#include <queue>

using namespace std;

// Define number of worker threads
const int num_threads = 10;

// Queue for storing active clients
queue<int> clients;

// Shared Key-value datastore
unordered_map<string, string> KV_DATASTORE;

// Define mutex locks for map access and queue access
pthread_mutex_t map_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

// Handle individual client connections
void handleConnection(int);

// Thread routine
void* startRoutine(void *);

// Create and configure server socket
int getServerSocket(const int &port);

// Add new client connection to queue
void addToQueue(int client_fd);

int main(int argc, char **argv)
{
	int port;

	/*
	 * check command line arguments
	 */
	if (argc != 2)
	{
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	// Server port number taken as command line argument
	port = atoi(argv[1]);

	// Create server socket
	int server_fd = getServerSocket(port);
	if (server_fd < 0)
	{
		cerr << "Error: Failed to start server" << endl;
		exit(1);
	}

	// Prepare to accept connections on socket FD.
	if (listen(server_fd, 5) < 0)
	{
		cerr << "Error: Couldn't listen on socket" << endl;
		close(server_fd);
		return -1;
	}

	cout << "Server listening on port: " << port << endl;


	// // Initialize mutex lock
	// if (pthread_mutex_init(&map_lock, NULL) != 0) { 
    //     printf("\n mutex init has failed\n"); 
    //     return 1; 
  	// } 

	// // Initialize mutex lock
	// if (pthread_mutex_init(&queue_lock, NULL) != 0) { 
    //     printf("\n mutex init has failed\n"); 
    //     return 1; 
  	// } 

	sockaddr_in client_addr;
	socklen_t caddr_len = sizeof(client_addr);

	vector<pthread_t> thread_ids(num_threads);

	// Create worker threads
	for(int i = 0; i < num_threads; i++) {
		pthread_create(&thread_ids[i], NULL, &startRoutine, NULL);
	}

	while (true)
	{
		// Await a connection on socket FD.
		int client_fd = accept(server_fd, (sockaddr *)(&client_addr), &caddr_len);
		if (client_fd < 0)
		{
			cerr << "Error: Couldn't accept connection" << endl;
			exit(1);
		}
		// Add new connection to clients queue
		addToQueue(client_fd);
	}

	// ## CHECK
	// for( pthread_t tid : thread_ids ) {
	// 	pthread_join( tid, NULL);
	// }
	
	// Destroy mutex lock
	pthread_mutex_destroy(&map_lock);
	pthread_mutex_destroy(&queue_lock);

	// Close socket
	close(server_fd);

	return 0;
}

int getServerSocket(const int &port)
{
	/* 	Creates a TCP socket and binds socket to specified
		port.
		Returns configured socket file descriptor.
	 */

	// TCP socket Creation and Configuration

	// Server socket file descriptor
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (server_fd < 0)
	{
		cerr << "Error: Couldn't open socket" << endl;
		return -1;
	}

	// Structure to store configuration details
	struct sockaddr_in server_addr;
	socklen_t saddr_len = sizeof(server_addr);

	memset(&server_addr, 0, saddr_len);

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(port);

	// Bind the socket to the address and port number
	if (bind(server_fd, (struct sockaddr *)&server_addr, saddr_len) < 0)
	{
		cerr << "Error: Couldn't bind socket" << endl;
		close(server_fd);
		return -1;
	}

	return server_fd;
}

void addToQueue(int client_fd) {
	/* 	Add client_fd to clients queue */

	// Acquire lock before pushing client descriptor onto queue
	pthread_mutex_lock(&queue_lock);
	clients.push(client_fd);
	pthread_mutex_unlock(&queue_lock);
}

void* startRoutine(void *) {
	/* 	Start routine for worker threads.
		Pops client_fd from queue and calls client handler function.
	*/

	// Detach current thread from calling thread
	pthread_detach(pthread_self());

	bool run = true;
	while( run ) {
			int client_fd;

			// Acquire queue_lock before accessing clients queue
			pthread_mutex_lock(&queue_lock);
			if(!clients.empty()) {
				client_fd = clients.front();
				clients.pop();
				// Release lock
				pthread_mutex_unlock(&queue_lock);

				// Call handler function for popped client
				handleConnection(client_fd);
			} else {
				// Release lock
				pthread_mutex_unlock(&queue_lock);
			}
	}
	pthread_exit(NULL);
}
void handleConnection(int client_fd)
{
	/* 	Handle Individual client connections and process
		and respond to messages sent by the client.
	*/

	// Buffer to read in messages from client
	char buffer[1024];
	bool end = false;
	string response;
	string key, value;

	// Until client sends END message
	while (!end)
	{
		memset(buffer, 0, sizeof(buffer));
		int bytesReceived = recv(client_fd, buffer, sizeof(buffer), 0);
		if (bytesReceived < 0)
		{
			cerr << "Error: Couldn't receive message" << endl;
			exit(1);
		}
		else if (bytesReceived == 0)
		{
			cout << "Client disconnected." << endl;
			break;
		}
		else
		{
			string query;
			stringstream strm(buffer);
			while (getline(strm, query))
			{

				if (query == "READ")
				{
					// READ query

					getline(strm, key);

					pthread_mutex_lock(&map_lock);
					// Check for presence of key
					if (KV_DATASTORE.find(key) != KV_DATASTORE.end())
					{
						response = KV_DATASTORE[key] + "\n";
					}
					else
					{
						// Return NULL if key not present
						response = "NULL\n";
					}
					pthread_mutex_unlock(&map_lock);

				}
				else if (query == "WRITE")
				{
					// WRITE Query

					getline(strm, key);
					getline(strm, value);

					// Strip colon
					value = value.substr(1);

					pthread_mutex_lock(&map_lock);

					KV_DATASTORE[key] = value;
					response = "FIN\n";

					pthread_mutex_unlock(&map_lock);

				}
				else if (query == "COUNT")
				{
					// COUNT query
					pthread_mutex_lock(&map_lock);
					response = to_string(KV_DATASTORE.size()) + "\n";
					pthread_mutex_unlock(&map_lock);

				}
				else if (query == "DELETE")
				{
					// DELETE query

					getline(strm, key);
					int count = 0;

					pthread_mutex_lock(&map_lock);
					// Check for presence of key
					if (KV_DATASTORE.find(key) != KV_DATASTORE.end())
					{
						KV_DATASTORE.erase(key);
						response = "FIN\n";
					}
					else
					{
						// Return NULL if key not present
						response = "NULL\n";
					}
					pthread_mutex_unlock(&map_lock);

				}
				else if (query == "END")
				{
					// End the connection
					end = true;
					break;
				}

				// Send response to client
				send(client_fd, response.c_str(), response.length(), 0);

				// Erase strings
				response.clear();
				key.clear();
				value.clear();
			}
		}
	}
	int res = close(client_fd);
}