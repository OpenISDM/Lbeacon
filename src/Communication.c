/*
 Copyright (c) 2016 Academia Sinica, Institute of Information Science

 License:

      GPL 3.0 : The content of this file is subject to the terms and
      cnditions defined in file 'COPYING.txt', which is part of this source
      code package.

 Project Name:

      BeDIS

 File Description:

      This file contains the programs to allow data transmission between
       LBeacon and Gateway.

 File Name:

      Communication.c

 Version:

       2.0, 20190103

 Abstract:

      BeDIS uses LBeacons to deliver 3D coordinates and textual
      descriptions of their locations to users' devices. Basically, a
      LBeacon is an inexpensive, Bluetooth Smart Ready device. The 3D
      coordinates and location description of every LBeacon are retrieved
      from BeDIS (Building/environment Data and Information System) and
      stored locally during deployment and maintenance times. Once
      initialized, each LBeacon broadcasts its coordinates and location
      description to Bluetooth enabled user devices within its coverage
      area.

 Authors:

      Holly Wang, hollywang@iis.sinica.edu.tw
      Gary Xiao, garyh0205@hotmail.com
      Chun Yu Lai, chunyu1202@gmail.com

*/

#include "Communication.h"

ErrorCode Wifi_init(sudp_config_beacon *udp_config){
    int optval = 0;
    int retry_times = 0;

    // 1. create receive socket
    retry_times = SOCKET_OPEN_RETRY;
    while(retry_times--){
        udp_config->recv_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if(udp_config->recv_socket >= 0){
            break;
        }
    }

    if(udp_config->recv_socket < 0){
        zlog_error(category_health_report,
                   "Unable to intitialize receive socket");
        zlog_error(category_debug,
                   "Unable to intitialize receive socket");
        return E_WIFI_INIT_FAIL;
    }
    optval=1;
    setsockopt(udp_config->recv_socket, SOL_SOCKET, SO_REUSEADDR,
               (const void*)&optval, sizeof(int));

    struct timeval timeout;
    timeout.tv_sec  = TIMEOUT_UDP_RECEIVCE_IN_SEC;
    timeout.tv_usec = 0;
    setsockopt(udp_config->recv_socket, SOL_SOCKET, SO_RCVTIMEO,
               &timeout, sizeof(timeout));

    bzero((char *)&udp_config->si_recv, sizeof(udp_config->si_recv));
    udp_config->si_recv.sin_family = AF_INET;
    udp_config->si_recv.sin_addr.s_addr = htonl(INADDR_ANY);
    udp_config->si_recv.sin_port =
        htons((unsigned short)udp_config->recv_portno);

    if(bind(udp_config->recv_socket,
            (struct sockaddr *)&udp_config->si_recv,
            sizeof(udp_config->si_recv)) == -1){

        zlog_error(category_health_report,
                   "Unable to bind to listen port");
        zlog_error(category_debug,
                   "Unable to bind to listen port");
        return E_WIFI_INIT_FAIL;
    }

    // 2. initialize packet queue (for received data from gateway)
    if(pkt_Queue_SUCCESS != init_Packet_Queue(&udp_config->recv_pkt_queue)){
        zlog_error(category_health_report,
                   "Unable to intitialize receive packet queue");
        zlog_error(category_debug,
                   "Unable to intitialize receive packet queue");
        return E_WIFI_INIT_FAIL;
    }

    // 3. initialize packet queue (for worker threads to send)
    if(pkt_Queue_SUCCESS != init_Packet_Queue(&udp_config->send_pkt_queue)){
        zlog_error(category_health_report,
                   "Unable to initialze send packet queue");
        zlog_error(category_debug,
                   "Unable to initialze send packet queue");
        return E_WIFI_INIT_FAIL;
    }

    return WORK_SUCCESSFULLY;
}


