//
// broker.h
//

#ifndef _broker_h
#define _broker_h

#include <circle/sched/mutex.h>
#include <circle/sched/task.h>
// #include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/net/ipaddress.h>
#include <circle/net/mqtt.h>
#include "u8string.h"
#include "topictree.h"

#define MAX_PACKET_SIZE 1024

typedef struct ConnectFlags {
  boolean username, password, willRetain, willFlag, cleanSession;
  u8 willQoS, protocolVersion;
} ConnectFlags;

typedef struct PublishFlags {
  boolean dup, retain;
  u8 qos;
} PublishFlags;

class Broker : public CTask {
public:
  Broker(CMutex            *pMutex,
         TopicTree         *topicTree,
         CNetSubSystem     *pNetSubSystem,
         CSocket           *pSocket   = 0,  // is 0 for 1st created instance (listener)
         const CIPAddress  *pClientIP = 0);
  ~Broker(void);

  void Run(void);

private:
  void Listener(void);  // accepts incoming connections and creates worker task
  void Worker(void);    // processes a connection

  void ProcessPacket(const U8String buffer); // main function
  TMQTTPacketType GetRequestPacketType(const u8* buffer); // returns the request packet type

  // Connect - ConnAck - Disconnect
  int ValidateConnectPacket(const u8* buffer, ConnectFlags *flags);  // if the packet is not processable returns -1 otherwise returns the 'keep alive' parameter
  void SendConnAck(const boolean sessionPresent, const u8 returnCode);
  void Disconnect(void); // eventually unsubscribes the client

  // Publish
  int ExtractPublishFlags(u8 byte, PublishFlags *flags); // 0 = ok, 1 = not ok
  Topic *ExtractPublishTopic(const U8String filter); // returns topic requested
  void PublishMessage(const Topic *topic, const U8String message);
  unsigned int PackMessage(u8 packet[], const PublishFlags flags, const Topic *topic, const U8String message); // returns the topic length

  // Subscribe - SubAck - Unsubscribe - Unsuback
  unsigned int ExtractRequestedTopics(const U8String payload, Topic *packetTopics[], const boolean qos); // extract the topics requested for the sub/unsub and returns the number of extracted topics
  void SendSubAck(const U8String packetID, const u8 *returnCodes, const unsigned int number);
  void SendUnsubAck(const U8String packetID);

  // utilities
  void DecodeTopicPath(U8String topicNodes[], unsigned int *nodesNumber, const U8String filter); // goes from "alpha/beta/gamma" (stored in the filter argument) to ["alpha", "beta", "gamma"] (stored in the topicNodes[] argument)
  unsigned int EncodeRemainingLength(u8 *remainingLength, unsigned int length); // encodes the remaining length field (algorithm show in the MQTT specification)
  int DecodeRemainingLength(u32 *encodedBytes, const u8 *buffer); // decodes the remaining length field (algorithm show in the MQTT specification)
  void EncodeUTFNumber(u8 encoded[], const unsigned int number); // encodes the number in 2 bytes
  u16 DecodeUTFNumber(const u8 msb, const u8 lsb); // returns the number represented by the combination of 2 bytes

private:
  boolean         m_Connected; // this is used to avoid calling Disconnect() twice
  CString         m_Name;
  CMutex          *m_pMutex;
  TopicTree       *m_pTopicTree;
  CNetSubSystem   *m_pNetSubSystem;
  CSocket         *m_pSocket;
  Client          m_Client;

  static unsigned s_nInstanceCount;
};

#endif // !_broker_h
