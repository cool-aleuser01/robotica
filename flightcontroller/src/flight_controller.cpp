#include <Eigen/Dense>
#include <map>
#include <string> 
#include <cmath>
#include <iostream>

#include "fc_constants.hpp"
#include "fc_config.hpp"
#include "flight_controller.hpp"
#include "drone.hpp"
#include "imu.hpp"

#define log(msg) std::cerr << msg << std::endl;

static Drone& drone = Drone::get();
static imu& IMU = imu::get();

void FlightController::run() {
	while (true) {
		
		actuate();

		//raise or lower the landing gear if needed. 
		if(drone.getHeight() > drone.gearRaiseHeight && !drone.gearUp) {
			drone.setRetracts(true); 
		} else if (drone.getHeight() < drone.gearLowerHeight && drone.gearUp) {
			drone.setRetracts(false); 
		}


		drone.setThrust(thrust);
	}
}

Eigen::Vector3f FlightController::getDifferenceAttitude() {
	return drone.referenceAttitude - IMU.get_angles();
}

Eigen::Vector3f FlightController::getDifferenceRotationalVel() {
	return drone.referenceRotationalVel - IMU.get_rotational_velocity();
}

void FlightController::setReferenceRotationalVel(Eigen::Vector3f newRefRotVel) {
	drone.referenceRotationalVel = newRefRotVel;
}

Eigen::Vector3f FlightController::getDifferenceVel() {
	Eigen::Vector3f diffSpeed; 
	diffSpeed = drone.referenceVelocity; 
	diffSpeed[2] = drone.referenceVelocity[2] - drone.getZSpeed();
}

void FlightController::setReferenceVel(Eigen::Vector3f newRefSpeed) {
	drone.referenceVelocity = newRefSpeed;
}

Eigen::Vector3f FlightController::getAbsoluteDirection() {
	drone.referencePosition - drone.getPosition(); 
}

void FlightController::setReferencePosition(Eigen::Vector3f newPos) {
	drone.referencePosition = newPos; 
}


FlightController::FlightController() {
	drone.setRetracts(false);
	pidGains["Roll"] = 0.0;
	pidGains["Pitch"] = 0.0;
	pidGains["Heading"] = 0.0;
	pidGains["Height"] = 0.0;
	navMode = "Hold";
}

void FlightController::setHoldPosition(Eigen::Vector3f newPosition) {
	drone.holdPosition = newPosition; 
	if(drone.holdPosition[2] < fc_config::safetyHeight) {
		drone.holdPosition[2] = fc_config::safetyHeight; 
	}
}

void FlightController::actuate() {
	/*if (navMode == "Waypoint") {
		wayPointNavigation(); 
	} else if (navMode == "Direct") {
		directControl();
	} else if (navMode == "Follow" && target) {
		directControl();
	} else */if (navMode == "Hold") {
		hold();
	} else if (navMode == "Land") {
		land();
	} 
}

void FlightController::hold() {
	thrust = drone.t00 / (cos(IMU.get_angles()[0]) * cos(IMU.get_angles()[1]));

	drone.referenceAttitude = Eigen::Vector3f::Zero(3);
	// log("Reference Attitude: " << drone.referenceAttitude);
	Eigen::Vector3f diffAtt = getDifferenceAttitude();
	// log("Difference Attitude: " << diffAtt);
	Eigen::Vector3f diffRotationalVel = getDifferenceRotationalVel();
	// log("Difference Rotational Velocity: " << diffRotationalVel);
	Eigen::Vector3f diffVelocity = getDifferenceVel(); 
	// log("Difference velocity: " << diffVelocity)
	Eigen::Vector3f absDirection = Eigen::Vector3f::Zero(3);
	// log("absolute direction: " << absDirection);

	// headingPID(diffAtt, diffRotationalVel);
	rollPID(diffAtt, diffRotationalVel);
	pitchPID(diffAtt, diffRotationalVel);
	heightPID(absDirection, diffVelocity);
}

void FlightController::land() {

}

void FlightController::updateReferenceThrust(float gain, int signs[]) {
	for (int i = 0; i < 4; i++) {
		if (signs[i] != 1 && signs[i] != -1) {
			log("Incorrect signs for updateReferenceThrust!");
		}
	}

	for (int i = 0; i < 4; i++) {
		thrust[i] *= (1 + gain * signs[i]);
		if(thrust[i] > fc_config::MAX_THRUST) {
			thrust[i] = fc_config::MAX_THRUST;
		} else if (thrust[i] < fc_config::MIN_THRUST) {
			thrust[i] = fc_config::MIN_THRUST;
		}
	}
}

