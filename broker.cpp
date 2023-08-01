//
// broker.cpp
//

#include <circle/net/in.h> // IPPROTO_TCP
#include <circle/logger.h>
#include <circle/alloc.h>
#include <circle/util.h>  // memcpy
#include <assert.h>
#include "broker.h"

const char m_Name[] = "broker";

unsigned Broker::s_nInstanceCount = 0;

Broker::Broker(CMutex *pMutex,
               TopicTree *pTopicTree,
               CNetSubSystem *pNetSubSystem,
               CSocket *pSocket,
               const CIPAddress *pClientIP)
  : m_pMutex(pMutex),
    m_pNetSubSystem(pNetSubSystem),
    m_pSocket(pSocket)
{
  s_nInstanceCount++;

  if (pClientIP != 0) {
    m_Client.pSocket = pSocket;
    m_Client.IPAddress.Set(*pClientIP);
  }

  m_pTopicTree = pTopicTree;

  CLogger::Get()->Write("broker", LogNotice, "NEW INSTANCE: %u", s_nInstanceCount);
}

Broker::~Broker(void) {
  assert(m_pSocket == 0);

  m_pNetSubSystem = 0;

  CLogger::Get()->Write(m_Name, LogNotice, "REMOVING INSTANCE: %u", s_nInstanceCount);

  m_Name.Format("");
  s_nInstanceCount--;
}

void Broker::Run(void) {
  if (m_pSocket == 0) {
    m_Name.Format("listener");
    this->SetName(m_Name);
    Listener();
  }
  else {
    m_Name.Format("worker %u", s_nInstanceCount);
    this->SetName(m_Name);
    Worker();
  }
}

/*
  First instance of broker must be a listener, subsequent will be workers
*/
void Broker::Listener(void) {
  assert(m_pNetSubSystem != 0);
  m_pSocket = new CSocket(m_pNetSubSystem, IPPROTO_TCP);
  assert(m_pSocket != 0);

  if (m_pSocket->Bind(MQTT_PORT) < 0) {
    CLogger::Get()->Write(m_Name,
                          LogError,
                          "Cannot bind socket (port %u)",
                          MQTT_PORT);

    delete m_pSocket;
    m_pSocket = 0;

    return;
  }

  if (m_pSocket->Listen(MAX_CLIENTS) < 0) {
    CLogger::Get()->Write(m_Name, LogError, "Cannot listen on socket");

    delete m_pSocket;
    m_pSocket = 0;

    return;
  }

  while (1) {
    CIPAddress ForeignIP;
    u16 nForeignPort;
    CSocket *pConnection = m_pSocket->Accept(&ForeignIP, &nForeignPort);
    if (pConnection == 0) {
      CLogger::Get()->Write(m_Name, LogWarning, "Cannot accept connection");
      continue;
    }

    CString IPString;
    ForeignIP.Format (&IPString);
    CLogger::Get()->Write(m_Name,
                          LogNotice,
                          "Incoming connection from %s",
                          (const char *) IPString);

    if (s_nInstanceCount >= MAX_CLIENTS+1) {
      CLogger::Get ()->Write (m_Name, LogWarning, "Too many clients");
      delete pConnection;
      continue;
    }

    /*
      Create a broker instance which will now be a worker and not a listener
      anymore. The worker instance requires all the constructor parameters.
    */
    new Broker(m_pMutex, m_pTopicTree, m_pNetSubSystem, pConnection, &ForeignIP);
  }
}

