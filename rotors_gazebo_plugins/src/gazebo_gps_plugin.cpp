/*
 * Copyright 2016 Pavel Vechersky, ASL, ETH Zurich, Switzerland
 * Copyright 2016 Geoffrey Hunter <gbmhunter@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// MODULE HEADER
#include "rotors_gazebo_plugins/gazebo_gps_plugin.h"

#include "ConnectGazeboToRosTopic.pb.h"

namespace gazebo {

GazeboGpsPlugin::GazeboGpsPlugin()
    : SensorPlugin(),
      //node_handle_(0),
      random_generator_(random_device_()),
      pubs_and_subs_created_(false) {}

GazeboGpsPlugin::~GazeboGpsPlugin() {
  this->parent_sensor_->DisconnectUpdated(this->updateConnection_);
}

void GazeboGpsPlugin::Load(sensors::SensorPtr _sensor, sdf::ElementPtr _sdf) {
  // Store the pointer to the parent sensor.
#if GAZEBO_MAJOR_VERSION > 6
  parent_sensor_ = std::dynamic_pointer_cast<sensors::GpsSensor>(_sensor);
  world_ = physics::get_world(parent_sensor_->WorldName());
#else
  parent_sensor_ = boost::dynamic_pointer_cast<sensors::GpsSensor>(_sensor);
  world_ = physics::get_world(parent_sensor_->GetWorldName());
#endif


  //==============================================//
  //========== READ IN PARAMS FROM SDF ===========//
  //==============================================//

  // Retrieve the necessary parameters.
  std::string node_namespace;
  std::string link_name;

  if (_sdf->HasElement("robotNamespace"))
    node_namespace = _sdf->GetElement("robotNamespace")->Get<std::string>();
  else
    gzerr << "[gazebo_gps_plugin] Please specify a robotNamespace.\n";


  node_handle_ = gazebo::transport::NodePtr(new transport::Node());
  node_handle_->Init(node_namespace);

  if (_sdf->HasElement("linkName"))
    link_name = _sdf->GetElement("linkName")->Get<std::string>();
  else
    gzerr << "[gazebo_gps_plugin] Please specify a linkName.\n";

  std::string frame_id = link_name;

  // Get the pointer to the link that holds the sensor.
  link_ = boost::dynamic_pointer_cast<physics::Link>(world_->GetByName(link_name));
  if (link_ == NULL)
    gzerr << "[gazebo_gps_plugin] Couldn't find specified link \"" << link_name << "\"\n";

  // Retrieve the rest of the SDF parameters.
  double hor_pos_std_dev;
  double ver_pos_std_dev;
  double hor_vel_std_dev;
  double ver_vel_std_dev;

//  getSdfParam<std::string>(_sdf, "gpsTopic", gps_topic_,
//                           mav_msgs::default_topics::GPS);
  getSdfParam<std::string>(_sdf, "gpsTopic", gps_topic_, "");

  getSdfParam<std::string>(_sdf, "groundSpeedTopic", ground_speed_topic_,
                           kDefaultGroundSpeedPubTopic);
  getSdfParam<double>(_sdf, "horPosStdDev", hor_pos_std_dev, kDefaultHorPosStdDev);
  getSdfParam<double>(_sdf, "verPosStdDev", ver_pos_std_dev, kDefaultVerPosStdDev);
  getSdfParam<double>(_sdf, "horVelStdDev", hor_vel_std_dev, kDefaultHorVelStdDev);
  getSdfParam<double>(_sdf, "verVelStdDev", ver_vel_std_dev, kDefaultVerVelStdDev);

  // Connect to the sensor update event.
  this->updateConnection_ =
      this->parent_sensor_->ConnectUpdated(
          boost::bind(&GazeboGpsPlugin::OnUpdate, this));

  // Make sure the parent sensor is active.
  parent_sensor_->SetActive(true);

  // Initialize the normal distributions for ground speed.
  ground_speed_n_[0] = NormalDistribution(0, hor_vel_std_dev);
  ground_speed_n_[1] = NormalDistribution(0, hor_vel_std_dev);
  ground_speed_n_[2] = NormalDistribution(0, ver_vel_std_dev);


  // ============================================ //
  // ======= POPULATE STATIC PARTS OF MSGS ====== //
  // ============================================ //

  // Fill the GPS message.
//  gps_message_.header.frame_id = frame_id;
//  gps_message_.status.service = sensor_msgs::NavSatStatus::SERVICE_GPS;
//  gps_message_.status.status = sensor_msgs::NavSatStatus::STATUS_FIX;
//  gps_message_.position_covariance_type = sensor_msgs::NavSatFix::COVARIANCE_TYPE_KNOWN;
//  gps_message_.position_covariance[0] = hor_pos_std_dev * hor_pos_std_dev;
//  gps_message_.position_covariance[4] = hor_pos_std_dev * hor_pos_std_dev;
//  gps_message_.position_covariance[8] = ver_pos_std_dev * ver_pos_std_dev;
  //gz_gps_message_->header().set_frame_id(fra);
  gz_gps_message_.mutable_header()->set_frame_id("test");
  gz_gps_message_.set_service(sensor_msgs::msgs::NavSatFix::SERVICE_GPS);
  gz_gps_message_.set_status(sensor_msgs::msgs::NavSatFix::STATUS_FIX);
  gz_gps_message_.set_position_covariance_type(sensor_msgs::msgs::NavSatFix::COVARIANCE_TYPE_KNOWN);

  for(int i = 0; i < 9; i++){
    switch (i){
      case 0:
        gz_gps_message_.add_position_covariance(hor_pos_std_dev * hor_pos_std_dev);
        break;
      case 1:
      case 2:
      case 3:
        gz_gps_message_.add_position_covariance(0);
        break;
      case 4:
        gz_gps_message_.add_position_covariance(hor_pos_std_dev * hor_pos_std_dev);
        break;
      case 5:
      case 6:
      case 7:
        gz_gps_message_.add_position_covariance(0);
        break;
      case 8:
        gz_gps_message_.add_position_covariance(ver_pos_std_dev * ver_pos_std_dev);
        break;
    }
  }

  // Fill the ground speed message.
  //ground_speed_message_.header.frame_id = frame_id;
  gz_ground_speed_message_.mutable_header()->set_frame_id(frame_id);
}

void GazeboGpsPlugin::OnUpdate() {

  if(!pubs_and_subs_created_) {
    CreatePubsAndSubs();
    pubs_and_subs_created_ = true;
  }

  // Get the time of the last measurement.
  common::Time current_time;

  // Get the linear velocity in the world frame.
  math::Vector3 W_ground_speed_W_L = link_->GetWorldLinearVel();

  // Apply noise to ground speed.
  W_ground_speed_W_L += math::Vector3(ground_speed_n_[0](random_generator_),
          ground_speed_n_[1](random_generator_),
          ground_speed_n_[2](random_generator_));

  // Fill the GPS message.
#if GAZEBO_MAJOR_VERSION > 6
  current_time = parent_sensor_->LastMeasurementTime();

  //gps_message_.latitude = parent_sensor_->Latitude().Degree();
  //gps_message_.longitude = parent_sensor_->Longitude().Degree();
  //gps_message_.altitude = parent_sensor_->Altitude();

  gz_gps_message_.set_latitude(parent_sensor_->Latitude().Degree());
  gz_gps_message_.set_longitude(parent_sensor_->Longitude().Degree());
  gz_gps_message_.set_altitude(parent_sensor_->Altitude());

#else
  current_time = parent_sensor_->GetLastMeasurementTime();

  //gps_message_.latitude = parent_sensor_->GetLatitude().Degree();
  //gps_message_.longitude = parent_sensor_->GetLongitude().Degree();
  //gps_message_.altitude = parent_sensor_->GetAltitude();

  gz_gps_message_.set_latitude(parent_sensor_->GetLatitude().Degree());
  gz_gps_message_.set_longitude(parent_sensor_->GetLongitude().Degree());
  gz_gps_message_.set_altitude(parent_sensor_->GetAltitude());

#endif

  //gps_message_.header.stamp.sec = current_time.sec;
  //gps_message_.header.stamp.nsec = current_time.nsec;
  gz_gps_message_.mutable_header()->mutable_stamp()->set_sec(current_time.sec);
  gz_gps_message_.mutable_header()->mutable_stamp()->set_nsec(current_time.nsec);

  // Fill the ground speed message.
//  ground_speed_message_.twist.linear.x = W_ground_speed_W_L.x;
//  ground_speed_message_.twist.linear.y = W_ground_speed_W_L.y;
//  ground_speed_message_.twist.linear.z = W_ground_speed_W_L.z;
//  ground_speed_message_.header.stamp.sec = current_time.sec;
//  ground_speed_message_.header.stamp.nsec = current_time.nsec;

  gz_ground_speed_message_.mutable_twist()->mutable_linear()->set_x(W_ground_speed_W_L.x);
  gz_ground_speed_message_.mutable_twist()->mutable_linear()->set_y(W_ground_speed_W_L.y);
  gz_ground_speed_message_.mutable_twist()->mutable_linear()->set_z(W_ground_speed_W_L.z);
  gz_ground_speed_message_.mutable_header()->mutable_stamp()->set_sec(current_time.sec);
  gz_ground_speed_message_.mutable_header()->mutable_stamp()->set_nsec(current_time.nsec);

  // Publish the GPS message.
  //gps_pub_.publish(gps_message_);
  gz_gps_pub_->Publish(gz_gps_message_);

  // Publish the ground speed message.
  //ground_speed_pub_.publish(ground_speed_message_);
  gz_ground_speed_pub_->Publish(gz_ground_speed_message_);

}

void GazeboGpsPlugin::CreatePubsAndSubs() {

  // Create temporary "ConnectGazeboToRosTopic" publisher and message
  gazebo::transport::PublisherPtr connect_gazebo_to_ros_topic_pub =
        node_handle_->Advertise<gz_std_msgs::ConnectGazeboToRosTopic>("~/" + kConnectGazeboToRosSubtopic, 1);

  gz_std_msgs::ConnectGazeboToRosTopic connect_gazebo_to_ros_topic_msg;

  // ============================================ //
  // =========== NAV SAT FIX MSG SETUP ========== //
  // ============================================ //
  gzmsg << "GazeboGpsPlugin creating publisher on \"" << gps_topic_ << "\"." << std::endl;
  gz_gps_pub_ = node_handle_->Advertise<sensor_msgs::msgs::NavSatFix>(gps_topic_, 1);

  connect_gazebo_to_ros_topic_msg.set_gazebo_topic(gps_topic_);
  connect_gazebo_to_ros_topic_msg.set_ros_topic(gps_topic_);
  connect_gazebo_to_ros_topic_msg.set_msgtype(gz_std_msgs::ConnectGazeboToRosTopic::NAV_SAT_FIX);
  connect_gazebo_to_ros_topic_pub->Publish(connect_gazebo_to_ros_topic_msg, true);

  // ============================================ //
  // == GROUND SPEED (TWIST STAMPED) MSG SETUP == //
  // ============================================ //
  gzmsg << "GazeboGpsPlugin creating publisher on \"" << ground_speed_topic_ << "\"." << std::endl;
  gz_ground_speed_pub_ = node_handle_->Advertise<sensor_msgs::msgs::TwistStamped>(ground_speed_topic_, 1);

  connect_gazebo_to_ros_topic_msg.set_gazebo_topic(ground_speed_topic_);
  connect_gazebo_to_ros_topic_msg.set_ros_topic(ground_speed_topic_);
  connect_gazebo_to_ros_topic_msg.set_msgtype(gz_std_msgs::ConnectGazeboToRosTopic::TWIST_STAMPED);
  connect_gazebo_to_ros_topic_pub->Publish(connect_gazebo_to_ros_topic_msg);

}

GZ_REGISTER_SENSOR_PLUGIN(GazeboGpsPlugin);
}
