/*  Copyright 2012 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.maidsafe.net/licenses

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#ifndef MAIDSAFE_ROUTING_TESTS_ROUTING_NETWORK_H_
#define MAIDSAFE_ROUTING_TESTS_ROUTING_NETWORK_H_

#include <chrono>
#include <future>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

#include "boost/asio/ip/udp.hpp"

#include "maidsafe/common/node_id.h"
#include "maidsafe/common/test.h"
#include "maidsafe/common/utils.h"

#include "maidsafe/rudp/nat_type.h"

#include "maidsafe/passport/types.h"

#include "maidsafe/routing/api_config.h"
#include "maidsafe/routing/node_info.h"
#include "maidsafe/routing/parameters.h"
#include "maidsafe/routing/return_codes.h"
#include "maidsafe/routing/routing_api.h"

namespace args = std::placeholders;

namespace maidsafe {

namespace routing {

class Routing;
namespace protobuf {
class Message;
}

namespace test {

bool IsPortAvailable(boost::asio::ip::udp::endpoint endpoint);

template <typename Future>
bool is_ready(std::future<Future>& f) {
  return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

struct NodeInfoAndPrivateKey;

enum ExpectedNodeType {
  kExpectVault,
  kExpectClient,
  kExpectDoesNotExist
};

const uint32_t kClientSize(5);
const uint32_t kServerSize(20);
const uint32_t kNetworkSize = kClientSize + kServerSize;

class GenericNetwork;
class NodesEnvironment;

class GenericNode {
 public:
  GenericNode(bool client_mode = false, bool has_symmetric_nat = false,
              bool non_mutating_client = false);
  GenericNode(bool client_mode, const rudp::NatType& nat_type);
  GenericNode(bool client_mode, const NodeInfoAndPrivateKey& node_info,
              bool has_symmetric_nat = false, bool non_mutating_client = false);
  virtual ~GenericNode();
  int GetStatus() const;
  NodeId node_id() const;
  size_t id() const;
  NodeId connection_id() const;
  boost::asio::ip::udp::endpoint endpoint() const;
  std::shared_ptr<Routing> routing() const;
  NodeInfo node_info() const;
  void set_joined(const bool node_joined);
  bool joined() const;
  bool IsClient() const;
  bool HasSymmetricNat() const;
  // void set_client_mode(bool client_mode);
  int expected();
  void set_expected(int expected);
  int ZeroStateJoin(const boost::asio::ip::udp::endpoint& peer_endpoint,
                    const NodeInfo& peer_node_info);
  void Join(const std::vector<boost::asio::ip::udp::endpoint>& peer_endpoints =
                std::vector<boost::asio::ip::udp::endpoint>());
  void SendDirect(const NodeId& destination_id, const std::string& data, bool cacheable,
                  ResponseFunctor response_functor);
  void SendGroup(const NodeId& destination_id, const std::string& data, bool cacheable,
                 ResponseFunctor response_functor);
  std::future<std::vector<NodeId>> GetGroup(const NodeId& info_id);
  GroupRangeStatus IsNodeIdInGroupRange(const NodeId& node_id);
  void SendToClosestNode(const protobuf::Message& message);
  void RudpSend(const NodeId& peer_endpoint, const protobuf::Message& message,
                rudp::MessageSentFunctor message_sent_functor);
  void PrintRoutingTable();
  std::vector<NodeId> ReturnRoutingTable();
  void PrintGroupMatrix();
  bool RoutingTableHasNode(const NodeId& node_id);
  bool ClientRoutingTableHasNode(const NodeId& node_id);
  NodeInfo GetRemovableNode();
  NodeInfo GetNthClosestNode(const NodeId& target_id, uint16_t node_number);
  testing::AssertionResult DropNode(const NodeId& node_id);
  std::vector<NodeInfo> RoutingTable() const;
  NodeId GetRandomExistingNode() const;
  std::vector<NodeInfo> ClosestNodes();
  bool IsConnectedVault(const NodeId& node_id);
  bool IsConnectedClient(const NodeId& node_id);
  void AddNodeToRandomNodeHelper(const NodeId& node_id);
  void RemoveNodeFromRandomNodeHelper(const NodeId& node_id);
  bool NodeSubscribedForGroupUpdate(const NodeId& node_id);
  std::vector<NodeInfo> GetGroupMatrixConnectedPeers();
  void SetMatrixChangeFunctor(MatrixChangedFunctor group_matrix_functor);

  void PostTaskToAsioService(std::function<void()> functor);
  rudp::NatType nat_type();
  std::string SerializeRoutingTable();

  static size_t next_node_id_;
  size_t MessagesSize() const;
  void ClearMessages();
  asymm::PublicKey public_key();
  int Health();
  void SetHealth(int health);
  friend class GenericNetwork;
  Functors functors_;

 protected:
  size_t id_;
  std::shared_ptr<NodeInfoAndPrivateKey> node_info_plus_;
  std::mutex mutex_;
  bool client_mode_;
  bool joined_;
  int expected_;
  rudp::NatType nat_type_;
  bool has_symmetric_nat_;
  boost::asio::ip::udp::endpoint endpoint_;
  std::vector<std::string> messages_;
  std::shared_ptr<Routing> routing_;

 private:
  std::mutex health_mutex_;
  int health_;
  void InitialiseFunctors();
  void InjectNodeInfoAndPrivateKey();
};

class GenericNetwork {
 public:
  typedef std::shared_ptr<GenericNode> NodePtr;
  GenericNetwork();
  virtual ~GenericNetwork();

  bool ValidateRoutingTables() const;
  virtual void SetUp();
  virtual void TearDown();
  void SetUpNetwork(size_t total_number_vaults, size_t total_number_clients = 0);
  // Use to specify proportion of vaults/clients that should behave as though they are behind
  // symmetric NAT. Two nodes behind symmetric NAT can't connect directly to each other.
  void SetUpNetwork(size_t total_number_vaults, size_t total_number_clients,
                    size_t num_symmetric_nat_vaults,
                    size_t num_symmetric_nat_clients);
  void AddNode(bool client_mode, const NodeId& node_id,
               MatrixChangedFunctor matrix_change_functor);
  void AddNode(bool client_mode, const NodeId& node_id,
               bool has_symmetric_nat = false, bool non_mutating_client = false);
  void AddNode(bool client_mode, const rudp::NatType& nat_type);
  void AddNode(bool client_mode, bool has_symmetric_nat);
  bool RemoveNode(const NodeId& node_id);
  bool WaitForNodesToJoin();
  bool WaitForNodesToJoin(size_t num_total_nodes);
  void Validate(const NodeId& node_id, GivePublicKeyFunctor give_public_key) const;
  void SetNodeValidationFunctor(NodePtr node);
  std::vector<NodeId> GroupIds(const NodeId& node_id) const;
  void PrintRoutingTables() const;
  uint16_t RandomNodeIndex() const;
  uint16_t RandomClientIndex() const;
  uint16_t RandomVaultIndex() const;
  NodePtr RandomClientNode() const;
  NodePtr RandomVaultNode() const;
  void RemoveRandomClient();
  void RemoveRandomVault();
  void ClearMessages();
  int NodeIndex(const NodeId& node_id) const;
  size_t ClientIndex() const { return client_index_; }
  std::vector<NodeId> GetAllNodeIds() const;
  std::vector<NodeId> GetGroupForId(const NodeId& node_id) const;
  std::vector<NodeInfo> GetClosestNodes(const NodeId& target_id, uint32_t quantity,
                                        bool vault_only = false) const;
  std::vector<NodeInfo> GetClosestVaults(const NodeId& target_id, uint32_t quantity) const;
  void ValidateExpectedNodeType(const NodeId& node_id,
                                const ExpectedNodeType& expected_node_type) const;
  bool RestoreComposition();
  // For num. vaults <= max_routing_table_size
  bool WaitForHealthToStabilise() const;
  // For num. vaults > max_routing_table_size
  bool WaitForHealthToStabiliseInLargeNetwork() const;
  bool NodeHasSymmetricNat(const NodeId& node_id) const;
  // Verifies that nodes' group matrices contain the Parameters::closest_nodes_size closest nodes
  testing::AssertionResult CheckGroupMatrixUniqueNodes(uint16_t check_length =
                                                           Parameters::closest_nodes_size + 1);
  // Do SendDirect between each pair of nodes and monitor results (do this 'repeats' times)
  testing::AssertionResult SendDirect(size_t repeats, size_t message_size = (2 ^ 10) * 256);
  // Do SendGroup from source_index node to target ID and monitor results (do this 'repeats' times)
  testing::AssertionResult SendGroup(const NodeId& target_id, size_t repeats,
                                     uint16_t source_index = 0,
                                     size_t message_size = (2 ^ 10) * 256);
  // Do SendDirect from each node to destination_node_id and monitor results. The ExpectedNodeType
  // of destination_node_id should be correctly specified when calling this function.
  testing::AssertionResult SendDirect(const NodeId& destination_node_id,
                                      const ExpectedNodeType& destination_node_type = kExpectVault);
  // Do SendDirect from source_node to destination_node_id and monitor results. The ExpectedNodeType
  // of destination_node_id should be correctly specified when calling this function.
  testing::AssertionResult SendDirect(std::shared_ptr<GenericNode> source_node,
                                      const NodeId& destination_node_id,
                                      const ExpectedNodeType& destination_node_type = kExpectVault);

  friend class NodesEnvironment;

 private:
  uint16_t NonClientNodesSize() const;
  uint16_t NonClientNonSymmetricNatNodesSize() const;
  void AddNodeDetails(NodePtr node);

  mutable std::mutex mutex_, fobs_mutex_;
  std::vector<boost::asio::ip::udp::endpoint> bootstrap_endpoints_;
  boost::filesystem::path bootstrap_path_;
  std::map<NodeId, asymm::PublicKey> public_keys_;
  uint16_t client_index_;
  bool nat_info_available_;

 public:
  std::vector<NodePtr> nodes_;
};

class NodesEnvironment : public testing::Environment {
 public:
  NodesEnvironment(size_t total_num_server_nodes, size_t total_num_client_nodes,
                   size_t num_symmetric_nat_server_nodes, size_t num_symmetric_nat_client_nodes)
      : total_num_server_nodes_(total_num_server_nodes),
        total_num_client_nodes_(total_num_client_nodes),
        num_symmetric_nat_server_nodes_(num_symmetric_nat_server_nodes),
        num_symmetric_nat_client_nodes_(num_symmetric_nat_client_nodes) {}

  void SetUp() override {
    g_env_->GenericNetwork::SetUp();
    g_env_->SetUpNetwork(total_num_server_nodes_, total_num_client_nodes_,
                         num_symmetric_nat_server_nodes_, num_symmetric_nat_client_nodes_);
  }
  void TearDown() override {
    //    if (g_env_.unique()) {
    //      g_env_.reset();
    //    }
    g_env_->TearDown();
  }

  static std::shared_ptr<GenericNetwork> g_environment() { return g_env_; }

 private:
  size_t total_num_server_nodes_;
  size_t total_num_client_nodes_;
  size_t num_symmetric_nat_server_nodes_;
  size_t num_symmetric_nat_client_nodes_;
  static std::shared_ptr<GenericNetwork> g_env_;
};

}  // namespace test

}  // namespace routing

}  // namespace maidsafe

#endif  // MAIDSAFE_ROUTING_TESTS_ROUTING_NETWORK_H_