// Doing the hard work
void Broker::Worker(void) {
  assert(m_pSocket != 0);

  m_Connected = TRUE;

  u8 Buffer[FRAME_BUFFER_SIZE];
  int nBytesReceived;

  do {
    nBytesReceived = m_pSocket->Receive(Buffer, sizeof Buffer, 0);
    if (nBytesReceived > 0) {
      // U8String buffer = { Buffer, static_cast<unsigned int>(nBytesReceived) };
      U8String buffer(Buffer, static_cast<unsigned int>(nBytesReceived));

      // CLogger::Get()->Write(m_Name, LogNotice, "Raw printing:");
      // for (int c = 0; c <= nBytesReceived; c++) {
      //   CLogger::Get()->Write(m_Name, LogNotice, "[%u]/[%d] %u", c, nBytesReceived, Buffer[c]);
      // }
      ProcessPacket(buffer);
    }
  } while (nBytesReceived > 0);

  // Unsubscribe the client from any existing subscriptions
  if (m_Connected) Disconnect();

  CString IPString;
  m_Client.IPAddress.Format(&IPString);
  CLogger::Get()->Write(m_Name, LogNotice, "Closing connection with %s", (const char *) IPString);

  delete m_pSocket;    // closes connection
  m_pSocket = 0;
}

