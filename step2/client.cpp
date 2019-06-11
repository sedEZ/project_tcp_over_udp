#include"tcp_over_udp.h"

void client(const char*,int,struct sockaddr_in*,int,char**);
int client_init(const char*,int,struct sockaddr_in*);
void reset_code(seg* s);
void three_way_handshake(int sockfd , int* my_port , struct sockaddr_in* server_addr,int request_c,char** request_v, unsigned int* my_seq_num , unsigned int* my_ack_num);
void receive_file(int sockfd, int my_port, struct sockaddr_in* server_addr, char* request_file_name,unsigned int* , unsigned int*);
int receive_seg(int sockfd, int my_port, struct sockaddr_in* server_addr, FILE* fp , seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num, unsigned int* my_ack_num);
void receive_fin(int sockfd , int my_port , struct sockaddr_in* server_addr , seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num , unsigned int* my_ack_num);
void receive_ack(int sockfd , int my_port , struct sockaddr_in* server_addr , seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num , unsigned int* my_ack_num);
void send_ack(int sockfd, int my_port, struct sockaddr_in* server_addr, seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num, unsigned int* my_ack_num);
void send_fin(int sockfd  , int my_port, struct sockaddr_in* server_addr , seg* snd_s  , unsigned int* my_seq_num , unsigned int* my_ack_num);

//Some setting & checking functions
void set_syn(seg* s);
void set_ack(seg* s);
void set_fin(seg* s);
int is_syn(seg* s);
int is_ack(seg *s);
int is_fin(seg* s);
int seq_num_generate();

//For debug
void print_sockaddr_info(const struct sockaddr_in);

//Set segement
void set_seg(seg* s , uint16_t source_port , uint16_t des_port, uint32_t seq_num, uint32_t ack_num);

/*******************************/
int main(int argc,char** argv){
	srand(time(NULL));   // Initialization.
    struct sockaddr_in server_addr;	
    char* server_ip = (char*)malloc(100);
	int server_port_num;
	if(argc < 3){
        printf("Usage : %s server_IP server_port_number [-f] {[file numbers]}\n",argv[0]);
        ERR_EXIT("Client:Invalid arguments");
    }
    strcpy(server_ip,argv[1]);
	server_port_num = atoi(argv[2]);//Set server's port number 

	int request_c = argc >= 4 ? (argc - 4) : 0;
	char** request_v = argv+4;
	client(server_ip,server_port_num,&server_addr,request_c,request_v);

	return 0;
}
/*******************************/

void client(const char* server_ip,int server_port_num,struct sockaddr_in* server_addr,int request_c,char** request_v){
	int sockfd;
    int my_port;
    unsigned int my_seq_num = seq_num_generate(),my_ack_num=0;
	//Initalize the connection
	if ((sockfd = client_init(server_ip,server_port_num,server_addr))<0){
		ERR_EXIT("Client:client_init");
	}

	//Three-way handshake & send request
	three_way_handshake(sockfd,&my_port,server_addr,request_c,request_v,&my_seq_num,&my_ack_num);

    //Receive file
    for(int i=0;i<request_c;i++)
        receive_file(sockfd,my_port,server_addr,request_v[i],&my_seq_num,&my_ack_num);

    printf("All Receiving Requests Complete!\n");

    seg* snd_s = (seg*)malloc(sizeof(seg));
    seg* rcv_s = (seg*)malloc(sizeof(seg));
    //Finish the connection 
    my_seq_num=0;

    send_fin(sockfd  , my_port, server_addr , snd_s  , &my_seq_num , &my_ack_num);
    receive_ack(sockfd,my_port ,server_addr , snd_s , rcv_s ,&my_seq_num , &my_ack_num);
    receive_fin(sockfd,my_port ,server_addr , snd_s , rcv_s ,&my_seq_num , &my_ack_num);
    send_ack(sockfd, my_port, server_addr, snd_s ,rcv_s ,&my_seq_num, &my_ack_num);
    
    printf("Closing connection with %s : %d\n",inet_ntoa(server_addr->sin_addr), ntohs(server_addr->sin_port));
    close(sockfd);

}

int client_init(const char* server_ip,int server_port,struct sockaddr_in* server_addr){
	int sockfd;

	memset(server_addr,0,sizeof(*server_addr));
	server_addr->sin_family = AF_INET;
	server_addr->sin_port = htons((unsigned short)server_port);
	server_addr->sin_addr.s_addr = inet_addr(server_ip);
	
	if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
		ERR_EXIT("Client:client_init:socket establish");
	}
	return sockfd;
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

