/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2012, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Author: Ruinian Xu */

#include <ros/ros.h>
#include <moveit/robot_model_loader/robot_model_loader.h>
#include <moveit/robot_model/robot_model.h>
#include <moveit/robot_state/robot_state.h>

#include <sensor_msgs/JointState.h>
#include <std_msgs/Float64.h>
#include <std_msgs/Bool.h>
#include <dynamixel_msgs/JointState.h>

#include <unistd.h>
#include <math.h>
#include <unistd.h>
#include <cstdlib> 
#include <iostream>
#include <cmath>
#include <iostream>
#include <fstream>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <unsupported/Eigen/MatrixFunctions>

#include <moveit/move_group_interface/move_group.h>
#include <moveit/planning_scene_interface/planning_scene_interface.h>
#include <moveit_msgs/DisplayRobotState.h>
#include <moveit_msgs/DisplayTrajectory.h>
#include <moveit_msgs/AttachedCollisionObject.h>
#include <moveit_msgs/CollisionObject.h>

std::vector<double> current_joint_values;
bool mutex = 0;
double load_8 = 0;
const double pi = std::acos(-1);
//const double pi = 3.1415926535897;
const double joint_8_home_value_parallel = 0.0;
const double gripper_close_value_parallel = -2.6;
const double joint_8_home_value_paw= -0.5;
const double gripper_close_value_paw = 1.0;
const double parallel_torque_compensate = 0.1;
const double paw_torque_compensate = 0.1;
geometry_msgs::Pose target_pickup;

void joint_state_handler(const sensor_msgs::JointState::ConstPtr& msg);
void traj_end_mutex(const std_msgs::Bool::ConstPtr& msg);
void update_state(const dynamixel_msgs::JointState::ConstPtr& msg);
void mutex_traj();
void mutex_rotate(double target_angle);

void openGrabber(ros::Publisher &pub_8, double angle);
void closeGrabber(ros::Publisher &pub_8, double angle);
void rotateGripper(ros::Publisher &pub_7, double angle);

void addObject2Scene(moveit::planning_interface::MoveGroup &group, moveit::planning_interface::PlanningSceneInterface &planning_scene_interface, ros::Publisher &collision_object_publisher);
const std::vector<std::string> listNamedPoses(moveit::planning_interface::MoveGroup &group);
void gotoNamedTarget(moveit::planning_interface::MoveGroup &group, std::string target, bool constraint_on, double joint_8_value);
void getConstraint(moveit::planning_interface::MoveGroup &group, std::string target);
void compensatePlan(moveit::planning_interface::MoveGroup::Plan &plan, double joint_8_value);

