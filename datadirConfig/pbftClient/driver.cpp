/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <iostream>
#include "pbftClient.h"

int main(){
    const std::string pbftClientIP = "127.0.0.1";
    int pbftClientPort = 16384;
    const std::string pbftLeaderIP = "127.0.0.1";
    int pbftLeaderPort = 8350;
    
    CPbftClient pbftClient(pbftClientIP, pbftClientPort, pbftLeaderIP, pbftLeaderPort);
    
    std::cout << "main" << std::endl;
    return 0;
}