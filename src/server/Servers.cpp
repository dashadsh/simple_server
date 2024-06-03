
#include "../../inc/AllHeaders.hpp"

Servers::Servers(ConfigDB &configDB) : _epoll_fds(-1), _server_fds(), _domain_to_server(), _ip_to_server(), 
	_keyValues(), server_index(), server_fd_to_index(), client_to_server(), _client_amount(0),
	_client_data(), _cgi_clients_childfd(), _client_time(), configDB_(configDB)
	// need to avoid condition jumps BUT
	// some have def initializers.
{
	servers = this;
	stop_fd = -1; // move to the top
	_cgi_clients = std::map<int, CgiClient*>(); // DELETE
	_keyValues = configDB_.getKeyValue();
	createServers(); // -> ePoll
	initEvents();
}


// CLEANED UP CONSTRUCTOR
//Servers::Servers(ConfigDB &configDB)
//    : _epoll_fds(-1), _client_amount(0), stop_fd(-1), configDB_(configDB)
//{
//    servers = this;
//    _keyValues = configDB_.getKeyValue();
//    createServers(); // ePoll
//    initEvents();
//}

Servers::~Servers() {
	for (std::map<int, CgiClient*>::iterator it = _cgi_clients.begin(); it != _cgi_clients.end(); it++)
	{
		if (it->second != NULL)
			delete it->second;
	}
	for (std::map<int, int>::iterator it = client_to_server.begin(); it != client_to_server.end(); it++)
	{
		if (close(it->first) == -1)
			std::cerr << "Close failed with error: " << strerror(errno) << std::endl;
	}
	for (std::vector<int>::iterator it = _server_fds.begin(); it != _server_fds.end(); ++it)
	{
		if (*it != -1)
			close(*it);
	}
    _server_fds.clear();
    if (_epoll_fds != -1)
        close(_epoll_fds);
}

Servers::Servers(const Servers &rhs)
    : _epoll_fds(rhs._epoll_fds),_server_fds(rhs._server_fds), 
      _domain_to_server(rhs._domain_to_server), _ip_to_server(rhs._ip_to_server),
      _keyValues(rhs._keyValues), server_index(rhs.server_index),
      server_fd_to_index(rhs.server_fd_to_index), _client_amount(rhs._client_amount),
	  configDB_(rhs.configDB_){}

Servers &Servers::operator=(const Servers &rhs) {
    if (this != &rhs) {
        _server_fds = rhs._server_fds;
        _epoll_fds = rhs._epoll_fds;
        _domain_to_server = rhs._domain_to_server;
        _ip_to_server = rhs._ip_to_server;
        _keyValues = rhs._keyValues;
        server_index = rhs.server_index;
        server_fd_to_index = rhs.server_fd_to_index;
		_client_amount = rhs._client_amount;
        configDB_ = rhs.configDB_;
    }
    return *this;
}

//------------------------------------------------------------------------------------------

int Servers::createSocket(){
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		std::cerr << "Socket creation failed" << std::endl;
		return (0);
	}
	_server_fds.push_back(server_fd);
	return (1);
}