void receive_file(int sockfd, int my_port , struct sockaddr_in* server_addr,char* request_file_name,unsigned int* my_seq_num, unsigned int* my_ack_num ){
    (*my_ack_num) = 0;
    char file_data[MAX_FILE_LEN+1];
    char file_name[MAX_FILE_NAME_LEN];
    
    memset(file_data,0,MAX_FILE_LEN+1);
    sprintf(file_name,"%s_result.mp4",request_file_name);
    seg *snd_s = (seg*)malloc(MSS);
    seg *rcv_s = (seg*)malloc(MSS);
    
    
    //File to be write
    FILE* fp ; 
    if((fp = fopen(file_name, "wb")) == NULL){
        ERR_EXIT("Client:receive_file:fopen");   
    }


    //Start receiving file
    printf("Receive file \"%s\" from %s :%d\n",file_name,inet_ntoa(server_addr->sin_addr),ntohs(server_addr->sin_port));
    while(1){
        //Receive seg
        int n;
        n = receive_seg(sockfd, my_port, server_addr, fp , snd_s , rcv_s ,my_seq_num, my_ack_num);

        if(n == 1){
            //n == 1 when all the segement of the file are received.
            break;
        }
        
        //Send ACK
        send_ack(sockfd, my_port, server_addr, snd_s ,rcv_s ,my_seq_num, my_ack_num);
        
        
    }
    //Send final ACK
    send_ack(sockfd, my_port, server_addr, snd_s ,rcv_s ,my_seq_num, my_ack_num);

    printf("===========================================================================\n");
    printf("===================Receive file \"%s\" complete.===================\n",file_name);
    printf("===========================================================================\n");

    fclose(fp);

}

//If the segement received had reach to the end , return 1
int receive_seg(int sockfd, int my_port, struct sockaddr_in* server_addr, FILE* fp , seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num, unsigned int* my_ack_num){

    int n;
    socklen_t size = sizeof(struct sockaddr_in);
    //Receive seg
    reset_seg(rcv_s);

    if((n = recvfrom(sockfd,rcv_s,MSS,0,(struct sockaddr*)server_addr,&size))==-1){
       ERR_EXIT("Server:new_connection:send_files:recvfrom"); printf("Closing connection with %s : %d\n",inet_ntoa(server_addr->sin_addr), ntohs(server_addr->sin_port));
    }
    printf("Receive a packet from %s : %d\n",inet_ntoa(server_addr->sin_addr),ntohs(server_addr->sin_port));
    printf("\t\tReceive a packet (seq_num = %d , ack_num = %d)\n",rcv_s->header.seq_num,rcv_s->header.ack_num);

    //If received final segement , finish receiveing.
    if(rcv_s->header.code.final_seg == 1){
        (*my_ack_num) = rcv_s->header.seq_num + sizeof(rcv_s->data);
        return 1;
    }

    //If the received seq_num is what I want , request next 
    if(rcv_s->header.seq_num != (*my_ack_num)){
        printf("Received package not fit!!\n");
    }
    else{
       (*my_ack_num) = rcv_s->header.seq_num + sizeof(rcv_s->data);
        
        //Write into file(video)
        if(fwrite(rcv_s->data,sizeof(char),sizeof(rcv_s->data),fp) <= 0 ){
            ERR_EXIT("Client:receive_file:fwrite");
        }
    }
    
    return 0 ;
}

void send_ack(int sockfd, int my_port, struct sockaddr_in* server_addr, seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num, unsigned int* my_ack_num){
    int n;
    socklen_t size = sizeof(struct sockaddr_in);

    reset_seg(snd_s);
    set_seg(snd_s,my_port,ntohs(server_addr->sin_port),(*my_seq_num),(*my_ack_num));
    set_ack(snd_s);
    
    if((n = sendto(sockfd , snd_s , MSS , 0 , (struct sockaddr*)server_addr , size)) < 0){
        ERR_EXIT("Client:receive_file:recvfrom");
    }
    (*my_seq_num)++;
}

void send_fin(int sockfd  , int my_port, struct sockaddr_in* server_addr , seg* snd_s  , unsigned int* my_seq_num , unsigned int* my_ack_num){
    int n ;
    socklen_t size;
    reset_seg(snd_s);
    set_seg(snd_s,my_port,ntohs(server_addr->sin_port),*my_seq_num,*my_ack_num);
    set_fin(snd_s);

    size = sizeof(struct sockaddr_in);

    if((n = sendto(sockfd,snd_s,MSS,0,(struct sockaddr*)server_addr,size))==-1){
        ERR_EXIT("Client:new_connection:send_files:sendto");
    }

    printf("\t\tSend FIN.\n");

    (*my_seq_num)++;

}
void receive_ack(int sockfd , int my_port , struct sockaddr_in* server_addr , seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num , unsigned int* my_ack_num){
      int n ;
      socklen_t size = sizeof(struct sockaddr_in);
      reset_seg(rcv_s);

      if((n = recvfrom(sockfd,rcv_s,MSS,0,(struct sockaddr*)server_addr,&size))==-1){
          ERR_EXIT("Client:receive_fin:recvfrom");
      }

      printf("\t\tReceive a packet (seq_num = %d , ack_num = %d)\n",rcv_s->header.seq_num,rcv_s->header.ack_num);

      if(!is_ack(rcv_s)){
          ERR_EXIT("Client:receive_ack:Invalid ACK");
      }

      (*my_ack_num) = rcv_s->header.seq_num + 1;

}