void Broker::ProcessPacket(const U8String buffer) {
  const TMQTTPacketType requestType = GetRequestPacketType(buffer.GetString());
  /*
    This if statement can be removed since it is rendundant with the default
    case in the following switch.
  */
  if (requestType == MQTTPacketTypeUnknown) {
    CLogger::Get()->Write(m_Name, LogError, "Packet was not recognized");
    return;
  }

  if (requestType == MQTTDisconnect) {
    CLogger::Get()->Write(m_Name, LogNotice, "Recieved a DISCONNECT");
    m_Connected = FALSE;
    Disconnect();
    return;
  }

  u32 remainingLength = 0;
  // Variable header first byte = Control Packet Type (1 byte) + Remaining Length (up to 4 bytes)
  const unsigned int VHIndex = 1 + DecodeRemainingLength(&remainingLength, buffer.GetString());

  switch (requestType) {
    case MQTTConnect: {
      // 0 = ok, 1 = version protocol not supported, 5 = not ok
      u8 returnCode = 0;
      ConnectFlags flags = { FALSE, FALSE, FALSE, FALSE, TRUE, 0, 4};
      CLogger::Get()->Write(m_Name, LogNotice, "Received a CONNECT");

      // validate packet
      const int keepAlive = ValidateConnectPacket(buffer.GetString(), &flags);
      if (keepAlive == -1) {
        CLogger::Get()->Write(m_Name, LogError, "The connection cannot be accepted");
        returnCode = 5;
      }
      // will messages and persistent sessions are currently not supported
      if (flags.willFlag || !flags.cleanSession) {
        CLogger::Get()->Write(m_Name, LogError, "Some of the requested features are currently not supported");
        returnCode = 5;
      }

      // CLogger::Get()->Write(m_Name,
      //                       LogNotice,
      //                       "Username %u, password %u, willRetain %u, willQoS %u, willFlag %u, cleanSession %u",
      //                       flags.username, flags.password, flags.willRetain, flags.willQoS, flags.willFlag, flags.cleanSession);

      // extract payload
      U8String payload(buffer, remainingLength-10, 12);

      // extract and validate client id
      const unsigned int clientIDBytes = static_cast<unsigned int>(DecodeUTFNumber(payload.GetString()[0], payload.GetString()[1]));
      if (clientIDBytes == 0) {
        CLogger::Get()->Write(m_Name, LogError, "Client ID is not valid");
        returnCode = 5;
      }
      U8String clientID(payload, clientIDBytes, 2);

      // Save client
      m_Client.ID = clientID;
      m_Client.ID.Print(m_Name, "Client id:");

      if (flags.protocolVersion != 4) {
        CLogger::Get()->Write(m_Name, LogWarning, "The protocol version requested is not supported. The client will be notified");
        returnCode = 1;
      }

      /*
        Here we simplify the return code implementation by sending only 3 types
        of codes:
        0 = connection accepted
        1 = protocol version not supported
        5 = connection not accepted for any of the other reasons:
            - request of username/password authentication
            - request of will messages
            - request of session restore
            - malformed packet
      */
      // dummy variable
      const boolean sessionPresent = FALSE;
      SendConnAck(sessionPresent, returnCode);
      CLogger::Get()->Write(m_Name, LogNotice, "CONNACK sent");

      break;
    }

    case MQTTPublish: {
      CLogger::Get()->Write(m_Name, LogNotice, "Received a PUBLISH");
      /*
        The PUBLISH packet must be processed in a different way because it's the
        only packet that has flags on the 4 least significant bits of the MQTT
        Control Packet type byte, that is the first byte of the packet.
      */
      PublishFlags flags = { FALSE, FALSE, 0 };
      if (ExtractPublishFlags(buffer.GetString()[0] & 0b00001111, &flags) != 0) {
        CLogger::Get()->Write(m_Name, LogError, "There packet cannot be processed");
        return;
      }

      if (flags.qos > 0) {
        CLogger::Get()->Write(m_Name, LogWarning, "QoS > 0 is currently not supported");
      }
      if (flags.retain) {
        CLogger::Get()->Write(m_Name, LogWarning, "Retain mechanism is currently not supported");
      }

      // variable header length = topic length (encoded in the first 2 bytes of the vh) + packet id (if qos > 0)
      unsigned int filterLength = DecodeUTFNumber(buffer.GetString()[VHIndex], buffer.GetString()[VHIndex+1]);
      // CLogger::Get()->Write(m_Name, LogNotice, "Filter length: %u", filterLength);

      U8String filter(buffer, filterLength, VHIndex + 2);

      // variable header length = filter name length encoded as UTF8 (2 bytes) + filter name (+ 2 if QoS > 0)
      unsigned int VHLength = 2 + filterLength + (flags.qos > 0 ? 2 : 0);
      // CLogger::Get()->Write(m_Name, LogNotice, "Variable header length: %u", VHLength);

      // currently useless
      if (flags.qos > 0) {
        U8String packetID(buffer, 2, VHIndex + VHLength - 2);
      }

      const Topic *topic = ExtractPublishTopic(filter);
      // topic->name.Print(m_Name, "Publish to topic:");
      if (topic->subscriptions == 0) {
        CLogger::Get()->Write(m_Name, LogWarning, "No subscribers to this topic: the message will be dropped");
        m_pTopicTree->Clean();
        return;
      }

      // CLogger::Get()->Write(m_Name, LogNotice, "Payload length: %u", remainingLength - VHLength);

      U8String payload(buffer, remainingLength - VHLength, VHIndex + VHLength);
      payload.Print(m_Name, "Message to publish:");

      PublishMessage(topic, payload);

      /*
        Since Qos > 0 is not currently supported (hence we process all publish
        packets as having QoS = 0) no ack packet will be sent back to the client
      */

      break;
    }

    case MQTTSubscribe: {
      CLogger::Get()->Write(m_Name, LogNotice, "Received a SUBSCRIBE");

      // get the packet id from the variable header
      U8String packetID(buffer, 2, VHIndex);
      // CLogger::Get()->Write(m_Name, LogNotice, "Packet ID: %u - %u", packetID.string[0], packetID.string[1]);

      // extract the payload
      U8String payload(buffer, remainingLength - 2, VHIndex + 2);

      // decode the topic filters
      Topic *topicFilters[MAX_TOPICS];
      unsigned int subscribeRequests = ExtractRequestedTopics(payload, topicFilters, TRUE);
      CLogger::Get()->Write(m_Name, LogNotice, "Topics to subscribe to (%u): ", subscribeRequests);
      for (unsigned int i = 0; i < subscribeRequests; i++) {
         topicFilters[i]->name.Print(m_Name);
      }

      u8 returnCodes[subscribeRequests];
      // CLogger::Get()->Write(m_Name, LogNotice, "Return codes:");
      for (unsigned int i = 0; i < subscribeRequests; i++) {
        returnCodes[i] = m_pTopicTree->SubscribeClient(topicFilters[i], &m_Client);
        // CLogger::Get()->Write(m_Name, LogNotice, "%u", returnCodes[i]);
      }

      SendSubAck(packetID, returnCodes, subscribeRequests);
      break;
    }

    case MQTTUnsubscribe: {
      CLogger::Get()->Write(m_Name, LogNotice, "Received a UNSUBSCRIBE");

      // get the packet id from the variable header
      U8String packetID(buffer, 2, VHIndex);
      // CLogger::Get()->Write(m_Name, LogNotice, "Packet ID: %u - %u", packetID.string[0], packetID.string[1]);

      // extract the payload
      U8String payload(buffer, remainingLength - 2, VHIndex - 2);

      // decode the topic filters
      Topic *topicFilters[MAX_TOPICS];
      unsigned int unsubscribeRequests = ExtractRequestedTopics(payload, topicFilters, FALSE);
      CLogger::Get()->Write(m_Name, LogNotice, "Topics to unsubscribe from (%u): ", unsubscribeRequests);
      for (unsigned int i = 0; i < unsubscribeRequests; i++) {
        // topicFilters[i]->name.Print(m_Name);
        m_pTopicTree->UnsubscribeClient(topicFilters[i], &m_Client);
      }

      SendUnsubAck(packetID);
      break;
    }

    case MQTTPingReq: {
      CLogger::Get()->Write(m_Name, LogNotice, "Received a PINGREQ, sending PINGRESP");
      // PrintString("Client:", m_ClientID);

      // sending PingResp
      const unsigned int responseLength = 2;
      const u8 response[responseLength] = {0b11010000, 0b00000000};
      if (m_Client.pSocket->Send(response, responseLength, MSG_DONTWAIT) != responseLength) {
        CLogger::Get()->Write(m_Name, LogWarning, "Cannot send response");
      }
      break;
    }

    default:
      CLogger::Get()->Write(m_Name, LogError, "Cannot process this type of packet");
      break;
  }
}

