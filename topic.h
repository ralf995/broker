#ifndef _topic_h
#define _topic_h

#include <circle/net/socket.h>
#include <circle/net/ipaddress.h>
#include "u8string.h"

#define MAX_CLIENTS 8

typedef struct Client {
  CSocket     *pSocket;
  CIPAddress  IPAddress;
  U8String    ID;
} Client;

class Topic {
public:
  Topic();
  ~Topic();

public:
  U8String      name;
  Topic         *pParent;
  Topic         **pChildren;
  unsigned int  children;
  Client        **pClients;
  unsigned int  subscriptions;
};

#endif // !_topic_h
