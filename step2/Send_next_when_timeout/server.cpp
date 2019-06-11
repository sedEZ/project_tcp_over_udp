#include"tcp_over_udp.h"

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
void    send_files(int sockfd , int*, struct sockaddr_in* client_addr  , seg* snd_s , seg* rcv_s , unsigned int* my_seq_num , unsigned int* my_ack_num);
int     send_seg(int sockfd , int* my_port , struct sockaddr_in* client_addr , FILE* fp ,  seg* snd_s , unsigned int* my_seq_num , unsigned int* my_ack_num , char* file_name , unacked_seg** unacked_list_head);
void    receive_ack(int sockfd , int* my_port , struct sockaddr_in* client_addr ,seg* snd_s, seg* rcv_s, unsigned int* my_seq_num , unsigned int* my_ack_num , unacked_seg** unacked_list_head);
void    send_fin(int sockfd  , int* my_port, struct sockaddr_in* client_addr  , seg* snd_s  , seg* rcv_s, unsigned int* my_seq_num , unsigned int* my_ack_num );

//Unacked list push & sremove
void empty_the_unacked_list(unacked_seg** head);
void seg_push(unacked_seg** head , seg* s );
void seg_remove(unacked_seg** head ,unsigned int seq_num);
seg find_lost_seg(unacked_seg** head , unsigned int seq_num);
int find_seg(unacked_seg** head , unsigned int seq_num);

//Push & Pop 
void push(const struct sockaddr_in* ,const seg* );
dgram pop();

//Print parameters 
void print_parameters(struct sockaddr_in,int);

//Some setting functions
void set_seg(seg* s , uint16_t source_port , uint16_t des_port, uint32_t seq_num, uint32_t ack_num);
void set_final_seg(seg* s);
void set_syn(seg* s);
void set_ack(seg* s);
void set_fin(seg* s);
void reset_seg(seg* s);
int seq_num_generate();
int is_syn(seg s);
int is_ack(seg *s);

int new_seg();

//For debug
void print_sockaddr_info(const struct sockaddr_in);

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
		sleep(1);
	}
	close(socket_fd);
}