/*
  Returns -1 if invalid or the 'keep alive' parameter otherwise
*/
int Broker::ValidateConnectPacket(const u8 *buffer, ConnectFlags *flags) {
  if (buffer[2] == 0 &&
      buffer[3] == 4 &&
      buffer[4] == 'M' &&
      buffer[5] == 'Q' &&
      buffer[6] == 'T' &&
      buffer[7] == 'T') {
    /*
      buffer[9] is comprised of the following flags:
      | username | password | will retain | will QoS (msb) | will QoS (lsb) | will flag | clean session | reserved=0 |
      None of these features is currently supported.
    */
    flags->protocolVersion = buffer[8];
    // Username and password
    if (buffer[9] >> 7) {
      if ((buffer[9] & 0b01000000) != 0) {
        flags->username = TRUE;
        flags->password = TRUE;
      } else {
        CLogger::Get()->Write(m_Name, LogError, "Malformed username/password falgs");
        return -1;
      }
    }
    // Will message      uprqqwcR
    if (  (buffer[9] & 0b00000100) == 0) {
      if ((buffer[9] & 0b00111000) != 0) {
        CLogger::Get()->Write(m_Name, LogError, "Malformed will message falgs");
        return -1;
      }
      flags->willRetain = FALSE;
      flags->willQoS = 0;
      flags->willFlag = FALSE;
    } else {
      if (((buffer[9] & 0b00011000) >> 3) < 3)  flags->willQoS = (buffer[9] & 0b00011000) >> 3;
      if ((buffer[9] & 0b00100000) != 0)        flags->willRetain = TRUE;
    }
    // Clean session and reserved bit (evaluated together)
    const u8 lastBits = buffer[9] << 6;
    if      (lastBits == 0)   flags->cleanSession = FALSE;
    else if (lastBits == 128) flags->cleanSession = TRUE;
    else {
      CLogger::Get()->Write(m_Name, LogError, "Reserved bit set to 1");
      return -1;
    }

    // Keep alive
    return static_cast<int>(DecodeUTFNumber(buffer[10], buffer[11]));
  } else {
    CLogger::Get()->Write(m_Name, LogError, "Malformed variable header");
    return -1;
  }
}

