//
// topictree.c
//

#include <circle/logger.h>
#include "circle/util.h"
#include "topictree.h"

#define m_Name "topictree"
// const char m_Name[] = "broker";

TopicTree::TopicTree(CMutex *pMutex) : m_pMutex(pMutex) {
  m_ppTopics = new Topic*[MAX_TOPICS]{};
  m_TopicsNumber = 0;
}

TopicTree::~TopicTree(void) {
  delete [] m_ppTopics;
  m_ppTopics = 0;
}

unsigned int TopicTree::GetTopicsNumber() const {
  return m_TopicsNumber;
};

Topic *TopicTree::GetTopic(unsigned int i) const {
  return i <= m_TopicsNumber ? m_ppTopics[i] : 0;
};

unsigned int TopicTree::ComposeTopicName(u8 topicName[], const Topic* topic) {
  const Topic *topics[MAX_TOPICS];
  const Topic *t = topic;
  unsigned level = 0;

  // travel the tree from children to parent until you reach the top level
  do {
    topics[level] = t;
    t = t->pParent;
    level++;
  } while(t != nullptr);

  // Alternative implementation (not as much readable as the other version IMO)
  // const Topic *topics[MAX_TOPICS];
  // unsigned level = 0;
  // topics[0] = topic;
  // while(topics[level]->pParent != nullptr) {
  //   level++;
  //   topics[level] = topics[level-1]->pParent;
  // }
  // level++;

  // compose the topic name
  unsigned int currentByte = 0;
  for (unsigned int i = level-1; i > 0; i--) {
    memcpy(&topicName[currentByte], topics[i]->name.GetString(), topics[i]->name.GetLength());
    currentByte += topics[i]->name.GetLength();
    topicName[currentByte] = '/';
    currentByte += 1;
  }
  // the last filter name should not be followed by '/'
  memcpy(&topicName[currentByte], topics[0]->name.GetString(), topics[0]->name.GetLength());
  currentByte += topics[0]->name.GetLength();
  return currentByte;
}

u8 TopicTree::SubscribeClient(Topic *topic, Client *client) {
  // topic->name.Print(m_Name, "Subscribe to topic:");
  // if (topic->pParent != nullptr) topic->pParent->name.Print(m_Name, "Parent:");
  // else CLogger::Get()->Write(m_Name, LogNotice, "Parent: nullptr");
  // CLogger::Get()->Write(m_Name, LogNotice, "subscriptions: %u", topic->subscriptions);

  for (unsigned int i = 0; i < topic->subscriptions; i++) {
    if (topic->pClients[i] == client) {
      topic->name.Print(m_Name, "The client is already subscribed to this topic:");
    }
  }
  if (topic->subscriptions < MAX_CLIENTS) {
    m_pMutex->Acquire();
    topic->pClients[topic->subscriptions] = client;
    (topic->subscriptions)++;
    m_pMutex->Release();
    /*
      Since we currently do not support QoS > 0, the only return code for
      success is 0.
    */
    return 0;
  } else {
  // Return 128 if a failure occurred
    CLogger::Get()->Write(m_Name, LogError, "Something went wrong while trying to subscribe the client");
    return 128;
  }
}

void TopicTree::UnsubscribeClient(Topic *topic, Client *client) {
  for (unsigned int i = 0; i < topic->subscriptions; i++) {
    if (topic->pClients[i] == client) {
      topic->name.Print(m_Name);

      // remove element from array
      m_pMutex->Acquire();
      for (unsigned int j = i+1; j < topic->subscriptions; j++) {
        topic->pClients[j-1] = topic->pClients[j];
      }
      topic->pClients[topic->subscriptions] = nullptr;
      (topic->subscriptions)--;
      m_pMutex->Release();
      break;
    }
  }
  // if (topic->subscriptions == 0) RemoveTopic(topic);
  Clean();
}

