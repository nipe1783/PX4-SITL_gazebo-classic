#include <ignition/math/Rand.hh>
#include <ignition/math/Vector3.hh>
#include <gazebo/common/Events.hh>
#include <gazebo/common/Assert.hh>
#include <gazebo/physics/Model.hh>
#include <gazebo/physics/Link.hh>
#include "gazebo_rgv_movement_plugin_private.h"
#include "gazebo_rgv_movement_plugin.h"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float64_multi_array.hpp"

using namespace gazebo;

GZ_REGISTER_MODEL_PLUGIN(RgvMovementPlugin)

/////////////////////////////////////////////////
RgvMovementPlugin::RgvMovementPlugin()
  : dataPtr(new RgvMovementPluginPrivate)
{
}

/////////////////////////////////////////////////
RgvMovementPlugin::~RgvMovementPlugin()
{
  delete this->dataPtr;
}

/////////////////////////////////////////////////
void RgvMovementPlugin::Load(physics::ModelPtr _model, sdf::ElementPtr _sdf)
{
  GZ_ASSERT(_model, "Model pointer is null");

  // Make sure the link has been specified
  if (!_sdf->HasElement("link"))
  {
    gzerr << "<link> element missing from RgvMovementPlugin. "
      << "The plugin will not function.\n";
    return;
  }

  // Get the link;
  this->dataPtr->link = _model->GetLink(_sdf->Get<std::string>("link"));
  if (!this->dataPtr->link)
  {
    gzerr << "Unable to find link[" << _sdf->Get<std::string>("link") << "] "
      << "in model[" << _model->GetName() << "]. The RgvMovementPluginRgvMovementPlugin "
      << "will not function.\n";
    return;
  }

  // Get x clamping values
  if (_sdf->HasElement("min_x"))
    this->dataPtr->xRange.X(_sdf->Get<double>("min_x"));
  if (_sdf->HasElement("max_x"))
    this->dataPtr->xRange.Y(_sdf->Get<double>("max_x"));

  ignition::math::Vector2d tmp = this->dataPtr->xRange;
  this->dataPtr->xRange.X(std::min(tmp.X(), tmp.Y()));
  this->dataPtr->xRange.Y(std::max(tmp.X(), tmp.Y()));

  // Get y clamping values
  if (_sdf->HasElement("min_y"))
    this->dataPtr->yRange.X(_sdf->Get<double>("min_y"));
  if (_sdf->HasElement("max_y"))
    this->dataPtr->yRange.Y(_sdf->Get<double>("max_y"));

  // Make sure min <= max
  tmp = this->dataPtr->yRange;
  this->dataPtr->yRange.X(std::min(tmp.X(), tmp.Y()));
  this->dataPtr->yRange.Y(std::max(tmp.X(), tmp.Y()));

  // Get z clamping values
  if (_sdf->HasElement("min_z"))
    this->dataPtr->zRange.X(_sdf->Get<double>("min_z"));
  if (_sdf->HasElement("max_z"))
    this->dataPtr->zRange.Y(_sdf->Get<double>("max_z"));

  // Make sure min <= max
  tmp = this->dataPtr->zRange;
  this->dataPtr->zRange.X(std::min(tmp.X(), tmp.Y()));
  this->dataPtr->zRange.Y(std::max(tmp.X(), tmp.Y()));

  // Set the initial velocity, if present
  if (_sdf->HasElement("initial_velocity"))
  {
    this->dataPtr->velocity =
      _sdf->Get<ignition::math::Vector3d>("initial_velocity");
  }

  // Set the velocity factor
  if (_sdf->HasElement("velocity_factor"))
    this->dataPtr->velocityFactor = _sdf->Get<double>("velocity_factor");

  // Set the update period
  if (_sdf->HasElement("update_period"))
    this->dataPtr->updatePeriod = _sdf->Get<double>("update_period");

  // Set the time to stop
  if (_sdf->HasElement("move_duration"))
    this->dataPtr->moveDuration = _sdf->Get<double>("move_duration");
    this->dataPtr->stopTime = _sdf->Get<double>("move_duration");

  // set the stop duration
  if (_sdf->HasElement("stop_duration"))
    this->dataPtr->stopDuration = _sdf->Get<double>("stop_duration");
    
  // Initialize ROS 2 if not already done
  if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
  }

  // Create a ROS 2 node
  if(_sdf->HasElement("ros_node_name")) {
    this->dataPtr->rosNodeName = _sdf->Get<std::string>("ros_node_name");
    this->dataPtr->rosNode = rclcpp::Node::make_shared(this->dataPtr->rosNodeName);
  }


  // Create a publisher
  if(_sdf->HasElement("ros_topic")){
    this->dataPtr->rosTopicGazeboFrame = _sdf->Get<std::string>("ros_topic") + "/gazebo_i_frame";
    this->dataPtr->publisherGazeboFrame = this->dataPtr->rosNode->create_publisher<std_msgs::msg::Float64MultiArray>(this->dataPtr->rosTopicGazeboFrame, 10);
    this->dataPtr->rosTopicUASFrame = _sdf->Get<std::string>("ros_topic") + "/uas_i_frame";
    this->dataPtr->publisherUASFrame = this->dataPtr->rosNode->create_publisher<std_msgs::msg::Float64MultiArray>(this->dataPtr->rosTopicUASFrame, 10);
  }

  // Connect to the world update signal for continuous operation
  this->dataPtr->updateConnection = event::Events::ConnectWorldUpdateBegin(
      std::bind(&RgvMovementPlugin::Update, this, std::placeholders::_1));
}