int server_init(struct sockaddr_in* server,int port_num){
	
    int socket_fd;
    //int ret,on=1;
	if((socket_fd = socket(AF_INET,SOCK_DGRAM,0))<0){
        ERR_EXIT("Server:server_init:socket:establish");
    }
    /*
    if((ret = setsockopt( socket_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0){
        ERR_EXIT("Server:server_init:setsockopt");    
    }
    */
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

    //Setting timeout 
	int sockfd = parameters.sockfd;
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

void send_files(int sockfd ,int *my_port ,struct sockaddr_in* client_addr , seg* snd_s , seg* rcv_s , unsigned int* my_seq_num , unsigned int* my_ack_num){

    //Store the "rcv_s" or the first request from client will be erase
    seg tmp_s;
    memcpy(&tmp_s,rcv_s,sizeof(seg));

    //If there is any request from client, send the files 
    if(rcv_s->header.code.file_request_c!=0){
        
        for(int i=0 , request_c = tmp_s.header.code.file_request_c ; i < request_c ; i++){
            //Setting file info
            FILE* fp;
            char file_name[MAX_FILE_NAME_LEN];
            sprintf(file_name,"%c.mp4",tmp_s.data[i]);
            struct stat st;
            stat(file_name, &st);
            
            //Set seqence number to 1
            (*my_seq_num) = 1;

            
            if((fp = fopen(file_name,"rb"))==NULL){
                ERR_EXIT("Server:new_connection:send_files:fopen");
            }

            //Start sending...
            printf("Start to send file \"%s\" to %s : %d , the file size is %ld bytes.\n",file_name,inet_ntoa(client_addr->sin_addr),ntohs(client_addr->sin_port),st.st_size);
            unacked_seg *unacked_list_head = NULL;
            
            while(1){
                int finish = 0;

                //Send seg
                finish = send_seg(sockfd,my_port ,client_addr , fp , snd_s,my_seq_num , my_ack_num , file_name , &unacked_list_head);
                //Receive ACK
                receive_ack(sockfd,my_port ,client_addr ,snd_s , rcv_s ,my_seq_num , my_ack_num , &unacked_list_head);

                if(finish)
                    break;
            }
            
            printf("=================================================================================\n");
            printf("===================Send a packet : file \"%s\" complete.========================\n",file_name);
            printf("=================================================================================\n");
            
            empty_the_unacked_list(&unacked_list_head);
            fclose(fp);
         

        }
        //send_fin(sockfd , my_port, client_addr ,snd_s  , rcv_s, my_seq_num , my_ack_num );
    
    }
    else{
        printf("No sending request. Ending the connection...");
        return;
    }
    

}

void send_fin(int sockfd  , int* my_port, struct sockaddr_in* client_addr  , seg* snd_s  , seg* rcv_s, unsigned int* my_seq_num , unsigned int* my_ack_num ){
     int n;
     socklen_t size = sizeof(struct sockaddr_in);
     if((n = recvfrom(sockfd,rcv_s,MSS,0,(struct sockaddr*)client_addr,&size))==-1){
        ERR_EXIT("Server:new_connection:send_files:recvfrom:receive fin");
     }
     (*my_ack_num) = rcv_s->header.seq_num + 1;            
            
            
     reset_seg(snd_s);
     set_seg(snd_s,*my_port,ntohs(client_addr->sin_port),*my_seq_num,*my_ack_num);
     set_fin(snd_s);
     if((n = sendto(sockfd,snd_s,MSS,0,(struct sockaddr*)client_addr,size))==-1){
         ERR_EXIT("Server:new_connection:send_files:sendto:send fin");
     }
     (*my_seq_num)++;
     //Close socket
     close(sockfd);

}
int send_seg(int sockfd  , int* my_port, struct sockaddr_in* client_addr  , FILE* fp , seg* snd_s  , unsigned int* my_seq_num , unsigned int* my_ack_num ,char* file_name , unacked_seg** unacked_list_head){
    int n ,file_size , finish = 0;
    socklen_t size = sizeof(struct sockaddr_in);
    reset_seg(snd_s);
    set_seg(snd_s,*my_port,ntohs(client_addr->sin_port),*my_seq_num,*my_ack_num);
    file_size = fread(snd_s->data , sizeof(char) , MAX_DATA_SIZE , fp);

    if(file_size==0){
        //Send segement with final_seg= 1
        set_final_seg(snd_s);
        finish = 1;
        printf("\t\tSend a packet : last segement of file \"%s\"",file_name);
    }
    else
        printf("\t\tSend a packet at : %ld bytes",sizeof(snd_s->data));
    
    size = sizeof(struct sockaddr_in);
    //Push the segement into linked list
    seg_push(unacked_list_head , snd_s );
    
    (*my_seq_num)++;
    
    //Random lost segments 
    int random_lost = rand()%10000;
    if(random_lost <= LOST_RATE*100 ){
        printf(" **lost\n");
        // Segement lost 
        finish = 0 ;
    }
    else{
        printf("\n");
        if((n = sendto(sockfd,snd_s,MSS,0,(struct sockaddr*)client_addr,size))==-1){
            ERR_EXIT("Server:new_connection:send_files:send_seg:sendto");
        }
    }

    return finish;
}

void receive_ack(int sockfd , int* my_port , struct sockaddr_in* client_addr , seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num , unsigned int* my_ack_num , unacked_seg** unacked_list_head){
    int n ;
    socklen_t size = sizeof(struct sockaddr_in);

    reset_seg(rcv_s);
    do{
        if((n = recvfrom(sockfd,rcv_s,MSS,0,(struct sockaddr*)client_addr,&size))==-1){
            //ERR_EXIT("Server:new_connection:send_files:receive_ack:recvfrom");
            //If Server recvfrom timeout, send next seg(no wait for ack)
            return;
        }
         
        printf("\t\tReceive a packet (seq_num = %d , ack_num = %d)\n",rcv_s->header.seq_num,rcv_s->header.ack_num);
        
        //Whenever if I receive segement , my_ack_num should be increased.
        (*my_ack_num) = rcv_s->header.seq_num + 1;            
        
        
        if(!is_ack(rcv_s)){
            ERR_EXIT("Server:new_connection:send_files:receive_ack:Invalid ACK");
        }
        else if(rcv_s->header.ack_num == *my_seq_num ){
            /*If the ack_num is correct, finish receiving ack*/ 
            seg_remove(unacked_list_head,rcv_s->header.ack_num);
            break;
        }

        //Resend seg
        size = sizeof(struct sockaddr_in);
        reset_seg(snd_s);
        seg tmp = find_lost_seg(unacked_list_head,rcv_s->header.ack_num);
        set_seg(snd_s,*my_port,ntohs(client_addr->sin_port),tmp.header.seq_num,(*my_ack_num));
        memcpy(snd_s->data , &(tmp.data) , sizeof(tmp.data));
        printf("\tResend a packet at : %ld bytes (seq_num = %d , ack_num = %d)\n",sizeof(snd_s->data),snd_s->header.seq_num,snd_s->header.ack_num);
                    
        if((n = sendto(sockfd,snd_s,MSS,0,(struct sockaddr*)client_addr,size))==-1){
            ERR_EXIT("Server:new_connection:send_files:receive_ack:sendto");
        }

    }while(rcv_s->header.ack_num != *my_seq_num);       
    
}

void empty_the_unacked_list(unacked_seg** head){
    while((*head)){
        unacked_seg *tmp = *head;
        (*head) = (*head)->next;
        free(tmp);
    }
}

void seg_remove(unacked_seg** head ,unsigned int seq_num ){
    if((*head) == NULL){
        ERR_EXIT("seg_remove : Unacked segement list is empty!");
    }
    unacked_seg* tmp = (*head) , *next_head = (*head) , *to_be_free = NULL;

    while(tmp){
        if(tmp->s.header.seq_num < seq_num){
            if(tmp == next_head){
                next_head = tmp->next;
            }
            if(tmp->prev) tmp->prev->next = tmp->next;
            if(tmp->next) tmp->next->prev = tmp->prev;
            to_be_free = tmp;
            //printf("Segement with seq_num = %d is removed successufully!\n",tmp->s.header.seq_num);
        }
        if(to_be_free){
            free(to_be_free);
            to_be_free = NULL;
        }
        tmp = tmp->next;

    }   
    
    (*head) = next_head;

}
void seg_push(unacked_seg** head , seg* s ){
    //If the segement had already been in the list , return 
    if(find_seg(head,s->header.seq_num) == 1){
        return;
    }
    unacked_seg* new_seg = (unacked_seg*)malloc(sizeof(unacked_seg));
    
    memcpy(&(new_seg->s) , s , sizeof(seg));
    new_seg->next = NULL;
    new_seg->prev = NULL;    

    if((*head) == NULL){
        (*head) = new_seg;
    }
    else{
        (*head)->prev = new_seg;
        new_seg->next = (*head);
        (*head) = new_seg;
    }

}
seg find_lost_seg(unacked_seg** head , unsigned int seq_num){
    if((*head)==NULL){
        ERR_EXIT("find_lost_seg : Unacked segement list is empty!");
    }
    unacked_seg* tmp = (*head);
    while(tmp){
        if(tmp->s.header.seq_num == seq_num){
            return tmp->s;
        }
        tmp = tmp->next;
    }
    return tmp->s;
}
int find_seg(unacked_seg** head , unsigned int seq_num){
    if((*head)==NULL){
        return 0 ;
    }
    unacked_seg* tmp = (*head);
    while(tmp){
        if(tmp->s.header.seq_num == seq_num){
            return 1;
        }
        tmp = tmp->next;
    }
    return 0;
}


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

/************Some checking & setting functions*****************/
void set_final_seg(seg *s){
    s->header.code.final_seg = 1;
}
void set_syn(seg* s){
    s->header.code.S = 1;
}
void set_ack(seg* s){
    s->header.code.A = 1;
}

void set_fin(seg* s){
    s->header.code.F = 1;
}
void reset_seg(seg* s){
    bzero(s,sizeof(seg));
}

int is_syn(seg s){
	return (s.header.code.S==1);
}
int is_ack(seg* s){
        return (s->header.code.A==1);
}

int new_seg(){
	return !(head==tail);
}

int seq_num_generate(){
    int r = rand()%10000+1;
    return r;
}
void set_seg(seg* s , uint16_t source_port , uint16_t des_port, uint32_t seq_num, uint32_t ack_num){
       reset_seg(s);
       s->header.source_port = source_port;
       s->header.des_port = des_port;
       s->header.seq_num = seq_num;
       s->header.ack_num = ack_num;
}

/**************Just print parameters************************/
void print_parameters(struct sockaddr_in server_addr,int listening_port){
    printf("===========Parameters==========\n");
    printf("The RTT delay = %d ms\n",RTT);
    printf("The threshold = %d bytes\n",THRESHOLD);
    printf("The MSS = %d bytes\n",MSS);
    printf("The buffer size = %d bytes\n",BUFF_SIZE);
    printf("Server's IP is %s\n",inet_ntoa(server_addr.sin_addr));
    printf("Server is listening on port %d\n",listening_port);
}


/******************For debug*****************************/
void print_sockaddr_info(const struct sockaddr_in target){
	printf("Info : IP = %s , port = %d \n",inet_ntoa(target.sin_addr),htons(target.sin_port));
}