TMQTTPacketType Broker::GetRequestPacketType(const u8 *buffer) {
  u8 controlPacketType = buffer[0], controlPacketFlags = buffer[0];
  controlPacketType >>= 4;          // the first 4 bits define the type
  controlPacketFlags &= 0b00001111; // the last 4 bits define the flags

  // CLogger::Get()->Write(m_Name, LogNotice, "first: %u, type: %u, flags: %u", buffer[0], controlPacketType, controlPacketFlags);

  if (controlPacketType == 0b0011) return MQTTPublish;

  TMQTTPacketType type;
  /*
    The following 2 switch cases are separated in order to evaluate togheter the
    packet types which should have the same flag patterns (either 0000 or 0010)
  */
  switch (controlPacketType) {
    case 0b0001:
      type = MQTTConnect;
      break;
    case 0b0010:
      type = MQTTConnAck;
      break;
    case 0b0100:
      type = MQTTPubAck;
      break;
    case 0b0101:
      type = MQTTPubRec;
      break;
    case 0b0111:
      type = MQTTPubComp;
      break;
    case 0b1001:
      type = MQTTSubAck;
      break;
    case 0b1011:
      type = MQTTUnsubAck;
      break;
    case 0b1100:
      type = MQTTPingReq;
      break;
    case 0b1101:
      type = MQTTPingResp;
      break;
    case 0b1110:
      type = MQTTDisconnect;
      break;
    default:
      /*
        In order to avoid checking for the correct flags pattern at each case we
        set the type temporarily to MQTTPacketTypeUnknown in order to skip the
        following if statement and continue checking for matches on the next
        switch/case pattern.
        While this method requires less code, it misses the possibility to log
        errors related to malformed flags patterns for any of the preceding
        cases. But since this can be considered a "debug" feature, it has been
        ignored.
      */
      type = MQTTPacketTypeUnknown;
      break;
  }
  if (controlPacketFlags == 0b0000 && type != MQTTPacketTypeUnknown) {
    // CLogger::Get()->Write(m_Name, LogNotice, "Returning type %d", type);
    return type;
  }

  switch (controlPacketType) {
    case 0b0110:
      type = MQTTPubRel;
      break;
    case 0b1000:
      type = MQTTSubscribe;
      break;
    case 0b1010:
      type = MQTTUnsubscribe;
      break;
    /*
      The "reserved" patterns are included in the default case.
    */
    default:
      CLogger::Get()->Write(m_Name, LogError, "Incorrect Type");
      return MQTTPacketTypeUnknown;
      break;
  }
  if (controlPacketFlags == 0b0010) {
    // CLogger::Get()->Write(m_Name, LogNotice, "Returning type %d", type);
    return type;
  } else {
    CLogger::Get()->Write(m_Name, LogError, "Malformed Flags");
    return MQTTPacketTypeUnknown;
  }
}

void Broker::SendConnAck(const boolean sessionPresent, const u8 returnCode) {
  const u8 response[4] = {0b00100000,                               // control packet type
                          0b00000010,                               // remaining length
                          static_cast<u8>(sessionPresent ? 1 : 0),  // last bit defines if there's a saved session for that client
                          returnCode};                              // return code (0-5)
  // for (unsigned int i = 0; i < 4; i++) {
  //   CLogger::Get()->Write(m_Name, LogNotice, "ConnAck byte %u: %u", i, response[i]);
  // }
  if (m_Client.pSocket->Send(response, 4, MSG_DONTWAIT) != 4) {
    CLogger::Get()->Write(m_Name, LogWarning, "Cannot send response");
  }
}

void Broker::Disconnect(void) {
  m_Client.ID.Print(m_Name, "Unsubscribing client:");
  CLogger::Get()->Write(m_Name, LogNotice, "from topic:");
  // for every topic
  for (unsigned int i = 0; i < m_pTopicTree->GetTopicsNumber(); i++) {
    Topic *t = m_pTopicTree->GetTopic(i);
    // unsubscribe the client (if it has any subscription)
    m_pTopicTree->UnsubscribeClient(t, &m_Client);
  }
}

