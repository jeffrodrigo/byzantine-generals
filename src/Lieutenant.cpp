//
// Created by jeffrodrigo on 18/07/16.
//

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sstream>
#include <unistd.h>
#include <iostream>
#include <cmath>
#include <cstring>
#include "Lieutenant.h"

Lieutenant::Lieutenant(int32_t id, Loyalty loyalty, int nGenerals, int nTraitors)
        : General(id, loyalty, lieutenant, nGenerals, nTraitors)
{
    //discoverGeneralsAddresses();
    discoverGenerals();
}

Lieutenant::Lieutenant(int32_t id, int nGenerals, int nTraitors)
        : General(id,
                  id <= nTraitors? traitor : loyal,
                  lieutenant, nGenerals, nTraitors)
{
    //discoverGeneralsAddresses();
    discoverGenerals();
}

void Lieutenant::run()
{
    openSocket();

    OM(this->numberOfGenerals, this->numberOfTraitors, this->numberOfTraitors);
}


void Lieutenant::discoverGeneralsAddresses()
{
    int numHosts = this->numberOfGenerals;

    for (int host = 1; host < numHosts; host++) {
        if (host == myID.name)
            continue;

        stringstream address;
        address << "10.0.0." << host;
        this->generalAddresses.push_back(address.str());

        cout << "Adding " << generalAddresses[this->generalAddresses.size() - 1] << endl;
    }
}

Message Lieutenant::receiveMessage()
{
    char buffer[6];
    recv(this->sock, buffer, 6, 0);

    Message message(buffer);

    cout << "Received: " << message.printCommand() << " from lieutenant " << to_string(message.source.name) << endl;

    return message;
}

void Lieutenant::openSocket() {
    struct sockaddr_in saddr;
    int len = sizeof(struct sockaddr_in);

    saddr.sin_port = htons(5000);
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;

    this->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bind(this->sock, (struct sockaddr*) &saddr, (socklen_t)  len);
}

void Lieutenant::saveReceivedMessages(int round, vector<Message> msgs) {
    this->messages[round] = msgs;
}

void Lieutenant::sendMessage(string address, Message msg) {
    char buffer[msg.size()];
    struct sockaddr_in saddr;
    int len = sizeof(struct sockaddr_in);

    saddr.sin_port = htons(5000);
    saddr.sin_family = AF_INET;
    inet_aton(address.c_str(), &saddr.sin_addr);

    msg.serialize(buffer);
    sendto(this->sock, buffer, (size_t) msg.size(), 0, (struct sockaddr*) &saddr, (socklen_t) len);
}

Lieutenant::~Lieutenant() {
    close(this->sock);
}

void Lieutenant::sabotage(Message *msg) {
    msg->command = retreat;
}

bool Lieutenant::isTraitor() {
    return this->loyalty==traitor;
}

vector<Message> Lieutenant::OM(int nGenerals, int nTraitors, int k) {
    int nMsgs = pow(nGenerals - 2, nTraitors - k);

    vector<Message> rcvMessages = receiveMessages(nMsgs);
    saveReceivedMessages(k, rcvMessages);

    sleep(1);
    cout << endl;

    if (k == 0)
        return rcvMessages;

    actAsCommander(rcvMessages);
    rcvMessages = OM(nGenerals, nTraitors, k-1);

    if (k == nTraitors) {
        Command c = majority(k);
        cout << (c==attack?"Attack!":"Retreat!") << endl;
    }

    return rcvMessages;
}

vector<Message> Lieutenant::receiveMessages(int nMessages) {
    vector<Message> msgs;

    for (int i = 0; i < nMessages; i++){
        Message msg = receiveMessage();
        msgs.push_back(msg);
    }

    return msgs;
}

Command Lieutenant::majority(int k) {
    int nAttack = 0, nRetreat = 0;

    for (int j = 0; j <= k; j++) {
        vector<Message> msgs = messages[j];
        for (int i = 0; i < msgs.size(); i++) {
            if (msgs[i].command == attack)
                nAttack++;
            else
                nRetreat++;
        }
    }

    if (nAttack > nRetreat)
        return attack;
    return retreat;

}

void Lieutenant::setSender(Message *msg) {
    msg->source = this->myID;
}

void Lieutenant::actAsCommander(vector<Message> msgs) {
    for (int i = 0; i < this->generalAddresses.size(); i++) {
        for (int j = 0; j < msgs.size(); j++) {
            Message sndMsg = msgs[j];

            setSender(&sndMsg);
            if (this->isTraitor())
                sabotage(&sndMsg);
            sendMessage(generalAddresses[i], sndMsg);
        }
    }
}

void Lieutenant::discoverGenerals() {
    socklen_t len;
    int serverSock, clientSock;
    struct sockaddr_in saddr;
    string prefix = "10.0.0.";

    len = sizeof(saddr);
    serverSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    memset(&saddr, 0 ,sizeof(struct sockaddr_in));
    saddr.sin_port = htons(5000);
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    bind(serverSock, (struct sockaddr*) &saddr, len);

    listen(serverSock, numberOfGenerals);

    // Connect to other generals
    for (int host = 1; host < myID.name; host++) {
        struct GeneralAddress general;
        struct sockaddr_in caddr;
        string generalIP = prefix + to_string(host);

        general.id = GeneralIdentity(host);
        general.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        generals.push_back(general);

        memset(&caddr, 0 ,sizeof(struct sockaddr_in));
        caddr.sin_port = htons(5000);
        caddr.sin_family = AF_INET;
        inet_aton(generalIP.c_str(), &caddr.sin_addr);
        connect(general.sock, (struct sockaddr*) &caddr, len);

        cout << "[LOG] connecting to " << generalIP << endl;

        send(general.sock, (char*) &myID.name, 4, 0);
    }

    // wait for connections
    for (int k = this->myID.name + 1; k < numberOfGenerals; k++) {
        socklen_t len2;
        uint32_t generalID;
        GeneralAddress general;

        clientSock = accept(serverSock, (struct sockaddr*) &saddr, &len2);
        cout << "[LOG] a general connected ";

        read(clientSock, (char*) &generalID, 4);

        cout << "with id " << generalID << endl;

        general.id = GeneralIdentity(generalID);
        general.sock = clientSock;
        generals.push_back(general);
    }
}
