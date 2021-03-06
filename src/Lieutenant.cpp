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
#include <fcntl.h>
#include <sys/select.h>
#include "Lieutenant.h"

extern int BYZ_RUNLOCAL;

Lieutenant::Lieutenant(int32_t id, Loyalty loyalty, int nGenerals, int nTraitors)
        : General(id, loyalty, lieutenant, nGenerals, nTraitors)
{
    discoverGenerals();
}

Lieutenant::Lieutenant(int32_t id, int nGenerals, int nTraitors)
        : Lieutenant(id, id <= nTraitors? traitor : loyal, nGenerals, nTraitors)
{
}

Lieutenant::~Lieutenant()
{
    close(serverSock);

    for (int i = 0; i < generals.size(); i++)
        close (generals[i].sock);

    close(commanderSock);
}

void Lieutenant::run()
{
    OM(numberOfGenerals, numberOfTraitors, numberOfTraitors);

    Message m(GeneralIdentity(0), attack);

    try {
        Command cmd = decide(numberOfTraitors, m);
        cout << endl << "Decision: " << (cmd==attack?"Attack!":"Retreat!");
    }
    catch(...) {
        cout << "Deciding... Out of memory\n";
    }
}

Command Lieutenant::decide(int round, Message message) {
    vector<Command> commands;

    for (int i = 0; i < messages[round].size(); i++) {
        if (!messages[round][i].comesFrom(message))
            continue;

        if (round > 0)
            commands.push_back(decide(round-1, messages[round][i]));
        else
            commands.push_back(messages[round][i].command);
    }

    return majority(commands);
}

Command Lieutenant::majority(vector<Command> commands) {
    int nAttack = 0, nRetreat = 0;

    for (int i = 0; i < commands.size(); i++){
        if (commands[i] == attack)
            nAttack++;
        else
            nRetreat++;
    }

    if (nAttack > nRetreat)
        return attack;
    return retreat;
}


vector<Message> Lieutenant::OM(int nGenerals, int nTraitors, int k)
{
    vector<Message> receivedMessages = receiveMessages(k);

    if (k == 0)
        return receivedMessages;

    actAsCommander(receivedMessages);
    receivedMessages = OM(nGenerals, nTraitors, k-1);

    return receivedMessages;
}

Message Lieutenant::receiveMessage(GeneralAddress general)
{
    int retval;
    size_t count;
    char buffer[MSG_MAXBUFLEN]; //TODO: optimize this
    uint16_t pathlen;

    fd_set fds;
    struct timeval tv;

    cout << "Receiving message from " << general.id.name << "...";
 
    tv.tv_sec = 15;
    tv.tv_usec = 0;

    FD_ZERO(&fds);
    FD_SET(general.sock, &fds);

    retval = select(general.sock+1, &fds, NULL, NULL, &tv);
    if (retval < 1)
        cout << "Error listening to socket\n";
    //TODO: on timeout, pretend the reception of a RETREAT

    retval = (int) recv(general.sock, &pathlen, 2, 0);
    if (retval < 2)
        cout << "Error receiving message\n";

    memcpy(&buffer[0], (char*) &pathlen, 2);

    count = (size_t) (4 * pathlen) + 1;
    if (recv(general.sock, &buffer[2], count, 0) < count)
        cout << "Error receiving message\n";

    Message message(buffer);

    cout << "Received: " << message.toString() << endl;

    return message;
}

void Lieutenant::saveReceivedMessages(int round, vector<Message> msgs)
{
    this->messages[round] = msgs;
}

