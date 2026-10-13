#ifndef UNKNOWNPARSER_H
#define UNKNOWNPARSER_H

#include <string>
#include <vector>
#include "eventpoller.h"

struct ParseResult {
    bool handled;
    QString contactName;
    QString senderName;
    QString messageText;
    std::string envelopeFrom;              // 信封 Value.from：解码+base58 的 libp2p PeerId（12D3KooW…），逐 peer 稳定
    std::vector<ContactData> contacts;
    std::vector<PeerInfo> peers;
    std::vector<HistoryMessage> messages;
};

class UnknownParser {
public:
    static ParseResult parse(const std::string& eventType, const std::string& jsonData);
};

#endif