int main(int argc, char **argv){
	/*****************************************************************
    *                         Arm initialization                     *
    *****************************************************************/
    // ros initialization
    ros::init(argc, argv, "arm_kinematics");
    ros::NodeHandle node_handle;
    ros::Publisher pub_7 = node_handle.advertise<std_msgs::Float64>("/finalasm_position_controller_7/command", 1, true);
    ros::Publisher pub_8 = node_handle.advertise<std_msgs::Float64>("/finalasm_position_controller_8/command", 1, true);
    ros::Subscriber sub = node_handle.subscribe<sensor_msgs::JointState>("/joint_states", 1, joint_state_handler);
    ros::Subscriber sub_endtime = node_handle.subscribe<std_msgs::Bool>("/finalasm_joint_trajectory_action_controller/mutex", 5, traj_end_mutex);
    ros::Subscriber sub_joint_8_state = node_handle.subscribe<dynamixel_msgs::JointState>("/finalasm_position_controller_8/state", 1, update_state);

    ros::AsyncSpinner spinner(1);
    spinner.start();

    // kinematic_state & kinematic_model loading & planner
    moveit::planning_interface::MoveGroup group("arm4");  //"arm4_full"
    group.setPlannerId("LBKPIECEkConfigDefault");//ForageRRTkConfigDefault//LBKPIECEkConfigDefault//RRTstarkConfigDefault

    // We will use the planning_scene_interface class to deal directly with the world.
    moveit::planning_interface::PlanningSceneInterface planning_scene_interface; 

    /*    Joint and link info    */
    const std::vector<std::string> &joint_names = group.getCurrentState()->getRobotModel()->getJointModelGroup(group.getName())->getJointModelNames();
    const std::vector<std::string> &link_names = group.getCurrentState()->getRobotModel()->getJointModelGroup(group.getName())->getLinkModelNames();

    /*****************************************************************
    *                       Visualization setup                      *
    *****************************************************************/
    ros::Publisher display_publisher = node_handle.advertise<moveit_msgs::DisplayTrajectory>("/move_group/display_planned_path", 1, true);
    moveit_msgs::DisplayTrajectory display_trajectory;

    /*****************************************************************
    *                     Adding objects to world                    *
    *****************************************************************/
    /* First put an object into the scene*/
    /* Advertise the collision object message publisher*/
    ros::Publisher collision_object_publisher = node_handle.advertise<moveit_msgs::CollisionObject>("collision_object", 1);
    while(collision_object_publisher.getNumSubscribers() < 1) {
        ros::WallDuration sleep_t(0.5);
        sleep_t.sleep();
    }
    addObject2Scene(group, planning_scene_interface ,collision_object_publisher);

    /*****************************************************************
    *                        List stored poses                       *
    *****************************************************************/
    const std::vector<std::string> namedTargets = listNamedPoses(group);

    /*****************************************************************
    *                      Specify initial pose                      *
    *****************************************************************/
    // namedTargets stores the names of pre-defined poses(joint values)
    // select the name (ex. "home"), gotoNamedTarget function will find plan to the specific pose
    int targetNum = 0;
    std::cout<<"select target pose above: (1:Home 2:Holding 3:Give_side)"; std::cin >> targetNum;
    gotoNamedTarget(group, namedTargets[targetNum], 0, joint_8_home_value_paw);
    
    /*********************************************************************************
    *     Pipeline: 1.Approach the top of bottle 2.Go down to the task position      *
    *     3.Close the gripper 4.Rotate the wrist 5.Lift the gripper                  *
    **********************************************************************************/
    int enable = 1;
    while(enable){
        double x_test, y_test, z_test;

        std::cout<<"Input the position(x,y,z)"<<std::endl;
        std::cin>>x_test;
        std::cin>>y_test;
        std::cin>>z_test;

        geometry_msgs::Pose target_pose_test;
        target_pose_test.position.x = x_test;
        target_pose_test.position.y = y_test;
        target_pose_test.position.z = z_test;
        
        target_pose_test.orientation.x = 0.49;
        target_pose_test.orientation.y = 0.49;
        target_pose_test.orientation.z = 0.49;
        target_pose_test.orientation.w = sqrt(1-target_pose_test.orientation.x*target_pose_test.orientation.x-
                target_pose_test.orientation.y*target_pose_test.orientation.y-target_pose_test.orientation.z*target_pose_test.orientation.z);

        group.setPoseTarget(target_pose_test);
        // plan
        moveit::planning_interface::MoveGroup::Plan my_plan_test;
        moveit::planning_interface::MoveItErrorCode success_test = group.plan(my_plan_test);
        my_plan_test.trajectory_.joint_trajectory.joint_names.push_back("joint_8");
        compensatePlan(my_plan_test, joint_8_home_value_paw);
        
        // visualization
        ROS_INFO("Visualizing plan 1 (pose goal) %s",success_test?"":"FAILED");  

        //execution  
        if(success_test) group.execute(my_plan_test);
        mutex_traj();

        std::cout<<"Would you like to repeat task? 1(Yes) 2(No)"<<std::endl;
        std::cin >> enable;

        gotoNamedTarget(group, namedTargets[targetNum], 0, joint_8_home_value_paw);
    }

    ros::spin();
    return 0;
}

void joint_state_handler(const sensor_msgs::JointState::ConstPtr& msg)
{
    current_joint_values.clear();
    for(std::size_t i = 0; i < msg->position.size(); ++i) {
        current_joint_values.push_back(msg->position[i]);
    }
}

void traj_end_mutex(const std_msgs::Bool::ConstPtr& msg){
    if (msg->data){
        mutex = 1;
    }
}

void update_state(const dynamixel_msgs::JointState::ConstPtr& msg){
    //update load for 8th motor
    load_8 = msg->load;
}

void mutex_traj(){
    clock_t start_time = clock();
    while(1){
        if(mutex){
            break;
        }
        else if ((clock() - start_time) / CLOCKS_PER_SEC > 6.0)
            break;
    }
    mutex = 0;
}

void mutex_rotate(double target_angle){
    clock_t start_time = clock();
    while(1){
        if (std::abs(target_angle - current_joint_values[6]) < 0.05)
            break;
        else if ((clock() - start_time) / CLOCKS_PER_SEC > 4.0)
            break;
    }
}

void openGrabber(ros::Publisher &pub_8, double angle){
    std_msgs::Float64 val_8 ;
    val_8.data = angle;
    pub_8.publish(val_8);
    double sleep_time = std::abs(angle - current_joint_values.back()) / 0.85 * 2.5;
    sleep(sleep_time);
}

void rotateGripper(ros::Publisher &pub_7, double angle) {
    std_msgs::Float64 val_7;

    //std::cout<<" current value of seventh joint: "<<current_joint_values[6]<<std::endl;
    //std::cout<<" compensate value: "<<std::abs(angle - 180.00) / 180.00 * pi<<std::endl;

    if ((current_joint_values[6] + (angle / 180.00 * pi)) > 3.14){
        val_7.data = current_joint_values[6] - (std::abs(angle - 360.00) / 360.00 * 2 * pi);
        pub_7.publish(val_7);
        mutex_rotate(val_7.data);
    }
    else if ((current_joint_values[6] + (angle / 180.00 * pi)) < -3.14){
        val_7.data = current_joint_values[6] + (std::abs(angle - 360.00) / 360.00 * 2 * pi);
        pub_7.publish(val_7);
        mutex_rotate(val_7.data);
    }
    else{
        val_7.data = current_joint_values[6] + (angle / 180 * pi);
        pub_7.publish(val_7);
        mutex_rotate(val_7.data);
    }
}