void receive_fin(int sockfd , int my_port , struct sockaddr_in* server_addr , seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num , unsigned int* my_ack_num){
    int n ;
    socklen_t size = sizeof(struct sockaddr_in);
    reset_seg(rcv_s);

    if((n = recvfrom(sockfd,rcv_s,MSS,0,(struct sockaddr*)server_addr,&size))==-1){
        ERR_EXIT("Client:receive_fin:recvfrom");
    }

    printf("\t\tReceive a packet (seq_num = %d , ack_num = %d)\n",rcv_s->header.seq_num,rcv_s->header.ack_num);

    if(!is_fin(rcv_s)){
        ERR_EXIT("Client:receive_fin:Invalid FIN");
    }

    (*my_ack_num) = rcv_s->header.seq_num + 1;

}


void three_way_handshake(int sockfd , int* my_port , struct sockaddr_in* server_addr,int request_c,char** request_v, unsigned int* my_seq_num , unsigned int* my_ack_num){
	
	seg *snd_s = (seg*)malloc(MSS);
	seg *rcv_s = (seg*)malloc(MSS);
    
	
	socklen_t addrlen = sizeof(struct sockaddr_in);
	int n;

	print_sockaddr_info(*server_addr);

	//Start three-way handshake
	printf("=========Start three-way handshake=========\n");
	
	//Step1
    reset_seg(snd_s);
    set_seg(snd_s,0,ntohs(server_addr->sin_port),*my_seq_num,*my_ack_num);
    set_syn(snd_s);

    printf("Send a packet(SYN) to %s : %d\n",inet_ntoa(server_addr->sin_addr),ntohs(server_addr->sin_port));
	if((n = sendto(sockfd,snd_s,MSS,0,(struct sockaddr*)server_addr,addrlen)) < 0){
		ERR_EXIT("Client:three_way_handshake:Step1:sendto");
	}
    (*my_seq_num)++;
	
	//Step2
    reset_seg(rcv_s);
	if((n = recvfrom(sockfd,rcv_s,MSS,0,(struct sockaddr*)server_addr,&addrlen)) < 0){
       ERR_EXIT("Client:three_way_handshake:Step2:recvfrom");
    }
	if(!is_syn(rcv_s) || !is_ack(rcv_s)){
		ERR_EXIT("Client:three_way_handshake:Step2:Invalid SYN&ACK ");
	}

    printf("Receive a packet(SYN/ACK) from %s : %d\n",inet_ntoa(server_addr->sin_addr),ntohs(server_addr->sin_port));
    printf("\t\tReceive a packet (seq_num = %d , ack_num = %d)\n",rcv_s->header.seq_num,rcv_s->header.ack_num);
    (*my_port) = rcv_s->header.des_port;
    (*my_ack_num) = rcv_s->header.seq_num + sizeof(rcv_s->data);

	//Step3
	reset_seg(snd_s);
    set_seg(snd_s,*my_port,ntohs(server_addr->sin_port),*my_seq_num,*my_ack_num);
	set_ack(snd_s);

	//Setting segement
    snd_s->header.code.file_request_c = request_c;
	if(request_c !=0){
		for(int i = 0 ; i < request_c ; i++ )
			snd_s->data[i] = request_v[i][0];
	}

    //Send
    printf("Send a packet(ACK) to %s : %d\n",inet_ntoa(server_addr->sin_addr),snd_s->header.des_port);
	if((n = sendto(sockfd,snd_s,MSS,0,(struct sockaddr*)server_addr,addrlen)) < 0){
		ERR_EXIT("Client:three_way_handshake:Step3:sendto");
	}
    (*my_seq_num)++;
	
	printf("=======Complete the three-way handshake======\n");
	
}
int is_syn(seg* s){
	return (s->header.code.S==1);
}
int is_ack(seg* s){
	return (s->header.code.A==1);
}
int is_fin(seg* s){
	return (s->header.code.F==1);
}
void print_sockaddr_info(const struct sockaddr_in target){
    printf("Info : IP = %s , port = %d \n",inet_ntoa(target.sin_addr),ntohs(target.sin_port));
}
int seq_num_generate(){
    int r = rand()%10000;    
    return r;
}

void set_seg(seg* s , uint16_t source_port , uint16_t des_port, uint32_t seq_num, uint32_t ack_num){
       s->header.source_port = source_port;
       s->header.des_port = des_port;
       s->header.seq_num = seq_num;
       s->header.ack_num = ack_num;
}
