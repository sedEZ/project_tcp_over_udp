#include"tcp_over_udp.h"

void    send_files(int sockfd , int*, struct sockaddr_in* client_addr  , seg* snd_s , seg* rcv_s , unsigned int* my_seq_num , unsigned int* my_ack_num);
int     send_seg(int sockfd , int* my_port , struct sockaddr_in* client_addr , seg* snd_s , unsigned int* my_seq_num , unsigned int* my_ack_num , char file_data[] , int* , int* , unsigned long , unsigned long*);
int receive_ack(int sockfd , int* my_port , struct sockaddr_in* client_addr , seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num , unsigned int* my_ack_num , char file_data[] , int *cwnd, unsigned int file_len , unsigned long*);
void    send_ack(int sockfd, int* my_port, struct sockaddr_in* client_addr, seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num, unsigned int* my_ack_num);
void    receive_fin(int sockfd , int* my_port , struct sockaddr_in* client_addr ,seg* snd_s, seg* rcv_s, unsigned int* my_seq_num , unsigned int* my_ack_num);
void    send_fin(int sockfd  , int* my_port, struct sockaddr_in* client_addr , seg* snd_s  , unsigned int* my_seq_num , unsigned int* my_ack_num );
int     receive_last_ack(int sockfd , int* my_port , struct sockaddr_in* client_addr ,seg* snd_s, seg* rcv_s, unsigned int* my_seq_num , unsigned int* my_ack_num );

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
int is_fin(seg *s);


//For debug
void print_sockaddr_info(const struct sockaddr_in);


