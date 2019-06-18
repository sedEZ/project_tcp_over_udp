#include"tcp_over_udp.h"

//Default server port number
int server_port_num = 3232;

//For waiting buffer
static dgram wait_queue[MAX_NUM_CLIENTS];
static int head = 0 , tail = 0;//head is the next one to pop; tail is the next index for pushing


//Functions
void 	server(int port_num);
int 	server_init(struct sockaddr_in* server,int port_num);
void    *my_listen(void*);
int 	my_accept(struct sockaddr_in* , int* , dgram*);

//For connection threads  
void    *new_connection(void* tmp);
void    three_way_handshake(int ,int*, struct sockaddr_in *,seg* ,seg* ,seg* ,unsigned int* ,unsigned int* );
extern void send_files(int sockfd , int*, struct sockaddr_in* client_addr  , seg* snd_s , seg* rcv_s , unsigned int* my_seq_num , unsigned int* my_ack_num);

//Push & Pop 
void push(const struct sockaddr_in* ,const seg* );
dgram pop();
int new_seg();

//Print parameters 
void print_parameters(struct sockaddr_in,int);

//Some setting functions

extern void set_seg(seg* s , uint16_t source_port , uint16_t des_port, uint32_t seq_num, uint32_t ack_num);
extern void set_syn(seg* s);
extern void set_ack(seg* s);
extern void reset_seg(seg* s);
extern int seq_num_generate();
extern int is_syn(seg s);


//For debug
extern void print_sockaddr_info(const struct sockaddr_in);

/********************************************/
int main(int argc,char** argv){
    srand(time(NULL));
	if(argc > 2){
		printf("Usage : %s port_number\n",argv[0]);
	}
	if(argc ==2){
		server_port_num = atoi(argv[1]);
	}
	server(server_port_num);
	return 0;
}

/*******************************************/
void server(int port_num){
	struct sockaddr_in server_addr,client_addr;
	new_connection_p* tmp;
	int socket_fd ;		 //socket file descripter

	if((socket_fd = server_init(&server_addr,port_num)) < 0){
		ERR_EXIT("Server:server_init");
	}
    print_parameters(server_addr,port_num);

	//Thread for listen
	pthread_t listen_thread;
	pthread_create(&listen_thread,NULL,my_listen,(void*)&socket_fd);


	int addrlen = sizeof(struct sockaddr_in);
	//thread variable array for connection establishment
	pthread_t connections[MAX_NUM_CLIENTS];
	int connection_c = 0;
	
	while(1){
		int sockfd_for_client;
        dgram* next_dgram = (dgram*)malloc(sizeof(dgram));

		//Accept next waiting client 
		sockfd_for_client = my_accept(&client_addr,&addrlen,next_dgram);

        //Parameters for connection 
		tmp = (new_connection_p*)malloc(sizeof(new_connection_p));
        memcpy(&(tmp->the_dgram) ,next_dgram,sizeof(dgram));
		tmp->sockfd = sockfd_for_client;

        //Create a thread for connection        
		pthread_create(connections+connection_c , NULL , new_connection ,(void*)tmp);
		connection_c = ( connection_c + 1 )% MAX_NUM_CLIENTS;
        sleep(1);
	}
	close(socket_fd);
}

int server_init(struct sockaddr_in* server,int port_num){
	
    int socket_fd;
	if((socket_fd = socket(AF_INET,SOCK_DGRAM,0))<0){
        ERR_EXIT("Server:server_init:socket:establish");
    }
    memset(server,0,sizeof(*server));
    server->sin_family = AF_INET;
    server->sin_port = htons((unsigned short)port_num);
    server->sin_addr.s_addr = inet_addr(SERVER_ADDR);

    if(bind(socket_fd, (struct sockaddr*)server, sizeof(*server)) == -1){
            ERR_EXIT("Server:server_init:bind");
    }
	return socket_fd;
}

/*******************************************************
 *My listen function, waiting for connection request from new client.
 *Note that it will also receive other (many) segments
 *******************************************************/ 
void* my_listen(void* socketfd){
	struct sockaddr_in client_addr;
	memset(&client_addr,0,sizeof(struct sockaddr_in));
	seg *rcv_s = (seg*)malloc(MSS);
	int sockfd = *((int*)socketfd);
	int n;
	socklen_t size = sizeof(struct sockaddr_in);
	
    printf("======================\n");
    printf("Listening for clients.....\n");

	while(1){
		if((n = recvfrom(sockfd,rcv_s,MSS,0,(struct sockaddr*)&client_addr,&size))==-1){
			ERR_EXIT("Server:my_listen:rcvfrom");
		}
		else{
			//Receive syn, push it into waiting queue
			if(is_syn(*rcv_s)){
				push(&client_addr,rcv_s);
			}

		}		
		
	}
}

/*****************Push and Pop********************/

//Push the client's request into the queue.
void push(const struct sockaddr_in* waiting_request,const seg* s){
	//Push it into queue
    
	memcpy(&(wait_queue[tail].addr),waiting_request,sizeof(struct sockaddr_in));
    memcpy(&(wait_queue[tail].s), s , sizeof(seg));
	tail++;
	if(tail == MAX_NUM_CLIENTS )
		tail = 0;
}