ErrorCode receive_data(void *udp_config){
    sudp_config_beacon *udp_config_ptr = NULL;
    int numbytes = 0;
    int clientlen;
    struct sockaddr_in clientaddr;
    char recv_buf[WIFI_MESSAGE_LENGTH];
    int ret_val = 0;

    udp_config_ptr = (sudp_config_beacon *) udp_config;

   // 1. receive data from gateway
    while(true == ready_to_work){
        memset(recv_buf, 0, sizeof(recv_buf));
        numbytes = recvfrom(udp_config_ptr->recv_socket, recv_buf,
                            sizeof(recv_buf), 0,
                            (struct sockaddr *)&clientaddr, &clientlen);
        if(numbytes < 0){
/*
            zlog_error(category_health_report,
                       "Unable to receive data from gateway "
                       "via recvfrom(), strerror(errno)=[%s]",
                       strerror(errno));
            zlog_error(category_debug,
                       "Unable to receive data from gateway "
                       "via recvfrom(), strerror(errno)=[%s]",
                       strerror(errno));
*/
        }else{
            ret_val = addpkt(&udp_config_ptr->recv_pkt_queue, UDP,
                             inet_ntoa(clientaddr.sin_addr), recv_buf,
                             strlen(recv_buf));

            if(pkt_Queue_SUCCESS != ret_val){
                zlog_error(category_health_report,
                           "Unable to add receive packet to receive "
                           "packet queue. error=[%d]", ret_val);
                zlog_error(category_debug,
                           "Unable to add receive packet to receive "
                           "packet queue. error=[%d]", ret_val);
            }
        }
    }

    return WORK_SUCCESSFULLY;
}

ErrorCode send_data(void *udp_config){
    sudp_config_beacon *udp_config_ptr = NULL;
    int send_socket = 0;
    int numbytes = 0;
    struct hostent *he;
    struct sockaddr_in addr;
    int retry_times = 0;
    bool is_in_failure_state = false;

    udp_config_ptr = (sudp_config_beacon*) udp_config;

    // 1. initialize send socket used by this worker thread
    if((he=gethostbyname(udp_config_ptr->send_ipv4_addr)) == NULL){
        zlog_error(category_health_report,
                   "Unable to gethostbyname(), addr=[%s]",
                   udp_config_ptr->send_ipv4_addr);
        zlog_error(category_debug,
                   "Unable to gethostbyname(), addr=[%s]",
                   udp_config_ptr->send_ipv4_addr);
        return E_OPEN_SOCKET;
    }

    retry_times = SOCKET_OPEN_RETRY;
    while(retry_times--){
        send_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if(send_socket >= 0){
            break;
        }
    }

    if(send_socket < 0){
        zlog_error(category_health_report,
                   "Unable to intitialize send socket");
        zlog_error(category_debug,
                   "Unable to intitialize send socket");
        return E_OPEN_SOCKET;
    }

    bzero((char*) &addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(udp_config_ptr->send_portno);
    addr.sin_addr = *((struct in_addr *)he->h_addr);

    // 2. send message to gateway
    while(true == ready_to_work){

        if(false == is_null(&udp_config_ptr->send_pkt_queue)){

            // 2.1 get a packet from packet queue
            sPkt tmp_pkt = get_pkt(&udp_config_ptr->send_pkt_queue);
            if(NONE != tmp_pkt.type){
                // 2.2 send data to gateway
                zlog_debug(category_debug,
                           "prepare to send message =[%s] len=[%d]",
                           tmp_pkt.content, strlen(tmp_pkt.content));

                numbytes=sendto(send_socket, tmp_pkt.content,
                                strlen(tmp_pkt.content), 0,
                                (struct sockaddr *)&addr,
                                sizeof(struct sockaddr));

                if(numbytes >= 0){
                    if(is_in_failure_state){
                        is_in_failure_state = false;

                        zlog_info(category_health_report,
                                  "send_data returns back to work");
                        zlog_info(category_debug,
                                  "send_data returns back to work");
                    }
                }else{
                    is_in_failure_state = true;
                    zlog_error(category_health_report,
                               "Unable to send data to gateway via "
                               "sendto(), strerror(errno)=[%s]",
                               strerror(errno));
                    zlog_error(category_debug,
                               "Unable to send data to gateway via "
                               "sendto(), strerror(errno)=[%s]",
                               strerror(errno));
                }
            }
        }else{
            sleep_t(BUSY_WAITING_TIME_IN_MS);
        }
    }
    // 3. close the send socket
    close(send_socket);

    return WORK_SUCCESSFULLY;
}

ErrorCode Wifi_free(sudp_config_beacon *udp_config){

    // 1. close receive socket
    close(udp_config->recv_socket);

    // 2. release the packet queue
    Free_Packet_Queue(&udp_config->recv_pkt_queue);

    // 3. release the packet queue
    Free_Packet_Queue(&udp_config->send_pkt_queue);

    return WORK_SUCCESSFULLY;
}
