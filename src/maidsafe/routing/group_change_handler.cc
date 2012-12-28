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

#include "maidsafe/routing/group_change_handler.h"

#include <string>
#include <vector>
#include <algorithm>

#include "maidsafe/common/log.h"
#include "maidsafe/common/node_id.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/routing/message_handler.h"
#include "maidsafe/routing/routing_pb.h"
#include "maidsafe/routing/rpcs.h"
#include "maidsafe/routing/utils.h"


namespace maidsafe {

namespace routing {

GroupChangeHandler::GroupChangeHandler(RoutingTable& routing_table, NetworkUtils& network)
  : mutex_(),
    routing_table_(routing_table),
    network_(network),
    update_subscribers_() {}

GroupChangeHandler::~GroupChangeHandler() {
  std::lock_guard<std::mutex> lock(mutex_);
  update_subscribers_.clear();
}

void GroupChangeHandler::ClosestNodesUpdate(protobuf::Message& message) {
  if (message.destination_id() != routing_table_.kNodeId().string()) {
    // Message not for this node and we should not pass it on.
    LOG(kError) << "Message not for this node.";
    message.Clear();
    return;
  }
  protobuf::ClosestNodesUpdate closest_node_update;
  if (!closest_node_update.ParseFromString(message.data(0))) {
    LOG(kError) << "No Data.";
    return;
  }

  if (closest_node_update.node().empty() || !CheckId(closest_node_update.node())) {
    LOG(kError) << "Invalid node id provided.";
    return;
  }

  std::vector<NodeInfo> closest_nodes;
  NodeInfo node_info;
  for (auto& basic_info : closest_node_update.nodes_info()) {
    if (CheckId(basic_info.node_id())) {
      node_info.node_id = NodeId(basic_info.node_id());
      node_info.rank = basic_info.rank();
      closest_nodes.push_back(node_info);
    }
  }
  assert(!closest_nodes.empty());
  UpdateGroupChange(NodeId(closest_node_update.node()), closest_nodes);
  message.Clear();  // No response
}

void GroupChangeHandler::ClosestNodesUpdateSubscribe(protobuf::Message& message) {
  if (message.destination_id() != routing_table_.kNodeId().string()) {
    // Message not for this node and we should not pass it on.
    LOG(kError) << "Message not for this node.";
    message.Clear();
    return;
  }
  protobuf::ClosestNodesUpdateSubscrirbe closest_node_update_subscribe;
  if (!closest_node_update_subscribe.ParseFromString(message.data(0))) {
    LOG(kError) << "No Data.";
    return;
  }

  if (closest_node_update_subscribe.peer().empty() ||
      !CheckId(closest_node_update_subscribe.peer())) {
    LOG(kError) << "Invalid node id provided.";
    return;
  }

  if (closest_node_update_subscribe.subscribe())
    Subscribe(NodeId(closest_node_update_subscribe.peer()));
  else
    Unsubscribe(NodeId(closest_node_update_subscribe.peer()));
  message.Clear();  // No response
}

void GroupChangeHandler::Unsubscribe(NodeId node_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  LOG(kVerbose) << "[" << DebugId(routing_table_.kNodeId())
                << "] unsubscribing " << DebugId(node_id);
  if (!update_subscribers_.empty())
    update_subscribers_.erase(std::remove_if(update_subscribers_.begin(),
                                             update_subscribers_.end(),
                                             [&](const NodeInfo& node_info) {
                                               return node_info.node_id == node_id;
                                             }));
}

void GroupChangeHandler::Subscribe(NodeId node_id) {
  LOG(kVerbose) << "[" << DebugId(routing_table_.kNodeId()) << "] subscribing " << DebugId(node_id);
  NodeInfo node_info;
  std::vector<NodeInfo> connected_closest_nodes;
  {
    connected_closest_nodes = routing_table_.GetClosestNodeInfo(routing_table_.kNodeId(),
                                                                Parameters::closest_nodes_size);
    if (connected_closest_nodes.size() < Parameters::closest_nodes_size)
      return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (routing_table_.GetNodeInfo(node_id, node_info)) {
      if (std::find_if(update_subscribers_.begin(),
                       update_subscribers_.end(),
                       [=](const NodeInfo& node)->bool {
                         return node.node_id == node_id;
                       }) == update_subscribers_.end())
        update_subscribers_.push_back(node_info);
        LOG(kVerbose) << "[" << DebugId(routing_table_.kNodeId()) << "] subscribed "
                      << DebugId(node_id) << " current size: "  << update_subscribers_.size();
    }
  }
  if (node_info.node_id != NodeId()) {
    assert(connected_closest_nodes.size() <= Parameters::closest_nodes_size);
    protobuf::Message closest_nodes_update_rpc(
        rpcs::ClosestNodesUpdateRequest(node_info.node_id,
                                        routing_table_.kNodeId(),
                                        connected_closest_nodes));
    network_.SendToDirect(closest_nodes_update_rpc, node_info.node_id, node_info.connection_id);
  } else {
    LOG(kVerbose) << "[" << DebugId(routing_table_.kNodeId()) << "] failed to subscribe "
                  << DebugId(node_id) << " current size: "  << update_subscribers_.size();
  }
}


void GroupChangeHandler::UpdateGroupChange(const NodeId& node_id,
                                           std::vector<NodeInfo> close_nodes) {
  if (routing_table_.IsConnected(node_id)) {
    LOG(kVerbose) << DebugId(routing_table_.kNodeId()) << "UpdateGroupChange for "
                  << DebugId(node_id) << " size of update: " << close_nodes.size();
    routing_table_.GroupUpdateFromConnectedPeer(node_id, close_nodes);
  } else {
    LOG(kVerbose) << DebugId(routing_table_.kNodeId()) << "UpdateGroupChange for failed"
                  << DebugId(node_id) << " size of update: " << close_nodes.size();
  }
  SendSubscribeRpc(true, NodeInfo());
}

void GroupChangeHandler::SendClosestNodesUpdateRpcs(const std::vector<NodeInfo>& closest_nodes) {
  if (closest_nodes.size() < Parameters::closest_nodes_size)
    return;
  std::vector<NodeInfo> update_subscribers;
  assert(closest_nodes.size() <= Parameters::closest_nodes_size);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    update_subscribers.resize(update_subscribers_.size());
    std::copy(update_subscribers_.begin(), update_subscribers_.end(), update_subscribers.begin());
  }
  for (auto itr(update_subscribers.begin()); itr != update_subscribers.end(); ++itr) {
    protobuf::Message closest_nodes_update_rpc(
        rpcs::ClosestNodesUpdateRequest(itr->node_id, routing_table_.kNodeId(), closest_nodes));
    network_.SendToDirect(closest_nodes_update_rpc, itr->node_id, itr->connection_id);
  }
}

void GroupChangeHandler::SendSubscribeRpc(const bool& subscribe,
                                          const NodeInfo& node_info) {
  std::vector<NodeInfo> nodes_needing_update;
  if (subscribe) {
    routing_table_.GetNodesNeedingGroupUpdates(nodes_needing_update);
  } else {
    nodes_needing_update.push_back(node_info);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!update_subscribers_.empty())
        update_subscribers_.erase(std::remove_if(update_subscribers_.begin(),
                                                 update_subscribers_.end(),
                                                 [&](const NodeInfo& node) {
                                                   return node.node_id == node_info.node_id;
                                                 }));
    }
  }
  LOG(kVerbose) << "SendSubscribeRpc: nodes_needing_update: " << nodes_needing_update.size();
  for (auto& node : nodes_needing_update) {
    LOG(kVerbose) << DebugId(routing_table_.kNodeId()) << " SendSubscribeRpc to "
                  << DebugId(node.node_id);
    protobuf::Message closest_nodes_update_rpc(
        rpcs::ClosestNodesUpdateSubscrirbe(node.node_id, routing_table_.kNodeId(), subscribe));
    network_.SendToDirect(closest_nodes_update_rpc, node.node_id, node.connection_id);
  }
}

}  // namespace routing

}  // namespace maidsafe