//Pop out next client waiting for connection.
dgram pop(){
	dgram tmp;
	if(head == tail){
		ERR_EXIT("Server:pop:queue is empty");
	}
    //Get next client 
	tmp = wait_queue[head];

    //Clean int 
	memset(wait_queue+head,0,sizeof(dgram));
	head++;
	if(head == MAX_NUM_CLIENTS )
                head = 0;
	return tmp;
}

int new_seg(){
	return !(head==tail);
}

/*****************my accept function*********************/
int my_accept(struct sockaddr_in* client_addr,int* addrlen,dgram* next_dgram){
	while(1){
		if(new_seg()){
			printf("Accepting new connection...\n");
			//Get next client connection request
            dgram new_dgram = pop();
            memcpy(next_dgram,&new_dgram,sizeof(dgram));

			//Copy it into client_addr
			memcpy(client_addr,&(new_dgram.addr),*addrlen);
			
			//Create a new socket
			int newsockfd;
			if((newsockfd = socket(AF_INET,SOCK_DGRAM,0))<0){
               		ERR_EXIT("Server:my_accept:establish");
       		}
			return newsockfd;
		}
	}
}

/**************Establish a new connection , run as a thread******************/
void* new_connection(void* tmp){

	//Copy the parameters 
	new_connection_p parameters;
	memcpy(&parameters,(new_connection_p*)tmp,sizeof(new_connection_p));

    //Datagram , one (struct sockaddr_in) & one (seg)
    dgram client_dgram = parameters.the_dgram;

	struct sockaddr_in client_addr = client_dgram.addr;
    seg client_seg = client_dgram.s;

	int sockfd = parameters.sockfd;
    
    //Setting timeout 
    struct timeval nTimeOut; // time out = 2*RTT
    nTimeOut.tv_usec = 2*RTT*1000;
    nTimeOut.tv_sec = 0;
    if(setsockopt(sockfd,SOL_SOCKET,SO_RCVTIMEO,(char*)&nTimeOut ,sizeof(struct timeval)) == -1){
        ERR_EXIT("Server:setsockopt");
    }

    //Initialize sequence number
    unsigned int my_seq_num = seq_num_generate() , my_ack_num = 0;
	
    //Segements to be send
	seg *snd_s = (seg*)malloc(MSS);
	seg *rcv_s = (seg*)malloc(MSS);

    //My port number
    int my_port;

    //Three-way handshake
    three_way_handshake(sockfd, &my_port,&client_addr,&client_seg,snd_s,rcv_s,&my_seq_num,&my_ack_num);

    //Send files
    send_files(sockfd, &my_port,&client_addr ,snd_s,rcv_s,&my_seq_num,&my_ack_num);

	pthread_exit(NULL);
}

/**************Three-way handshake******************/
void three_way_handshake(int sockfd , int* my_port,struct sockaddr_in* client_addr , seg* client_seg , seg* snd_s , seg* rcv_s , unsigned int* my_seq_num , unsigned int* my_ack_num){
    int n;
    socklen_t size = sizeof(struct sockaddr_in);

    printf("=========Start three-way handshake=========\n");

    printf("Receive a packet(SYN) from %s : %d\n",inet_ntoa(client_addr->sin_addr),ntohs(client_addr->sin_port));
    printf("\t\tReceive a packet (seq_num = %d , ack_num = %d)\n",client_seg->header.seq_num,client_seg->header.ack_num);
    (*my_ack_num) = client_seg->header.seq_num + 1;
	//Step2 of three-way handshake
	
	//Setting segement
	reset_seg(snd_s);
    set_seg(snd_s,server_port_num,ntohs(client_addr->sin_port), *my_seq_num,*my_ack_num);
	set_syn(snd_s);
	set_ack(snd_s);


    printf("Send a packet(SYN/ACK) to %s : %d\n",inet_ntoa(client_addr->sin_addr),ntohs(client_addr->sin_port));
	//Send
    if((n = sendto(sockfd,snd_s,MSS,0,(struct sockaddr*)client_addr,size))==-1){
        ERR_EXIT("Server:new_connection:step2:sendto");
    }
    (*my_seq_num)++;
	
	//Step3 of three-way handshake 
    reset_seg(rcv_s);
	if((n = recvfrom(sockfd,rcv_s,MSS,0,(struct sockaddr*)client_addr,&size))==-1){
        ERR_EXIT("Server:new_connection:step3:recvfrom");
    }

    printf("Receive a packet(SYN) from %s : %d\n",inet_ntoa(client_addr->sin_addr),ntohs(client_addr->sin_port));
    printf("\t\tReceive a packet (seq_num = %d , ack_num = %d)\n",rcv_s->header.seq_num,rcv_s->header.ack_num);
    
    
    (*my_ack_num) = rcv_s->header.seq_num + 1;
    (*my_port) = rcv_s->header.des_port;

    printf("=======Complete the three-way handshake======\n");
 
}