int Servers::bindSocket(std::string s_port){
	if (_server_fds.back() == -1)
	{
		std::cerr << "Socket binding impossible!" << std::endl;
		return (0);
	}
	std::stringstream ss;
	ss << s_port;
	int port;
	std::string ip_string;
	const char *c_ip = NULL;
	if (s_port.find(":") == std::string::npos)
	{
		port = std::atoi(s_port.c_str());
		_ip_to_server[_server_fds.back()] = "127.0.0.1:" + s_port;
	}
	else
	{
		_ip_to_server[_server_fds.back()] = s_port;
		getline(ss, ip_string, ':');
		ss >> port;
		c_ip = ip_string.c_str();
	}
	int opt = 1;
	setsockopt(_server_fds.back(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
	struct sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	if (c_ip != NULL)
		address.sin_addr.s_addr = inet_addr(c_ip);
	else
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(port);
	if (bind(_server_fds.back(), (struct sockaddr *)&address, sizeof(address)) == -1) {
		std::cerr << "Bind failed" << std::endl;
		return (0);
	}
	for (std::map<int, std::vector<std::string> >::iterator it = server_index.begin(); it != server_index.end(); it++){
		for (std::vector<std::string>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++){
			if (*it2 == s_port)
				server_fd_to_index[_server_fds.back()] = it->first;
		}
	}
	return (1);
}

void	Servers::createEpoll(){
	int epoll_fd = epoll_create1(0);
	this->_epoll_fds = epoll_fd;
	if (epoll_fd < 0) {
		std::cerr << "Epoll creation failed" << std::endl;
		exit(1);
	}
}

int Servers::listenSocket(){
	if (listen(_server_fds.back(), SOMAXCONN) == -1) {
		std::cerr << "Listen failed" << std::endl;
		return (0);
	}
	return (1);
}

// add file descriptor to epoll instance for monitoring, takes "new_socket"
// 		int new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
int Servers::combineFds(int socket_fd){ // socket_fd - file descriptor to be monitored by epoll instance

// struct epoll_event
//{
//  uint32_t events;	/* Epoll events */ = EPOLLIN (indicates that FD is ready for reading)
//  epoll_data_t data;	/* User data variable */
// } __EPOLL_PACKED;

	// epoll_event structure is used to describe interest in particular file descriptor
	struct epoll_event event;
	std::memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;

//	typedef union epoll_data
//{
//  void *ptr;
//  int fd;
//  uint32_t u32;
//  uint64_t u64;
//} epoll_data_t;

	// event.data.fd = new_socket - file descriptor to be monitored by epoll instance
	event.data.fd = socket_fd;

	// function to control epoll interface -> add / modify / remove FD from epoll instance
	// ->_epoll_fds => see "epoll_create1"
	if (epoll_ctl(this->_epoll_fds, EPOLL_CTL_ADD, socket_fd, &event) == -1) {
		std::cerr << "Epoll_ctl failed" << std::endl;
		return (0);
	}
	return (1);
}

void printRow(int width, int serverNum, int ipWidth, std::string ip) {
	std::cout << "| " << std::left << std::setw(width) << serverNum << " | " << std::setw(ipWidth) << ip << " |" << std::endl;	
}


void Servers::createServers() {
    std::cout << "|" << std::string(7, ' ') << CURSIVE_GRAY << " Creating servers..." << RESET << std::string(9, ' ') << "|" << std::endl;
    std::vector<std::string> ports;
    createEpoll();
    ports = getPorts();

    const int serverNumberWidth = 15;
    const int portWidth = 20;

    std::cout << BHWHITE << "+---------------+--------------------+" << std::endl;
    std::cout << "| " << std::left << std::setw(serverNumberWidth - 2) << "Server ID" << " | " << std::setw(portWidth - 2) << "Port" << " |" << std::endl;
    std::cout << "+---------------+--------------------+" << std::endl;

    for (std::vector<std::string>::iterator it2 = ports.begin(); it2 != ports.end(); it2++) {
        if (!checkSocket(*it2)) {
            if (createSocket()) {
                if (!bindSocket(*it2) || !listenSocket() || !setNonBlocking(_server_fds.back()) || !combineFds(_server_fds.back()))
                    _server_fds.pop_back();
                else {
                    assignDomain(*it2, _server_fds.back());
					printRow(serverNumberWidth - 2,_server_fds.back() - 3, portWidth - 2,_ip_to_server[_server_fds.back()]);
                }
            }
        }
    }

    std::cout << "+---------------+--------------------+" << RESET << std::endl;
}


Listen getTargetIpAndPort(std::string requestedUrl) {
	size_t pos = requestedUrl.find(":");
	Listen listen;

	if (requestedUrl.empty())
		return (std::cout << "ERROR: empty host and port" <<std::endl, listen);

	if ( pos == std::string::npos)	
		return (std::cout << "Warning: default host and port used 127.0.0.1:80" <<std::endl, listen);

	std::string x_ip = requestedUrl.substr(0, pos);
	std::string x_port = requestedUrl.substr(pos + 1);
	uint32_t port_x;
	std::istringstream iss(x_port); 
	iss >> port_x;
	return Listen (x_ip, port_x);
}

// handle incoming connection to the server
void Servers::handleIncomingConnection(int server_fd){

	// address of the client "endpoint address", used for:
	// accept, bind, connect, and recvfrom
	struct sockaddr_in address; // address data
    socklen_t addrlen = sizeof(address); // size of structure above

	// accept the connection from the client and create a new socket
	// socket is created for the client to communicate with the server
    int new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
    if (new_socket == -1) {
        std::cerr << "Accept failed." << std::endl;
        return;
    }

	// set the new socket to be non-blocking by using epoll -> close new_socket
	if (combineFds(new_socket) == 0)
	{
		if (close(new_socket) == -1)
			std::cerr << "Close failed with error: " << strerror(errno) << std::endl;
		return ;
	}

	// 	std::map<int, int> client_to_server; -------------- MAP CLIENT->SERVER from HERE
	client_to_server[new_socket] = server_fd;
	// 	std::map<int, HttpRequest> _client_data; ------------ SEE handleIncomingData
	_client_data[new_socket] = HttpRequest();

	setTimeout(new_socket);
	_client_amount++;
}

// here we will check for the first time if client requested CGI
void Servers::handleIncomingData(int client_fd) { 

	int reqStatus = -1;
	std::string request;
	int server_fd = client_to_server[client_fd]; // see ----- HandleIncomingConnection
	bool finish = false;

	// get the request from the client
	getRequest(client_fd, request);

	// which client it is?
	reqStatus = _client_data.find(client_fd)->second.parseRequest(request);
	if (reqStatus != 200) {
		finish = true;
	}

	// here we can check if client requested CGI
	if (!handleResponse(reqStatus, server_fd, client_fd, _client_data.find(client_fd)->second))
		return;
	if (finish && _cgi_clients.find(client_fd) == _cgi_clients.end())
		deleteClient(client_fd);
}

/*
typedef union epoll_data
{
  void *ptr;
  int fd;
  uint32_t u32;
  uint64_t u64;
} epoll_data_t;

struct epoll_event
{
  uint32_t events;
  epoll_data_t data;
} __EPOLL_PACKED;
*/

// as long as there are events to handle, we will keep looping
// events are notifications from epoll interface about the changes in status of FD
void Servers::initEvents() {
	while (1) {
		try{
			struct epoll_event events[_server_fds.size() + _client_amount];
// ??? at the end we will we will monitor child process --------------------- ??? 
			
			// 1. instance to which FD attached // 	int _epoll_fds;
			// 2. arr of epoll events // JUST INITIALIZED
			// 3. max number of events to handle // std::vector<int> _server_fds;
			// 4. timeout // 1000 ms = 1 sec
			int n = epoll_wait(this->_epoll_fds, events, _server_fds.size() + _client_amount, 1000);
			if (n == -1) {
				std::cerr << "Epoll_wait failed" << std::endl;
				return ;
			} 

			// n -> nbr of FD with events ready
			// if there are events to handle, we will loop through them
			for (int i = 0; i < n; i++) {
				bool server = false;
				for (std::vector<int>::iterator it2 = _server_fds.begin(); it2 != _server_fds.end(); ++it2) {
					// [i] <- we initialized it: struct epoll_event events[_server_fds.size() + _client_amount];
					if (events[i].data.fd == *it2) {

						// if the event is a server, we will handle the incoming connection
						handleIncomingConnection(*it2); // SEE EPOLL

						server = true; // --------- qestion regarding TRUE
						break ;
					}
				} 
				
				// check if it s reqular request or CGI
				// 1. actually it will go to regular one first and create CGI client
				// 2. then it will go to "if" (if there is child process and will (repeatedly) handleIncomingCgi)
				if (!server && events[i].events & EPOLLIN) {
					// 	std::map<int, int> _cgi_clients_childfd; // map of child fds to each client (idx -> FD of child process)
					if (_cgi_clients_childfd.find(events[i].data.fd) != _cgi_clients_childfd.end())
					{
						setTimeout(_cgi_clients_childfd[events[i].data.fd]);
						handleIncomingCgi(events[i].data.fd); // fromCgi->returns -1 bc socket non-blockin- nothing to read
					} 
					
					// 1. check if client is sending DATA
					// _client_data[new_socket] = HttpRequest(); --------- from HandlingIncomingConnection
					else if (_client_data.find(events[i].data.fd) != _client_data.end())
					{
						setTimeout(events[i].data.fd);
						// go here!
						handleIncomingData(events[i].data.fd);
					}
				}
			}
		} 
		catch (std::exception &e){
			std::cerr << e.what() << std::endl;
		}
		checkClientTimeout();
		// printData();
	}
}

std::vector<std::string> Servers::getPorts(){
	
	std::map<std::string, std::vector<std::string> > config = getKeyValue();
	std::stringstream ss;
	std::vector<std::string> ports_temp;
	std::vector<std::string> ports;

	for (int i = 0; i != std::numeric_limits<int>::max(); i++){
		ss.str("");
		ss << i;
		std::string server_name = "server[" + ss.str() + "]";
		std::string server;
		std::map<std::string, std::vector<std::string> >::iterator it_server_name = config.end();
		for (std::map<std::string, std::vector<std::string> >::iterator it = config.begin(); it != config.end(); it++)
		{
			if (it->first.find(server_name) != std::string::npos){
				std::size_t pos = it->first.find(server_name);
				it_server_name = it;
				std::string prefix = it->first.substr(0, pos + server_name.length());
				server = prefix + ".listen";
				break;
			}
		}
		std::map<std::string, std::vector<std::string> >::iterator it_server = config.find(server);
		if (it_server != config.end()){
			ports_temp = it_server->second;
			for (std::vector<std::string>::iterator it2 = ports_temp.begin(); it2 != ports_temp.end(); it2++){
				if (std::find(ports.begin(), ports.end(), *it2) == ports.end())
				{
					ports.push_back(*it2);
					server_index[i].push_back(*it2);
				}
			}
		}
		else if (it_server_name != config.end() && it_server == config.end()){
			if (std::find(ports.begin(), ports.end(), "80") == ports.end())
			{
				ports.push_back("80");
				server_index[i].push_back("80");
			}
		}
		else
			return (ports);
	}
	return (ports);
}

void Servers::assignLocalDomain(int server_fd){
	std::map<std::string, std::vector<std::string> > config = getKeyValue();
	for (std::map<std::string, std::vector<std::string> >::iterator it_domain = config.begin(); it_domain != config.end(); it_domain++){
		if (it_domain->first.find("server_name") != std::string::npos){
			std::string server_name = it_domain->first;
			std::size_t pos = server_name.find("]");
			server_name = server_name.substr(0, pos + 1);
			std::string domain = server_name + ".listen";
			std::map<std::string, std::vector<std::string> >::iterator it_domain_listen = config.find(domain);
				if (it_domain_listen == config.end()){
					for (std::vector<std::string>::iterator it = it_domain->second.begin(); it != it_domain->second.end(); it++){
					if (std::find(_domain_to_server[server_fd].begin(), _domain_to_server[server_fd].end(), *it) == _domain_to_server[server_fd].end())
						_domain_to_server[server_fd].push_back(*it);
				}
			}
		}
	}
}

void Servers::assignDomain(std::string port, int server_fd){
	if (port == "80")
		assignLocalDomain(server_fd);
	std::map<std::string, std::vector<std::string> > config = getKeyValue();
	for (std::map<std::string, std::vector<std::string> >::iterator it = config.begin(); it != config.end(); it++)
	{
		for (std::vector<std::string>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
		{
			if (*it2 == port){
				std::string server_name = it->first;
				std::size_t pos = server_name.find("]");
				server_name = server_name.substr(0, pos + 1);
				std::string domain = server_name + ".server_name";
				std::map<std::string, std::vector<std::string> >::iterator it_domain = config.find(domain);
				if (it_domain != config.end()){
					for (std::vector<std::string>::iterator it3 = it_domain->second.begin(); it3 != it_domain->second.end(); it3++){
						if (std::find(_domain_to_server[server_fd].begin(), _domain_to_server[server_fd].end(), *it3) == _domain_to_server[server_fd].end())
							_domain_to_server[server_fd].push_back(*it3);
					}
				}
				else{
					if (std::find(_domain_to_server[server_fd].begin(), _domain_to_server[server_fd].end(), "localhost") == _domain_to_server[server_fd].end())
						_domain_to_server[server_fd].push_back("localhost");
				}
			}
		}
	}
}

std::map<std::string, std::vector<std::string> > Servers::getKeyValue() const {
	return (this->_keyValues);
}

int Servers::checkSocketPort(std::string port){
	for (std::string::iterator it = port.begin(); it != port.end(); it++)
	{
		if (!std::isdigit(*it) || std::atoi(port.c_str()) < 0 || std::atoi(port.c_str()) > 65535)
		{
			std::cerr << "Invalid port" << std::endl;
			return 1;
		}
	}
	return (0);
}

int Servers::checkSocket(std::string ip){
	std::string ip_string;
	std::string port_string;
	if (ip.find(":") == std::string::npos)
		return checkSocketPort(ip);
	else{
		std::stringstream ss_ip;
		std::stringstream ss_port;
		ss_ip << ip;
		getline(ss_ip, ip_string, ':');
		ss_ip >> port_string;
		if (checkSocketPort(port_string) == 1)
			return 1;
	}
	struct sockaddr_in sa;
	int result = inet_pton(AF_INET, ip_string.c_str(), &(sa.sin_addr));
	if (result == 0) {
		std::cerr << "Invalid address" << std::endl;
		return 1;
	}
	else if (result == -1) {
		std::cerr << "Address conversion failed" << std::endl;
		return 1;
	}
	return 0;
}

bool Servers::getRequest(int client_fd, std::string &request){
	
	char buffer[4096];
	request.clear();
	int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
	if (bytes > 0)
	{
		buffer[bytes] = '\0';
		request.append(buffer, bytes);
	}
	else if (bytes == -1)
	{
		std::cerr << "Recv failed" << std::endl;
		return true;
	}
	return false;
}

// here it can happen that client requested CGI
// 
		// see RequestConfig.hpp
		// ----  Client &client_;
		// ---- Listen host_port_;
		// ----	DB db_; - they are creates 1 time only
size_t Servers::handleResponse(int reqStatus, int server_fd, int new_socket, HttpRequest &parser) {
		
		std::string response;
		
		if (reqStatus != 200)
		{ // stuff happens 1 time only
			Listen host_port = getTargetIpAndPort(_ip_to_server[server_fd]);
			DB db = {configDB_.getServers(), configDB_.getRootConfig()};
			Client client(db, host_port, parser, server_fd_to_index[server_fd], reqStatus);
			
			client.setupResponse();
			// if client requested CGI
			if (client.getResponseRef().getStatus() <= 400 && (client.getCgi() || client.getCgiResponse()))
			{
				// create new CgiClient (copy) & add it to epoll structure (FD which ubderstands events)
				_cgi_clients[new_socket] = NULL;
				
				// COPY CONSTRUCTOR FOR CGI CLIENT -> FD : CgiClient
				// !!!
				_cgi_clients[new_socket] = new CgiClient(client, this->_epoll_fds);			
					
				// saving it into fd of child process
				// was problem with YES CAT (bc of while loop - while had to go 1 level higher)
				// cgi handling happens 1 time -> creating client obj 1 time
				_cgi_clients_childfd[_cgi_clients[new_socket]->getPipeOut()] =  new_socket;
				return (1);
			}
			response = client.getResponseString();
		}
		ssize_t bytes = write(new_socket, response.c_str(), response.size());
		if (bytes == -1) {
			std::cerr << "Write failed with error: " << strerror(errno) << std::endl;
			return 0;
		}
		return 1;
}

int Servers::setNonBlocking(int fd){
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1)
	{
		std::cerr << "Fcntl failed" << std::endl;
		return (0);
	}
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
	{
		std::cerr << "Fcntl failed" << std::endl;
		return (0);
	}
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
    {
        std::cerr << "Fcntl failed" << std::endl;
        return (0);
    }
	return (1);
}

// cycle went here until 200 or 500 will be returned - when it will be nothing to read
int Servers::handleIncomingCgi(int child_fd){
	std::string response;
	int	client_fd;	
	for (std::map<int, int>::iterator it = _cgi_clients_childfd.begin(); it != _cgi_clients_childfd.end(); it++)
	{
		if (it->first == child_fd)
		{
			client_fd = it->second;
			break;
		}
	}
	if (_cgi_clients[client_fd] == NULL)
	{
		removeFromEpoll(child_fd);
		deleteClient(client_fd);
		for (std::map<int, int>::iterator it = _cgi_clients_childfd.begin(); it != _cgi_clients_childfd.end();)
		{
			if (it->second == client_fd)
			{
				removeFromEpoll(it->first);	
				std::map<int, int>::iterator eraseIt = it;
				// _cgi_clients_childfd.erase(it->first);
				it++;
				_cgi_clients_childfd.erase(eraseIt);
			}
			else
				it++;
		}
		return 0;
	}
	_cgi_clients[client_fd]->HandleCgi(); // !!! MAIN STUFF
	if (_cgi_clients[client_fd]->getStatusCode() == 200 || _cgi_clients[client_fd]->getStatusCode() == 500)
	{
		_cgi_clients[client_fd]->getResponse().createResponse(); // after all data is read -> create response
		response = _cgi_clients[client_fd]->getResponseString();
		ssize_t bytes = write(client_fd, response.c_str(), response.size());
		if (bytes == -1) {
			std::cerr << "Write failed with error: " << strerror(errno) << std::endl;
			return 0;
		}
		
		deleteClient(client_fd); // delete client
		_cgi_clients_childfd.erase(child_fd); // delete FD so it stops looping calling!
	}
	return 1;
}

void Servers::setTimeout(int client_fd){
	_client_time[client_fd] = time(NULL);
}

void Servers::checkClientTimeout(){
	time_t current_time = time(NULL);
	for (std::map<int, time_t>::iterator it = _client_time.begin(); it != _client_time.end(); it++)
	{
		if (_cgi_clients.find(it->first) != _cgi_clients.end())
		{
			if (current_time - it->second >= 2)
			{
				for (std::map<int, int>::iterator it2 = _cgi_clients_childfd.begin(); it2 != _cgi_clients_childfd.end(); it2++)
				{
					if (it2->second == it->first)
					{
						handleIncomingCgi(it2->first);
						return ;
					}
				}
			}
		}
		if (current_time - it->second > 10)
		{
			std::cout << "Client FD: " << it->first << " timed out\n";
			deleteClient(it->first);
			return ;
		}
	}
}

// remove client from waiting list -> remove cgi elements
// FD removes from epoll
// pipes are closed
void Servers::deleteClient(int client_fd)
{
	removeFromEpoll(client_fd); // remove client from waiting list -> remove cgi elements
    if (close(client_fd) == -1)
		std::cerr << "Close failed with error: " << strerror(errno) << std::endl;
	if (_client_amount > 0)
		_client_amount--;
	if (_cgi_clients.find(client_fd) != _cgi_clients.end())
	{
		delete _cgi_clients[client_fd];
		_cgi_clients.erase(client_fd);
	}
	if (_client_data.find(client_fd) != _client_data.end())
		_client_data.erase(client_fd);
	if (client_to_server.find(client_fd) != client_to_server.end())
		client_to_server.erase(client_fd);
	if (_client_time.find(client_fd) != _client_time.end())
		_client_time.erase(client_fd);
}

void Servers::printData()
{
	std::cout << "--------CLIENT-TO-SERVER--------" << std::endl;
	for (std::map<int, int>::iterator it = client_to_server.begin(); it != client_to_server.end(); it++)
		std::cout << "Client: " << it->first << " Server: " << it->second << std::endl;
	std::cout << "----------CLIENT-DATA-----------" << std::endl;
	for (std::map<int, HttpRequest>::iterator it = _client_data.begin(); it != _client_data.end(); it++)
		std::cout << "Client: " << it->first << std::endl;
	std::cout << "----------CGI-CLIENTS-----------" << std::endl;
	for (std::map<int, CgiClient*>::iterator it = _cgi_clients.begin(); it != _cgi_clients.end(); it++)
		std::cout << "Client: " << it->first << " Response: " << it->second->getResponseString() << std::endl;
	std::cout << "--------CGI-CLIENTS-CHILDFD--------" << std::endl;
	for (std::map<int, int>::iterator it = _cgi_clients_childfd.begin(); it != _cgi_clients_childfd.end(); it++)
		std::cout << "Child: " << it->first << " Client: " << it->second << std::endl;
	std::cout << "----------CLIENT-TIME-----------" << std::endl;
	for (std::map<int, time_t>::iterator it = _client_time.begin(); it != _client_time.end(); it++)
		std::cout << "Client: " << it->first << " Time: " << it->second << std::endl;
	std::cout << std::endl;
}

void	Servers::removeFromEpoll(int fd){
	if (epoll_ctl(this->_epoll_fds, EPOLL_CTL_DEL, fd, NULL) == -1) {
		std::cerr << "Failed to remove client file descriptor from epoll instance." << std::endl;
	}
}