int Broker::ExtractPublishFlags(u8 byte, PublishFlags *flags) {
  flags->dup    = ((byte & 0b00001000) >> 3) == 1;
  flags->retain = ((byte & 0b00000001) == 1);
  flags->qos    = static_cast<u8>((byte & 0b00000110) >> 1);
  if (flags->qos > 2 || (flags->qos > 0 && flags->dup)) {
    CLogger::Get()->Write(m_Name, LogError, "Malformed publish flags");
    return 1;
  }
  return 0;
}

Topic *Broker::ExtractPublishTopic(const U8String filter) {
  U8String topicNodes[64];
  unsigned int nodesNumber = 0;
  DecodeTopicPath(topicNodes, &nodesNumber, filter);
  // CLogger::Get()->Write(m_Name, LogNotice, "Topics number: %u", nodesNumber);
  // for (unsigned int i = 0; i < nodesNumber; i++) {
  //   topicNodes[i].Print(m_Name);
  // }

  /*
    Here we don't really want to create a new topic to publish to because that
    would mean that the message would have no recipient. But for the consistency
    and simplicity of the code this approach was nevertheless used.
    The unused topics will be cleaned afterwards.
  */
  return m_pTopicTree->Update(topicNodes, nodesNumber);
}

void Broker::PublishMessage(const Topic *topic, const U8String message) {
  /*
    Set flags to FALSE and QoS to 0 because these features currently are not
    supported.
  */
  PublishFlags flags = { FALSE, FALSE, 0 };
  /*
    The fixed header size is 1 byte + the remaining length size (up to 4 bytes).
    The maximum size of the topic name is 2^16 - 1 = 65535 (+ 2 bytes for the
    lenght). There could also be 2 bytes for the client ID but we do not
    currently support this feature. Furthermore we should add the size of the
    payload which can be as big as 2^(4*8).
    This would require 4GB of memory so we simply use an arbitrary size.
  */
  u8 packet[MAX_PACKET_SIZE];
  unsigned int packetLength = PackMessage(packet, flags, topic, message);

  // CLogger::Get()->Write(m_Name, LogNotice, "Packet to send");
  // for (unsigned int i = 0; i < packetLength; i++) {
  //   CLogger::Get()->Write(m_Name, LogNotice, "%u | %c", packet[i]);
  // }

  // CLogger::Get()->Write(m_Name, LogNotice, "Subscribers to this topic: %u", topic->subscriptions);
  for (unsigned int i = 0; i < topic->subscriptions; i++) {
    if (topic->pClients[i]->pSocket->Send(packet, packetLength, MSG_DONTWAIT) != static_cast<int>(packetLength)) {
      topic->pClients[i]->ID.Print(m_Name, "Cannot publish to:");
    }
    topic->pClients[i]->ID.Print(m_Name, "Published to:");
  }
  return;
}