vector<Message> Lieutenant::receiveMessages(int round)
{
    int j;
    Message msg;
    int nMessages;
    vector<Message> msgs;
    GeneralAddress commander, *general;
    GeneralAddress myself(myID, recvSock);

    cout << endl;

    if (round == numberOfTraitors) {
        commander = GeneralAddress(GeneralIdentity(0), commanderSock);

        msg = receiveMessage(commander);

        try {
            msgs.push_back(msg);
        }
        catch(...) {
            cout << "Receiving message... Out of memory\n";
        }
    }
    else {
        nMessages = 1;
        int k = this->numberOfTraitors - round;
        for (int i = 1; i <= k; i++) {
            nMessages *= (this->numberOfGenerals - i);
        }

        for (int i = 0; i < nMessages; i++) {

            j = i % (numberOfGenerals - 1);

            if (j == (numberOfGenerals - 2)) {
                general = &myself;
            }
            else {
                general = &generals[j];
            }

            msg = receiveMessage(*general);

            try {
                msgs.push_back(msg);
            }
            catch(...) {
                cout << "Receiving message... Out of memory\n";
            }
        }

    }

    saveReceivedMessages(round, msgs);

    return msgs;
}

void Lieutenant::actAsCommander(vector<Message> msgs)
{
    vector<Message> remainingMessages;

    // Try to append our ID in the path of each message
    for (int i = 0; i < msgs.size(); i++) {
        if (msgs[i].appendSource(myID)) {
            try {
                remainingMessages.push_back(msgs[i]);
            }
            catch (...) {
                cout << "Acting like a commander... Out of memory\n";
            }

        }
    }

    // Send messages to all lieutenants
    for (int i = 0; i < generals.size(); i++) {
        sendMessages(generals[i], remainingMessages);
    }

    //send messages also to myself
    GeneralAddress myself(myID, sendSock);
    sendMessages(myself, remainingMessages);
}

void Lieutenant::sendMessages(GeneralAddress general, vector<Message> msgs)
{
    Message sndMsg;
    for (int i = 0; i < msgs.size(); i++) {
        sndMsg = msgs[i];

        //TODO: call sabotage instead
        prepareMessage(&sndMsg);

        sendMessage(general, sndMsg);
    }
}

void Lieutenant::prepareMessage(Message *msg)
{
    if (this->isTraitorous())
        sabotage(msg);
}

void Lieutenant::sabotage(Message *msg)
{
    msg->command = retreat;
}

void Lieutenant::sendMessage(GeneralAddress general, Message msg)
{
    ssize_t count;
    char buffer[msg.size()];

    cout << "Sending message to " << general.id.name << "...";

    msg.serialize(buffer);

    count = send(general.sock, buffer, (size_t) msg.size(), 0);
    if (count < msg.size())
        cout << "Error sending message\n";

    cout << " Success.\n";
}


static struct sockaddr_in buildSockAddr(string *ip, int port)
{
    struct sockaddr_in addr;

    memset(&addr, 0 ,sizeof(struct sockaddr_in));
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;

    if (ip)
        inet_aton(ip->c_str(), &addr.sin_addr);
    else
        addr.sin_addr.s_addr = INADDR_ANY;

    return addr;
}

//TODO: close sockets on error
void Lieutenant::discoverGenerals()
{
    openServerSocket();

    if (connectToGenerals()) {
        cout << "Could not connect to generals. Exiting...\n";
        exit(1);
    }

    if (waitNewGeneralsConnections()) {
        cout << "Could not be found by generals. Exiting...\n";
        exit(1);
    }
}

void Lieutenant::openServerSocket()
{
    int retval;
    int option;
    socklen_t len;
    struct linger lngr;
    struct sockaddr_in addr;

    option = 1;
    len = sizeof(addr);
    this->serverSock = socket(AF_INET,
                              SOCK_STREAM,
                              IPPROTO_TCP);

    if (setsockopt(this->serverSock,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &option,
                   sizeof(int)) == -1) {
        perror("setsockopt: reuseaddr");
        exit(1);
    }

    lngr.l_linger = 120;
    lngr.l_onoff = 1;
    if (setsockopt(this->serverSock,
                   SOL_SOCKET,
                   SO_LINGER,
                   &lngr,
                   sizeof(struct linger))) {
        perror("setsockopt: linger\n");
        exit(1);
    }

    int port = 15000 + myID.name;

    addr = buildSockAddr(NULL, port);
    retval = bind(serverSock, (struct sockaddr*) &addr, len);
    if (retval < 0) {
        perror("bind\n");
        exit(1);
    }
    
    retval = listen(this->serverSock, this->numberOfGenerals);
    if (retval < 0) {
        perror("listen\n");
    	exit(1);
    }
}

