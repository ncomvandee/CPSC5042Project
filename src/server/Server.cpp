#include "Server.h"

using namespace std;

/**
 * Constructor for server, sets up socket file descriptor and TCP/IP internet socket. Sets up the 
 * server so it listens to incoming connections.
 *
 *  @param argc number of command line arguments forwarded from main inputs
 *  @param argv[] array of command line arugment forwarded from main, used to set custom port if desired
 */
Server::Server(int argc, char const *argv[]) {
    if (argc == 2) {
        port = atoi(argv[1]);
    } else {
        port = AUDREYS_PORT;
    }

    //creating a listening socket
    int opt = 1; 
    addrlen = sizeof(address); 
    
    // Creating socket file descriptor 
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) 
    { 
        throw "Server's socket creation failed"; 
    } 

    //setting up the options for the socket
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) 
    { 
        throw "setsockopt failed";
    } 

    //specifying the address and the port the server will connect to
    address.sin_family = AF_INET; 
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons( port ); 

    // Forcefully attaching socket to the port  
    if (::bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) 
    { 
        throw "bind failed";
    } 

    //prepare to accept connections
    if (listen(server_fd, 3) < 0) 
    { 
        throw "listen failed"; 
    } 
    cout << "Server is listening on port " << port << endl;
}

/**
 *  Server destructor
 */
Server::~Server() {
}

/**
 *  Blocking call that waits for a new socket connection
 *  on accept, returns socket number for a connection  
 * 
 *  @returns int the socket number for a new client connection
 */
int Server::acceptConnection() {
    int newSocket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    if (newSocket < 0) 
    { 
        throw "accept failed";
    } 
    cout << "New connection was made." << endl;
    return newSocket;
}

/**
 * Primary Server loop, use to start the server
 * Creates Network singleton the enters infinite loop to wait on new connects.
 * When a new connection is made, put a lock on the new socket number until it can be read by a thread.
 * Start a new thread for each new connect, pass in the network singleton as shared global context, startNewGame as thread main function
 */
void Server::acceptConnections() {
    //Initialize network data
    Network * networkPtr = new Network();

	//infinite loop to keep the server running indefinitely
    while (1) {
		try{
			// establish connection with a client		
			int newSocket = this->acceptConnection();
            
            // lock mutex here
            pthread_mutex_lock(&networkPtr->network_socket_lock);
            // cout << "before sleep..." << endl;
            // sleep(20);
            // cout << "after sleep..." << endl;
            
            // temporarily set network's socket to the new socket
            networkPtr->setSocket(newSocket);
            
            // start a new thread for the client
            pthread_t p1;
            pthread_create(&p1, NULL, startNewGame, (void *) networkPtr);
		
        } catch (const char* message) {
			cerr << message << endl;
			exit(EXIT_FAILURE);
		}
	}
}

/**
 *  The main function for each new thread. 
 *  Reads socket number from network singleton shared context, which is used to create a unique Connection object.
 *  Once socket number has been read, unlock socket address in network object.
 *  Authenticates the user making the connection.
 *  Creates unique Game Session object for this thread and starts the game
 *  Handles user commands that reference shared state in network
 */
void *Server::startNewGame(void * arg) {
    Network * network = (Network *) arg; // the shared data
    Connection * connection = new Connection(network->getSocket()); //the thread-specific data
    pthread_mutex_unlock(&network->network_socket_lock);

    //receive client's authentication info: log in or sign up, username and pw
    string authInfo = connection->receive();
    if (authInfo.empty()) {
		cout << "The client has unexpectedly disconnected" << endl;
	    connection->disconnectClient();
        return NULL;
	}
    // ask the network object to validate authentication info
    int authResult = network->checkAuthentication(authInfo);
   
    // send the result back to the client, positive user index if success
    // if failure, send negative int fail code that client has mappings for meaning
    connection->sendToClient(to_string(authResult));
    
    if (authResult > -1) { 
        //successful authentication. Handshake from client.
        connection->setCurrentUser(authResult);
        string clientConfirmsAuth = connection->receive();
    } else { 
        // authentication failed. Disconnect and force exit thread.
        connection->disconnectClient();
        return NULL;
    }
    
	//set up a new game session
	GameSession * thisSession = new GameSession(network->getWordsAndHints());

	//welcome user and start game
	connection->sendToClient(thisSession->startSession());

	string userInput;
	while (thisSession->getStatus() == 1) { //while session is active

		//receive client's answer
		userInput = connection->receive();
		// cout << "userInput : " << userInput << endl;
		if (userInput.empty()) {
			cout << "The client has unexpectedly disconnected" << endl;
			thisSession->setStatus(0);
			break;
		}
        if (GameSession::isAMatch(userInput, ".addword")) {
            //send a command to client to start gathering word and hint
            connection->sendToClient(".");
            //receive word and hint and pass to network to add to file
            network->addWord(connection->receive());
        }
        if (GameSession::isAMatch(userInput, ".leaderboard")){
            connection->sendToClient(network->getLeaderBoard());
        }
        if (GameSession::isAMatch(userInput, ".highScore")){
            connection->sendToClient(network->getHighScore(connection->getCurrentUser()));
        }

		//handle client's answer and send feedback
		connection->sendToClient(thisSession->handleUserInput(userInput));

        network->updateUserScores(thisSession->getScore(), thisSession->getBestStreak(), connection->getCurrentUser());

	}

    network->logOutUser(connection->getCurrentUser());
	connection->disconnectClient();
    return NULL;
}

