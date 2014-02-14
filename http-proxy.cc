/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include "compat.h"
#include "http-request.h"
#include "http-response.h"
#include "http-headers.h"
#include <exception>
#include <cstring>
#include <string>
#include <map>
#include <sys/time.h>

using namespace std;

#define MAX_PROCESS_NUM 20  //Maximum number of process
#define MAX_CONNECTION_NUM 20 //Maximum number of connections
#define LISTENING_PORT "14886" //server listening port

const char *_FAIL_TO_CREATE_PROCESS_ = "Fail to spwan a new process\n";
const char *_WELCOME_MESSAGE_ = "Welcome to Sixiang's http proxy\n";
pid_t CONNECTIONS[MAX_CONNECTION_NUM]; 

//check if request has carriage-return line-feed"(\r\n)
//at the end-of-line
bool 
CheckCarriageReturn(char *buf){
  int size = strlen(buf);
  if(memmem(buf, size, "\r\n\r\n", 4) != NULL)
    return true;
  return false;
}

//Add new process id to array
void 
AddNewPID(pid_t new_pid){
  pid_t n = 0;
  while(n != MAX_CONNECTION_NUM){
    if(CONNECTIONS[n] == 0){
      CONNECTIONS[n] = new_pid;
    }
  }
}

//Return host name
string 
GetHostName(HttpRequest *req){
  if((req->GetHost()).length() == 0){
    req->SetHost(req->FindHeader("Host"));
  }
  return req->GetHost();
}

//Return port number; return 80 if not specified
int 
GetPortNum(HttpRequest *req){
  if(req->GetPort() == 0){
    req->SetPort(80);
  }
  return req->GetPort();
}

//Setup a new socket between proxy and remote server
//INPUT: client request struct
//RETURN: a new socket file descriptor
int 
SetUpSocketToRemoteServer(HttpRequest client_req){
    int remote_sfd;
    string remote_host = GetHostName(&client_req);
    int remote_port = GetPortNum(&client_req);
    struct addrinfo remote, *remote_info;
    cout<<"Remote host name: "<<remote_host<<endl;
    cout<<" Port number: "<<remote_port<<endl;
    memset(&remote, 0, sizeof(remote));
    remote.ai_family = PF_INET;
    remote.ai_socktype = SOCK_STREAM;
    remote.ai_flags = AI_PASSIVE;
    getaddrinfo(remote_host.c_str(), "http", &remote, &remote_info);
    remote_sfd = socket(remote_info->ai_family, remote_info->ai_socktype, remote_info->ai_protocol);
    //Fail to create a socket
    if(remote_sfd == -1){
      perror("Fail to create a socket");
    }
    //Fail to connect
    if(connect(remote_sfd, remote_info->ai_addr,remote_info->ai_addrlen) == -1){
      perror("Fail to connect");
      close(remote_sfd);
    }
    //Fail to find a connection
    if(remote_info == NULL){
      perror("Didn't find a connection");
    }
    freeaddrinfo(remote_info);
    return remote_sfd;
}

//Generate a new remote server request
//INPUT: Client request struct
//RETURN: remote server request struct
void
SendRemoteRequest(HttpRequest client_req, int remote_sfd){
  int size = client_req.GetTotalLength();
  char *buf = new char[size+1];
  client_req.FormatRequest(buf);
  if(send(remote_sfd, buf, size, 0) == -1){
    perror("Fail to send request to remote server");
  }
}

//ProxyServerInitialization set up the proxy server. Create a new
//Socket file descriptor and start to listen on designated port
//Return value-> Socket file descripator
int 
ProxyServerInitialization(){
  cout<< "HTTP proxy is starting..." <<endl;
  cout<< "Setting up the server..."<<endl;
  //initialize connections array
  for(pid_t i = 0; i < MAX_CONNECTION_NUM; i++){
    CONNECTIONS[i] = 0;
  }
  //set up basic info 
  struct addrinfo hints, *servinfo;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  getaddrinfo(NULL, LISTENING_PORT, &hints, &servinfo);
  
  //set up socket, bind and start to listen
  int sfd;
  sfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
  if(sfd == -1){
    perror("Fail to create a socket");
    exit(1);
  }
  if(bind(sfd, servinfo->ai_addr, servinfo->ai_addrlen) == -1){
    perror("Fail to bind");
    exit(1);
  }
  if(listen(sfd, MAX_CONNECTION_NUM) == -1){
    perror("Fail to start listen");
    exit(1);
  }
  cout<< "Proxy is now listening to port "<< LISTENING_PORT << endl;
  return sfd;
}

HttpRequest
HandleConditiaonReq(HttpRequest req, int remote_sfd, ){

}

int main (int argc, char *argv[])
{
  string client_buffer = "";
  char* buffer = new char[512];
  int sfd = ProxyServerInitialization();
  //Ready to accpet incoming connections
  bool stop = false;
  struct sockaddr_storage their_addr;
  int new_sfd;
  int remote_sfd;
  int bytesrecv;
  pid_t new_pid;
  string client_error;
  HttpRequest client_req;
  HttpRequest proxy_req;
  HttpResponse proxy_response;
  socklen_t addr_size = sizeof(struct sockaddr_storage);

  while(!stop){
    new_sfd = accept(sfd, (struct sockaddr *)&their_addr, &addr_size);
    if(new_sfd == -1){
      perror("Fail to accpet request");
      continue;
    }
    //spawn a new process for each new connection
    new_pid = fork();
    //Fail to spawn a process and notify user by sending the failure message
    if(new_pid < 0){
      send(new_sfd, _FAIL_TO_CREATE_PROCESS_, strlen(_FAIL_TO_CREATE_PROCESS_), 0);
      perror("Fail to spawn a new process");
      close(new_sfd);
      close(sfd);
    }
    //parent process
    else if(new_pid > 0){
      AddNewPID(new_pid);
    }
    //child process
    else{
      send(new_sfd,_WELCOME_MESSAGE_, strlen(_WELCOME_MESSAGE_), 0);
      cout << "A new connection is established..."<<endl;
      //receive messages from client
      do
      {
        bzero(buffer, sizeof(buffer));
        bytesrecv = recv( new_sfd, buffer, sizeof(buffer), 0);
        if(bytesrecv < 0)
        {
          perror("Fail to receive data");
          break;
        }
        client_buffer.append(buffer);
        if(client_buffer.find("\r\n\r\n") != string::npos){
          break;
        }
      }
      while (bytesrecv > 0);
      //Check client request
      try{
        client_req.ParseRequest(client_buffer.c_str(), client_buffer.length());   
      }catch(ParseException& e){
        string s = e.what();
        if(s.compare("Request is not GET") == 0){
          client_error = "Not Implemented(501)";
        }
        else{
          cout<<s<<endl;
          client_error = "Bad Request(404)";
        }
        if(send(new_sfd, client_error.c_str(), client_error.size(), 0) == -1){
          perror("Fail to send request");
        }
        cout<<"Oops, " << client_error<<endl;
      }
      cout<<"Request received"<<endl;
      //Now we create a new socket connection to remote server
      remote_sfd = SetUpSocketToRemoteServer(client_req);

      cout<<"Sending request to remote host"<<endl;
    }
  }
  close(new_sfd);
  close(sfd);
  return 0;   
}