void FlightController::headingPID(Eigen::Vector3f diffAtt, Eigen::Vector3f diffRotationalVelocity){
 	//get PID values
	float KP = fc_config::pidHeading[0];
	float KD = fc_config::pidHeading[1];

	//calculate gain
	float gain = diffAtt[2] * KP + diffRotationalVelocity[2] * KD;

	gain *= fc_config::masterGain;
	pidGains["Heading"] = gain;

	//update reference thrust
	if(gain > 0 && diffRotationalVelocity[2] <= fc_config::maxYawRotationalVel) {
		log("Turn-R");
		updateReferenceThrust(gain, drone.motorRotationSigns);
	} else if (gain < 0 && diffRotationalVelocity[2] >= -fc_config::maxYawRotationalVel) {
		log("Turn-L");
		updateReferenceThrust(gain, drone.motorRotationSigns);
	} else {
		log("Heading out of bounds set by maxYawRotationalVel");
	}
}


void FlightController::rollPID(Eigen::Vector3f diffAtt, Eigen::Vector3f diffRotationalVelocity){
	//get PID values
	float KP = fc_config::pidRoll[0];
	float KD = fc_config::pidRoll[1];

	//calculate gain
	float gain = diffAtt[0] * KP + diffRotationalVelocity[0] * KD;

	gain *= fc_config::masterGain;
	pidGains["Roll"] = gain;
	
	//update reference thrust
	int signs[4] = {1, -1, -1, 1};
	if(gain > 0 && diffRotationalVelocity[0] <= fc_config::maxRollRotationalVel) {
		log("Roll-R");
		updateReferenceThrust(gain, signs);
	} else if (gain < 0 && diffRotationalVelocity[0] >= -fc_config::maxRollRotationalVel) {
		log("Roll-L");
		updateReferenceThrust(gain, signs);
	} else {
		log("Roll out of bounds set by maxRollRotationalVel");
	}
}


void FlightController::pitchPID(Eigen::Vector3f diffAtt, Eigen::Vector3f diffRotationalVelocity){
	//get PID values
	float KP = fc_config::pidPitch[0];
	float KD = fc_config::pidPitch[1];

	//calculate gain
	float gain = diffAtt[1] * KP + diffRotationalVelocity[1] * KD;

	gain *= fc_config::masterGain;
	pidGains["Pitch"] = gain;
	
	//update reference thrust
	int signs[4] = {-1, -1, 1, 1};
	if (gain > 0 && diffRotationalVelocity[1] <= fc_config::maxPitchRotationalVel) {
		log("Forward");
		updateReferenceThrust(gain, signs);
	} else if (gain < 0 && diffRotationalVelocity[1] >= -fc_config::maxPitchRotationalVel) {
		log("Backward");
		updateReferenceThrust(gain, signs);
	} else {
		log("Pitch out of bounds set by maxPitchRotationalVel");
	}
}

void FlightController::heightPID(Eigen::Vector3f absoluteDirection, Eigen::Vector3f differenceVelocity){
	//get PID values
	float KP = fc_config::pidHeight[0];
	float KD = fc_config::pidHeight[1];

	//calculate gain
	float gain = absoluteDirection[2] * KP + differenceVelocity[2] * KD;

	//check whether or not we're above the safety height
	if (drone.getHeight() < fc_config::safetyHeight && drone.distanceToLandingSpot > fc_config::landingPrecision) {
		log("Below safety height!");
		gain = KP * (fc_config::safetyHeight - drone.getHeight());
	}

	gain *= fc_config::masterGain;
	pidGains["Height"] = gain;

	//update reference thrust
	int signs[4] = {1, 1, 1, 1};
	if (drone.getZSpeed() <= -fc_config::maxDownSpeed || 
		IMU.get_acceleration()[2] <= -fc_config::maxDownAcceleration && 
		drone.getZSpeed() < 0) {
		log("Moving down too fast");

		gain = fabs(gain);
		updateReferenceThrust(gain, signs);
	} else if (drone.getZSpeed() <= fc_config::maxUpSpeed) {
		if (gain > 0) {
			log("Up");
			updateReferenceThrust(gain, signs);
		} else if (gain < 0 && drone.getZSpeed() >= -fc_config::maxDownSpeed) {
			log("Down");
			updateReferenceThrust(gain, signs);
		}
	}

}