"use strict";

//  nearTrigger.js
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html


/* global Script, Entities, MyAvatar, Controller, RIGHT_HAND, LEFT_HAND,
   enableDispatcherModule, disableDispatcherModule, getGrabbableData, Vec3,
   TRIGGER_OFF_VALUE, makeDispatcherModuleParameters, makeRunningValues, NEAR_GRAB_RADIUS
*/

Script.include("/~/system/libraries/controllerDispatcherUtils.js");

(function() {

    function entityWantsNearTrigger(props) {
        var grabbableData = getGrabbableData(props);
        return grabbableData.triggerable || grabbableData.wantsTrigger;
    }

    function NearTriggerEntity(hand) {
        this.hand = hand;
        this.targetEntityID = null;
        this.grabbing = false;
        this.previousParentID = {};
        this.previousParentJointIndex = {};
        this.previouslyUnhooked = {};

        this.parameters = makeDispatcherModuleParameters(
            520,
            this.hand === RIGHT_HAND ? ["rightHandTrigger", "rightHand"] : ["leftHandTrigger", "leftHand"],
            [],
            100);

        this.getTargetProps = function (controllerData) {
            // nearbyEntityProperties is already sorted by length from controller
            var nearbyEntityProperties = controllerData.nearbyEntityProperties[this.hand];
            for (var i = 0; i < nearbyEntityProperties.length; i++) {
                var props = nearbyEntityProperties[i];
                var handPosition = controllerData.controllerLocations[this.hand].position;
                var distance = Vec3.distance(props.position, handPosition);
                if (distance > NEAR_GRAB_RADIUS) {
                    continue;
                }
                if (entityWantsNearTrigger(props)) {
                    return props;
                }
            }
            return null;
        };

        this.startNearTrigger = function (controllerData) {
            var args = [this.hand === RIGHT_HAND ? "right" : "left", MyAvatar.sessionUUID];
            Entities.callEntityMethod(this.targetEntityID, "startNearTrigger", args);
        };

        this.continueNearTrigger = function (controllerData) {
            var args = [this.hand === RIGHT_HAND ? "right" : "left", MyAvatar.sessionUUID];
            Entities.callEntityMethod(this.targetEntityID, "continueNearTrigger", args);
        };

        this.endNearTrigger = function (controllerData) {
            var args = [this.hand === RIGHT_HAND ? "right" : "left", MyAvatar.sessionUUID];
            Entities.callEntityMethod(this.targetEntityID, "endNearTrigger", args);
        };

        this.isReady = function (controllerData) {
            this.targetEntityID = null;

            if (controllerData.triggerValues[this.hand] < TRIGGER_OFF_VALUE) {
                return makeRunningValues(false, [], []);
            }

            var targetProps = this.getTargetProps(controllerData);
            if (targetProps) {
                this.targetEntityID = targetProps.id;
                this.startNearTrigger(controllerData);
                return makeRunningValues(true, [this.targetEntityID], []);
            } else {
                return makeRunningValues(false, [], []);
            }
        };

        this.run = function (controllerData) {
            if (controllerData.triggerClicks[this.hand] === 0) {
                this.endNearTrigger(controllerData);
                return makeRunningValues(false, [], []);
            }

            this.continueNearTrigger(controllerData);
            return makeRunningValues(true, [this.targetEntityID], []);
        };

        this.cleanup = function () {
            if (this.targetEntityID) {
                this.endNearTrigger();
            }
        };
    }

    var leftNearTriggerEntity = new NearTriggerEntity(LEFT_HAND);
    var rightNearTriggerEntity = new NearTriggerEntity(RIGHT_HAND);

    enableDispatcherModule("LeftNearTriggerEntity", leftNearTriggerEntity);
    enableDispatcherModule("RightNearTriggerEntity", rightNearTriggerEntity);

    this.cleanup = function () {
        leftNearTriggerEntity.cleanup();
        rightNearTriggerEntity.cleanup();
        disableDispatcherModule("LeftNearTriggerEntity");
        disableDispatcherModule("RightNearTriggerEntity");
    };
    Script.scriptEnding.connect(this.cleanup);
}());