void closeGrabber(ros::Publisher &pub_8, double angle){
    std_msgs::Float64 val_8 ;
    val_8.data = angle;
    pub_8.publish(val_8);

    while(1){
        if(std::abs(load_8) > 0.6){
            val_8.data = current_joint_values.back() + paw_torque_compensate;
            pub_8.publish(val_8);
            break;
        }
    }
}

void addObject2Scene(moveit::planning_interface::MoveGroup &group, moveit::planning_interface::PlanningSceneInterface &planning_scene_interface, ros::Publisher &collision_object_publisher){
    /* Define the object message */
    moveit_msgs::CollisionObject object;

    /* The header must contain a valid TF frame */
    object.header.frame_id = group.getPlanningFrame();
    /* The id of the object */
    object.id = "box";

    /* A default pose */
    geometry_msgs::Pose pose;
    pose.orientation.w = 1.0;
    pose.position.x =  0.5;
    pose.position.y =  0.5;
    pose.position.z = -0.28;//-0.1;//-0.07

    /* Define a box to be attached */
    shape_msgs::SolidPrimitive primitive;
    primitive.type = primitive.BOX;
    primitive.dimensions.resize(3);
    primitive.dimensions[0] = 0.9;
    primitive.dimensions[1] = 0.9;
    primitive.dimensions[2] = 0.05;

    object.primitives.push_back(primitive);
    object.primitive_poses.push_back(pose);


    /* An attach operation requires an ADD */
    object.operation = object.ADD;

    /* Publish and sleep (to view the visualized results) */
    collision_object_publisher.publish(object);

    std::vector<moveit_msgs::CollisionObject> collision_objects;  
    collision_objects.push_back(object);  

    // Now, let's add the collision object into the world
    ROS_INFO("Add an object into the world");  
    planning_scene_interface.addCollisionObjects(collision_objects);

    ros::WallDuration sleep_time(1.0);
    sleep_time.sleep();
}

const std::vector<std::string> listNamedPoses(moveit::planning_interface::MoveGroup &group){
    // list of all stored pose name in SRDF 
    const std::vector<std::string> namedTargets = group.getNamedTargets();
    std::cout<<"stored position in SRDF: "<<std::endl;
    for(int i = 0; i < namedTargets.size(); i++){
        std::cout<<i<<": ";
        std::cout<<namedTargets[i]<<std::endl;
    }

    return namedTargets;
}

void compensatePlan(moveit::planning_interface::MoveGroup::Plan &plan, double joint_8_value){
    for(size_t idx = 0; idx < plan.trajectory_.joint_trajectory.points.size(); idx++){
        plan.trajectory_.joint_trajectory.points[idx].positions.push_back(joint_8_value);
    }
}

void getConstraint(moveit::planning_interface::MoveGroup &group, std::string target){
    ROS_INFO("get constraints");

    // switch wont take string
    int targetNum = 0;
    if(target == "home") targetNum = 0;
    else if(target == "holding") targetNum = 1;
    else targetNum = -1;

    // avoid cross init
    moveit_msgs::OrientationConstraint ocm; 
    moveit_msgs::Constraints test_constraints;
    switch(targetNum){
        case 0: // no constraints
          break;

        case 1: // hoding position  
          ocm.link_name = "link_8";  
          ocm.header.frame_id = "base_link";
          ocm.orientation.z = 1.0;
          ocm.absolute_x_axis_tolerance = 0.1; //0.1
          ocm.absolute_y_axis_tolerance = 0.1; //0.1
          ocm.absolute_z_axis_tolerance = 0.5; //0.1
          ocm.weight = 1.0;

          test_constraints.orientation_constraints.push_back(ocm);  
          group.setPathConstraints(test_constraints);
          group.setPlanningTime(20.0);
          break;

        default: // no constraints
          break;
    }
}

void gotoNamedTarget(moveit::planning_interface::MoveGroup &group, std::string target, bool constraint_on, double joint_8_value){
    ROS_INFO("TASK: Go to %s pose", target.c_str());

    // get joint values of stored pose by name
    std::vector<double> group_variable_values;
    std::map<std::string, double> positions = group.getNamedTargetValues(target);
    for (std::map<std::string, double> ::iterator it=positions.begin(); it!=positions.end(); ++it){
      std::cout << it->first << " => " << it->second << '\n';
      group_variable_values.push_back(it->second);
    }
 
    // get constraints
    if(constraint_on) getConstraint(group, target);

    // plan
    moveit::planning_interface::MoveGroup::Plan my_plan;
    group.setJointValueTarget(group_variable_values);
    moveit::planning_interface::MoveItErrorCode success = group.plan(my_plan);
    my_plan.trajectory_.joint_trajectory.joint_names.push_back("joint_8");
    compensatePlan(my_plan, joint_8_value);

    // set planning time to default
    group.setPlanningTime(20.0);
    group.clearPathConstraints();

    // visualization
    ROS_INFO("Visualizing plan home (joint space goal) %s",success?"":"FAILED");   
 
    // execution
    ROS_INFO("Execution if plan successfully");
    if(success) group.execute(my_plan);
    mutex_traj();
    

}