int Lieutenant::connectToGenerals()
{
    int sock;
    int port;
    int flags;
    socklen_t len;
    bool connected;
    string ip, prefix;
    struct sockaddr_in addr;
    struct linger lngr;

    lngr.l_linger = 120;
    lngr.l_onoff = 1;

    prefix = "10.0.0.";
    ip = "127.0.0.1";
    len = sizeof(struct sockaddr_in);

    for (int host = 1; host <= myID.name; host++) {
        cout << "Connecting to " << host << "... ";

        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock <= 0) {
            cout << "Could not open socket\n";
            return -1;
        }

        if (!BYZ_RUNLOCAL) {
            ip = prefix + to_string(host);
        }
        port = 15000 + host;
        addr = buildSockAddr(&ip, port);

        flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        connected = false;
        while (connected == false) {
            connect(sock, (struct sockaddr*) &addr, len);
        
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sock, &fds);
            select(sock + 1, NULL, &fds, NULL, NULL);

            if (select < 0) {
                cout << "Error connecting)\n";
                return -1;
            }

            if (errno != EINPROGRESS && errno != ECONNREFUSED)
                connected = true;
        }

        flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags&~O_NONBLOCK);

        if (setsockopt(sock,
                       SOL_SOCKET,
                       SO_LINGER,
                       &lngr,
                       sizeof(struct linger))) {
            perror("Could not set linger time\n");
            exit(1);
        }

        if (send(sock, (char*) &myID.name, 4, 0) < 4) {
            cout << "Error sending my ID\n";
            return -1;
        }

        if (host == myID.name) {
            sendSock = sock;
        }
        else {
            GeneralAddress newGeneral(GeneralIdentity(host), sock);

            try {
                generals.push_back(newGeneral);
            }
            catch (...) {
                cout << "Connecting to general... Out of memory.";
            }
        }

        cout << " OK" << endl;
    }

    return 0;
}

int Lieutenant::waitNewGeneralsConnections()
{
    int error;
    int sock;

    socklen_t len;
    uint32_t generalID;
    struct sockaddr addr;
    struct linger lngr;

    lngr.l_linger = 120;
    lngr.l_onoff = 1;

    len = sizeof(struct sockaddr);

    for (int i = myID.name; i <= numberOfGenerals; i++) {
        sock = accept(serverSock, &addr, &len);

        if (sock <= 0) {
            cout << "Connection error. errno: " << errno << endl;
            return -1;
        }

        if (setsockopt(sock,
                       SOL_SOCKET,
                       SO_LINGER,
                       &lngr,
                       sizeof(struct linger))) {
            perror("Could not set linger\n");
            exit(1);
        }

        generalID = receiveGeneralIdentification(sock, &error);

        if (error) {
            return -1;
        }

        if (generalID == 0) {
            this->commanderSock = sock;
        }
        else if (generalID != myID.name) {
            cout << "Connection from " << generalID << endl;

            GeneralAddress newGeneral(GeneralIdentity(generalID), sock);

            try {
                generals.push_back(newGeneral);
            }
            catch (...) {
                cout << "Connection from general... Out of memory\n";
            }
        }
        else {
            recvSock = sock;
        }
    }

    return 0;
}

uint32_t Lieutenant::receiveGeneralIdentification(int sock, int *error)
{
    uint32_t id;
    fd_set fds;
    struct timeval tv;

    tv.tv_sec = 15;
    tv.tv_usec = 0;

    FD_ZERO(&fds);
    FD_SET(sock, &fds);

    if (select(sock+1, &fds, NULL, NULL, &tv) < 1) {
        cout << "Connection error\n";
        *error = 1;
        return 0;
    }

    if (read(sock, (char*) &id, 4) < 4) {
        cout << "Read error\n";
        *error = 1;
        return 0;
    }

    *error = 0;
    return id;
}
