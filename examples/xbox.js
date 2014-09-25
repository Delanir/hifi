//
//  xbox.js
//  examples
//
//  Created by Stephen Birarda on September 23, 2014
//
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

gamepad = Joysticks.joystickWithName("Wireless 360 Controller");

function reportAxisValue(axis, newValue, oldValue) {
  print("The value for axis " + axis + " has changed to " + newValue  + ". It was " + oldValue);
}

gamepad.axisValueChanged.connect(reportAxisValue);