/*
  TODO: Implement wildcard detection (# and +)
*/
Topic *TopicTree::Update(const U8String topicNodes[], const unsigned int nodesNumber) {
  Topic *parent = nullptr;
  Topic *topic = nullptr;
  for (unsigned int i = 0; i < nodesNumber; i++) {
    topic = FindTopicByParent(topicNodes[i], parent);

    if (topic == nullptr) {
      topic = CreateTopic(topicNodes[i], parent);
      // topic->name.Print(m_Name, "Created:");
      if (parent != nullptr) { // it's the same condition as if (i == 0)
        AddTopicChild(parent, topic);
      }
    }

    // topic->name.Print(m_Name, "Topic name:");
    // if (topic->pParent != nullptr) {
    //   topic->pParent->name.Print(m_Name, "Parent topic name:");
    // } else CLogger::Get()->Write(m_Name, LogNotice, "Parent nullptr");
    parent = topic;
  }
  /*
    The only way this can return a nullptr is if the number of nodes is 0 but
    this case should not occur.
  */
  return topic;
}

Topic *TopicTree::FindTopicByParent(const U8String name, const Topic *parent) {
  for (unsigned int i = 0; i < m_TopicsNumber; i++) {
    Topic *t = m_ppTopics[i];
    if (t->name == name && t->pParent == parent) {
      return t;
    }
  }
  return nullptr;
}

Topic *TopicTree::CreateTopic(const U8String name, Topic *parent) {
  Topic *t = new Topic;
  t->name = name;
  t->pParent = parent;
  t->subscriptions = 0;
  t->children = 0;

  m_pMutex->Acquire();
  m_ppTopics[m_TopicsNumber] = t;
  m_TopicsNumber++;
  m_pMutex->Release();
  return t;
  // return topics[topicsNumber];
}

void TopicTree::AddTopicChild(Topic *parent, Topic *child) {
  parent->pChildren[parent->children] = child;
  parent->children++;
}

void TopicTree::Clean(void) {
  m_pMutex->Acquire();
  // for each topic in the tree
  for (unsigned int i = 0; i < m_TopicsNumber; i++) {
    // if the topic is a top level node (i.e. has no parent)
    if (m_ppTopics[i]->pParent == nullptr) {
      // remove unused children and eventually remove the topic itself
      RemoveUnusedNodes(m_ppTopics[i]);
    }
  }
  m_pMutex->Release();
}

/*
  Recursive function
*/
void TopicTree::RemoveUnusedNodes(Topic *topic) {
  // for every child node
  for (unsigned int i = 0; i < topic->children; i++) {
    // call this function recursively
    RemoveUnusedNodes(topic->pChildren[i]);
  }
  // if the topic is unused remove it
  if (topic->children == 0 && topic->subscriptions == 0) {
    RemoveTopic(topic);
  }
}

void TopicTree::RemoveTopic(Topic *topic) {
  // PrintString("Removing topic", topic->name);
  // topic->name.Print(m_Name, "Removing topic");
  for (unsigned int i = 0; i < m_TopicsNumber; i++) {
    if (m_ppTopics[i] == topic) {
      // remove the node pointer from the parent
      RemoveChildFromParent(m_ppTopics[i]);
      // remove element from array
      for (unsigned int j = i+1; j < m_TopicsNumber; j++) {
        m_ppTopics[j-1] = m_ppTopics[j];
      }

      // delete(topics[topicsNumber]->name.string);
      // delete(topics[topicsNumber]);

      m_ppTopics[m_TopicsNumber] = nullptr;
      m_TopicsNumber--;
      // break the loop by returning
      return;
    }
  }
}

// creepy name
void TopicTree::RemoveChildFromParent(Topic *child) {
  Topic *parent = child->pParent;
  if (parent == nullptr) return;

  // search the topic through the parent's children
  for (unsigned int i = 0; i < parent->children; i++) {
    if (parent->pChildren[i] == child) {
      // remove element from array
      for (unsigned int j = i+1; j < parent->children; j++) {
        parent->pChildren[j-1] = parent->pChildren[j];
      }
      parent->pChildren[parent->children] = nullptr;
      parent->children--;
      // break the loop by returning
      return;
    }
  }
}