void send_files(int sockfd ,int *my_port ,struct sockaddr_in* client_addr , seg* snd_s , seg* rcv_s , unsigned int* my_seq_num , unsigned int* my_ack_num){

    //Store the "rcv_s" or the first request from client will be erase
    seg tmp_s;
    memcpy(&tmp_s,rcv_s,sizeof(seg));

    //If there is any request from client, send the files 
    if(tmp_s.header.code.file_request_c!=0){
        
        for(int i=0 , request_c = tmp_s.header.code.file_request_c ; i < request_c ; i++){
            unsigned long int max_recv_ack = 0, already_sent_bytes_counter=0;
            int cwnd = MAX_DATA_SIZE , rwnd = MAX_FILE_LEN;
            //Setting file info
            FILE* fp;
            char file_data[MAX_FILE_LEN] = {0};
            char file_name[MAX_FILE_NAME_LEN];
            sprintf(file_name,"%c.mp4",tmp_s.data[i]);
            struct stat st;
            stat(file_name, &st);

            
            //Set seqence number to 1
            (*my_seq_num) = 1;

            
            if((fp = fopen(file_name,"rb"))==NULL){
                ERR_EXIT("Server:new_connection:send_files:fopen");
            }

            unsigned long file_len;
            if(( file_len = fread(file_data,sizeof(char),MAX_FILE_LEN,fp)) <=0){
                ERR_EXIT("Server:send_files:fread");
            }

            //Start sending...
            printf("Start to send file \"%s\" to %s : %d , the file size is %ld bytes.\n",file_name,inet_ntoa(client_addr->sin_addr),ntohs(client_addr->sin_port),file_len);
            
            while(1){
                int finish = 0 ,resend_finish = 0;

                printf("cwnd = %d , rwnd = %d , threshold = %d\n",cwnd , rwnd , THRESHOLD);
                //Send seg
                for(int pipeCounter = 0 ; pipeCounter <= cwnd/MSS ; pipeCounter++){
                    finish = send_seg(sockfd,my_port ,client_addr , snd_s,my_seq_num , my_ack_num , file_data , &cwnd , &rwnd , file_len, &already_sent_bytes_counter);
                    
                    if(finish)
                        break;
                }
                

                //Receive ACK
                resend_finish = receive_ack(sockfd,my_port ,client_addr ,snd_s , rcv_s ,my_seq_num , my_ack_num , file_data , &cwnd, file_len , &max_recv_ack);
                
                (*my_seq_num) = max_recv_ack;

                if(finish && resend_finish)
                    break;

            }

            
            printf("=================================================================================\n");
            printf("===================Send a packet : file \"%s\" complete.========================\n",file_name);
            printf("=================================================================================\n");
            
            fclose(fp);

        }


        //Recieve & Send FIN
        receive_fin(sockfd,my_port ,client_addr ,snd_s , rcv_s ,my_seq_num , my_ack_num);
        send_ack(sockfd, my_port, client_addr, snd_s , rcv_s ,my_seq_num, my_ack_num);
        send_fin(sockfd,my_port ,client_addr , snd_s,my_seq_num , my_ack_num);
        receive_last_ack(sockfd,my_port ,client_addr ,snd_s , rcv_s ,my_seq_num , my_ack_num);


        printf("Closing connection with %s : %d\n",inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
        close(sockfd) ;  
    }
    else{
        printf("No sending request. Ending the connection...");
        return;
    }

}
int send_seg(int sockfd  , int* my_port, struct sockaddr_in* client_addr  , seg* snd_s  , unsigned int* my_seq_num , unsigned int* my_ack_num ,char file_data[] ,int* cwnd , int* rwnd , unsigned long file_len , unsigned long *already_sent_bytes_counter){
    int n , finish = 0;
    socklen_t size = sizeof(struct sockaddr_in);

    reset_seg(snd_s);
    set_seg(snd_s,*my_port,ntohs(client_addr->sin_port),*my_seq_num,*my_ack_num);
    unsigned int i;
    for(i=0 ; i < (unsigned int)(*cwnd) ; i++){
        snd_s->data[i] = file_data[(*my_seq_num) + i];
    }

    if(*my_seq_num >= file_len){
        set_final_seg(snd_s);
        finish = 1;
        //Send segement with final_seg= 1
        printf("\t\tSend a packet : file-ending segement");
    }
    else
        printf("\t\tSend a packet at : %d bytes",*my_seq_num);
    
    //Setting lost 
    int random_lost = rand()%10000;

    if(random_lost <= LOST_R*100){ //If LOST_R = 5 => 5% lost
        printf("    **lost");
        finish = 0;
    }
    else if((n = sendto(sockfd,snd_s,i,0,(struct sockaddr*)client_addr,size))==-1){
        ERR_EXIT("Server:new_connection:send_files:sendto");
    }

    printf("\n");
    
    (*rwnd) -= (*cwnd);
    //(*cwnd) *= 2
    (*my_seq_num) += i;

    return finish;
}
void send_fin(int sockfd  , int* my_port, struct sockaddr_in* client_addr , seg* snd_s  , unsigned int* my_seq_num , unsigned int* my_ack_num){
    int n ;
    socklen_t size;
    reset_seg(snd_s);
    set_seg(snd_s,*my_port,ntohs(client_addr->sin_port),*my_seq_num,*my_ack_num);
    set_fin(snd_s);

    size = sizeof(struct sockaddr_in);
    
    if((n = sendto(sockfd,snd_s,MSS,0,(struct sockaddr*)client_addr,size))==-1){
        ERR_EXIT("Server:new_connection:send_files:sendto");
    }

    printf("\t\tSend FIN.\n");

    (*my_seq_num)+=1;

}
void send_ack(int sockfd, int* my_port, struct sockaddr_in* client_addr, seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num, unsigned int* my_ack_num){
      int n;
      socklen_t size = sizeof(struct sockaddr_in);
  
      reset_seg(snd_s);
      set_seg(snd_s,(*my_port),ntohs(client_addr->sin_port),(*my_seq_num),(*my_ack_num));
      set_ack(snd_s);
  
      if((n = sendto(sockfd , snd_s , MSS , 0 , (struct sockaddr*)client_addr , size)) < 0){
          ERR_EXIT("Server:send_ack");
      }
      (*my_seq_num)+=1;
  }


int receive_ack(int sockfd , int* my_port , struct sockaddr_in* client_addr , seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num , unsigned int* my_ack_num , char file_data[] , int *cwnd, unsigned int file_len, unsigned long *max_recv_ack){
    int n ,finish = 0;
    socklen_t size = sizeof(struct sockaddr_in);
    
    //Check if the packet lost , if lost -> resend 
    reset_seg(rcv_s);
    
    for(int pipeCounter = 0 ; pipeCounter <= (*cwnd)/MSS ; pipeCounter++){
        if((n = recvfrom(sockfd,rcv_s,MSS,0,(struct sockaddr*)client_addr,&size))==-1){
            //If timeout => resend 
            /*
            reset_seg(snd_s);
            set_seg(snd_s,*my_port,ntohs(client_addr->sin_port),(*max_recv_ack),*my_ack_num);
            
            //If the ack_num not fit -> resend the segement
            for(unsigned int i=0 ; i < MAX_DATA_SIZE && *max_recv_ack + i < file_len; i++){
               snd_s->data[i] = file_data[*max_recv_ack+i];
            }
            if((n = sendto(sockfd,snd_s,MSS,0,(struct sockaddr*)client_addr,size))==-1){
                ERR_EXIT("Server:new_connection:receive_ack:sendto");
            }*/
            printf("Resend bytes : %ld\n",(*max_recv_ack));
        }
        else if(rcv_s->header.ack_num != (*my_seq_num)){
            (*my_ack_num) = rcv_s->header.seq_num + 1;
            
            if(!is_ack(rcv_s)){
                ERR_EXIT("Server:new_connection:send_files:Invalid ACK");
            }
            //If the ack number is not reaching (*my_seq_num) => set new received ack
            printf("\t\tReceive a packet (seq_num = %d , ack_num = %d)\n",rcv_s->header.seq_num,rcv_s->header.ack_num);
            if(rcv_s->header.ack_num > (*max_recv_ack)){
                (*max_recv_ack) = rcv_s->header.ack_num;
            }
            if((*my_seq_num) >= file_len){
                finish = 1;
                break;
            }
        }
        else if(rcv_s->header.ack_num ==  (*my_seq_num)){
            (*my_ack_num) = rcv_s->header.seq_num + 1;
            
            if(!is_ack(rcv_s)){
                ERR_EXIT("Server:new_connection:send_files:Invalid ACK");
            }
            printf("\t\tReceive a packet (seq_num = %d , ack_num = %d)\n",rcv_s->header.seq_num,rcv_s->header.ack_num);
            
            if((*my_seq_num) >= file_len)
                finish = 1;
            
            (*max_recv_ack) = rcv_s->header.ack_num;
            
            break;
        }
    }
   
    return finish; 
}
void receive_fin(int sockfd , int* my_port , struct sockaddr_in* client_addr , seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num , unsigned int* my_ack_num){
      int n ;
      socklen_t size = sizeof(struct sockaddr_in);
      reset_seg(rcv_s);
  
      do{
        if((n = recvfrom(sockfd,rcv_s,MSS,0,(struct sockaddr*)client_addr,&size))==-1){
            ERR_EXIT("Server:new_connection:receive_fin:recvfrom");
        }
      }while(!is_fin(rcv_s));
  
  
      printf("\t\tReceived FIN\n");
      (*my_ack_num) = rcv_s->header.seq_num + 1;
  
}
int receive_last_ack(int sockfd , int* my_port , struct sockaddr_in* client_addr , seg* snd_s , seg* rcv_s ,unsigned int* my_seq_num , unsigned int* my_ack_num){
    int n ;
    socklen_t size = sizeof(struct sockaddr_in);
    reset_seg(rcv_s);
    
    while((n = recvfrom(sockfd,rcv_s,MSS,0,(struct sockaddr*)client_addr,&size))==-1){
        //Timeout => retransmitt
        if((n = sendto(sockfd,snd_s,MSS,0,(struct sockaddr*)client_addr,size))==-1){
            ERR_EXIT("Server:new_connection:receive_ack:sendto");
        }  
        printf("Resend Sucesses!\n");
        if(snd_s->header.code.F)
            return 1;
    }

    printf("\t\tReceive a packet (seq_num = %d , ack_num = %d)\n",rcv_s->header.seq_num,rcv_s->header.ack_num);
                    
    if(!is_ack(rcv_s)){
        ERR_EXIT("Server:new_connection:send_files:Invalid ACK");
    }

    (*my_ack_num) = rcv_s->header.seq_num + 1;
   
    return 0; 
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

void reset_seg(seg* s){
    memset(s->data,0,MAX_DATA_SIZE);
    memset(s,0,MSS);
}

int is_syn(seg s){
	return (s.header.code.S==1);
}
int is_fin(seg* s){
	return (s->header.code.F==1);
}
int is_ack(seg* s){
    return (s->header.code.A==1);
}

void set_fin(seg* s){
    s->header.code.F = 1;
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


