#include <chrono>
#include <cmath>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

#include "sensor_msgs/msg/battery_state.hpp"
#include "std_msgs/msg/bool.hpp"
#include "geometry_msgs/msg/pose_stamped.hpp"

#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "nav2_msgs/action/navigate_through_poses.hpp"
#include "nav2_msgs/action/follow_waypoints.hpp"

#include "tf2/LinearMath/Quaternion.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"

using namespace std::chrono_literals;

class AutoChargeManager : public rclcpp::Node
{
public:
  using NavigateToPose = nav2_msgs::action::NavigateToPose;
  using NavigateThroughPoses = nav2_msgs::action::NavigateThroughPoses;
  using FollowWaypoints = nav2_msgs::action::FollowWaypoints;

  using NavigateGoalHandle =
    rclcpp_action::ClientGoalHandle<NavigateToPose>;

  AutoChargeManager()
  : Node("auto_charge_manager")
  {
    // Battery monitoring parameters
    battery_topic_ = declare_parameter<std::string>(
      "battery_topic", "/battery_status");

    use_voltage_ = declare_parameter<bool>(
      "use_voltage", false);

    low_threshold_ = declare_parameter<double>(
      "low_threshold", 0.25);

    reset_threshold_ = declare_parameter<double>(
      "reset_threshold", 0.35);

    confirm_count_required_ = declare_parameter<int>(
      "confirm_count", 5);

    // -------------------------
    // Charging station staging pose
    // -------------------------
    map_frame_ = declare_parameter<std::string>(
      "map_frame", "map");

    staging_x_ = declare_parameter<double>(
      "staging_x", 0.0);

    staging_y_ = declare_parameter<double>(
      "staging_y", 0.0);

    staging_yaw_ = declare_parameter<double>(
      "staging_yaw", 0.0);

    // Wait a fixed interval after cancellation requests before sending the return goal.
    cancel_wait_ms_ = declare_parameter<int>(
      "cancel_wait_ms", 800);

    max_return_retries_ = declare_parameter<int>(
      "max_return_retries", 2);

    // -------------------------
    // Navigation action clients
    // -------------------------
    navigate_client_ =
      rclcpp_action::create_client<NavigateToPose>(
      this, "/navigate_to_pose");

    navigate_through_poses_client_ =
      rclcpp_action::create_client<NavigateThroughPoses>(
      this, "/navigate_through_poses");

    follow_waypoints_client_ =
      rclcpp_action::create_client<FollowWaypoints>(
      this, "/follow_waypoints");

    // -------------------------
    // State publishers
    // -------------------------
    start_docking_pub_ =
      create_publisher<std_msgs::msg::Bool>(
      "/start_docking", 10);

    return_active_pub_ =
      create_publisher<std_msgs::msg::Bool>(
      "/return_to_charge_active", 10);

    // -------------------------
    // Battery subscriber
    // -------------------------
    battery_sub_ =
      create_subscription<sensor_msgs::msg::BatteryState>(
      battery_topic_,
      rclcpp::SensorDataQoS(),
      std::bind(
        &AutoChargeManager::batteryCallback,
        this,
        std::placeholders::_1));

    RCLCPP_INFO(
      get_logger(),
      "Auto charge manager started.");

    RCLCPP_INFO(
      get_logger(),
      "Battery topic: %s",
      battery_topic_.c_str());

    RCLCPP_INFO(
      get_logger(),
      "Staging pose: x=%.3f, y=%.3f, yaw=%.3f rad",
      staging_x_, staging_y_, staging_yaw_);
  }

private:
  enum class State
  {
    NORMAL,
    CANCELING,
    RETURNING,
    AT_STAGING,
    CHARGING,
    FAILED
  };

  void batteryCallback(
    const sensor_msgs::msg::BatteryState::SharedPtr msg)
  {
    // Charging or full status takes precedence over the low-battery trigger.
    if (
      msg->power_supply_status ==
      sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_CHARGING ||
      msg->power_supply_status ==
      sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_FULL)
    {
      if (state_ != State::CHARGING) {
        RCLCPP_INFO(
          get_logger(),
          "Charging state detected.");
      }

      state_ = State::CHARGING;
      low_count_ = 0;
      publishReturnActive(false);
      return;
    }

    const double battery_value =
      use_voltage_ ? msg->voltage : msg->percentage;

    if (!std::isfinite(battery_value)) {
      RCLCPP_WARN_THROTTLE(
        get_logger(),
        *get_clock(),
        5000,
        "Battery value is invalid.");
      return;
    }

    RCLCPP_INFO_THROTTLE(
      get_logger(),
      *get_clock(),
      5000,
      "Battery value: %.3f",
      battery_value);

    // Re-arm low-battery detection after recovery above the reset threshold.
    if (
      battery_value >= reset_threshold_ &&
      (state_ == State::FAILED ||
      state_ == State::CHARGING))
    {
      state_ = State::NORMAL;
      retry_count_ = 0;
      low_count_ = 0;
      publishReturnActive(false);
    }

    // Do not retrigger while returning, at staging or charging.
    if (state_ != State::NORMAL) {
      return;
    }

    if (battery_value <= low_threshold_) {
      low_count_++;

      RCLCPP_WARN(
        get_logger(),
        "Low battery confirmation: %d/%d",
        low_count_,
        confirm_count_required_);
    } else {
      low_count_ = 0;
    }

    if (low_count_ >= confirm_count_required_) {
      triggerReturnToCharge();
    }
  }

  void triggerReturnToCharge()
  {
    if (state_ != State::NORMAL) {
      return;
    }

    state_ = State::CANCELING;
    retry_count_ = 0;

    publishReturnActive(true);

    RCLCPP_WARN(
      get_logger(),
      "Low battery confirmed. Canceling current navigation.");

    cancelAllNavigation();

    scheduleReturnGoal(cancel_wait_ms_);
  }

