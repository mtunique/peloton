//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// protocol_handler.h
//
// Identification: src/include/network/protocol_handler.h
//
// Copyright (c) 2015-17, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "network/protocol_handler.h"

#include <boost/algorithm/string.hpp>

namespace peloton {
namespace network {

  ProtocolHandler::ProtocolHandler(tcop::TrafficCop *traffic_cop) {
    this->traffic_cop_ = traffic_cop;
  }

  ProtocolHandler::~ProtocolHandler() {}


  /* Manage the startup packet */
  //  bool ManageStartupPacket();
  void ProtocolHandler::SendInitialResponse() {}

  bool ProtocolHandler::ProcessInitialPacket(UNUSED_ATTRIBUTE InputPacket* pkt,
                            UNUSED_ATTRIBUTE Client client,
                            UNUSED_ATTRIBUTE bool ssl_able,
                            UNUSED_ATTRIBUTE bool& ssl_sent,
                            UNUSED_ATTRIBUTE bool& finish_startup_packet) {
    return true;
  }

  ProcessResult ProtocolHandler::Process(
      UNUSED_ATTRIBUTE Buffer& rbuf,
      UNUSED_ATTRIBUTE const size_t thread_id) {
    return ProcessResult::TERMINATE;
  }

  void ProtocolHandler::Reset() {
    SetFlushFlag(false);
    responses.clear();
    request.Reset();
  }
  
  void ProtocolHandler::GetResult() {}
}  // namespace network
}  // namespace peloton

