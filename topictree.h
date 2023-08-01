//
// topictree.h
//

#ifndef _topictree_h
#define _topictree_h

#include <circle/sched/mutex.h>
#include <circle/net/socket.h>
#include <circle/net/ipaddress.h>
#include "u8string.h"

#define MAX_CLIENTS 8
#define MAX_TOPICS 32

typedef struct Client {
  CSocket     *pSocket;
  CIPAddress  IPAddress;
  U8String    ID;
} Client;

typedef struct Topic {
  U8String      name; // const?
  Topic         *pParent; // const?
  Topic         *pChildren[MAX_TOPICS] = { nullptr };
  unsigned int  children = 0;
  Client        *pClients[MAX_CLIENTS] = { nullptr };
  unsigned int  subscriptions = 0;
} Topic;

class TopicTree {
public:
  TopicTree(CMutex *pMutex);
  ~TopicTree(void);

public:
  unsigned int GetTopicsNumber() const;
  Topic *GetTopic(unsigned int i) const; // get the topic at the index i

  unsigned int ComposeTopicName(u8 topicName[], const Topic* topic); // returns the length of the topicName; stores in topicName[] the full path of the topic
  u8 SubscribeClient(Topic *topic, Client *client); // returns the return code for the requested subscription
  void UnsubscribeClient(Topic *topic, Client *client);
  unsigned int Update2(Topic *topics[], const U8String topicNodes[], const unsigned int nodesNumber); // given the topicNodes[] from DecodeTopicPath(), it makes shure that all the nodes exist in the tree and returns the needed topic (gamma in the example)
  Topic *Update(const U8String topicNodes[], const unsigned int nodesNumber); // given the topicNodes[] from DecodeTopicPath(), it makes shure that all the nodes exist in the tree and returns the needed topic (gamma in the example)
  void Clean(void); // check for useless topic nodes in the tree and remove them (in a recursive manner)

private:
  Topic *FindTopicByParent(const U8String name, const Topic *parent); // returns the topic with that specific name and parent, if not found returns nullptr
  Topic *CreateTopic(const U8String name, Topic *parent); // creates a topic from his name and the pointer to the parent topic
  void AddTopicChild(Topic *parent, Topic *child); // adds the child to the parent
  void RemoveUnusedNodes(Topic *topic); // recursive function that removes unused children (and the topic itself eventually)
  void RemoveTopic(Topic *topic); // removes a specific topic
  void RemoveChildFromParent(Topic *child); // removes the child from the parent

private:
  CMutex        *m_pMutex;
  Topic         **m_ppTopics;
  unsigned int  m_TopicsNumber;
};

#endif // !_topictree_h