  void cancelAllNavigation()
  {
    // Request cancellation of ordinary single-pose navigation goals.
    navigate_client_->async_cancel_all_goals();

    // Request cancellation of NavigateThroughPoses goals.
    navigate_through_poses_client_->async_cancel_all_goals();

    // Request cancellation of FollowWaypoints goals.
    follow_waypoints_client_->async_cancel_all_goals();
  }

  void scheduleReturnGoal(int delay_ms)
  {
    return_timer_ = create_wall_timer(
      std::chrono::milliseconds(delay_ms),
      [this]()
      {
        return_timer_->cancel();
        sendReturnGoal();
      });
  }

  void sendReturnGoal()
  {
    if (!navigate_client_->wait_for_action_server(2s)) {
      RCLCPP_ERROR(
        get_logger(),
        "NavigateToPose action server is unavailable.");

      retryOrFail();
      return;
    }

    state_ = State::RETURNING;

    NavigateToPose::Goal goal;

    goal.pose.header.frame_id = map_frame_;
    goal.pose.header.stamp = now();

    goal.pose.pose.position.x = staging_x_;
    goal.pose.pose.position.y = staging_y_;
    goal.pose.pose.position.z = 0.0;

    tf2::Quaternion quaternion;
    quaternion.setRPY(0.0, 0.0, staging_yaw_);
    goal.pose.pose.orientation = tf2::toMsg(quaternion);

    auto options =
      rclcpp_action::Client<NavigateToPose>::SendGoalOptions();

    options.goal_response_callback =
      [this](NavigateGoalHandle::SharedPtr goal_handle)
      {
        if (!goal_handle) {
          RCLCPP_ERROR(
            get_logger(),
            "Return-to-charge goal was rejected.");

          retryOrFail();
          return;
        }

        RCLCPP_INFO(
          get_logger(),
          "Return-to-charge goal accepted.");
      };

    options.feedback_callback =
      [this](
      NavigateGoalHandle::SharedPtr,
      const std::shared_ptr<
        const NavigateToPose::Feedback> feedback)
      {
        RCLCPP_INFO_THROTTLE(
          get_logger(),
          *get_clock(),
          3000,
          "Returning to charger. Remaining distance: %.2f m",
          feedback->distance_remaining);
      };

    options.result_callback =
      [this](const NavigateGoalHandle::WrappedResult & result)
      {
        switch (result.code) {
          case rclcpp_action::ResultCode::SUCCEEDED:
            RCLCPP_INFO(
              get_logger(),
              "Reached charging staging pose.");

            state_ = State::AT_STAGING;
            publishStartDocking(true);
            break;

          case rclcpp_action::ResultCode::ABORTED:
            RCLCPP_ERROR(
              get_logger(),
              "Return-to-charge navigation aborted.");

            retryOrFail();
            break;

          case rclcpp_action::ResultCode::CANCELED:
            RCLCPP_ERROR(
              get_logger(),
              "Return-to-charge navigation canceled.");

            retryOrFail();
            break;

          default:
            RCLCPP_ERROR(
              get_logger(),
              "Unknown navigation result.");

            retryOrFail();
            break;
        }
      };

    RCLCPP_WARN(
      get_logger(),
      "Sending charging staging goal: %.3f, %.3f, %.3f",
      staging_x_, staging_y_, staging_yaw_);

    navigate_client_->async_send_goal(goal, options);
  }

  void retryOrFail()
  {
    if (retry_count_ < max_return_retries_) {
      retry_count_++;

      RCLCPP_WARN(
        get_logger(),
        "Retrying return navigation: %d/%d",
        retry_count_,
        max_return_retries_);

      cancelAllNavigation();
      scheduleReturnGoal(2000);
      return;
    }

    state_ = State::FAILED;

    RCLCPP_ERROR(
      get_logger(),
      "Failed to return to charging station. "
      "Robot remains in low-battery lock state.");
  }

  void publishStartDocking(bool value)
  {
    std_msgs::msg::Bool msg;
    msg.data = value;
    start_docking_pub_->publish(msg);
  }

  void publishReturnActive(bool value)
  {
    std_msgs::msg::Bool msg;
    msg.data = value;
    return_active_pub_->publish(msg);
  }

  // Parameters
  std::string battery_topic_;
  std::string map_frame_;

  bool use_voltage_{false};

  double low_threshold_{0.25};
  double reset_threshold_{0.35};

  double staging_x_{0.0};
  double staging_y_{0.0};
  double staging_yaw_{0.0};

  int confirm_count_required_{5};
  int cancel_wait_ms_{800};
  int max_return_retries_{2};

  // Runtime state
  State state_{State::NORMAL};

  int low_count_{0};
  int retry_count_{0};

  // ROS interfaces
  rclcpp::Subscription<
    sensor_msgs::msg::BatteryState>::SharedPtr battery_sub_;

  rclcpp::Publisher<
    std_msgs::msg::Bool>::SharedPtr start_docking_pub_;

  rclcpp::Publisher<
    std_msgs::msg::Bool>::SharedPtr return_active_pub_;

  rclcpp_action::Client<
    NavigateToPose>::SharedPtr navigate_client_;

  rclcpp_action::Client<
    NavigateThroughPoses>::SharedPtr
    navigate_through_poses_client_;

  rclcpp_action::Client<
    FollowWaypoints>::SharedPtr follow_waypoints_client_;

  rclcpp::TimerBase::SharedPtr return_timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  rclcpp::spin(
    std::make_shared<AutoChargeManager>());

  rclcpp::shutdown();
  return 0;
}