unsigned int Broker::PackMessage(u8 packet[], const PublishFlags flags, const Topic *topic, const U8String message) {
  u8 topicName[64];
  unsigned int topicNameLength = m_pTopicTree->ComposeTopicName(topicName, topic);
  // U8String tn(topicName, topicNameLength);
  // tn.Print(m_Name, "Topic name:");

  u8 topicLengthBytes[2];
  EncodeUTFNumber(topicLengthBytes, topicNameLength);
  // CLogger::Get()->Write(m_Name, LogNotice, "topicLengthBytes(%u): MSB=%u LSB=%u", topicNameLength, topicLengthBytes[0], topicLengthBytes[1]);

  u8 packetID[2];
  if (flags.qos > 0) {
    packetID[0] = 0;
    packetID[1] = 1;
  }

  unsigned int length = 2 + topicNameLength + (flags.qos > 0 ? 2 : 0) + message.GetLength();
  // u8 *remainingLength = (u8 *)malloc(sizeof(u8) * 0);
  u8 remainingLength[4];
  unsigned int remainingLengthBytes = EncodeRemainingLength(remainingLength, length);
  // CLogger::Get()->Write(m_Name, LogNotice, "Remaining length(%u):", length);
  // for (unsigned int i = 0; i < remainingLengthBytes; i++) {
  //   CLogger::Get()->Write(m_Name, LogNotice, "%u", remainingLength[i]);
  // }

  packet[0] = 0b00110000 + (flags.dup ? 8 : 0 ) + (flags.qos * 2) + (flags.retain ? 0 : 1);
  memcpy(&packet[1], remainingLength, remainingLengthBytes);
  memcpy(&packet[1+remainingLengthBytes], topicLengthBytes, 2);
  memcpy(&packet[1+remainingLengthBytes+2], topicName, topicNameLength);

  if (flags.qos > 0) {
    memcpy(&packet[1+remainingLengthBytes+2+topicNameLength], packetID, 2);
    memcpy(&packet[1+remainingLengthBytes+2+topicNameLength+2], message.GetString(), message.GetLength());
    return 1+remainingLengthBytes+2+topicNameLength+2+message.GetLength();
  }
  memcpy(&packet[1+remainingLengthBytes+2+topicNameLength], message.GetString(), message.GetLength());
  return 1+remainingLengthBytes+2+topicNameLength+message.GetLength();
}

unsigned int Broker::ExtractRequestedTopics(const U8String payload, Topic *packetTopics[], const boolean qos) {
  unsigned int currentByte = 0, requests = 0;
  do {
    // Decode current topic filter length
    const unsigned int filterLength = static_cast<unsigned int>(
      DecodeUTFNumber(payload.GetString()[currentByte], payload.GetString()[currentByte+1]));
    currentByte += 2;
    // CLogger::Get()->Write(m_Name, LogNotice, "filterLength: %u", filterLength);

    // Extract current topic filter
    U8String topicFilter(payload, filterLength, currentByte);
    // topicFilter.Print(m_Name, "Topic filter:");
    currentByte += filterLength;

    /*
      If this function gets called for a subscribe operation we have to check
      the QoS byte after each topic filter.
    */
    if (qos) {
      // Extract the requested QoS
      const u8 requestedQoS = payload.GetString()[currentByte];
      if (requestedQoS > 2) {
        CLogger::Get()->Write(m_Name, LogWarning, "Malformed Requested QoS field");
      }
      if (requestedQoS > 0) {
        CLogger::Get()->Write(m_Name, LogWarning, "The broker currently supports only QoS=0");
      }
      // CLogger::Get()->Write(m_Name, LogNotice, "Requested QoS: %u", requestedQoS);
      currentByte++;
    }

    // Extract the topic "path"
    unsigned int nodesNumber = 0;
    U8String topicNodes[64];
    DecodeTopicPath(topicNodes, &nodesNumber, topicFilter);

    // CLogger::Get()->Write(m_Name, LogNotice, "Topics number: %u", nodesNumber);
    // for (unsigned int i = 0; i < nodesNumber; i++) {
    //   topicNodes[i].Print(m_Name);
    // }

    // Add new nodes to the topic tree if needed
    packetTopics[requests] = m_pTopicTree->Update(topicNodes, nodesNumber);
    requests++;
  } while(currentByte < payload.GetLength());
  return requests;
}

