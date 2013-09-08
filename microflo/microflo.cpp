#include "microflo.h"

Packet::Packet()
    : buf('0')
    , boolean(false)
    , msg(MsgInvalid)
{}

Packet::Packet(char c)
    : buf(c)
    , boolean(false)
    , msg(MsgCharacter)
{}

Packet::Packet(bool b)
    : buf('0')
    , boolean(b)
    , msg(MsgBoolean)
{}

Packet::Packet(Msg _msg)
    : buf('0')
    , boolean(false)
    , msg(_msg)
{}

GraphStreamer::GraphStreamer()
    : network(0)
    , currentByte(0)
    , state(ParseHeader)
{}

void GraphStreamer::parseByte(char b) {

    buffer[currentByte++] = b;
    // printf("%s: state=%d, currentByte=%d, input=%d\n", __PRETTY_FUNCTION__, state, currentByte, b);

    if (state == ParseHeader) {
        if (currentByte == GRAPH_MAGIC_SIZE) {
            // FIXME: duplication of magic definition
            if (buffer[0] == 'u' && buffer[1] == 'C' && buffer[2] == '/', buffer[3] == 'F', buffer[4] == 'l', buffer[5] == 'o') {
                state = ParseCmd;
            } else {
                state = Invalid;
            }
            currentByte = 0;
        }
    } else if (state == ParseCmd) {
        if (currentByte == GRAPH_CMD_SIZE) {
            GraphCmd cmd = (GraphCmd)buffer[0];
            if (cmd >= GraphCmdInvalid) {
                state = Invalid; // XXX: or maybe just ignore?
            } else {
                if (cmd == GraphCmdReset) {
                    // TODO: implement
                } else if (cmd == GraphCmdCreateComponent) {
                    ComponentId id = (ComponentId)buffer[1];
                    // FIXME: validate
                    network->addNode(Component::create(id));
                } else if (cmd == GraphCmdConnectNodes) {
                    // FIXME: validate
                    int src = (unsigned int)buffer[1];
                    int target = (unsigned int)buffer[2];
                    network->connectTo(src, target);
                }
            }
            currentByte = 0;
        }

    } else if (state == Invalid) {
        currentByte = 0; // avoid overflow
    } else {

    }
}

void Component::send(Packet out) {
    network->sendMessage(connection.target, out, this);
}


#ifdef ARDUINO
void Debugger::setup(Network *network) {
    Serial.begin(9600);
    network->setNotifications(&Debugger::printSend, &Debugger::printDeliver);
}

// FIXME: print async, currently output gets truncated on networks with > 3 edges
void Debugger::printPacket(Packet *p) {
    Serial.print("Packet(");
    Serial.print("type=");
    Serial.print(p->msg);
    Serial.print(",");
    Serial.print(")");
}

void Debugger::printSend(int index, Message m, Component *sender) {
    Serial.print("SEND: ");
    Serial.print("index=");
    Serial.print(index);
    Serial.print(",");
    Serial.print("from=");
    Serial.print((unsigned long)sender);
    Serial.print(",");
    Serial.print("to=");
    Serial.print((unsigned long)m.target);
    Serial.print(" ");
    printPacket(&m.pkg);
    Serial.println();
}

void Debugger::printDeliver(int index, Message m) {
    Serial.print("DELIVER: ");
    Serial.print("index=");
    Serial.print(index);
    Serial.print(",");
    Serial.print("to=");
    Serial.print((unsigned long)m.target);
    Serial.print(" ");
    printPacket(&m.pkg);
    Serial.println();
}
#endif

Network::Network()
    : lastAddedNodeIndex(0)
    , messageWriteIndex(0)
    , messageReadIndex(0)
    , messageSentNotify(0)
    , messageDeliveredNotify(0)
{
    for (int i=0; i<MAX_NODES; i++) {
        nodes[i] = 0;
    }
}

void Network::setNotifications(MessageSendNotification send, MessageDeliveryNotification deliver) {
    messageSentNotify = send;
    messageDeliveredNotify = deliver;
}

void Network::deliverMessages(int firstIndex, int lastIndex) {
        if (firstIndex > lastIndex || lastIndex > MAX_MESSAGES-1 || firstIndex < 0) {
            return;
        }

        for (int i=firstIndex; i<=lastIndex; i++) {
            messages[i].target->process(messages[i].pkg);
            if (messageDeliveredNotify) {
                messageDeliveredNotify(i, messages[i]);
            }
        }
}

void Network::processMessages() {
    // Messages may be emitted during delivery, so copy the range we intend to deliver
    const int readIndex = messageReadIndex;
    const int writeIndex = messageWriteIndex;
    if (readIndex > writeIndex) {
        deliverMessages(readIndex, MAX_MESSAGES-1);
        deliverMessages(0, writeIndex);
    } else if (readIndex < writeIndex) {
        deliverMessages(readIndex, writeIndex);
    } else {
        // no messages
    }
    messageReadIndex = writeIndex;
}

void Network::sendMessage(Component *target, Packet &pkg, Component *sender) {

    if (messageWriteIndex > MAX_MESSAGES-1) {
        messageWriteIndex = 0;
    }
    Message &msg = messages[messageWriteIndex++];
    msg.target = target;
    msg.pkg = pkg;
    if (messageSentNotify) {
        messageSentNotify(messageWriteIndex-1, msg, sender);
    }
}

void Network::runSetup() {
    for (int i=0; i<MAX_NODES; i++) {
        if (nodes[i]) {
            nodes[i]->process(Packet(MsgSetup));
        }
    }
}

void Network::runTick() {

    // TODO: consider the balance between scheduling and messaging (bounded-buffer problem)

    // Deliver messages
    processMessages();

    // Schedule
    for (int i=0; i<MAX_NODES; i++) {
        Component *t = nodes[i];
        if (t) {
            t->process(Packet(MsgTick));
        }
    }
}

void Network::connectTo(int srcId, int targetId) {
    if (srcId < 0 || srcId > lastAddedNodeIndex ||
        targetId < 0 || targetId > lastAddedNodeIndex) {
        return;
    }

    connectTo(nodes[srcId], nodes[targetId]);
}

void Network::connectTo(Component *src, Component *target) {
    src->connectTo(target);
}

int Network::addNode(Component *node) {
    nodes[lastAddedNodeIndex++] = node;
    node->setNetwork(this);
    return lastAddedNodeIndex;
}