/////////////////////////////////////////////////
void RgvMovementPlugin::Reset()
{
  this->dataPtr->prevUpdate.Set(0, 0);
  this->dataPtr->startTime.Set(0, 0);
}

/////////////////////////////////////////////////
void RgvMovementPlugin::Update(const common::UpdateInfo &_info)
{
  GZ_ASSERT(this->dataPtr->link, "<link> in RgvMovementPlugin is null");

  // Short-circuit in case the link is invalid.
  if (!this->dataPtr->link)
    return;
  
  // Publishing logic
  if (this->dataPtr->rosNode && this->dataPtr->publisherGazeboFrame && this->dataPtr->publisherUASFrame) {
    ignition::math::Pose3d pose = this->dataPtr->link->WorldPose();
    ignition::math::Vector3d position = pose.Pos();
    auto message = std_msgs::msg::Float64MultiArray();
    message.data.push_back(position.X());
    message.data.push_back(position.Y());
    message.data.push_back(position.Z());
    this->dataPtr->publisherGazeboFrame->publish(message);
    message.data.clear();
    message.data.push_back(position.Y() - 1.0);
    message.data.push_back(position.X() - 1.0);
    message.data.push_back(position.Z());
    this->dataPtr->publisherUASFrame->publish(message);
  }

  // Stop the movement if the stop time has been reached`````````````````
  if(_info.simTime > this->dataPtr->stopTime &&
     _info.simTime <= this->dataPtr->stopTime + this->dataPtr->stopDuration)
  {
    this->dataPtr->velocity.Set(0, 0, 0.04);
    this->dataPtr->link->SetLinearVel(this->dataPtr->velocity);
    this->dataPtr->prevUpdate = _info.simTime;
  }

  // Change direction when enough time has elapsed
  else if (_info.simTime - this->dataPtr->prevUpdate > this->dataPtr->updatePeriod)
  {
    /* Define a square velocity pattern */
    if(_idx == 0){
      this->dataPtr->velocity.Set(1,0,0);
      _idx = 1; 
    } else if ( _idx == 1) {
      this->dataPtr->velocity.Set(0,1,0);
      _idx = 2;
    } else if ( _idx == 2) {
      this->dataPtr->velocity.Set(-1,0,0);
      _idx = 3;
    } else if (_idx == 3) {
      this->dataPtr->velocity.Set(0,-1,0);
      _idx = 0;
    }

    // Apply scaling factor
    this->dataPtr->velocity.Normalize();
    this->dataPtr->velocity *= this->dataPtr->velocityFactor;

    // Clamp X value
    this->dataPtr->velocity.X(ignition::math::clamp(this->dataPtr->velocity.X(),
        this->dataPtr->xRange.X(), this->dataPtr->xRange.Y()));

    // Clamp Y value
    this->dataPtr->velocity.Y(ignition::math::clamp(this->dataPtr->velocity.Y(),
        this->dataPtr->yRange.X(), this->dataPtr->yRange.Y()));

    // Clamp Z value
    this->dataPtr->velocity.Z(ignition::math::clamp(this->dataPtr->velocity.Z(),
        this->dataPtr->zRange.X(), this->dataPtr->zRange.Y()));

    this->dataPtr->prevUpdate = _info.simTime;
  }

  if(_info.simTime >= this->dataPtr->stopTime + this->dataPtr->stopDuration)
  {
    this->dataPtr->stopTime = _info.simTime + this->dataPtr->moveDuration;
  }

  // Apply velocity
  this->dataPtr->link->SetLinearVel(this->dataPtr->velocity);
}