void Broker::SendSubAck(const U8String packetID, const u8 *returnCodes, const unsigned int requests) {
  // Get the remaining length bytes
  u8 encodedBytes[4];
  int remainingLengthBytes = static_cast<int>(EncodeRemainingLength(encodedBytes, 2 + requests));

  // pack the response
  // response = packet type (1 byte) + remaining length (up to 4 bytes) + packet id (2 bytes) + return codes
  const int responseLength = 1 + remainingLengthBytes + 2 + requests;
  u8 response[responseLength];
  response[0] = 0b10010000;
  for (int i = 0; i < remainingLengthBytes; i++) {
    response[i+1] = encodedBytes[i];
  }
  response[remainingLengthBytes+1] = packetID.GetString()[0];
  response[remainingLengthBytes+2] = packetID.GetString()[1];
  for (unsigned int i = 0; i < requests; i++) {
    response[remainingLengthBytes+3] = returnCodes[i];
  }

  // CLogger::Get()->Write(m_Name, LogNotice, "SendAck bytes:");
  // for (int i = 0; i < responseLength; i++) {
  //   CLogger::Get()->Write(m_Name, LogNotice, "byte %u: %u", i, response[i]);
  // }

  if (m_Client.pSocket->Send(response, responseLength, MSG_DONTWAIT) != responseLength) {
    CLogger::Get()->Write(m_Name, LogWarning, "Cannot send response");
  }
  CLogger::Get()->Write(m_Name, LogNotice, "SUBACK sent");
}

void Broker::SendUnsubAck(const U8String packetID) {
  u8 response[4] = { 0b10110000, 0b00000010, packetID.GetString()[0], packetID.GetString()[1] };
  if (m_Client.pSocket->Send(response, 4, MSG_DONTWAIT) != 4) {
    CLogger::Get()->Write(m_Name, LogWarning, "Cannot send response");
  }
  CLogger::Get()->Write(m_Name, LogNotice, "UNSUBACK sent");
}

void Broker::DecodeTopicPath(U8String nodes[], unsigned int *nodesNumber, const U8String filter) {
  u8 name[32];
  unsigned int nameLength = 0;
  // U8String *topics = (U8String *)malloc(32);

  for (unsigned int i = 0; i < filter.GetLength(); i++) {
    if (filter.GetString()[i] == '/') {
      nodes[*nodesNumber] = U8String(name, nameLength);
      // Increment the number of topics
      (*nodesNumber)++;

      nameLength = 0;

    } else {
      // CLogger::Get()->Write(m_Name, LogNotice, "char: %c |UTF| %u", filter[i], filter[i]);
      name[nameLength] = filter.GetString()[i];
      nameLength++;
    }
  }
  nodes[*nodesNumber] = U8String(name, nameLength);
  // Increment the number of topics
  (*nodesNumber)++;
}

/*
  Adapted from the MQTT specification
*/
unsigned int Broker::EncodeRemainingLength(u8 *encodedByte, unsigned int length) {
  unsigned int currentByte = 0;
  do {
    // encodedByte = (u8 *)realloc(encodedByte, sizeof(u8) * (currentByte+1));
    encodedByte[currentByte] = length % 128;
    length = length / 128;

    // if there are more data to encode, set the top bit of this byte
    if (length > 0) encodedByte[currentByte] |= 128;

    currentByte++;
  } while (length > 0);
  return currentByte;
}

/*
  Adapted from the MQTT specification
*/
int Broker::DecodeRemainingLength(u32 *remainingLength, const u8 *buffer) {
  u8 byte = 1, encodedByte;
  int multiplier = 1, index = 0;
  do {
    encodedByte = buffer[byte];
    *remainingLength += (encodedByte & 127) * multiplier;
    multiplier *= 128;
    if (multiplier > 128*128*128) {
      CLogger::Get()->Write(m_Name, LogError, "Malformed Remaining Length");
      return 0;
    }
    index++;
  } while ((encodedByte & 128) != 0);
  return index;
}

void Broker::EncodeUTFNumber(u8 encoded[], const unsigned int number) {
  if (number > 65535) {
    CLogger::Get()->Write(m_Name, LogError, "The number is to big to be encoded");
    return;
  }
  encoded[0] = static_cast<u8>(number >> 8);
  encoded[1] = static_cast<u8>(number & 255);
}

u16 Broker::DecodeUTFNumber(const u8 msb, const u8 lsb) {
  // CLogger::Get()->Write(m_Name, LogNotice, "MSB: %u, LSB: %u", msb, lsb);
  return static_cast<u16>(msb) * 256 + static_cast<u16>(lsb);
}

