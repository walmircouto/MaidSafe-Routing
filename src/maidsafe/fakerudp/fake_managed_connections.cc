/*******************************************************************************
 *  Copyright 2012 maidsafe.net limited                                        *
 *                                                                             *
 *  The following source code is property of maidsafe.net limited and is not   *
 *  meant for external use.  The use of this code is governed by the licence   *
 *  file licence.txt found in the root of this directory and also on           *
 *  www.maidsafe.net.                                                          *
 *                                                                             *
 *  You are not free to copy, amend or otherwise use this source code without  *
 *  the explicit written permission of the board of directors of maidsafe.net. *
 ******************************************************************************/

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

#include "boost/asio/ip/address.hpp"
#include "boost/asio/ip/udp.hpp"
#include "boost/date_time/posix_time/posix_time_duration.hpp"
#include "boost/signals2/connection.hpp"

#include "maidsafe/rudp/managed_connections.h"
#include "maidsafe/rudp/return_codes.h"
#include "maidsafe/fakerudp/fake_network.h"
#include "maidsafe/common/asio_service.h"
#include "maidsafe/common/utils.h"
#include "maidsafe/common/log.h"

namespace maidsafe {

namespace rudp {

typedef boost::asio::ip::udp::endpoint Endpoint;

ManagedConnections::ManagedConnections()
    : asio_service_(2),
      message_received_functor_(),
      connection_lost_functor_(),
      private_key_(),
      public_key_(),
      transports_(),
      connection_map_(),
      shared_mutex_(),
      local_ip_(),
      resilience_transport_(),
      fake_endpoints_() {
  Node node;
  fake_endpoints_.push_back(node.endpoint);
  FakeNetwork::instance().AddEmptyNode(node);
  asio_service_.Start();
}

ManagedConnections::~ManagedConnections() {
  LOG(kVerbose) << " ManagedConnections::~ManagedConnections() ";
  asio_service_.Stop();
  if (!FakeNetwork::instance().RemoveMyNode(fake_endpoints_[0])) {
    LOG(kVerbose) << "Failed to remove my node in destructor.";
  }
  LOG(kVerbose) << " ManagedConnections::~ManagedConnections() exiting...";
}

Endpoint ManagedConnections::Bootstrap(const std::vector<Endpoint> &bootstrap_endpoints,
                                       MessageReceivedFunctor message_received_functor,
                                       ConnectionLostFunctor connection_lost_functor,
                                       std::shared_ptr<asymm::PrivateKey> private_key,
                                       std::shared_ptr<asymm::PublicKey> public_key,
                                       Endpoint local_endpoint) {
  LOG(kVerbose) << "In Bootstrap";

  if (!message_received_functor) {
    LOG(kError) << "You must provide a valid MessageReceivedFunctor.";
    return Endpoint();
  }
  message_received_functor_ = message_received_functor;
  if (!connection_lost_functor) {
    LOG(kError) << "You must provide a valid ConnectionLostFunctor.";
    return Endpoint();
  }
  connection_lost_functor_ = connection_lost_functor;

  if (bootstrap_endpoints.empty()) {
    LOG(kError) << "You must provide at least one Bootstrap endpoint.";
    return Endpoint();
  }

  if (!private_key || !asymm::ValidateKey(*private_key) ||
      !public_key || !asymm::ValidateKey(*public_key)) {
    LOG(kError) << "You must provide a valid private and public key.";
    return Endpoint();
  }

  auto mynode = FakeNetwork::instance().FindNode(fake_endpoints_[0]);  // me
  assert(mynode != FakeNetwork::instance().GetEndIterator() && "Apparently not in network.");

  Node &node = (*mynode);
  if (!local_endpoint.address().is_unspecified()) {
    node.endpoint = local_endpoint;
    fake_endpoints_[0] = local_endpoint;
  }

  if (connection_lost_functor) {
    node.connection_lost = [&](const Endpoint &peer_endpoint) {
                               OnConnectionLostSlot(peer_endpoint,
                                                    std::shared_ptr<detail::Transport>(),
                                                    bool(), bool());
                             };
  }
  if (message_received_functor) {
    node.message_received = [&](const std::string &message) {
                                OnMessageSlot(message);
                              };
  }

  for (auto i : bootstrap_endpoints) {
    for (int j = 0; j < 200; ++j) {
      Sleep(boost::posix_time::milliseconds(10));
      if (local_endpoint.address().is_unspecified() &&
          (FakeNetwork::instance().FindNode(i) != FakeNetwork::instance().GetEndIterator())) {
        LOG(kVerbose) << "Found viable bootstrap node.\n";
        if (FakeNetwork::instance().BootStrap(node, i)) {
          LOG(kVerbose) << "Bootstrap sucessfull!!\n";
          FakeNetwork::instance().AddConnection(node.endpoint, i, true);
          return i;
        }
      } else {
        FakeNetwork::instance().AddConnection(node.endpoint, i, true);
        return i;
      }
    }
  }
  return Endpoint();
}

int ManagedConnections::GetAvailableEndpoint(const Endpoint& /*peer_endpoint*/,
                                             EndpointPair &this_endpoint_pair) {
  assert((fake_endpoints_.size() != 0) && "I do not know my own endpoint");
  this_endpoint_pair.external = fake_endpoints_[0];
  this_endpoint_pair.local = fake_endpoints_[0];
  LOG(kInfo) << " endpoint ip address "
             << this_endpoint_pair.external.address().to_string() << "\n";
  return kSuccess;
}

int ManagedConnections::Add(const Endpoint &this_endpoint,
                            const Endpoint &peer_endpoint,
                            const std::string &validation_data) {
  int add_result(FakeNetwork::instance().AddConnection(this_endpoint, peer_endpoint));
  asio_service_.service().post([=]() {
    if (!validation_data.empty() && add_result == kSuccess)
      FakeNetwork::instance().SendMessageToNode(peer_endpoint, validation_data);
  });
  return add_result;
}

void ManagedConnections::Send(const Endpoint &peer_endpoint,
                              const std::string &message,
                              MessageSentFunctor message_sent_functor) {
  asio_service_.service().post([=]() {
    bool message_sent(FakeNetwork::instance().SendMessageToNode(peer_endpoint, message));
    if (message_sent_functor)
      message_sent_functor(message_sent? kSuccess: kInvalidConnection);
  });
}

void ManagedConnections::Remove(const Endpoint &peer_endpoint) {
  asio_service_.service().post([=]() {
    if (!FakeNetwork::instance().RemoveConnection(fake_endpoints_[0], peer_endpoint))
      LOG(kVerbose) << "Failed to remove " << peer_endpoint;
  });
}

void ManagedConnections::OnMessageSlot(const std::string& message) {
  asio_service_.service().post([=]() {
    if (message_received_functor_)
      message_received_functor_(message);
  });
}

void ManagedConnections::OnConnectionLostSlot(const Endpoint& peer_endpoint,
                            std::shared_ptr<detail::Transport> /*transport*/,
                            bool /*connections_empty*/,
                            bool /*temporary_connection*/) {
  asio_service_.service().post([=]() {
    if (connection_lost_functor_)
      connection_lost_functor_(peer_endpoint);
    });
}

ManagedConnections::TransportAndSignalConnections::TransportAndSignalConnections()
    : transport(),
      on_message_connection(),
      on_connection_added_connection(),
      on_connection_lost_connection() {}
}  // namespace rudp

}  // namespace maidsafe
