#ifndef MISSION_MANAGER_NODE_H_INCLUDED
#define MISSION_MANAGER_NODE_H_INCLUDED

#include <cstdio>
#include <chrono>
#include <functional>
#include <memory>
#include <cstdint>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "airstack_msgs/msg/search_mission_request.hpp"
#include "airstack_msgs/msg/task_assignment.hpp"
#include "mission_manager/BeliefMap.h"
#include "mission_manager/MissionManager.h"

/*
Subscirbers
- all the drones odoms
- search map updates
- targets

Publisher
- task assignment with plan request. Be together to avoid timing issues

When receive mission request or change in number of targets or change in number of drones
- check how many targets
- check how many drones
- see how many left for search
- divide up the space based on number of drones
- assign nearest drone to each task (targets and search regions)
- send plan request to each drone
*/


class MissionManagerNode : public rclcpp::Node
{
  public:
    MissionManagerNode()
    : Node("mission_manager"), count_(0)
    {
      mission_subscriber_ = this->create_subscription<airstack_msgs::msg::SearchMissionRequest>(
        "search_mission_request", 1, std::bind(&MissionManagerNode::search_mission_request_callback, this, std::placeholders::_1));

      // Create subscribers and publishers for max number of agents
      for (uint8_t i = 0; i < max_number_agents_; i++)
      {
        std::string topic_name = "agent_" + std::to_string(i) + "/odom";
        agent_odoms_subs_.push_back(
                this->create_subscription<std_msgs::msg::String>(
                    topic_name, 1,
                    [this, i](const std_msgs::msg::String::SharedPtr msg) {
                        this->agent_odom_callback(msg, i);
                    }
                )
            );
        
        
        
        std::string agent_topic = "agent_" + std::to_string(i) + "/plan_request";
        plan_request_pubs_.push_back(
          this->create_publisher<airstack_msgs::msg::TaskAssignment>(agent_topic, 10));
      }

      tracked_targets_sub_ = this->create_subscription<std_msgs::msg::String>(
        "tracked_targets", 1, std::bind(&MissionManagerNode::tracked_targets_callback, this, std::placeholders::_1));

      search_map_sub_ = this->create_subscription<std_msgs::msg::String>(
        "search_map_updates", 1, std::bind(&MissionManagerNode::search_map_callback, this, std::placeholders::_1));

      mission_manager_ = std::make_shared<MissionManager>(this->max_number_agents_);

      // TODO: set param for rate, make sure not communicated over network
      search_map_publisher_ = this->create_publisher<grid_map_msgs::msg::GridMap>(
        "search_map_basestation", rclcpp::QoS(1).transient_local());
      timer_ = this->create_wall_timer(
        std::chrono::seconds(1), std::bind(&MissionManagerNode::search_map_publisher, this));
    }

  private:
    /* --- ROS SPECIFIC --- */

    // Publisher
    std::vector<rclcpp::Publisher<airstack_msgs::msg::TaskAssignment>::SharedPtr> plan_request_pubs_;
    rclcpp::Publisher<grid_map_msgs::msg::GridMap>::SharedPtr search_map_publisher_;

    // Subscribers
    rclcpp::Subscription<airstack_msgs::msg::SearchMissionRequest>::SharedPtr mission_subscriber_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr tracked_targets_sub_;
    std::vector<rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> agent_odoms_subs_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr search_map_sub_;


    /* --- MEMBER ATTRIBUTES --- */

    size_t count_;
    rclcpp::TimerBase::SharedPtr timer_;
    int max_number_agents_ = 5; // TODO: get from param server
    airstack_msgs::msg::SearchMissionRequest latest_search_mission_request_;
    std::shared_ptr<MissionManager> mission_manager_;

    void publish_tasks(std::vector<airstack_msgs::msg::TaskAssignment> tasks) const
    {
      std::vector<bool> valid_agents = this->mission_manager_->get_valid_agents();
      for (uint8_t i = 0; i < this->max_number_agents_; i++)
      {
        if (valid_agents[i])
        {
          plan_request_pubs_[i]->publish(tasks[i]);
        }
      }
    }

    void search_map_publisher()
    {
      if (this->mission_manager_->belief_map_.is_initialized())
      {
        this->mission_manager_->belief_map_.map_.setTimestamp(this->now().nanoseconds());
        std::unique_ptr<grid_map_msgs::msg::GridMap> message;
        message = grid_map::GridMapRosConverter::toMessage(this->mission_manager_->belief_map_.map_);
        RCLCPP_DEBUG(
          this->get_logger(), "Publishing grid map (timestamp %f).",
          rclcpp::Time(message->header.stamp).nanoseconds() * 1e-9);
        search_map_publisher_->publish(std::move(message));
      }
    }

    void search_mission_request_callback(const airstack_msgs::msg::SearchMissionRequest::SharedPtr msg)
    {
      RCLCPP_INFO(this->get_logger(), "Received new search mission request");
      latest_search_mission_request_ = *msg;

      // TODO: clear the map knowledge? Only if the search area has changed?

      // TODO: visualize the seach mission request
      this->mission_manager_->belief_map_.reset_map(this->get_logger(), *msg);
      this->publish_tasks(this->mission_manager_->assign_tasks(this->get_logger()));
    }

    void agent_odom_callback(const std_msgs::msg::String::SharedPtr msg, const uint8_t &robot_id)
    {
      RCLCPP_INFO(this->get_logger(), "Received agent odom '%s'", msg->data.c_str());
      if (this->mission_manager_->check_agent_changes(this->get_logger(), robot_id, this->now()))
      {
        this->publish_tasks(this->mission_manager_->assign_tasks(this->get_logger()));
      }
    }

    void tracked_targets_callback(const std_msgs::msg::String::SharedPtr msg)
    {
      RCLCPP_INFO(this->get_logger(), "Received target track list '%s'", msg->data.c_str());
      // TODO: save the list of tracked target

      // Check if change in the number of targets or id numbers
      if (this->mission_manager_->check_target_changes(this->get_logger(), msg->data, this->now()))
      {
        this->publish_tasks(this->mission_manager_->assign_tasks(this->get_logger()));
      }
    }

    void search_map_callback(const std_msgs::msg::String::SharedPtr msg) const
    {
      RCLCPP_INFO(this->get_logger(), "Received search map '%s'", msg->data.c_str());

      // TODO: save the search map using that class
    }

};
#endif /* MISSION_MANAGER_NODE_H_INCLUDED */