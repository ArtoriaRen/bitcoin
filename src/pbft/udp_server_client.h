/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   tcp_server_client.h
 * Author: l27ren
 *
 * Created on June 24, 2019, 5:15 PM
 */

#ifndef UDP_SERVER_CLIENT_H
#define UDP_SERVER_CLIENT_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdexcept>
#include "util.h"


class UdpClient_server_runtime_error : public std::runtime_error
{
public:
    UdpClient_server_runtime_error(const char *w) : std::runtime_error(w) {}
};


class UdpClient
{
public:
                        UdpClient();
                        ~UdpClient();

    int                 get_socket() const;
    int                 get_port() const;
    std::string         get_addr() const;

    int                 sendto(std::ostringstream& oss, const std::string& addr, int port);

private:
    int                 f_socket;
};


class UdpServer
{
public:
                        UdpServer(const std::string& addr, int port);
                        ~UdpServer();

    int                 get_socket() const;
    int                 get_port() const;
    std::string         get_addr() const;

    int                 recv(char *msg, size_t max_size);
    int                 timed_recv(char *msg, size_t max_size, int max_wait_ms, sockaddr_in* p_src_addr, size_t* p_len);

private:
    int                 f_socket;
    int                 f_port;
    std::string         f_addr;
    struct addrinfo *   f_addrinfo;
};

#endif /* UDP_SERVER_CLIENT_H */

